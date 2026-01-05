#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#define NAME "icm20608"
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

struct icm20608_dev {
    dev_t devid;
    int major;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct spi_device *spi;
};


// 通用SPI写寄存器函数
static int icm20608_write_reg(struct icm20608_dev *dev, u8 reg, u8 value)
{
    u8 tx_buf[2] = {reg & 0x7F, value}; // 写操作：bit7=0
    return spi_write(dev->spi, tx_buf, 2);
}

// 通用SPI读寄存器函数
static int icm20608_read_reg(struct icm20608_dev *dev, u8 reg)
{
    u8 tx_buf = reg | 0x80; // 读操作：bit7=1
    u8 rx_buf;
    int ret = spi_write_then_read(dev->spi, &tx_buf, 1, &rx_buf, 1);
    return ret < 0 ? ret : rx_buf;
}

// 通用SPI读多个寄存器函数
static int icm20608_read_regs(struct icm20608_dev *dev, u8 reg, u8 *buf, int len)
{
    u8 tx_buf = reg | 0x80; // 读操作：bit7=1
    return spi_write_then_read(dev->spi, &tx_buf, 1, buf, len);
}

static int icm20608_open(struct inode *inode, struct file *filp)
{
    struct icm20608_dev *dev = container_of(inode->i_cdev, struct icm20608_dev, cdev);
    int who_am_i;
    
    filp->private_data = dev;

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

static int icm20608_release(struct inode *inode, struct file *filp)
{
    return 0;
}


static ssize_t icm20608_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    struct icm20608_dev *dev = filp->private_data;
    u8 data[14]; // 加速度计+温度+陀螺仪数据
    int ret;
    
    if (!dev->spi)
        return -ENODEV;
        
    // 读取加速度计+温度+陀螺仪数据 (0x3B-0x48)
    ret = icm20608_read_regs(dev, 0x3B, data, 14);
    if (ret < 0)
        return ret;
    
    ret = copy_to_user(buf, data, sizeof(data));
    return ret ? -EFAULT : sizeof(data);
}

static const struct file_operations icm20608_fops = {
    .owner = THIS_MODULE,
    .open = icm20608_open,
    .release = icm20608_release,
    .read = icm20608_read,
};

static int icm20608_spi_probe(struct spi_device *spi)
{
    struct icm20608_dev *icm20608;
    int ret = 0;

    icm20608 = devm_kzalloc(&spi->dev, sizeof(*icm20608), GFP_KERNEL);
    if (!icm20608)
        return -ENOMEM;

    printk(NAME " spi probe\n");
    
    // 打印SPI配置信息
    printk(NAME " SPI config: max_speed=%d, mode=0x%x, bits_per_word=%d\n",
           spi->max_speed_hz, spi->mode, spi->bits_per_word);
    
    icm20608->spi = spi;
    spi_set_drvdata(spi, icm20608);

    // 分配设备号
    ret = alloc_chrdev_region(&icm20608->devid, 0, ICM_20608_COUNT, NAME);
    if (ret < 0) {
        printk(NAME " alloc_chrdev_region failed\n");
        return ret;
    }
    icm20608->major = MAJOR(icm20608->devid);

    // 初始化cdev
    cdev_init(&icm20608->cdev, &icm20608_fops);
    ret = cdev_add(&icm20608->cdev, icm20608->devid, ICM_20608_COUNT);
    if (ret < 0) {
        printk(NAME " cdev_add failed\n");
        goto err_cdev;
    }

    // 创建类
    icm20608->class = class_create(THIS_MODULE, NAME);
    if (IS_ERR(icm20608->class)) {
        printk(NAME " class_create failed\n");
        ret = PTR_ERR(icm20608->class);
        goto err_class;
    }

    // 创建设备
    icm20608->device = device_create(icm20608->class, NULL, icm20608->devid, NULL, NAME);
    if (IS_ERR(icm20608->device)) {
        printk(NAME " device_create failed\n");
        ret = PTR_ERR(icm20608->device);
        goto err_device;
    }

    printk(NAME " probe success, major: %d\n", icm20608->major);
    return 0;

err_device:
    class_destroy(icm20608->class);
err_class:
    cdev_del(&icm20608->cdev);
err_cdev:
    unregister_chrdev_region(icm20608->devid, ICM_20608_COUNT);
    return ret;
}

static int icm20608_spi_remove(struct spi_device *spi)
{
    struct icm20608_dev *icm20608 = spi_get_drvdata(spi);

    printk(NAME " spi remove\n");

    device_destroy(icm20608->class, icm20608->devid);
    class_destroy(icm20608->class);
    cdev_del(&icm20608->cdev);
    unregister_chrdev_region(icm20608->devid, ICM_20608_COUNT);

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

static struct spi_driver icm20608_spi_driver = {
    .driver = {
        .name = "icm20608",
        .of_match_table = icm20608_of_match,
    },
    .probe = icm20608_spi_probe,
    .remove = icm20608_spi_remove,
    .id_table = icm20608_id,
};

static int __init icm20608_driver_init(void)
{
    return spi_register_driver(&icm20608_spi_driver);
}

static void __exit icm20608_driver_exit(void)
{
    spi_unregister_driver(&icm20608_spi_driver);
}

module_init(icm20608_driver_init);
module_exit(icm20608_driver_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("icm20608 with spi");
MODULE_LICENSE("GPL");