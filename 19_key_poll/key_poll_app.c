#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>


#define USE_POLL   1
#define USE_SELECT 0


#if USE_POLL // poll
#include <poll.h>
#endif // pol

#if USE_SELECT // select
#include <sys/select.h>
#endif // select

/*
*
* This is a sample user application to demonstrate the usage of the
* character device driver implemented in char_dev_base.c.
* The application opens the device file, writes a string to it,
* reads the string back, and then closes the device file.
* ./char_dev_base_app /dev/char_dev_base <1-read,2-write> <string to write>
*/
int main(int argc, char *argv[])
{
    int fd;
    char buf[100];
    char *filename;
    int cmd;
    int cnt = 0;
#if USE_SELECT // select
    fd_set read_fds;
    struct timeval timeout;
#endif // select

#if USE_POLL // poll
    struct pollfd poll_fds[1];
#endif // poll
    if(argc < 3) {
        printf("Usage: %s <device_file> <1-read> \n", argv[0]);
        return -1;
    }
    filename = argv[1];
    cmd = atoi(argv[2]);

    // "/dev/char_dev_base"
    fd = open(filename, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        printf("Failed to open device file\n");
        return -1;
    }

    if(cmd == 1)
    {
        int ret = 0;
        int key_val[2];

        while(1)
        {
#if USE_SELECT // select
            FD_ZERO(&read_fds);
            FD_SET(fd, &read_fds);
            
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            
            ret = select(fd + 1, &read_fds, NULL, NULL, &timeout);
            if(ret > 0 && FD_ISSET(fd, &read_fds))
            {
                ret = read(fd, &key_val, sizeof key_val);
                if(ret < 0)
                {
                    printf("Failed to read from device file\n");
                }
                else if(ret == 0)
                {
                    // printf("No data available (EOF)\n");
                }
                else
                {
                    printf("key[%d]val = %d\n", key_val[0], key_val[1]);
                }
            }
            else if(ret == 0)
            {
                printf("select timeout\n");
            }
            else
            {
                printf("Error\n");
            }
#endif // select
        
#if USE_POLL // poll
            poll_fds[0].fd = fd;
            poll_fds[0].events = POLLIN;
            poll_fds[0].revents = 0;
            ret = poll(poll_fds, 1, 2000);
            if(ret > 0)
            {
                if(poll_fds[0].revents & POLLIN)
                {
                    ret = read(fd, &key_val, sizeof key_val);
                    if(ret < 0)
                    {
                        printf("Failed to read from device file\n");
                    }
                    else if(ret == 0)
                    {
                        // printf("No data available (EOF)\n");
                    }
                    else
                    {
                        printf("key[%d]val = %d\n", key_val[0], key_val[1]);
                    }
                }
            }
            else if(ret == 0)
            {
                printf("poll timeout\n");
            }
            else
            {
                printf("Error\n");
            }

#endif // poll

        }
    }
    else
    {
        printf("Invalid command\n");
    }

    close(fd);
    return 0;
}