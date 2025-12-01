#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

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

    if(argc < 3) {
        printf("Usage: %s <device_file> <1-read,2-write> <string to write>\n", argv[0]);
        return -1;
    }
    filename = argv[1];
    cmd = atoi(argv[2]);

    // "/dev/char_dev_base"
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
    else
    {
        printf("Invalid command\n");
    }

    // loop
    while(1)
    {
        sleep(5);
        cnt++;
        printf("atomic app running: %d\n", cnt);
        if(cnt>=6)
        {
            printf("atomic app exit\n");
            break;
        
        }
    }

    close(fd);
    return 0;
}