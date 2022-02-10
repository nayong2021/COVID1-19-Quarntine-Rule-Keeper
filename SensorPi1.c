#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <pthread.h>

#define BUFFER_MAX 3
#define DIRECTION_MAX 35
#define DIST_MAX 5

#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1

#define MAXTIMINGS 83
#define DHTPIN 16

int dht11_dat[5] = {0,0,0,0,0} ;

char ip_addr[20];

char **argv_save;

int read_dht11_dat()
{
   uint8_t laststate = HIGH ;
   uint8_t counter = 0 ;
   uint8_t j = 0, i ;
   uint8_t flag = HIGH ;
   uint8_t state = 0 ;
   float f ;

   dht11_dat[0] = dht11_dat[1] = dht11_dat[2] = dht11_dat[3] = dht11_dat[4] = 0 ;

  pinMode(DHTPIN, OUTPUT) ;
  digitalWrite(DHTPIN, LOW) ;
  delay(18) ;

  digitalWrite(DHTPIN, HIGH) ;
  delayMicroseconds(30) ;
  pinMode(DHTPIN, INPUT) ;

  for (i = 0; i < MAXTIMINGS; i++) {
    counter = 0 ;

    while ( digitalRead(DHTPIN) == laststate) { 
      counter++ ;
      delayMicroseconds(1) ;
      if (counter == 200) break ;
    }

    laststate = digitalRead(DHTPIN) ;

    if (counter == 200) break ; // if while breaked by timer, break for

    if ((i >= 4) && (i % 2 == 0)) {
      dht11_dat[j / 8] <<= 1 ;

      if (counter > 25) dht11_dat[j / 8] |= 1 ;
      j++ ;
    }
  }
    if ((j >= 40) && (dht11_dat[4] == ((dht11_dat[0] + dht11_dat[1] + dht11_dat[2] + dht11_dat[3]) & 0xff))) {
    return 1; 
  }
  else return 0;

}




#define ARRAY_SIZE(array) sizeof(array) / sizeof(array[0])

static const char *DEVICE = "/dev/spidev0.0";
static uint8_t MODE = SPI_MODE_0;
static uint8_t BITS = 8;
static uint32_t CLOCK = 1000000;
static uint16_t DELAY = 5;

void error_handling(char *message){
  fputs(message,stderr);
  fputc('\n',stderr);
  exit(1);
}

static int prepare(int fd) {
   
   if(ioctl(fd, SPI_IOC_WR_MODE, &MODE) == -1) {
      perror("Can't set MODE");
      return -1;
   }
   
   if(ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &BITS) == -1) {
      perror("Can't set number of BITS");
      return -1;
   }
   
   if(ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &CLOCK) == -1) {
      perror("Can't set write CLOCK");
      return -1;
   }
   
   if(ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &CLOCK) == -1) {
      perror("Can't set read CLOCK");
      return -1;
   }
   
   return 0;
}

uint8_t control_bits_differential(uint8_t channel){
   return(channel & 7) << 4;
}

uint8_t control_bits(uint8_t channel){
   return 0x8 | control_bits_differential(channel);
}

int readadc(int fd, uint8_t channel){
   uint8_t tx[] = {1, control_bits(channel), 0};
   uint8_t rx[3];
   
   struct spi_ioc_transfer tr = {
      .tx_buf = (unsigned long)tx,
      .rx_buf = (unsigned long)rx,
      .len = ARRAY_SIZE(tx),
      .delay_usecs = DELAY,
      .speed_hz = CLOCK,
      .bits_per_word = BITS,
   };
   
   if(ioctl(fd, SPI_IOC_MESSAGE(1), &tr) == 1){
      perror("IO Error");
      abort();
   }
   
   return ((rx[1] << 8) & 0x300) | (rx[2] & 0xFF);
}

void *dht111_thd(){
  int sock;

   char msg[6];
   struct sockaddr_in serv_addr;
   
   sock = socket(PF_INET, SOCK_STREAM, 0);
   if(sock == -1)
      error_handling("socket() error");
   memset(&serv_addr, 0, sizeof(serv_addr));
   serv_addr.sin_family = AF_INET;
   serv_addr.sin_addr.s_addr = inet_addr(ip_addr);
   serv_addr.sin_port = htons(atoi(argv_save[2]));
   if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))==-1)
      error_handling("connect() error");
      
   if (wiringPiSetup() == -1) exit(1) ;
    int i = 0;
   while(1){
        i = read_dht11_dat();
        if(i == 1){     // not fail
            snprintf(msg,6,"%d.%d",dht11_dat[2], dht11_dat[3]); //size..
            write(sock, msg, sizeof(msg));
        }
        delay(1200);
   }
   
   close(sock);
  

}

void *pressure_thd(){
  int pressure = 0;
   
  int sock;

   char msg[20];
   struct sockaddr_in serv_addr;
   
   sock = socket(PF_INET, SOCK_STREAM, 0);
   if(sock == -1)
      error_handling("socket() error");
   memset(&serv_addr, 0, sizeof(serv_addr));
   serv_addr.sin_family = AF_INET;
   serv_addr.sin_addr.s_addr = inet_addr(ip_addr);
   serv_addr.sin_port = htons(atoi(argv_save[3]));
   if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))==-1)
      error_handling("connect() error");
   int fd = open(DEVICE, O_RDWR);
   if(fd <=0){
      printf("Device %s not found\n", DEVICE);
      pthread_exit(NULL);
   }
   
   if(prepare(fd) == -1){
      pthread_exit(NULL);
   }
   
   /*while(1){
      light = readadc(fd, 0);
      PWMWriteDutyCycle(0, light*7500);
      usleep(1000);
   }*/
    int immvalue = 0;
   while(1){
      pressure = readadc(fd, 0);
      //PWMWriteDutyCycle(0, light*7500);
      snprintf(msg,20,"%d",pressure);
      write(sock, msg, sizeof(msg));
      usleep(150000);
   }
    close(sock);
   close(fd);
}


int main(int argc, char **argv)
{
  pthread_t p_thread, p_thread2;
  int thr_id, thr_id2;
  
  if(argc != 4){
    printf("Usage : %s <IP> <port>\n",argv[0]);
    exit(1);
  }
  argv_save = argv;
  strcpy(ip_addr, argv[1]);
  
  thr_id = pthread_create(&p_thread, NULL, dht111_thd, NULL);
  if(thr_id < 0){
	 perror("thread create error : ");
	 exit(0);
  }
  thr_id2 = pthread_create(&p_thread2, NULL, pressure_thd, NULL);
  if(thr_id2 < 0){
	 perror("thread create error : ");
	 exit(0);
  }
      
  pthread_join(p_thread, NULL);
  pthread_join(p_thread2, NULL);
      
  return 0;
}
