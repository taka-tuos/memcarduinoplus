#include "serial.h"
#include <time.h>
#include <string.h>

#if defined(__MINGW32__) || defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#define sleep(n) Sleep(n * 1000)
#endif

int mc_read(SERIAL *device, FILE *fp, int i)
{
	unsigned char block[130];
	
	unsigned char frame_lsb = i & 0xff;
	unsigned char frame_msb = i >> 8;
	
	printf("block %d read", i);
	
	serial_write(device,"\xa2",1);
	
	serial_write(device,&frame_msb,1);
	serial_write(device,&frame_lsb,1);
	
	printf("...");
	
	unsigned char xor_data = (frame_msb ^ frame_lsb);
	
	while(serial_avaiable(device) < 130);
	
	serial_read(device,block,130);
	
	for(int j = 0; j < 128; j++)
	{
		xor_data ^= block[j];
	}
	
	if(block[129] == 0x47 && block[128] == xor_data) {
		fwrite(block,1,128,fp);
		i++;
		printf("done\n");
	} else {
		printf("fail\n");
	}
	
	return i;
}

int mc_write(SERIAL *device,FILE *fp, int i)
{
	unsigned char block[130];
	
	unsigned char frame_lsb = i & 0xff;
	unsigned char frame_msb = i >> 8;
	
	serial_write(device,"\xa3",1);
	
	serial_write(device,&frame_msb,1);
	serial_write(device,&frame_lsb,1);
	
	unsigned char xor_data = (frame_msb ^ frame_lsb);
	
	fread(block,1,128,fp);
	
	for(int j = 0; j < 128; j++)
	{
		xor_data ^= block[j];
	}
	
	printf("block %d write", i);
	
	serial_write(device,block,128);
	serial_write(device,&xor_data,1);
	
	printf("...");
	
	while(serial_avaiable(device) < 1);
	
	serial_read(device,block,1);
	
	if(block[0] == 0x47) {
		i++;
		printf("done\n");
	} else {
		fseek(fp,-128,SEEK_CUR);
		printf("fail(code=%02x)\n",block[0]);
	}
	
	return i;
}

int main(int argc, char *argv[])
{
	char portname[256] = "";
	char filename[256] = "";
	int rw_mode = -1;
	char *to_write = NULL;
	
	if(argc == 1) {
		printf("usage : %s -p Serialport [-r Filename] [-w Filename] [-c] [-help]\n\n",argv[0]);
		printf("-p    : Specify serial port(Required)\n");
		printf("-r    : Specify the data load destination file(Optional)\n");
		printf("-w    : Specify the data writing source file(Optional)\n");
		printf("-c    : Specify auto-naming mode (read-only, -r and -w will be ignore)(Optional)\n");
		printf("-help : Show this message\n");
		return 1;
	}
	
	for(int i = 1; i < argc; i++) {
		if(strcmp(argv[i],"-help") == 0) {
			printf("usage : %s -p Serialport [-r Filename] [-w Filename] [-c] [-help]\n\n",argv[0]);
			printf("-p    : Specify serial port(Required)\n");
			printf("-r    : Specify the data load destination file(Optional)\n");
			printf("-w    : Specify the data writing source file(Optional)\n");
			printf("-c    : Specify auto-naming mode (read-only, -r and -w will be ignore)(Optional)\n");
			printf("-help : Show this message\n");
			return 1;
		}
		
		if(to_write != NULL) {
			strcpy(to_write,argv[i]);
			to_write = NULL;
			continue;
		} else {
			if(strcmp(argv[i],"-p") == 0) {
				to_write = portname;
			}
			else if(strcmp(argv[i],"-r") == 0) {
				if(rw_mode != -1) {
					printf("The options \"-r\" and \"-w\" can not be used at the same time.\n");
					return 2;
				}
				to_write = filename;
				rw_mode = 0;
			}
			else if(strcmp(argv[i],"-w") == 0) {
				if(rw_mode != -1) {
					printf("The options \"-r\" and \"-w\" can not be used at the same time.\n");
					return 2;
				}
				to_write = filename;
				rw_mode = 1;
			}
			else if(strcmp(argv[i],"-c") == 0) {
				time_t utctime = time(NULL);
				struct tm *local = localtime(&utctime);
				sprintf(filename,"%d-%d-%d-%02d-%02d-%02d.mcr",
					local->tm_year+1900,
					local->tm_mon+1,
					local->tm_mday,
					local->tm_hour,
					local->tm_min,
					local->tm_sec);
				rw_mode = 0;
			}
		}
	}
	
	if(filename[0] == 0) {
		puts("nothing to do.");
		return 3;
	}
	
	if(portname[0] == 0) {
		puts("fatal : Serial port designation is a required option.");
		return 4;
	}
	
	SERIAL *device = serial_open(portname, SerialBaud38400);
	
	if(!device) {
		puts("fatal : Failed to open serial port.");
		return 5;
	}
	
	char id[7];
	
	sleep(2);
	
	while(serial_avaiable(device)) {
		char dmy;
		serial_read(device,&dmy,1);
	}
	
	puts("Detecting device ...");
	serial_write(device,"\xa0",1);
	if(serial_read_with_timeout(device,id,6,10000)) {
		puts("fatal : Connection failed.");
		return 6;
	} else {
		if(strncmp(id,"MCDINO",6) == 0) {
			puts("Detected MemCARDuino.");
		}
		else if(strncmp(id,"MCDPLS",6) == 0) {
			puts("Detected MemCarDuinoPlus.");
		}
		else {
			puts("fatal : Unknown Device");
			//puts(id);
			return 7;
		}
		unsigned char version;
		serial_write(device,"\xa1",1);
		while(serial_avaiable(device) < 1);
		serial_read(device,&version,1);
		int version_major = version >> 4, version_minor = version & 0xf;
		printf("Version : %d.%d\n",version_major,version_minor);
	}
	
	
	FILE *fp = fopen(filename, rw_mode == 0 ? "wb" : "rb");
	
	if(!fp) {
		printf("fatal : Failed to open the filename %s.\n",filename);
		return 6;
	}
	
	int i = 0;
	
	while(i < 1024)
	{
		if(rw_mode == 0) i = mc_read(device,fp,i);
		if(rw_mode == 1) i = mc_write(device,fp,i);
	}
	
	fclose(fp);
	serial_close(device);
	
	return 0;
}
