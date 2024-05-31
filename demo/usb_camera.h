#ifndef _USB_CAMERA_H_
#define _USB_CAMERA_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <getopt.h> 
#include <fcntl.h> 
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h> 
#include <linux/videodev2.h>

#define VIDEO_DEV "/dev/video0"

typedef struct _buffer
{
    void *start;
    size_t length;
}buffer;

int open_camer_device(char * videoDev);
int init_mmap(int fd);
int init_camer_device(int fd);
int start_capturing(int fd);
int process_image(void *addr,int length);
int read_frame(int fd);
int mainloop(int fd);
void stop_capturing(int fd);
void uninit_camer_device();
void close_camer_device(int fd);

void camera_get_image(void);

#endif