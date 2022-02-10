#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define IN 0
#define OUT 1

#define LOW 0
#define HIGH 1
#define VALUE_MAX 256

#define BUFFER_MAX 3
#define DIRECTION_MAX 45

#define i2c_addr 0x27
#define LCD_WIDTH 16

#define LCD_CHR 1
#define LCD_CMD 0

#define LCD_LINE_1 0x80
#define LCD_LINE_2 0xC0
#define LCD_LINE_3 0x94
#define LCD_LINE_4 0xD4

#define LCD_ON 0x08

#define ENABLE 0b00000100

#define E_delay 0.0005
#define E_pulse 0.0005

int press = 0, num_of_people = 0;
float temperature = 0;

int file_i2c;
int length = 0;

void error_handling(char *message){
	fputs(message,stderr);
	fputc('\n',stderr);
	exit(1);
}

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

static void OPEN_I2C_BUS()
{
	char *filename = (char*)"/dev/i2c-1";
	if ((file_i2c = open(filename, O_RDWR)) < 0)
	{
		//ERROR HANDLING: you can check errno to see what went wrong
		printf("Failed to open the i2c bus");
	}
	int addr = 0x27;          //<<<<<The I2C address of the slave
	if (ioctl(file_i2c, I2C_SLAVE, addr) < 0)
	{
		printf("Failed to acquire bus access and/or talk to slave.\n");
		//ERROR HANDLING; you can check errno to see what went wrong
	}
}

static int WRITE_BYTES(int addr, int output)
{
	length = 1;			//<<< Number of bytes to write
	ioctl(file_i2c, 0, 0x27);

	if (write(file_i2c, &output, length) != length)
		//write() returns the number of bytes actually written, 
		//if it doesn't match then an error occurred (e.g. no response from the device)
	{
		/* ERROR HANDLING: i2c transaction failed */
		printf("Failed to write to the i2c bus.\n");
	}
	return 0;
}

static void lcd_toggle_enable(int bits)
{
	usleep(E_delay);
	WRITE_BYTES(0x27, (bits | ENABLE));
	usleep(E_pulse);
	WRITE_BYTES(0x27, (bits & ~ENABLE));
	usleep(E_delay);

}
static void LCD_BYTE(int bits, int mode) // bits = ascii code
{
	unsigned int bits_high = mode | (bits & 0xF0) | LCD_ON;
	unsigned int bits_low = mode | ((bits << 4) & 0xF0) | LCD_ON;

	//HIGH BITS
	WRITE_BYTES(0x27, bits_high);
	lcd_toggle_enable(bits_high);
	//LOW BITS
	WRITE_BYTES(0x27, bits_low);
	lcd_toggle_enable(bits_low);

}
static void LCD_INIT()
{
	LCD_BYTE(0x33, LCD_CMD);
	LCD_BYTE(0x32, LCD_CMD);
	LCD_BYTE(0x06, LCD_CMD);
	LCD_BYTE(0x0C, LCD_CMD);
	LCD_BYTE(0x28, LCD_CMD);
	LCD_BYTE(0x01, LCD_CMD);

	usleep(E_delay);
}
static void lcd_string(char buffer[], int line)
{
	LCD_BYTE(line, LCD_CMD);

	for (int i = 0; i < LCD_WIDTH; i++)
	{
		LCD_BYTE((char)buffer[i], LCD_CHR);
	}
}

void *press_listening_thd(void *data){
	char* port_num = (char*)data;
	char buffer[10];
	int serv_sock, clnt_sock=-1;
	struct sockaddr_in serv_addr, clnt_addr;
	socklen_t clnt_addr_size;
	serv_sock = socket(PF_INET, SOCK_STREAM, 0);
	if(serv_sock == -1)
		error_handling("socket() error");
	memset(&serv_addr, 0 , sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(port_num));
	if(bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr))==-1)
		error_handling("bind() error");
	if(listen(serv_sock,5) == -1)
		error_handling("listen() error");
	if(clnt_sock<0){
		clnt_addr_size = sizeof(clnt_addr);
		clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
		if(clnt_sock == -1)
			error_handling("accept() error");
	}
	while(1){
		if (read(clnt_sock, buffer, sizeof(buffer)) != 0)
			press = atoi(buffer);
		else press = 0;
		usleep(100000);
	}
	close(clnt_sock);
	clnt_sock = -1;
	close(serv_sock);
}

void *temperature_listening_thd(void *data){
	char buffer2[6];
	char* port_num2 = (char*)data;
	float temp;
	int serv_sock2, clnt_sock2=-1;
	struct sockaddr_in serv_addr2, clnt_addr2;
	socklen_t clnt_addr_size2;
	serv_sock2 = socket(PF_INET, SOCK_STREAM, 0);
	if(serv_sock2 == -1)
		error_handling("socket() error");
	memset(&serv_addr2, 0 , sizeof(serv_addr2));
	serv_addr2.sin_family = AF_INET;
	serv_addr2.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr2.sin_port = htons(atoi(port_num2));
	if(bind(serv_sock2, (struct sockaddr*) &serv_addr2, sizeof(serv_addr2))==-1)
		error_handling("bind() error");
	if(listen(serv_sock2,5) == -1)
		error_handling("listen() error");
	if(clnt_sock2<0){
		clnt_addr_size2 = sizeof(clnt_addr2);
		clnt_sock2 = accept(serv_sock2, (struct sockaddr*)&clnt_addr2, &clnt_addr_size2);
		if(clnt_sock2 == -1)
			error_handling("accept() error");
	}
	while(1){
		if(read(clnt_sock2, buffer2, sizeof(buffer2))!=0){
			temp = atof(buffer2);
			if(temp != 0) temperature = temp;
		}	
		usleep(1000000);
	}
	close(clnt_sock2);
	clnt_sock2 = -1;
	close(serv_sock2);
}

void *people_listening_thd(void *data){
	char buffer3[10];
	char* port_num3 = (char*)data;
	int serv_sock3, clnt_sock3=-1;
	struct sockaddr_in serv_addr3, clnt_addr3;
	socklen_t clnt_addr_size3;
	serv_sock3 = socket(PF_INET, SOCK_STREAM, 0);
	if(serv_sock3 == -1)
		error_handling("socket() error");
	memset(&serv_addr3, 0 , sizeof(serv_addr3));
	serv_addr3.sin_family = AF_INET;
	serv_addr3.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr3.sin_port = htons(atoi(port_num3));
	if(bind(serv_sock3, (struct sockaddr*) &serv_addr3, sizeof(serv_addr3))==-1)
		error_handling("bind() error");
	if(listen(serv_sock3,5) == -1)
		error_handling("listen() error");
	if(clnt_sock3<0){
		clnt_addr_size3 = sizeof(clnt_addr3);
		clnt_sock3 = accept(serv_sock3, (struct sockaddr*)&clnt_addr3, &clnt_addr_size3);
		if(clnt_sock3 == -1)
			error_handling("accept() error");
	}
	while(1){
		if(read(clnt_sock3, buffer3, sizeof(buffer3))!=0)
			num_of_people = atoi(buffer3);
		usleep(100000);
	}
	close(clnt_sock3);
	clnt_sock3 = -1;
	close(serv_sock3);
}


void *door_control_thd(){
	int i;
	char notice[17];
	OPEN_I2C_BUS();
	LCD_INIT();
	while(1){
	//temperature_check
		LCD_INIT();
		sleep(1);
		lcd_string("Please check    ", LCD_LINE_1);
		lcd_string("Your Temperature", LCD_LINE_2);
		while(temperature<30 || temperature > 37){
			usleep(100000);
		}
		LCD_INIT();
		sleep(1);
		sprintf(notice, "Temperature %.1f", temperature);
		lcd_string(notice, LCD_LINE_1);
		sprintf(notice, "You are normal. ", temperature);
		lcd_string(notice, LCD_LINE_2);
		usleep(3000000);
		LCD_INIT();
		sleep(1);
		lcd_string("Please use      ", LCD_LINE_1);
		lcd_string("hand sanitizer  ", LCD_LINE_2);
		for(i = 0; i < 100; i++){
			if(press >= 300)break;
			usleep(100000);
		}
		LCD_INIT();
		sleep(1);
		if(i!=100) {
			lcd_string("Welcome!        ", LCD_LINE_1);
			PWMWriteDutyCycle(0,2000000);
			usleep(5000000);
			PWMWriteDutyCycle(0,750000);
			LCD_INIT();
		}
	}
	LCD_BYTE(0x01, LCD_CMD);
}

void *people_check_thd(void *data)
{
	char msg[2];
	char buffer4[10];
	char* port_num4 = (char*)data;
	int serv_sock4, clnt_sock4=-1;
	struct sockaddr_in serv_addr4, clnt_addr4;
	socklen_t clnt_addr_size4;
	serv_sock4 = socket(PF_INET, SOCK_STREAM, 0);
	if(serv_sock4 == -1)
		error_handling("socket() error");
	memset(&serv_addr4, 0 , sizeof(serv_addr4));
	serv_addr4.sin_family = AF_INET;
	serv_addr4.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr4.sin_port = htons(atoi(port_num4));
	if(bind(serv_sock4, (struct sockaddr*) &serv_addr4, sizeof(serv_addr4))==-1)
		error_handling("bind() error");
	if(listen(serv_sock4,5) == -1)
		error_handling("listen() error");
	if(clnt_sock4<0){
		clnt_addr_size4 = sizeof(clnt_addr4);
		clnt_sock4 = accept(serv_sock4, (struct sockaddr*)&clnt_addr4, &clnt_addr_size4);
		if(clnt_sock4 == -1)
			error_handling("accept() error");
	}
	while(1){
		if(num_of_people >= 5) {
			sprintf(msg, "1"); //size.
			write(clnt_sock4, msg, sizeof(msg));
        }
		else {
			sprintf(msg, "0"); //size.
			write(clnt_sock4, msg, sizeof(msg));
        }
        usleep(1500000);
	}
}

int main(int argc, char *argv[]) {
	PWMExport(0);
	PWMWritePeriod(0, 20000000);
	PWMWriteDutyCycle(0, 750000);
	PWMEnable(0);

	pthread_t p_thread[5];
	int thr_id[5];
	if(argc!=5){
		printf("Usage : %s <port>\n",argv[0]);
		exit(0);
	}
	thr_id[0] = pthread_create(&p_thread[0], NULL, temperature_listening_thd, argv[1]);
	if(thr_id[0] < 0){
		perror("thread create error : ");
		exit(0);
	}
	thr_id[1] = pthread_create(&p_thread[1], NULL, press_listening_thd, argv[2]);
	if(thr_id[1] < 0){
		perror("thread create error : ");
		exit(0);
	}
	thr_id[2] = pthread_create(&p_thread[2], NULL, people_listening_thd, argv[3]);
	if(thr_id[2] < 0){
		perror("thread create error : ");
		exit(0);
	}
	thr_id[3] = pthread_create(&p_thread[3], NULL, door_control_thd, NULL);
	if(thr_id[0] < 0){
		perror("thread create error : ");
		exit(0);
	}
	thr_id[4] = pthread_create(&p_thread[4], NULL, people_check_thd, argv[4]);
	if(thr_id[4] < 0){
		perror("thread create error : ");
		exit(0);
	}
	pthread_join(p_thread[0], NULL);
	pthread_join(p_thread[1], NULL);
	pthread_join(p_thread[2], NULL);
	pthread_join(p_thread[3], NULL);
	pthread_join(p_thread[4], NULL);
	return(0);
}
