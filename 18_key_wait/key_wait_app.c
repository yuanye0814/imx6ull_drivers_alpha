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
        printf("Usage: %s <device_file> <1-read> \n", argv[0]);
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
        int key_val[2];

        while(1)
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
    else
    {
        printf("Invalid command\n");
    }

    close(fd);
    return 0;
}