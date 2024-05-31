#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
 
#define FBDEVICE "/dev/graphics/fb0"
void draw_back(unsigned int *pfb, unsigned int width, unsigned int height, unsigned int color);
 
void draw_line(unsigned int *pfb, unsigned int width, unsigned int height);
 
int main(void)
{
    int fd = -1;
    int ret = -1;
    unsigned int *pfb = NULL;
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;
 
    fd = open(FBDEVICE, O_RDWR);
    if (fd < 0)
    {
        perror("open");
        return -1;
    }
    printf("open %s success \n", FBDEVICE);
 
   
    ret = ioctl(fd, FBIOGET_FSCREENINFO, &finfo);
    if (ret < 0)
    {
        perror("ioctl");
        return -1;
    }
 
    ret = ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
    if (ret < 0)
    {
        perror("ioctl");
        return -1;
    }
    
    pfb = (unsigned int *)mmap(NULL, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (NULL == pfb)
    {
        perror("mmap");
        return -1;
    }
    printf("pfb :0x%x \n", *pfb);
 
    draw_back(pfb, vinfo.xres_virtual, vinfo.yres_virtual, 0xffff0000);
    draw_line(pfb, vinfo.xres_virtual, vinfo.yres_virtual);
 
    close(fd);
    return 0;
}
 
 
void draw_back(unsigned int *pfb, unsigned int width, unsigned int height, unsigned int color)
{
    unsigned int x, y;
    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            *(pfb + y * width + x) = color;
        }
    }
}
 
void draw_line(unsigned int *pfb, unsigned int width, unsigned int height)
{
    unsigned int x, y;
    for (x = 50; x < width - 50; x++)
    {
        *(pfb + 50 * width + x) = 0xffffff00;
    }
    for (y = 50; y < height -50; y++)
    {
        *(pfb + y * width + 50) = 0xffffff00;
    }
}