#include "usb_camera.h"
#define DEBUG

buffer *user_buf = NULL;
static unsigned int n_buffer = 0;
static unsigned long file_length;
char   picture_name[20] ="demo_picture"; 
int    num = 0;

int open_camer_device(char *videoDev)
{
    int fd;

    if((fd = open(videoDev,O_RDWR | O_NONBLOCK)) < 0)
    {
        perror("Fail to open");
        pthread_exit(NULL);
    } 
    return fd;
}

int init_camer_device(int fd)
{
    struct v4l2_fmtdesc fmt;
    struct v4l2_capability cap;
    struct v4l2_format stream_fmt;
    int ret;

    ret = ioctl(fd,VIDIOC_QUERYCAP,&cap);
    if(ret < 0)
    {
        perror("FAIL to ioctl VIDIOC_QUERYCAP");
        exit(EXIT_FAILURE);
    }

    if(!(cap.capabilities & V4L2_BUF_TYPE_VIDEO_CAPTURE))
    {
        perror("The Current device is not a video capture device\n");
        exit(EXIT_FAILURE);

    }

    if(!(cap.capabilities & V4L2_CAP_STREAMING))
    {
        perror("The Current device does not support streaming i/o\n");
        exit(EXIT_FAILURE);
    }

    memset(&fmt,0,sizeof(fmt));
    fmt.index = 0;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    while((ret = ioctl(fd,VIDIOC_ENUM_FMT,&fmt)) == 0)
    {
        fmt.index ++ ;
#ifdef DEBUG
        printf("{pixelformat = %c%c%c%c},description = '%s'\n",
                fmt.pixelformat & 0xff,(fmt.pixelformat >> 8)&0xff,
                (fmt.pixelformat >> 16) & 0xff,(fmt.pixelformat >> 24)&0xff,
                fmt.description);
#endif
    }

    stream_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    stream_fmt.fmt.pix.width = 680;
    stream_fmt.fmt.pix.height = 480;
    stream_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    stream_fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if(-1 == ioctl(fd,VIDIOC_S_FMT,&stream_fmt))
    {
        perror("Fail to ioctl");
        exit(EXIT_FAILURE);
    }

    file_length = stream_fmt.fmt.pix.bytesperline * stream_fmt.fmt.pix.height; 

    init_mmap(fd);

    return 0;
}

int init_mmap(int fd)
{
    int i;
    struct v4l2_requestbuffers reqbuf;

    bzero(&reqbuf,sizeof(reqbuf));
    reqbuf.count = 4;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;

    if(-1 == ioctl(fd,VIDIOC_REQBUFS,&reqbuf))
    {
        perror("Fail to ioctl 'VIDIOC_REQBUFS'");
        exit(EXIT_FAILURE);
    }

    n_buffer = reqbuf.count;
#ifdef DEBUG
    printf("n_buffer = %d\n",n_buffer);
#endif
    user_buf = calloc(reqbuf.count,sizeof(*user_buf));
    if(user_buf == NULL)
    {
        fprintf(stderr,"Out of memory\n");
        exit(EXIT_FAILURE);
    }

    for(i = 0; i< 4; i ++)
    {
        struct v4l2_buffer buf;

        bzero(&buf,sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if(-1 == ioctl(fd,VIDIOC_QUERYBUF,&buf))
        {
            perror("Fail to ioctl : VIDIOC_QUERYBUF");
            exit(EXIT_FAILURE);
        }

        user_buf[i].length = buf.length;
        user_buf[i].start = 
            mmap(
                    NULL,
                    buf.length,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    fd,buf.m.offset
                );
        if(MAP_FAILED == user_buf[i].start)
        {
            perror("Fail to mmap");
            exit(EXIT_FAILURE);
        }
    }   

    return 0;
}

int start_capturing(int fd)
{
    unsigned int i;
    enum v4l2_buf_type type;

    for(i = 0;i < n_buffer;i ++)
    {
        struct v4l2_buffer buf;

        bzero(&buf,sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if(-1 == ioctl(fd,VIDIOC_QBUF,&buf))
        {
            perror("Fail to ioctl 'VIDIOC_QBUF'");
            exit(EXIT_FAILURE);
        }
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if(-1 == ioctl(fd,VIDIOC_STREAMON,&type))
    {
        perror("Fail to ioctl 'VIDIOC_STREAMON'");
        exit(EXIT_FAILURE);
    }

    return 0;
}

int mainloop(int fd)
{ 
    int count = 2;

    while(count-- > 0)
    {
        for(;;)
        {
            fd_set fds;
            struct timeval tv;
            int r;

            FD_ZERO(&fds);
            FD_SET(fd,&fds);

            tv.tv_sec = 2;
            tv.tv_usec = 0;

            r = select(fd + 1,&fds,NULL,NULL,&tv);

            if(-1 == r)
            {
                if(EINTR == errno)
                    continue;

                perror("Fail to select");
                exit(EXIT_FAILURE);
            }

            if(0 == r)
            {
                fprintf(stderr,"select Timeout\n");
                exit(EXIT_FAILURE);
            }

            if(read_frame(fd))
                break;
        }
    }
    return 0;
}


int process_image(void *addr,int length)
{
    FILE *fp;
    char name[20];

    sprintf(name,"%s%d.jpg",picture_name,num ++);

    if((fp = fopen(name,"w")) == NULL)
    {
        perror("Fail to fopen");
        exit(EXIT_FAILURE);
    }

    fwrite(addr,length,1,fp);
    usleep(500);

    fclose(fp);

    return 0;
}

int read_frame(int fd)
{
    struct v4l2_buffer buf;
    //unsigned int i;

    bzero(&buf,sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if(-1 == ioctl(fd,VIDIOC_DQBUF,&buf))
    {
        perror("Fail to ioctl 'VIDIOC_DQBUF'");
        exit(EXIT_FAILURE);
    }

    assert(buf.index < n_buffer);
    {
#ifdef DEBUG
        printf ("buf.index dq is %d,\n",buf.index);
#endif
    }
    process_image(user_buf[buf.index].start,user_buf[buf.index].length);

    if(-1 == ioctl(fd,VIDIOC_QBUF,&buf))
    {
        perror("Fail to ioctl 'VIDIOC_QBUF'");
        exit(EXIT_FAILURE);
    }

    return 1;
}

void stop_capturing(int fd)
{
    enum v4l2_buf_type type;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(-1 == ioctl(fd,VIDIOC_STREAMOFF,&type))
    {
        perror("Fail to ioctl 'VIDIOC_STREAMOFF'");
        exit(EXIT_FAILURE);
    }
    return;
}

void uninit_camer_device()
{
    unsigned int i;

    for(i = 0;i < n_buffer; i ++)
    {
        if(-1 == munmap(user_buf[i].start,user_buf[i].length))
        {
            exit(EXIT_FAILURE);
        }
    }

    free(user_buf);

    return;
}

void close_camer_device(int fd)
{
    if(-1 == close(fd))
    {
        perror("Fail to close fd");
        exit(EXIT_FAILURE);
    }

    return;
}


int main(int argc, char* argv[])
{   
    int camera_fd;       
    if(argc == 2 )
    {
        camera_fd = open_camer_device(argv[1]);
        init_camer_device(camera_fd);
        start_capturing(camera_fd);

        num = 0;

        mainloop(camera_fd);

        stop_capturing(camera_fd);
        uninit_camer_device();
        close_camer_device(camera_fd);

        printf("Camera get pic success!\n");
    }
    else
    {
        printf("Please input video device!\n");
    }
    return 0;

}