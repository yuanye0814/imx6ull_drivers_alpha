#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#define LED_IOCTL_MAGIC 'x'
#define LED_ON          _IO(LED_IOCTL_MAGIC, 0)
#define LED_OFF         _IO(LED_IOCTL_MAGIC, 1)
#define LED_GET_STATE   _IOR(LED_IOCTL_MAGIC, 2, int)
#define LED_SET_PERIOD  _IOW(LED_IOCTL_MAGIC, 3, int)
#define LED_GET_PERIOD  _IOR(LED_IOCTL_MAGIC, 4, int)

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

    if(argc < 3) {
        printf("Usage: %s <device_file> <1-read,2-write,3-ioctl> <string to write>\n", argv[0]);
        return -1;
    }
    filename = argv[1];
    if(filename == NULL) {
        printf("Please provide a device file\n");
        return -1;
    }

    cmd = atoi(argv[2]);

    fd = open(filename, O_RDWR);
    if (fd < 0) {
        printf("Failed to open device file\n");
        return -1;
    }

    if(cmd == 1)
    {
        int ret = 0;

        memset(buf, 0, sizeof(buf));

        ret = read(fd, buf, sizeof(buf) - 1);
        if(ret < 0)
        {
            printf("Failed to read from device file\n");
        }
        else if(ret == 0)
        {
            printf("No data available (EOF)\n");
        }
        else
        {
            buf[ret] = '\0'; // Null-terminate the string
            printf("Read %d bytes from device: %s\n", ret, buf);
        }

    }
    else if(cmd == 2) // write
    {
        int ret = 0;
        if(argc < 4) {
            printf("Please provide a string to write\n");
            close(fd);
            return -1;
        }
        ret = write(fd, argv[3], strlen(argv[3]));
        if(ret < 0)
        {
            printf("Failed to write to device file\n");
        }
        else
        {
            printf("Wrote %d to device\n", ret);
        }
    }
    else if(cmd == 3) // ioctl
    {
        int ret = 0;
        int period = 0;
        int state = 0;
        int ioctl_cmd = 0;

        while(1)
        {
            // print cmd
            printf("\nPlease input cmd:\n");
            printf("1: Turn on LED\n");
            printf("2: Turn off LED\n");
            printf("3: Get LED state\n");
            printf("4: Set LED period\n");
            printf("5: Get LED period\n");
            printf("0: Exit\n");

            printf("Enter command (0-5): ");
            ret = scanf("%d", &ioctl_cmd);
            if(ret != 1)
            {
                // 清理输入缓冲区
                int c;
                while((c = getchar()) != '\n' && c != EOF);
                printf("Invalid input, please enter a number\n");
                continue;
            }
            

            // handle cmd
            if(ioctl_cmd == 0)
            {
                break;
            }
            else if(ioctl_cmd == 1)
            {
                ret = ioctl(fd, LED_ON);
                if(ret < 0)
                {
                    printf("Failed to turn on LED\n");
                }
                else
                {
                    printf("Turned on LED\n");
                }
            }
            else if(ioctl_cmd == 2)
            {
                ret = ioctl(fd, LED_OFF);
                if(ret < 0)
                {
                    printf("Failed to turn off LED\n");
                }
                else
                {
                    printf("Turned off LED\n");
                }
            }
            else if(ioctl_cmd == 3)
            {
                ret = ioctl(fd, LED_GET_STATE);
                if(ret < 0)
                {
                    printf("Failed to get LED state\n");
                }
                else
                {
                    printf("LED state: %d\n", ret);
                }
            }
            else if(ioctl_cmd == 4)
            {
                int new_period = 0;
                // get input period
                printf("please input perid\n");
                ret = scanf("%d", &new_period);
                if(ret != 1)
                {
                    // 清理输入缓冲区
                    int c;
                    while((c = getchar()) != '\n' && c != EOF);
                    printf("Invalid input, please enter a number\n");
                    continue;
                }

                ret = ioctl(fd, LED_SET_PERIOD, new_period);
                if(ret < 0)
                {
                    printf("Failed to set LED period\n");
                }
                else
                {
                    printf("Set LED period\n");
                }
            }
            else if(ioctl_cmd == 5)
            {
                ret = ioctl(fd, LED_GET_PERIOD);
                if(ret < 0)
                {
                    printf("Failed to get LED period\n");
                }
                else
                {
                    printf("LED period: %d\n", ret);
                }
            }
            else
            {
                printf("Invalid command\n");
            }
        }
    }
    else
    {
        printf("Invalid command\n");
    }

    close(fd);
    return 0;
}