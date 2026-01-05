#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

// ICM20608数据解析函数
void parse_icm20608_data(unsigned char *raw_data, short *accel_x, short *accel_y, short *accel_z, short *temp, short *gyro_x, short *gyro_y, short *gyro_z)
{
    // 加速度计数据 (0x3B-0x40)
    *accel_x = (raw_data[0] << 8) | raw_data[1];
    *accel_y = (raw_data[2] << 8) | raw_data[3];
    *accel_z = (raw_data[4] << 8) | raw_data[5];
    
    // 温度数据 (0x41-0x42)
    *temp = (raw_data[6] << 8) | raw_data[7];
    
    // 陀螺仪数据 (0x43-0x48)
    *gyro_x = (raw_data[8] << 8) | raw_data[9];
    *gyro_y = (raw_data[10] << 8) | raw_data[11];
    *gyro_z = (raw_data[12] << 8) | raw_data[13];
}

int main(int argc, char *argv[])
{
    int fd;
    unsigned char raw_data[14]; // 原始寄存器数据
    short accel_x, accel_y, accel_z;
    short temp;
    short gyro_x, gyro_y, gyro_z;
    float x_g, y_g, z_g;
    float temp_c;
    float x_dps, y_dps, z_dps;
    
    if(argc < 2) {
        printf("Usage: %s <device_file>\n", argv[0]);
        printf("Example: %s /dev/icm20608\n", argv[0]);
        return 1;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    printf("Reading ICM20608 accelerometer data... Press Ctrl+C to exit\n");
    
    while (1) {
        if (read(fd, raw_data, sizeof(raw_data)) == sizeof(raw_data)) {
            parse_icm20608_data(raw_data, &accel_x, &accel_y, &accel_z, &temp, &gyro_x, &gyro_y, &gyro_z);
            
            // 转换为g值 (±16g量程，2048 LSB/g)
            x_g = accel_x / 2048.0f;
            y_g = accel_y / 2048.0f;
            z_g = accel_z / 2048.0f;
            
            // 转换为温度 (326.8 LSB/°C, 25°C时为0)
            temp_c = (temp / 326.8f) + 25.0f;
            
            // 转换为角速度 (±2000dps量程，16.4 LSB/dps)
            x_dps = gyro_x / 16.4f;
            y_dps = gyro_y / 16.4f;
            z_dps = gyro_z / 16.4f;
            
            printf("Accel X:%6d(%6.3fg) Y:%6d(%6.3fg) Z:%6d(%6.3fg) | Temp:%6d(%6.1f°C) | Gyro X:%6d(%7.2f°/s) Y:%6d(%7.2f°/s) Z:%6d(%7.2f°/s)\n", 
                   accel_x, x_g, accel_y, y_g, accel_z, z_g,
                   temp, temp_c,
                   gyro_x, x_dps, gyro_y, y_dps, gyro_z, z_dps);
        } else {
            perror("Read failed");
            break;
        }
        usleep(500000);
    }

    close(fd);
    return 0;
}