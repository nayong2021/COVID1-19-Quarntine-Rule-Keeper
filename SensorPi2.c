#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <pthread.h>

#define BUFFER_MAX 3
#define DIRECTION_MAX 50
#define VALUE_MAX 256
#define DIST_MAX 5

#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1

#define PIN 24
#define POUT 23
#define POUT2 25

int argc_save;
char **argv_save;

double distance = 0;

int num_of_people = 0;

double dist_rec[20] = {15,};

int warning = 0;

static int PWMExport(int pwmnum)
{  
   char buffer[BUFFER_MAX];
   int bytes_written;
   int fd;
   
   fd = open("/sys/class/pwm/pwmchip0/unexport", O_WRONLY);
   if(-1 == fd){
      fprintf(stderr, "Failed to open in unexport!\n");
      return(-1);
   }
   
   bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
   write(fd, buffer, bytes_written);
   close(fd);
   
   sleep(1);
   fd = open("/sys/class/pwm/pwmchip0/export", O_WRONLY);
   if(-1 == fd){
      fprintf(stderr, "Failed to open in export!\n");
      return(-1);
   }
   bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
   write(fd, buffer, bytes_written);
   close(fd);
   sleep(1);
   return(0);
}

static int
PWMEnable(int pwmnum)
{
	static const char s_unenable_str[] = "0";
	static const char s_enable_str[] = "1";
	char path[DIRECTION_MAX];
	int fd;
	
	snprintf(path, DIRECTION_MAX, "/sys/class/pwm/pwmchip0/pwm%d/enable", pwmnum);
	fd = open(path, O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open in enable!\n");
		return -1;
	}
	
	write(fd, s_unenable_str, strlen(s_unenable_str));
	close(fd);
	
	fd = open(path, O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open in enable!\n");
		return -1;
	}
	
	write(fd, s_enable_str, strlen(s_enable_str));
	close(fd);
	return(0);
}

static int
PWMWritePeriod(int pwmnum, int value)
{
	char s_values_str[VALUE_MAX];
	char path[VALUE_MAX];
	int fd, byte;
	
	snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/period", pwmnum);
	fd = open(path, O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open in period!\n");
		return (-1);
	}
	
	byte = snprintf(s_values_str, VALUE_MAX, "%d", value);
	
	if (-1 == write(fd, s_values_str, byte)) {
		fprintf(stderr, "Failed to write value in period!\n");
		close(fd);
		return(-1);
	}
	
	close(fd);
	return(0);
}

static int
PWMWriteDutyCycle(int pwmnum, int value)
{
	char path[VALUE_MAX];
	char s_values_str[VALUE_MAX];
	int fd, byte;
	
	snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/duty_cycle", pwmnum);
	fd = open(path, O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open in duty_cycle!\n");
		return(-1);
	}
	
	byte = snprintf(s_values_str, VALUE_MAX, "%d", value);
	
	if (-1 == write(fd, s_values_str, byte)) {
		fprintf(stderr, "Failed to write value! in duty_cycle\n");
		close(fd);
		return(-1);
	}
	
	close(fd);
	return(0);
}

static int GPIOExport(int pin) {
   char buffer[BUFFER_MAX];
   ssize_t bytes_written;
   int fd;
   
   fd = open("/sys/class/gpio/export", O_WRONLY);
   if (-1 == fd) {
      fprintf(stderr, "Failed to open export for writing!\n");
      return(-1);
   }
   
   bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
   write(fd, buffer, bytes_written);
   close(fd);
   return(0);
}

static int GPIOUnexport(int pin) {
   char buffer[BUFFER_MAX];
   ssize_t bytes_written;
   int fd;
   
   fd = open("/sys/class/gpio/unexport", O_WRONLY);
   if (-1 == fd) {
      fprintf(stderr, "Failed to open unexport for writing!\n");
      return(-1);
   }
   
   bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
   write(fd, buffer, bytes_written);
   close(fd);
   return(0);
}

static int GPIODirection(int pin, int dir) {
   static const char s_directions_str[] = "in\0out";
   
   //char path[DIRECTION_MAX]="/sys/class/gpio/gpio24/diretion";
   char path[DIRECTION_MAX]="/sys/class/gpio/gpio%d/direction";
   int fd;
   
   snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
   
   fd = open(path, O_WRONLY);
   if (-1 == fd) {
      fprintf(stderr, "Failed to open gpio direction for writing!\n");
      return(-1);
   }
   
   if (-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3)) {
      fprintf(stderr, "Failed to set direction!\n");
      return(-1);
   }
   
   close(fd);
   return(0);
}

static int GPIORead(int pin) {
   char path[VALUE_MAX];
   char value_str[3];
   int fd;
   
   snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
   fd = open(path, O_RDONLY);
   if (-1 == fd) {
      fprintf(stderr, "Failed to open gpio value for reading!\n");
      return(-1);
   }
   
   if (-1 == read(fd, value_str, 3)) {
      fprintf(stderr, "Failed to read value!\n");
      return(-1);
   }
   
   close(fd);
   
   return(atoi(value_str));
}

static int GPIOWrite(int pin, int value) {
   static const char s_values_str[] = "01";
   
   char path[VALUE_MAX];
   int fd;
   
   snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
   fd = open(path, O_WRONLY);
   if (-1 == fd) {
      fprintf(stderr, "Failed to open gpio value for writing!\n");
      return(-1);
   }
   
   if(1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1)) {
      fprintf(stderr, "Failed to write value!\n");
      return(-1);
      
      
   }
   close(fd);
   return(0);
}
void error_handling(char *message){
   fputs(message,stderr);
   fputc('\n',stderr);
   exit(1);
}
void *ultrawave_thd() {
   
	clock_t start_t, end_t;
	double time;
   
   char msg[10];
   int sock;
   
   struct sockaddr_in serv_addr;
	if(-1 == GPIOExport(POUT) || -1 == GPIOExport(PIN)){
		printf("gpio export err\n");
		exit(0);
	}
	
	usleep(100000);
   
   if(-1 == GPIODirection(POUT, OUT) || -1 == GPIODirection(PIN, IN)){
      printf("gpio direction err\n");
      exit(0);
   }
   
   GPIOWrite(POUT, 0);
   usleep(10000);
   sock = socket(PF_INET, SOCK_STREAM, 0);
   if(sock == -1)
      error_handling("socket() error");
   memset(&serv_addr, 0, sizeof(serv_addr));
   serv_addr.sin_family = AF_INET;
   serv_addr.sin_addr.s_addr = inet_addr(argv_save[1]);
   serv_addr.sin_port = htons(atoi(argv_save[2]));
   if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))==-1)
      error_handling("connect() error");
   
   while(1){
	if(-1 == GPIOWrite(POUT,1)){
	   printf("gpio write/trigger err\n");
	   exit(0);
	 }
	 usleep(10);
	 GPIOWrite(POUT,0);
	 
	 while(GPIORead(PIN)==0){
	    start_t = clock();
	 }
	 
	 while(GPIORead(PIN)==1){
	    end_t = clock();
	 }
	 
	 time = (double)(end_t - start_t)/CLOCKS_PER_SEC;
	 distance = time/2*34000;
	 if(distance > 30 || distance < 5){
	    for(int i = 0; i < 20; i++)
	       dist_rec[i] = 15;
      write(sock, msg, sizeof(msg));
      usleep(150000);
	    continue;
	 }
	 for(int i = 0; i < 19; i++)
	    dist_rec[i] = dist_rec[i+1];
	 
	 dist_rec[5] = distance;
	 
#define old_mean_dist (dist_rec[0] + dist_rec[1] + dist_rec[2] + dist_rec[3] + dist_rec[4] + dist_rec[5] + dist_rec[6] + dist_rec[7] + dist_rec[8] + dist_rec[9]) / 10
#define recent_mean_dist (dist_rec[10] + dist_rec[11] + dist_rec[12] + dist_rec[13] + dist_rec[14] + dist_rec[15] + dist_rec[16] + dist_rec[17] + dist_rec[18] + dist_rec[19]) / 10

	 if(old_mean_dist > 16 && old_mean_dist < 25 && recent_mean_dist < 14 && recent_mean_dist > 8){
	    printf("someone just entered the door\n");
       num_of_people++;
	    for(int i = 0; i < 20; i++)
	       dist_rec[i] = distance;
	 }
	    
	 if(recent_mean_dist > 16 && recent_mean_dist < 25 && old_mean_dist < 14 && old_mean_dist > 8){
	    printf("someone just went out the door\n");
       num_of_people--;
	    for(int i = 0; i < 20; i++)
	       dist_rec[i] = distance;
	 }
	 printf("%f %f %f\n", distance, old_mean_dist, recent_mean_dist);
    sprintf(msg, "%d",num_of_people);
    write(sock, msg, sizeof(msg));
	 usleep(150000);
   }
}

void *led_thd()
{
   if(-1 == GPIOExport(POUT2)){
		printf("gpio export err\n");
		exit(0);
	}
	usleep(100000);
   
   if(-1 == GPIODirection(POUT2, OUT)){
      printf("gpio direction err\n");
      exit(0);
   }
   
   GPIOWrite(POUT2, 0);
   PWMExport(0);
   PWMWritePeriod(0, 20000000);
   PWMWriteDutyCycle(0, 0);
   PWMEnable(0);
   char buffer2[2];
   int sock2;
   
   struct sockaddr_in serv_addr2;
	sock2 = socket(PF_INET, SOCK_STREAM, 0);
   if(sock2 == -1)
      error_handling("socket() error");
   memset(&serv_addr2, 0, sizeof(serv_addr2));
   serv_addr2.sin_family = AF_INET;
   serv_addr2.sin_addr.s_addr = inet_addr(argv_save[1]);
   serv_addr2.sin_port = htons(atoi(argv_save[3]));
   if(connect(sock2, (struct sockaddr*)&serv_addr2, sizeof(serv_addr2))==-1)
      error_handling("connect() error");
   
	while(1){
		while(warning==1){
			for(int i=0; i < 1500; i++)
			{
				PWMWriteDutyCycle(0,i*10000);
				usleep(200);
			}
			for(int i=1500; i > 0; i--)
			{
				PWMWriteDutyCycle(0,i*10000);
				usleep(200);
			}
         if(read(sock2, buffer2, sizeof(buffer2))!=0)
            warning = atoi(buffer2);
		}
		PWMWriteDutyCycle(0, 0);

		GPIOWrite(POUT2, HIGH);
		while(warning==0) {
         if(read(sock2, buffer2, sizeof(buffer2))!=0)
            warning = atoi(buffer2);
         usleep(600000);
      }
		
		GPIOWrite(POUT2, LOW);
	}
}

int main(int argc, char *argv[]) {
   
   
      pthread_t p_thread, p_thread2;
      int thr_id;
      int status;
      
      argv_save = argv;
      
      thr_id = pthread_create(&p_thread, NULL, ultrawave_thd, NULL);
      if(thr_id < 0){
         perror("thread create error : ");
         exit(0);
      }
      thr_id = pthread_create(&p_thread2, NULL, led_thd, NULL);
      if(thr_id < 0){
         perror("thread create error : ");
         exit(0);
      }
      
      pthread_join(p_thread, (void**)&status);
      pthread_join(p_thread2, (void**)&status);
      
      if(-1 == GPIOUnexport(POUT) || -1 == GPIOUnexport(PIN))
         return(-1);
      
      return(0);
}
