#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger.h>

#define NAME "icm20608_iio"
#define ICM_20608_COUNT 1

// ICM20608寄存器定义
#define ICM20_SMPLRT_DIV        0x19    // 采样率分频器
#define ICM20_GYRO_CONFIG       0x1B    // 陀螺仪配置
#define ICM20_ACCEL_CONFIG      0x1C    // 加速度计配置
#define ICM20_ACCEL_CONFIG2     0x1D    // 加速度计配置2
#define ICM20_LP_MODE_CFG       0x1E    // 低功耗模式配置
#define ICM20_PWR_MGMT_1        0x6B    // 电源管理1
#define ICM20_PWR_MGMT_2        0x6C    // 电源管理2
#define ICM20_CONFIG            0x1A    // 配置寄存器
#define ICM20_FIFO_EN           0x23    // FIFO使能
#define ICM20_WHO_AM_I          0x75    // WHO AM I

#define ICM20_GYRO_XCALIB_H    0x13    // 陀螺仪X轴校准高字节
#define ICM20_GYRO_XCALIB_L    0x14    // 陀螺仪X轴校准低字节
#define ICM20_GYRO_YCALIB_H    0x15    // 陀螺仪Y轴校准高字节
#define ICM20_GYRO_YCALIB_L    0x16    // 陀螺仪Y轴校准低字节
#define ICM20_GYRO_ZCALIB_H    0x17    // 陀螺仪Z轴校准高字节
#define ICM20_GYRO_ZCALIB_L    0x18    // 陀螺仪Z轴校准低字节

#define ICM20_ACCEL_XCALIB_H   0x77    // 加速度计X轴校准高字节
#define ICM20_ACCEL_XCALIB_L   0x78    // 加速度计X轴校准低字节
#define ICM20_ACCEL_YCALIB_H   0x7A    // 加速度计Y轴校准高字节
#define ICM20_ACCEL_YCALIB_L   0x7B    // 加速度计Y轴校准低字节
#define ICM20_ACCEL_ZCALIB_H   0x7C    // 加速度计Z轴校准高字节
#define ICM20_ACCEL_ZCALIB_L   0x7D    // 加速度计Z轴校准低字节

#define ICM20_ACCEL_XOUT_H      0x3B    // 加速度计X轴高字节
#define ICM20_ACCEL_XOUT_L      0x3C    // 加速度计X轴低字节
#define ICM20_ACCEL_YOUT_H      0x3D    // 加速度计Y轴高字节
#define ICM20_ACCEL_YOUT_L      0x3E    // 加速度计Y轴低字节
#define ICM20_ACCEL_ZOUT_H      0x3F    // 加速度计Z轴高字节
#define ICM20_ACCEL_ZOUT_L      0x40    // 加速度计Z轴低字节
#define ICM20_TEMP_OUT_H        0x41    // 温度高字节
#define ICM20_TEMP_OUT_L        0x42    // 温度低字节
#define ICM20_GYRO_XOUT_H       0x43    // 陀螺仪X轴高字节
#define ICM20_GYRO_XOUT_L       0x44    // 陀螺仪X轴低字节
#define ICM20_GYRO_YOUT_H       0x45    // 陀螺仪Y轴高字节
#define ICM20_GYRO_YOUT_L       0x46    // 陀螺仪Y轴低字节
#define ICM20_GYRO_ZOUT_H       0x47    // 陀螺仪Z轴高字节
#define ICM20_GYRO_ZOUT_L       0x48    // 陀螺仪Z轴低字节

struct icm20608_dev {

    struct spi_device *spi;

    struct mutex mutex;

    struct regmap *regmap;
    struct regmap_config regmap_config;
};

// 在文件开头，靠近其他函数声明的位置添加
static int icm20608_get_temp_scale(int *val, int *val2);

// 通用SPI写寄存器函数
static int icm20608_write_reg(struct icm20608_dev *dev, u8 reg, u8 value)
{
    return regmap_write(dev->regmap, reg, value);
}

// 通用SPI读寄存器函数
static int icm20608_read_reg(struct icm20608_dev *dev, u8 reg)
{
    unsigned int value;
    int ret = regmap_read(dev->regmap, reg, &value);
    return ret < 0 ? ret : (u8)value;
}

// 通用SPI读多个寄存器函数
static int icm20608_read_regs(struct icm20608_dev *dev, u8 reg, u8 *buf, int len)
{
    return regmap_bulk_read(dev->regmap, reg, buf, len);
}

// icm20608 configuration initialization
static int icm20608_init_configuration(struct icm20608_dev *dev)
{
    int who_am_i;
    
    // 读取WHO_AM_I寄存器验证通信
    who_am_i = icm20608_read_reg(dev, ICM20_WHO_AM_I);
    printk(NAME " WHO_AM_I: 0x%02x (expected: 0xAF)\n", who_am_i);
    
    if (who_am_i != 0xAF) {
        printk(NAME " WHO_AM_I mismatch, SPI communication may have issues\n");
    }

    // 按照图片配置初始化ICM20608
    icm20608_write_reg(dev, ICM20_PWR_MGMT_1, 0x80);    // 复位
    mdelay(50);
    icm20608_write_reg(dev, ICM20_PWR_MGMT_1, 0x01);    // 自动选择时钟
    
    icm20608_write_reg(dev, ICM20_SMPLRT_DIV, 0x00);    // 输出速率是内部采样率
    icm20608_write_reg(dev, ICM20_GYRO_CONFIG, 0x18);   // 陀螺仪±2000dps量程
    icm20608_write_reg(dev, ICM20_ACCEL_CONFIG, 0x18);  // 加速度计±16G量程
    icm20608_write_reg(dev, ICM20_CONFIG, 0x04);        // 陀螺仪低通滤波BW=20Hz
    icm20608_write_reg(dev, ICM20_ACCEL_CONFIG2, 0x04); // 加速度计低通滤波BW=21.2Hz
    icm20608_write_reg(dev, ICM20_PWR_MGMT_2, 0x00);    // 打开加速度计和陀螺仪所有轴
    icm20608_write_reg(dev, ICM20_LP_MODE_CFG, 0x00);   // 关闭低功耗
    icm20608_write_reg(dev, ICM20_FIFO_EN, 0x00);       // 关闭FIFO

    printk(NAME " ICM20608 initialized with full configuration\n");

    return 0;
}

/**
 * @brief 读取 ICM20608 通道的原始数据
 * @param icm20608 设备指针
 * @param chan 通道描述符
 * @return 返回通道对应的 16 位有符号整数值
 *
 * 寄存器地址映射（读取的14字节数据）：
 * 0x3B-0x3C: ACCEL_XOUT_H/L -> buf[0]
 * 0x3D-0x3E: ACCEL_YOUT_H/L -> buf[1]
 * 0x3F-0x40: ACCEL_ZOUT_H/L -> buf[2]
 * 0x41-0x42: TEMP_OUT_H/L   -> buf[3]
 * 0x43-0x44: GYRO_XOUT_H/L  -> buf[4]
 * 0x45-0x46: GYRO_YOUT_H/L  -> buf[5]
 * 0x47-0x48: GYRO_ZOUT_H/L  -> buf[6]
 */
static int icm20608_read_sensor_data(struct icm20608_dev *icm20608,
                                     struct iio_chan_spec const *chan)
{
    u8 data[14];
    s16 *buf = (s16 *)data;
    int ret;
    int buf_index = -1;

    // 获取互斥锁，保护寄存器访问
    mutex_lock(&icm20608->mutex);

    // 读取所有传感器数据（加速度计 + 温度 + 陀螺仪）
    ret = icm20608_read_regs(icm20608, ICM20_ACCEL_XOUT_H, data, 14);
    
    // 立即释放锁，防止长时间占用
    mutex_unlock(&icm20608->mutex);
    
    if (ret < 0) {
        printk(NAME " Failed to read sensor data\n");
        return ret;
    }

    // buf index 根据通道类型和 channel2 计算
    if (chan->type == IIO_ACCEL && chan->modified) {
        // 加速度计：channel2 对应 X/Y/Z (IIO_MOD_X=1, IIO_MOD_Y=2, IIO_MOD_Z=3)，buf[0/1/2]
        // 需要从 channel2 中减去 1 才能得到正确的 buf_index
        if (chan->channel2 >= 1 && chan->channel2 <= 3) {
            buf_index = chan->channel2 - 1;  // 1->0, 2->1, 3->2
        } else {
            printk(NAME " Invalid accel axis channel2: %d\n", chan->channel2);
            return -EINVAL;
        }
    } else if (chan->type == IIO_TEMP) {
        // 温度通道：buf[3]
        buf_index = 3;
    } else if (chan->type == IIO_ANGL_VEL && chan->modified) {
        // 陀螺仪：channel2 对应 X/Y/Z (IIO_MOD_X=1, IIO_MOD_Y=2, IIO_MOD_Z=3)，buf[4/5/6]
        // 需要计算为 3 + channel2
        if (chan->channel2 >= 1 && chan->channel2 <= 3) {
            buf_index = 3 + chan->channel2;  // 1->4, 2->5, 3->6
        } else {
            printk(NAME " Invalid gyro axis channel2: %d\n", chan->channel2);
            return -EINVAL;
        }
    } else {
        // 未知的通道类型
        printk(NAME " Unknown channel type: %d, modified: %d\n", 
               chan->type, chan->modified);
        return -EINVAL;
    }

    // 根据计算出的索引读取对应的数据并转换为主机字节序
    if (buf_index >= 0 && buf_index <= 6) {
        s16 value = (s16)be16_to_cpu(buf[buf_index]);

#if 0
        // print reg data
        printk(NAME " Sensor Data Registers:\n");
        printk(NAME " ACCEL_XOUT: 0x%02x %02x\n", data[0], data[1]);
        printk(NAME " ACCEL_YOUT: 0x%02x %02x\n", data[2], data[3]);
        printk(NAME " ACCEL_ZOUT: 0x%02x %02x\n", data[4], data[5]);
        printk(NAME " TEMP_OUT: 0x%02x %02x\n", data[6], data[7]);
        printk(NAME " GYRO_XOUT: 0x%02x %02x\n", data[8], data[9]);
        printk(NAME " GYRO_YOUT: 0x%02x %02x\n", data[10], data[11]);
        printk(NAME " GYRO_ZOUT: 0x%02x %02x\n", data[12], data[13]);

        // 打印原始数据用于调试
        printk(NAME " Raw Data - Accel_X: %d, Accel_Y: %d, Accel_Z: %d, Temp: %d, Gyro_X: %d, Gyro_Y: %d, Gyro_Z: %d\n",
               (s16)be16_to_cpu(buf[0]), (s16)be16_to_cpu(buf[1]), (s16)be16_to_cpu(buf[2]),
               (s16)be16_to_cpu(buf[3]),
               (s16)be16_to_cpu(buf[4]), (s16)be16_to_cpu(buf[5]), (s16)be16_to_cpu(buf[6]));

        printk(NAME " Channel index: %d, Channel type: %d, Channel2: %d, Buf index: %d, Returning value: %d\n",
               chan->channel, chan->type, chan->channel2, buf_index, value);
#endif

        return value;
    } else {
        printk(NAME " Invalid buffer index: %d (must be 0-6)\n", buf_index);
        return -EINVAL;
    }
}

/**
 * @brief 获取加速度计的量程和比例因子
 * @param icm20608 设备指针
 * @param val 整数部分（指针）
 * @param val2 小数部分（指针）
 * @return IIO_VAL_INT_PLUS_MICRO 表示格式为整数 + 微小数部分
 */
static int icm20608_get_accel_scale(struct icm20608_dev *icm20608,
                                    int *val, int *val2)
{
    int accel_config;
    int full_scale_range;

    // 获取互斥锁，保护寄存器访问
    mutex_lock(&icm20608->mutex);

    // 读取加速度计配置寄存器 (ICM20_ACCEL_CONFIG 0x1C)
    accel_config = icm20608_read_reg(icm20608, ICM20_ACCEL_CONFIG);

    mutex_unlock(&icm20608->mutex);

    if (accel_config < 0) {
        printk(NAME " Failed to read ACCEL_CONFIG\n");
        return accel_config;
    }

    // 提取量程位 [4:3]
    // 00: ±2G   -> 分辨率 = 4G / 65536 = 0.061 mg/LSB
    // 01: ±4G   -> 分辨率 = 8G / 65536 = 0.122 mg/LSB
    // 10: ±8G   -> 分辨率 = 16G / 65536 = 0.244 mg/LSB
    // 11: ±16G  -> 分辨率 = 32G / 65536 = 0.488 mg/LSB
    full_scale_range = (accel_config >> 3) & 0x3;

    *val = 0;
    switch (full_scale_range) {
    case 0:
        *val2 = 61;  // 0.061 mg/LSB ≈ 61 微g/LSB
        break;
    case 1:
        *val2 = 122; // 0.122 mg/LSB ≈ 122 微g/LSB
        break;
    case 2:
        *val2 = 244; // 0.244 mg/LSB ≈ 244 微g/LSB
        break;
    case 3:
        *val2 = 488; // 0.488 mg/LSB ≈ 488 微g/LSB
        break;
    default:
        return -EINVAL;
    }

    return IIO_VAL_INT_PLUS_MICRO;
}

/**
 * @brief 获取陀螺仪的量程和比例因子
 * @param icm20608 设备指针
 * @param val 整数部分（指针）
 * @param val2 小数部分（指针）
 * @return IIO_VAL_INT_PLUS_MICRO 表示格式为整数 + 微小数部分
 */
static int icm20608_get_gyro_scale(struct icm20608_dev *icm20608,
                                   int *val, int *val2)
{
    int gyro_config;
    int full_scale_range;

    // 获取互斥锁，保护寄存器访问
    mutex_lock(&icm20608->mutex);

    // 读取陀螺仪配置寄存器 (ICM20_GYRO_CONFIG 0x1B)
    gyro_config = icm20608_read_reg(icm20608, ICM20_GYRO_CONFIG);
    mutex_unlock(&icm20608->mutex);

    if (gyro_config < 0) {
        printk(NAME " Failed to read GYRO_CONFIG\n");
        return gyro_config;
    }

    // 提取量程位 [4:3]
    // 00: ±250dps   -> 分辨率 = 500dps / 65536 = 7.629 mdps/LSB
    // 01: ±500dps   -> 分辨率 = 1000dps / 65536 = 15.258 mdps/LSB
    // 10: ±1000dps  -> 分辨率 = 2000dps / 65536 = 30.517 mdps/LSB
    // 11: ±2000dps  -> 分辨率 = 4000dps / 65536 = 61.035 mdps/LSB
    full_scale_range = (gyro_config >> 3) & 0x3;

    *val = 0;
    switch (full_scale_range) {
    case 0:
        *val2 = 7629;  // 7.629 mdps/LSB
        break;
    case 1:
        *val2 = 15258; // 15.258 mdps/LSB
        break;
    case 2:
        *val2 = 30517; // 30.517 mdps/LSB
        break;
    case 3:
        *val2 = 61035; // 61.035 mdps/LSB
        break;
    default:
        return -EINVAL;
    }

    return IIO_VAL_INT_PLUS_MICRO;
}


static int icm20608_get_temp_scale(int *val, int *val2)
{
    // 温度传感器的比例因子是固定的
    // 比例因子 = 1/326.8 °C/LSB
    // 转换为微单位: (1/326.8) * 1,000,000 ≈ 3059.97
    // 近似为 3060 微度/LSB
    
    *val = 0;        // 整数部分为0
    *val2 = 3060;    // 小数部分约为 0.00306 °C/LSB (以微单位表示)
    
    return IIO_VAL_INT_PLUS_MICRO;
}

/**
 * @brief 读取陀螺仪X轴校准值
 * @param icm20608 设备指针
 * @param val 校准值指针
 * @return 0表示成功，负数表示失败
 */
static int icm20608_get_gyro_x_calib(struct icm20608_dev *icm20608, int *val)
{
    u8 data[2];
    s16 calib_val;
    int ret;

    // 获取互斥锁，保护寄存器访问
    mutex_lock(&icm20608->mutex);

    // 读取陀螺仪X轴校准值 (0x13-0x14)
    ret = icm20608_read_regs(icm20608, ICM20_GYRO_XCALIB_H, data, 2);

    mutex_unlock(&icm20608->mutex);
    if (ret < 0) {
        printk(NAME " Failed to read GYRO_XCALIB\n");
        return ret;
    }

    // 转换为有符号16位数据（大端序）
    calib_val = (s16)((data[0] << 8) | data[1]);
    
    *val = calib_val;
    printk(NAME " Gyro X Calibration: %d\n", calib_val);

    return 0;
}

/**
 * @brief 读取陀螺仪Y轴校准值
 * @param icm20608 设备指针
 * @param val 校准值指针
 * @return 0表示成功，负数表示失败
 */
static int icm20608_get_gyro_y_calib(struct icm20608_dev *icm20608, int *val)
{
    u8 data[2];
    s16 calib_val;
    int ret;

    // 获取互斥锁，保护寄存器访问
    mutex_lock(&icm20608->mutex);

    // 读取陀螺仪Y轴校准值 (0x15-0x16)
    ret = icm20608_read_regs(icm20608, ICM20_GYRO_YCALIB_H, data, 2);

    mutex_unlock(&icm20608->mutex);
    if (ret < 0) {
        printk(NAME " Failed to read GYRO_YCALIB\n");
        return ret;
    }

    // 转换为有符号16位数据（大端序）
    calib_val = (s16)((data[0] << 8) | data[1]);
    
    *val = calib_val;
    printk(NAME " Gyro Y Calibration: %d\n", calib_val);

    return 0;
}

/**
 * @brief 读取陀螺仪Z轴校准值
 * @param icm20608 设备指针
 * @param val 校准值指针
 * @return 0表示成功，负数表示失败
 */
static int icm20608_get_gyro_z_calib(struct icm20608_dev *icm20608, int *val)
{
    u8 data[2];
    s16 calib_val;
    int ret;

    // 获取互斥锁，保护寄存器访问
    mutex_lock(&icm20608->mutex);

    // 读取陀螺仪Z轴校准值 (0x17-0x18)
    ret = icm20608_read_regs(icm20608, ICM20_GYRO_ZCALIB_H, data, 2);

    mutex_unlock(&icm20608->mutex);
    if (ret < 0) {
        printk(NAME " Failed to read GYRO_ZCALIB\n");
        return ret;
    }

    // 转换为有符号16位数据（大端序）
    calib_val = (s16)((data[0] << 8) | data[1]);
    
    *val = calib_val;
    printk(NAME " Gyro Z Calibration: %d\n", calib_val);

    return 0;
}


/**
 * @brief 读取通道的校准偏置值
 * @param icm20608 设备指针
 * @param chan 通道描述符
 * @return 校准值，-EINVAL表示该通道不支持校准
 *
 * 只有陀螺仪通道有校准值（存储在硬件寄存器中）
 */
static int icm20608_get_calib(struct icm20608_dev *icm20608,
                              struct iio_chan_spec const *chan)
{
    int calib_val = 0;
    int ret;

    // 只有陀螺仪通道有校准值
    if (chan->type != IIO_ANGL_VEL || !chan->modified) {
        // 加速度计和温度通道返回0
        return 0;
    }

    // 根据陀螺仪的轴（channel2）读取对应的校准值
    // IIO_MOD_X=1, IIO_MOD_Y=2, IIO_MOD_Z=3
    switch (chan->channel2) {
    case 1:  // X轴
        ret = icm20608_get_gyro_x_calib(icm20608, &calib_val);
        break;
    case 2:  // Y轴
        ret = icm20608_get_gyro_y_calib(icm20608, &calib_val);
        break;
    case 3:  // Z轴
        ret = icm20608_get_gyro_z_calib(icm20608, &calib_val);
        break;
    default:
        printk(NAME " Invalid gyro axis for calib: %d\n", chan->channel2);
        return -EINVAL;
    }

    if (ret < 0) {
        printk(NAME " Failed to read gyro calib for channel2: %d\n", chan->channel2);
        return ret;
    }

    return calib_val;
}



/**
 * @brief 获取通道的比例因子
 * @param icm20608 设备指针
 * @param chan 通道描述符
 * @param val 整数部分（指针）
 * @param val2 小数部分（指针）
 * @return IIO_VAL_INT_PLUS_MICRO 表示格式为整数 + 微小数部分
 */
static int icm20608_get_scale(struct icm20608_dev *icm20608,
                              struct iio_chan_spec const *chan,
                              int *val, int *val2)
{
    switch (chan->type) {
    case IIO_ACCEL:
        // 从 ACCEL_CONFIG 寄存器读取量程配置
        return icm20608_get_accel_scale(icm20608, val, val2);

    case IIO_ANGL_VEL:
        // 从 GYRO_CONFIG 寄存器读取量程配置
        return icm20608_get_gyro_scale(icm20608, val, val2);

    case IIO_TEMP:
        // 温度传感器比例因子固定
        return icm20608_get_temp_scale(val, val2);

    default:
        printk(NAME " Unknown channel type: %d\n", chan->type);
        return -EINVAL;
    }
}

/**
 * @brief 获取温度传感器的偏移量
 * @param icm20608 设备指针
 * @param val 偏移值指针
 * @param val2 小数部分指针
 * @return IIO_VAL_INT 表示格式为单个整数
 *
 * 根据 ICM20608 规格书：
 * 温度 (°C) = (TEMP_OUT / 326.8) + 25°C
 * 其中 TEMP_OUT 为温度原始值（有符号16位）
 * 当 TEMP_OUT = 0 时，对应温度为 25°C
 */
static int icm20608_get_offset(struct icm20608_dev *icm20608, int *val, int *val2)
{
    u8 data[2];
    s16 temp_raw;
    int ret;

    // 获取互斥锁，保护寄存器访问
    mutex_lock(&icm20608->mutex);

    // 读取温度寄存器 (0x41-0x42)
    ret = icm20608_read_regs(icm20608, ICM20_TEMP_OUT_H, data, 2);

    mutex_unlock(&icm20608->mutex);
    if (ret < 0) {
        printk(NAME " Failed to read TEMP_OUT registers\n");
        return ret;
    }

    // 转换为有符号16位数据（大端序）
    temp_raw = (s16)((data[0] << 8) | data[1]);

    // 根据规格书计算实际偏移
    // 偏移 = 25 + (TEMP_OUT / 326.8)
    // 为避免浮点运算，使用整数计算：
    // 但由于我们需要返回整数部分，先简化为：
    // 当 TEMP_OUT = 0 时，偏移 = 25°C
    
    *val = 25;  // 基础偏移温度
    *val2 = 0;
    
    printk(NAME " Temperature offset: %d°C (TEMP_RAW: %d)\n", *val, temp_raw);

    return IIO_VAL_INT;
}

/**
 * @brief 获取温度传感器的比例因子
 * @param val 整数部分（指针）
 * @param val2 小数部分（指针）
 * @return IIO_VAL_INT_PLUS_MICRO 表示格式为整数 + 微小数部分
 *
 * 根据 ICM20608 规格书：
 * 温度 (°C) = (TEMP_OUT / 326.8) + 25°C
 * 其中 TEMP_OUT 为温度原始值（有符号16位）
 * 比例因子为 1/326.8 °C/LSB ≈ 0.00306 °C/LSB
 */

// icm20608 read raw
static int icm20608_read_raw(struct iio_dev *indio_dev,
                             struct iio_chan_spec const *chan,
                             int *val,
                             int *val2,
                             long mask)
{
    struct icm20608_dev *icm20608 = iio_priv(indio_dev);
    int raw_val;

    // printk(NAME " read_raw channel2: %d, mask: %ld\n", chan->channel2, mask);

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        // 读取传感器原始数据
        raw_val = icm20608_read_sensor_data(icm20608, chan);

        *val = raw_val;
        return IIO_VAL_INT;

    case IIO_CHAN_INFO_SCALE:
        // 获取通道的比例因子（从寄存器配置读取）
        return icm20608_get_scale(icm20608, chan, val, val2);

    case IIO_CHAN_INFO_OFFSET:
        // 仅温度通道有偏移量（从温度寄存器读取）
        if (chan->type == IIO_TEMP) {
            return icm20608_get_offset(icm20608, val, val2);
        } else {
            return -EINVAL;
        }

    case IIO_CHAN_INFO_CALIBBIAS:
        // 读取校准偏置值
        raw_val = icm20608_get_calib(icm20608, chan);
        *val = raw_val;
        return IIO_VAL_INT;

    default:
        printk(NAME " Unknown mask: %ld\n", mask);
        return -EINVAL;
    }
}

// icm20608 write raw
// icm20608 write raw
static int icm20608_write_raw(struct iio_dev *indio_dev,
                              struct iio_chan_spec const *chan,
                              int val,
                              int val2,
                              long mask)
{
    struct icm20608_dev *icm20608 = iio_priv(indio_dev);
    int ret = 0;

    switch (mask) {
    case IIO_CHAN_INFO_SCALE:
        // Only support changing scale for accelerometer and gyroscope
        switch (chan->type) {
        case IIO_ACCEL:
            // Update accelerometer scale based on val/val2 values
            // For ICM20608, we need to write to ICM20_ACCEL_CONFIG register
            mutex_lock(&icm20608->mutex);
            
            // Determine which scale range was requested and update config register
            // This would require mapping val/val2 back to appropriate FS_SEL bits
            if (val2 == 488) { // ±16G
                ret = icm20608_write_reg(icm20608, ICM20_ACCEL_CONFIG, 0x18);
            } else if (val2 == 244) { // ±8G
                ret = icm20608_write_reg(icm20608, ICM20_ACCEL_CONFIG, 0x10);
            } else if (val2 == 122) { // ±4G
                ret = icm20608_write_reg(icm20608, ICM20_ACCEL_CONFIG, 0x08);
            } else if (val2 == 61) { // ±2G
                ret = icm20608_write_reg(icm20608, ICM20_ACCEL_CONFIG, 0x00);
            } else {
                ret = -EINVAL;
            }
            
            mutex_unlock(&icm20608->mutex);
            break;
            
        case IIO_ANGL_VEL:
            // Update gyroscope scale based on val/val2 values
            // For ICM20608, we need to write to ICM20_GYRO_CONFIG register
            mutex_lock(&icm20608->mutex);
            
            // Determine which scale range was requested and update config register
            if (val2 == 61035) { // ±2000dps
                ret = icm20608_write_reg(icm20608, ICM20_GYRO_CONFIG, 0x18);
            } else if (val2 == 30517) { // ±1000dps
                ret = icm20608_write_reg(icm20608, ICM20_GYRO_CONFIG, 0x10);
            } else if (val2 == 15258) { // ±500dps
                ret = icm20608_write_reg(icm20608, ICM20_GYRO_CONFIG, 0x08);
            } else if (val2 == 7629) { // ±250dps
                ret = icm20608_write_reg(icm20608, ICM20_GYRO_CONFIG, 0x00);
            } else {
                ret = -EINVAL;
            }
            
            mutex_unlock(&icm20608->mutex);
            break;
            
        default:
            return -EINVAL;
        }
        break;
        
    case IIO_CHAN_INFO_CALIBBIAS:
        // Support writing calibration bias values for gyroscope
        if (chan->type == IIO_ANGL_VEL && chan->modified) {
            u8 calib_high, calib_low;
            s16 calib_value = (s16)val;
            
            mutex_lock(&icm20608->mutex);
            
            switch (chan->channel2) {
            case IIO_MOD_X:  // X-axis
                calib_high = (calib_value >> 8) & 0xFF;
                calib_low = calib_value & 0xFF;
                ret = icm20608_write_reg(icm20608, ICM20_GYRO_XCALIB_H, calib_high);
                if (ret == 0)
                    ret = icm20608_write_reg(icm20608, ICM20_GYRO_XCALIB_L, calib_low);
                break;
                
            case IIO_MOD_Y:  // Y-axis
                calib_high = (calib_value >> 8) & 0xFF;
                calib_low = calib_value & 0xFF;
                ret = icm20608_write_reg(icm20608, ICM20_GYRO_YCALIB_H, calib_high);
                if (ret == 0)
                    ret = icm20608_write_reg(icm20608, ICM20_GYRO_YCALIB_L, calib_low);
                break;
                
            case IIO_MOD_Z:  // Z-axis
                calib_high = (calib_value >> 8) & 0xFF;
                calib_low = calib_value & 0xFF;
                ret = icm20608_write_reg(icm20608, ICM20_GYRO_ZCALIB_H, calib_high);
                if (ret == 0)
                    ret = icm20608_write_reg(icm20608, ICM20_GYRO_ZCALIB_L, calib_low);
                break;
                
            default:
                ret = -EINVAL;
                break;
            }
            
            mutex_unlock(&icm20608->mutex);
        } else {
            ret = -EINVAL;
        }
        break;
        
    case IIO_CHAN_INFO_OFFSET:
        // Temperature offset cannot be written to hardware directly
        // This is just for reference - we don't actually support writing offset
        if (chan->type == IIO_TEMP) {
            ret = -EINVAL; // Not supported for temperature
        } else {
            ret = -EINVAL;
        }
        break;
        
    default:
        printk(NAME " Unsupported write mask: %ld\n", mask);
        ret = -EINVAL;
        break;
    }
    
    return ret;
}

// icm20608_write_raw_get_fmt
static int icm20608_write_raw_get_fmt(struct iio_dev *indio_dev,
                                     struct iio_chan_spec const *chan,
                                     long mask)
{
    printk(NAME " write_raw_get_fmt not supported\n");

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        return IIO_VAL_INT;
    case IIO_CHAN_INFO_SCALE:
        return IIO_VAL_INT_PLUS_MICRO;
    case IIO_CHAN_INFO_OFFSET:
        return IIO_VAL_INT;
    case IIO_CHAN_INFO_CALIBBIAS:
        return IIO_VAL_INT;
    default:
        return -EINVAL;
    }
}

// iio_info
static const struct iio_info icm20608_info = {
    .driver_module = THIS_MODULE,
    .read_raw = icm20608_read_raw,
    .write_raw = icm20608_write_raw,
    .write_raw_get_fmt = icm20608_write_raw_get_fmt,
};

// scan type endianness
enum icm20608_scan_type{
    ICM20608_SCAN_TYPE_ACCEL_X = 0,
    ICM20608_SCAN_TYPE_ACCEL_Y,
    ICM20608_SCAN_TYPE_ACCEL_Z,
    ICM20608_SCAN_TYPE_TEMP,
    ICM20608_SCAN_TYPE_GYRO_X,
    ICM20608_SCAN_TYPE_GYRO_Y,
    ICM20608_SCAN_TYPE_GYRO_Z,
    ICM20608_SCAN_TYPE_TIMESTAMP,
};

// define accelerometer channels
#define ICM20608_ACCEL_CHANNEL(_channel2, _scan_index) \
{ \
    .type = IIO_ACCEL, \
    .modified = 1, \
    .channel2 = _channel2, \
    .scan_index = _scan_index, \
    .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_CALIBBIAS), \
    .scan_type = { \
        .sign = 's', \
        .realbits = 16, \
        .storagebits = 16, \
        .shift = 0, \
        .endianness = IIO_BE, \
    }, \
}

// define gyroscope channels
#define ICM20608_GYRO_CHANNEL(_channel2, _scan_index) \
{ \
    .type = IIO_ANGL_VEL, \
    .modified = 1, \
    .channel2 = _channel2, \
    .scan_index = _scan_index, \
    .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_CALIBBIAS), \
    .scan_type = { \
        .sign = 's', \
        .realbits = 16, \
        .storagebits = 16, \
        .shift = 0, \
        .endianness = IIO_BE, \
    }, \
}

// iio channels
static const struct iio_chan_spec icm20608_channels[] = {
    // temperature
    {
        .type = IIO_TEMP,
        .channel = 1,
        .scan_index = ICM20608_SCAN_TYPE_TEMP,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_OFFSET),
        .scan_type = {
            .sign = 's',
            .realbits = 16,
            .storagebits = 16,
            .shift = 0,
            .endianness = IIO_BE,
        },
    },
    // accelerometer
    ICM20608_ACCEL_CHANNEL(IIO_MOD_X, ICM20608_SCAN_TYPE_ACCEL_X),
    ICM20608_ACCEL_CHANNEL(IIO_MOD_Y, ICM20608_SCAN_TYPE_ACCEL_Y),
    ICM20608_ACCEL_CHANNEL(IIO_MOD_Z, ICM20608_SCAN_TYPE_ACCEL_Z),
    // gyroscope
    ICM20608_GYRO_CHANNEL(IIO_MOD_X, ICM20608_SCAN_TYPE_GYRO_X),
    ICM20608_GYRO_CHANNEL(IIO_MOD_Y, ICM20608_SCAN_TYPE_GYRO_Y),
    ICM20608_GYRO_CHANNEL(IIO_MOD_Z, ICM20608_SCAN_TYPE_GYRO_Z),
};

static int icm20608_probe(struct spi_device *spi)
{
    struct icm20608_dev *icm20608;
    struct iio_dev *indio_dev;
    int ret = 0;

    indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*icm20608));
    if (!indio_dev)
        return -ENOMEM;

    icm20608 = iio_priv(indio_dev);
    icm20608->spi = spi;
    
    // 设置 indio_dev 为私有数据
    spi_set_drvdata(spi, indio_dev);

    mutex_init(&icm20608->mutex);

    // init regmap
    icm20608->regmap_config.name = "icm20608";
    icm20608->regmap_config.reg_bits = 8;
    icm20608->regmap_config.val_bits = 8;
    icm20608->regmap_config.read_flag_mask = 0x80;
    icm20608->regmap = devm_regmap_init_spi(spi, &icm20608->regmap_config);
    if (IS_ERR(icm20608->regmap)) {
        printk(NAME " regmap init failed\n");
        return PTR_ERR(icm20608->regmap);
    }

    // init iio
    indio_dev->name = "icm20608";
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->info = &icm20608_info;
    indio_dev->channels = icm20608_channels;
    indio_dev->num_channels = ARRAY_SIZE(icm20608_channels);
    dev_set_drvdata(&spi->dev, indio_dev);

    // register iio device
    ret = iio_device_register(indio_dev);
    if (ret < 0) {
        printk(NAME " iio_device_register failed\n");
        return ret;
    }
    
    // 初始化ICM20608配置
    icm20608_init_configuration(icm20608);
    
    return 0;
}

static int icm20608_remove(struct spi_device *spi)
{
    struct iio_dev *indio_dev = spi_get_drvdata(spi);
    struct icm20608_dev *icm20608 = iio_priv(indio_dev);

    printk(NAME " spi remove\n");

    iio_device_unregister(indio_dev);

    // free mutex
    mutex_destroy(&icm20608->mutex);

    return 0;
}


static const struct spi_device_id icm20608_id[] = {
    {"icm20608", 0},
    {}
};

static const struct of_device_id icm20608_of_match[] = {
    {.compatible = "alpha,icm20608"},
    {}
};

static struct spi_driver icm20608_driver = {
    .driver = {
        .name = "icm20608",
        .of_match_table = icm20608_of_match,
    },
    .probe = icm20608_probe,
    .remove = icm20608_remove,
    .id_table = icm20608_id,
};

static int __init icm20608_driver_init(void)
{
    return spi_register_driver(&icm20608_driver);
}

static void __exit icm20608_driver_exit(void)
{
    spi_unregister_driver(&icm20608_driver);
}

module_init(icm20608_driver_init);
module_exit(icm20608_driver_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("icm20608 with spi");
MODULE_LICENSE("GPL");


#if 0
// 硬中断处理函数
static irqreturn_t icm20608_trigger_handler(int irq, void *p)
{
    struct iio_poll_func *pf = p;
    struct iio_dev *indio_dev = pf->indio_dev;
    struct icm20608_dev *icm20608 = iio_priv(indio_dev);
    
    // 分配数据缓冲
    s16 data[8];  // 最多 7 个通道 + timestamp
    u8 raw_data[14];
    
    // 读取硬件数据
    icm20608_read_regs(icm20608, 0x3B, raw_data, 14);
    
    // 转换为有符号16位数据
    s16 *raw_buf = (s16 *)raw_data;
    
    // 按 scan_index 顺序填充缓冲
    int buf_idx = 0;
    
    // Accel_X: scan_index = 0
    if (enabled[0]) data[buf_idx++] = be16_to_cpu(raw_buf[0]);
    
    // Accel_Y: scan_index = 1
    if (enabled[1]) data[buf_idx++] = be16_to_cpu(raw_buf[1]);
    
    // Accel_Z: scan_index = 2
    if (enabled[2]) data[buf_idx++] = be16_to_cpu(raw_buf[2]);
    
    // Temp: scan_index = 3
    if (enabled[3]) data[buf_idx++] = be16_to_cpu(raw_buf[3]);
    
    // Gyro_X: scan_index = 4
    if (enabled[4]) data[buf_idx++] = be16_to_cpu(raw_buf[4]);
    
    // Gyro_Y: scan_index = 5
    if (enabled[5]) data[buf_idx++] = be16_to_cpu(raw_buf[5]);
    
    // Gyro_Z: scan_index = 6
    if (enabled[6]) data[buf_idx++] = be16_to_cpu(raw_buf[6]);
    
    // 推送到缓冲区
    iio_push_to_buffers_with_timestamp(indio_dev, data, iio_get_time_ns(indio_dev));
    
    iio_trigger_notify_done(pf->trigger);
    return IRQ_HANDLED;
}

#endif