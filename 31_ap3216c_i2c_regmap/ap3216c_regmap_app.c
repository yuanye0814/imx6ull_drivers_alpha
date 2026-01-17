#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

// AP3216C数据解析函数
void parse_ap3216c_data(unsigned char *raw_data, unsigned short *als, unsigned short *ps, unsigned short *ir)
{
    // IR数据 (0x0A-0x0B): 10位数据
    if (raw_data[0] & 0x80) {
        *ir = 0; // IR溢出，无效数据
        printf("[WARNING] IR overflow detected (0x0A=0x%02x)\n", raw_data[0]);
    } else {
        *ir = (raw_data[1] << 2) | (raw_data[0] & 0x03); // IR[9:0] = 0x0B[7:0]<<2 + 0x0A[1:0]
    }
    
    // ALS数据 (0x0C-0x0D): 16位数据
    *als = (raw_data[3] << 8) | raw_data[2]; // ALS[15:0] = 0x0D[7:0] + 0x0C[7:0]
    
    // PS数据 (0x0E-0x0F): 10位数据，需要先读低字节再读高字节
    if ((raw_data[4] & 0x40) || (raw_data[5] & 0x40)) {
        *ps = 0; // IR溢出导致PS数据无效
        printf("[WARNING] PS data invalid due to IR overflow (0x0E=0x%02x, 0x0F=0x%02x)\n", raw_data[4], raw_data[5]);
    } else {
        *ps = ((raw_data[5] & 0x3F) << 4) | (raw_data[4] & 0x0F); // PS[9:0] = 0x0F[5:0]<<4 + 0x0E[3:0]
    }
    
    // 检查PS对象检测状态 (OBJ bit)
    if ((raw_data[4] & 0x80) || (raw_data[5] & 0x80)) {
        printf("[INFO] Object detected (close)\n");
    }
}

int main(int argc, char *argv[])
{
    int fd;
    unsigned char raw_data[6]; // 原始寄存器数据
    unsigned short als, ps, ir;
    
    if(argc < 2) {
        printf("Usage: %s <device_file>\n", argv[0]);
        printf("Example: %s /dev/ap3216c\n", argv[0]);
        return 1;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    printf("Reading AP3216C sensor data... Press Ctrl+C to exit\n");
    
    while (1) {
        if (read(fd, raw_data, sizeof(raw_data)) == sizeof(raw_data)) {
            parse_ap3216c_data(raw_data, &als, &ps, &ir);
            printf("ALS: %5d, PS: %5d, IR: %5d\n", als, ps, ir);
        } else {
            perror("Read failed");
            break;
        }
        usleep(500000);
    }

    close(fd);
    return 0;
}