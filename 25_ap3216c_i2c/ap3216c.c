#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/cdev.h>


#define NAME "ap3216c"
#define AP3216C_COUNT 1

struct ap3216c_dev {
    dev_t devid;
    int major;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct i2c_client *client;
};

static int ap3216c_write_reg(struct i2c_client *client, u8 reg, u8 value)
{
    struct i2c_msg msg;
    u8 buf[2] = {reg, value};
    int ret;
    
    msg.addr = client->addr;
    msg.flags = 0;
    msg.len = 2;
    msg.buf = buf;
    
    ret = i2c_transfer(client->adapter, &msg, 1);
    return (ret == 1) ? 0 : -EIO;
}

static int ap3216c_read_reg(struct i2c_client *client, u8 reg)
{
    struct i2c_msg msgs[2];
    u8 value;
    int ret;
    
    msgs[0].addr = client->addr;
    msgs[0].flags = 0;
    msgs[0].len = 1;
    msgs[0].buf = &reg;
    
    msgs[1].addr = client->addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = 1;
    msgs[1].buf = &value;
    
    ret = i2c_transfer(client->adapter, msgs, 2);
    return (ret == 2) ? value : -EIO;
}

static int ap3216c_open(struct inode *inode, struct file *filp)
{
    struct ap3216c_dev *dev = container_of(inode->i_cdev, struct ap3216c_dev, cdev);
    int config;
    const char *mode_str;
    int ret;
    
    filp->private_data = dev;
    
    // 初始化AP3216C传感器
    // 软复位
    ret = ap3216c_write_reg(dev->client, 0x00, 0x04);
    if (ret < 0) {
        printk(NAME " write reset failed\n");
        return ret;
    }
    mdelay(50);
    
    // 配置为ALS+PS+IR模式
    ret = ap3216c_write_reg(dev->client, 0x00, 0x03);
    if (ret < 0) {
        printk(NAME " write config failed\n");
        return ret;
    }
    mdelay(50);
    
    // 读取并解析当前配置
    config = ap3216c_read_reg(dev->client, 0x00);
    if (config < 0) {
        printk(NAME " read config failed\n");
        return config;
    }
    switch (config & 0x07) {
    case 0x00:
        mode_str = "Power Down";
        break;
    case 0x01:
        mode_str = "ALS Only";
        break;
    case 0x02:
        mode_str = "PS+IR Only";
        break;
    case 0x03:
        mode_str = "ALS+PS+IR";
        break;
    case 0x04:
        mode_str = "SW Reset";
        break;
    case 0x05:
        mode_str = "ALS Once";
        break;
    case 0x06:
        mode_str = "PS+IR Once";
        break;
    case 0x07:
        mode_str = "ALS+PS+IR Once";
        break;
    default:
        mode_str = "Unknown";
        break;
    }
    
    printk(NAME " open: initialized, config=0x%02x (%s)\n", config, mode_str);
    
    return 0;
}

static int ap3216c_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static int ap3216c_read_data(struct i2c_client *client, u8 *buf)
{
    int i, ret;
    
    // 分别读取每个寄存器，避免连续读取问题
    for (i = 0; i < 6; i++) {
        ret = ap3216c_read_reg(client, 0x0A + i);
        if (ret < 0)
            return ret;
        buf[i] = ret;
    }
        
    return 0;
}

static ssize_t ap3216c_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    struct ap3216c_dev *dev = filp->private_data;
    u8 data[6]; // 原始寄存器数据
    int ret;
    
    if (!dev->client)
        return -ENODEV;
        
    ret = ap3216c_read_data(dev->client, data);
    if (ret < 0)
        return ret;
    
    ret = copy_to_user(buf, data, sizeof(data));
    return ret ? -EFAULT : sizeof(data);
}

static const struct file_operations ap3216c_fops = {
    .owner = THIS_MODULE,
    .open = ap3216c_open,
    .release = ap3216c_release,
    .read = ap3216c_read,
};

static int ap3216c_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct ap3216c_dev *ap3216c;
    int ret;
    
    ap3216c = devm_kzalloc(&client->dev, sizeof(*ap3216c), GFP_KERNEL);
    if (!ap3216c)
        return -ENOMEM;
        
    printk(NAME " i2c probe\n");
    ap3216c->client = client;
    i2c_set_clientdata(client, ap3216c);
    
    // 分配设备号
    ret = alloc_chrdev_region(&ap3216c->devid, 0, AP3216C_COUNT, NAME);
    if (ret < 0) {
        printk(NAME " alloc_chrdev_region failed\n");
        return ret;
    }
    ap3216c->major = MAJOR(ap3216c->devid);
    
    // 初始化cdev
    cdev_init(&ap3216c->cdev, &ap3216c_fops);
    ret = cdev_add(&ap3216c->cdev, ap3216c->devid, AP3216C_COUNT);
    if (ret < 0) {
        printk(NAME " cdev_add failed\n");
        goto err_cdev;
    }
    
    // 创建类
    ap3216c->class = class_create(THIS_MODULE, NAME);
    if (IS_ERR(ap3216c->class)) {
        printk(NAME " class_create failed\n");
        ret = PTR_ERR(ap3216c->class);
        goto err_class;
    }
    
    // 创建设备
    ap3216c->device = device_create(ap3216c->class, NULL, ap3216c->devid, NULL, NAME);
    if (IS_ERR(ap3216c->device)) {
        printk(NAME " device_create failed\n");
        ret = PTR_ERR(ap3216c->device);
        goto err_device;
    }
    
    printk(NAME " probe success, major: %d\n", ap3216c->major);
    return 0;
    
err_device:
    class_destroy(ap3216c->class);
err_class:
    cdev_del(&ap3216c->cdev);
err_cdev:
    unregister_chrdev_region(ap3216c->devid, AP3216C_COUNT);
    return ret;
}

static int ap3216c_i2c_remove(struct i2c_client *client)
{
    struct ap3216c_dev *ap3216c = i2c_get_clientdata(client);
    
    printk(NAME " i2c remove\n");
    
    device_destroy(ap3216c->class, ap3216c->devid);
    class_destroy(ap3216c->class);
    cdev_del(&ap3216c->cdev);
    unregister_chrdev_region(ap3216c->devid, AP3216C_COUNT);
    
    return 0;
}

static const struct i2c_device_id ap3216c_id[] = {
    {"ap3216c", 0},
    {}
};

static const struct of_device_id ap3216c_of_match[] = {
    {.compatible = "alpha,ap3216c"},
    {}
};

static struct i2c_driver ap3216c_i2c_driver = {
    .driver = {
        .name = "ap3216c",
        .of_match_table = ap3216c_of_match,
    },
    .probe = ap3216c_i2c_probe,
    .remove = ap3216c_i2c_remove,
    .id_table = ap3216c_id,
};

static int __init ap3216c_driver_init(void)
{
    return i2c_add_driver(&ap3216c_i2c_driver);
}

static void __exit ap3216c_driver_exit(void)
{
    i2c_del_driver(&ap3216c_i2c_driver);
}

module_init(ap3216c_driver_init);
module_exit(ap3216c_driver_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("ap3216c with i2c");
MODULE_LICENSE("GPL");