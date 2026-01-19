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

// 查找ADC设备目录
int find_adc_device_dir(const char *device_name, char *path, size_t path_len) {
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
                    
                    if (strstr(device_name_content, device_name) != NULL) {
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

// 读取ADC通道原始值
int read_adc_raw_value(const char *device_path, int channel, int *raw_value) {
    char filepath[MAX_PATH_LEN];
    FILE *file;
    
    snprintf(filepath, sizeof(filepath), "%s/in_voltage%d_raw", device_path, channel);
    
    file = fopen(filepath, "r");
    if (file != NULL) {
        if (fscanf(file, "%d", raw_value) == 1) {
            fclose(file);
            return 0;
        }
        fclose(file);
    }
    
    return -1;
}

// 读取ADC参考电压
int read_adc_reference_voltage(const char *device_path, float *vref) {
    char filepath[MAX_PATH_LEN];
    FILE *file;
    
    snprintf(filepath, sizeof(filepath), "%s/vref-supply", device_path);
    
    file = fopen(filepath, "r");
    if (file != NULL) {
        if (fscanf(file, "%f", vref) == 1) {
            *vref /= 1000000; // Convert from microvolts to volts
            fclose(file);
            return 0;
        }
        fclose(file);
    }
    
    // 如果无法读取，则返回默认值
    *vref = 3.3;
    return -1;
}

// 读取ADC比例因子
int read_adc_scale(const char *device_path, float *scale) {
    char filepath[MAX_PATH_LEN];
    FILE *file;
    
    snprintf(filepath, sizeof(filepath), "%s/in_voltage_scale", device_path);
    
    file = fopen(filepath, "r");
    if (file != NULL) {
        if (fscanf(file, "%f", scale) == 1) {
            fclose(file);
            return 0;
        }
        fclose(file);
    }
    
    // 默认为1.0，表示无缩放
    *scale = 1.0;
    return -1;
}

int main(int argc, char *argv[])
{
    char device_path[MAX_PATH_LEN];
    int adc_channel_0, adc_channel_1;
    float voltage_0, voltage_1;
    float reference_voltage = 3.3;
    float scale_factor = 1.0;
    int num_channels = 2;
    
    if(argc < 2) {
        printf("Usage: %s <adc_device_name>\n", argv[0]);
        printf("Example: %s adc\n", argv[0]);
        return 1;
    }

    // 查找ADC设备目录
    if (find_adc_device_dir(argv[1], device_path, sizeof(device_path)) < 0) {
        fprintf(stderr, "ADC device '%s' not found!\n", argv[1]);
        return 1;
    }
    
    printf("Found ADC device at: %s\n", device_path);

    // 读取参考电压
    if (read_adc_reference_voltage(device_path, &reference_voltage) == 0) {
        printf("Reference voltage: %.2fV\n", reference_voltage);
    } else {
        printf("Using default reference voltage: %.2fV\n", reference_voltage);
    }
    
    // 读取缩放因子
    if (read_adc_scale(device_path, &scale_factor) == 0) {
        printf("Scale factor: %.6f\n", scale_factor);
    } else {
        printf("Using default scale factor: %.6f\n", scale_factor);
    }
    
    printf("Reading ADC data from %d channels... Press Ctrl+C to exit\n", num_channels);
    printf("Reference voltage: %.2fV\n", reference_voltage);
    
    // 读取ADC数据
    while (1) {
        // 读取ADC通道0的原始值
        if (read_adc_raw_value(device_path, 0, &adc_channel_0) == 0) {
            voltage_0 = adc_channel_0 * scale_factor;
            
            // 读取ADC通道1的原始值
            if (read_adc_raw_value(device_path, 1, &adc_channel_1) == 0) {
                voltage_1 = adc_channel_1 * scale_factor;
                
                printf("ADC Channel 0 - Raw: %d, Voltage: %.3fmV | ADC Channel 1 - Raw: %d, Voltage: %.3fmV\n", 
                       adc_channel_0, voltage_0, adc_channel_1, voltage_1);
            } else {
                voltage_1 = 0.0;
                printf("ADC Channel 0 - Raw: %d, Voltage: %.3fmV | Failed to read Channel 1\n", 
                       adc_channel_0, voltage_0);
            }
        } else {
            printf("Failed to read ADC Channel 0\n");
        }
        
        usleep(500000); // 500ms延迟
    }

    return 0;
}