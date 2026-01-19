#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>

#define MAX_PATH_LEN 256
#define MAX_NAME_LEN 50

// 查找指定名称的IIO设备
int find_iio_device_dir(const char *device_name, char *path, size_t path_len) {
    DIR *dir;
    struct dirent *entry;
    char sys_path[MAX_PATH_LEN];
    char name_path[MAX_PATH_LEN];
    char device_name_content[MAX_NAME_LEN];
    FILE *file;
    
    snprintf(sys_path, sizeof(sys_path), "/sys/bus/iio/devices");
    dir = opendir(sys_path);
    
    if (dir == NULL) {
        perror("Cannot open /sys/bus/iio/devices");
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "iio:device", strlen("iio:device")) == 0) {
            snprintf(name_path, sizeof(name_path), "%s/%s/name", sys_path, entry->d_name);
            file = fopen(name_path, "r");
            if (file != NULL) {
                if (fgets(device_name_content, sizeof(device_name_content), file) != NULL) {
                    // 移除换行符
                    device_name_content[strcspn(device_name_content, "\n")] = 0;
                    
                    if (strcmp(device_name_content, device_name) == 0) {
                        snprintf(path, path_len, "/sys/bus/iio/devices/%s", entry->d_name);
                        fclose(file);
                        closedir(dir);
                        return 0;
                    }
                }
                fclose(file);
            }
        }
    }
    
    closedir(dir);
    return -1;
}

// 读取IIO通道值
int read_channel_value(const char *device_path, const char *filename, float *value) {
    char filepath[MAX_PATH_LEN];
    FILE *file;
    
    snprintf(filepath, sizeof(filepath), "%s/%s", device_path, filename);
    
    file = fopen(filepath, "r");
    if (file != NULL) {
        if (fscanf(file, "%f", value) == 1) {
            fclose(file);
            return 0;
        }
        fclose(file);
    }
    
    return -1;
}

int main(int argc, char *argv[])
{
    char device_path[MAX_PATH_LEN];
    
    // 缩放因子
    float accel_scale = 0.0, gyro_scale = 0.0, temp_scale = 0.0, temp_offset = 0.0;
    float accel_x = 0.0, accel_y = 0.0, accel_z = 0.0;
    float gyro_x = 0.0, gyro_y = 0.0, gyro_z = 0.0;
    float temp = 0.0;
    
    if(argc < 2) {
        printf("Usage: %s <iio_device_name>\n", argv[0]);
        printf("Example: %s icm20608\n", argv[0]);
        return 1;
    }

    // 查找IIO设备目录
    if (find_iio_device_dir(argv[1], device_path, sizeof(device_path)) < 0) {
        fprintf(stderr, "IIO device '%s' not found!\n", argv[1]);
        return 1;
    }
    
    printf("Found IIO device at: %s\n", device_path);

    // 读取缩放因子
    if (read_channel_value(device_path, "in_accel_scale", &accel_scale) == 0) {
        printf("Accelerometer scale: %.6f\n", accel_scale);
    } else {
        accel_scale = 0.000488; // 默认值，对应±16g量程
        printf("Using default accelerometer scale: %.6f\n", accel_scale);
    }
    
    if (read_channel_value(device_path, "in_anglvel_scale", &gyro_scale) == 0) {
        printf("Gyroscope scale: %.6f\n", gyro_scale);
    } else {
        gyro_scale = 0.061035; // 默认值，对应±2000dps量程
        printf("Using default gyroscope scale: %.6f\n", gyro_scale);
    }
    
    if (read_channel_value(device_path, "in_temp_scale", &temp_scale) == 0) {
        printf("Temperature scale: %.6f\n", temp_scale);
    } else {
        temp_scale = 0.00306; // 默认值
        printf("Using default temperature scale: %.6f\n", temp_scale);
    }
    
    if (read_channel_value(device_path, "in_temp_offset", &temp_offset) == 0) {
        printf("Temperature offset: %.6f\n", temp_offset);
    } else {
        temp_offset = 25.0; // 默认值
        printf("Using default temperature offset: %.6f\n", temp_offset);
    }
    
    printf("Reading ICM20608 IIO device data... Press Ctrl+C to exit\n");
    printf("Using scales - Accel: %.6f g/LSB, Gyro: %.6f dps/LSB, Temp: %.6f degC/LSB, Offset: %.2f degC\n", 
           accel_scale, gyro_scale, temp_scale, temp_offset);
    
    // 读取IIO设备数据
    while (1) {
        // 读取加速度计数据 - 尝试不同的文件名格式
        if (read_channel_value(device_path, "in_accel_x_raw", &accel_x) == 0 &&
            read_channel_value(device_path, "in_accel_y_raw", &accel_y) == 0 &&
            read_channel_value(device_path, "in_accel_z_raw", &accel_z) == 0) {
            
            // 读取陀螺仪数据
            if (read_channel_value(device_path, "in_anglvel_x_raw", &gyro_x) == 0 &&
                read_channel_value(device_path, "in_anglvel_y_raw", &gyro_y) == 0 &&
                read_channel_value(device_path, "in_anglvel_z_raw", &gyro_z) == 0) {
                
                // 读取温度数据
                if (read_channel_value(device_path, "in_temp_raw", &temp) == 0) {
                    // 应用缩放因子
                    float x_g = accel_x * accel_scale;
                    float y_g = accel_y * accel_scale;
                    float z_g = accel_z * accel_scale;
                    
                    float temp_c = (temp * temp_scale) + temp_offset;
                    
                    float x_dps = gyro_x * gyro_scale;
                    float y_dps = gyro_y * gyro_scale;
                    float z_dps = gyro_z * gyro_scale;
                    
                    printf("Accel X:%6.0f(%6.3fg) Y:%6.0f(%6.3fg) Z:%6.0f(%6.3fg) | Temp:%6.0f(%6.1f°C) | Gyro X:%6.0f(%7.2f°/s) Y:%6.0f(%7.2f°/s) Z:%6.0f(%7.2f°/s)\n", 
                           accel_x, x_g, accel_y, y_g, accel_z, z_g,
                           temp, temp_c,
                           gyro_x, x_dps, gyro_y, y_dps, gyro_z, z_dps);
                } else {
                    printf("Failed to read temperature data\n");
                }
            } else {
                printf("Failed to read gyroscope data\n");
            }
        } else {
            printf("Failed to read accelerometer data\n");
        }
        
        usleep(500000); // 500ms延迟
    }

    return 0;
}