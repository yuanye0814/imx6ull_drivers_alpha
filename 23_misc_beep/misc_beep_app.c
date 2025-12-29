#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    int fd, ret;
    char buf[64];

    if(argc < 3) {
        printf("Usage: %s <device_file> <1-read,2-write> [string]\n", argv[0]);
        return 1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    switch(atoi(argv[2])) {
    case 1: // read
        ret = read(fd, buf, sizeof(buf) - 1);
        if (ret > 0) {
            buf[ret] = '\0';
            printf("Read: %s", buf);
        } else {
            perror("Read failed");
        }
        break;
        
    case 2: // write
        if (argc < 4) {
            printf("Please provide a string to write\n");
            close(fd);
            return 1;
        }
        ret = write(fd, argv[3], strlen(argv[3]));
        if (ret > 0) {
            printf("Wrote %d bytes\n", ret);
        } else {
            perror("Write failed");
        }
        break;
        
    default:
        printf("Invalid command (use 1 for read, 2 for write)\n");
    }

    close(fd);
    return 0;
}