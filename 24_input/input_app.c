#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>

int main(int argc, char *argv[])
{
    int fd;
    struct input_event event;
    
    if(argc < 2) {
        printf("Usage: %s <input_device>\n", argv[0]);
        printf("Example: %s /dev/input/event0\n", argv[0]);
        return 1;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("Failed to open input device");
        return 1;
    }

    printf("Listening for key events... Press Ctrl+C to exit\n");
    
    while (1) {
        if (read(fd, &event, sizeof(event)) == sizeof(event)) {
            if (event.type == EV_KEY) {
                printf("Key event: code=%d, value=%d (%s)\n", 
                       event.code, event.value,
                       event.value ? "pressed" : "released");
                       
                if (event.code == KEY_ENTER) {
                    printf("ENTER key %s\n", 
                           event.value ? "pressed" : "released");
                }
            }
        } else {
            perror("Read failed");
            break;
        }
    }

    close(fd);
    return 0;
}