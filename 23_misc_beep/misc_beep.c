#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h> // strlen
#include <linux/types.h>  // size_t
#include <linux/errno.h> // -EFAULT
#include <linux/uaccess.h> // copy_to_user
#include <linux/fs.h> // file_operations
#include <linux/platform_device.h> // platform_device
#include <linux/miscdevice.h> // misc_register
#include <linux/of.h> // of_find_node_by_path
#include <linux/device.h> // device functions
#include <linux/gpio.h> // gpio function
#include <linux/of_gpio.h> // of_get_named_gpio

#define NAME "misc_beep"


struct beep_dev {
    struct miscdevice dev;
    struct device_node *node;
    int beep_gpio;
    int beep_state; // beep state: 1=on, 0=off
};

static void beep_on(struct beep_dev *dev);
static void beep_off(struct beep_dev *dev);

static int beep_open(struct inode *inode, struct file *file);
static int beep_release(struct inode *inode, struct file *file);
static ssize_t beep_read(struct file *file, char *buf, size_t count, loff_t *f_pos);
static ssize_t beep_write(struct file *file, const char *buf, size_t count, loff_t *f_pos);

static const struct file_operations beep_fops = {
	.owner		= THIS_MODULE,
    .open      = beep_open,
    .release   = beep_release,
    .read      = beep_read,
    .write     = beep_write
};

int beep_open(struct inode *inode, struct file *file)
{
    struct beep_dev *dev = container_of(file->private_data, struct beep_dev, dev);
    printk(NAME " open\n");
    return 0;
}
int beep_release(struct inode *inode, struct file *file)
{
    printk(NAME " release\n");
    return 0;
}

ssize_t beep_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos)
{
    struct beep_dev *dev = container_of(file->private_data, struct beep_dev, dev);
    char read_buf[16];
    int len;

    len = snprintf(read_buf, sizeof(read_buf), "BEEP is %s\n", 
                   dev->beep_state ? "ON" : "OFF");
    
    if (count < len)
        len = count;

    return copy_to_user(buf, read_buf, len) ? -EFAULT : len;
}

ssize_t beep_write(struct file *file, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct beep_dev *dev = container_of(file->private_data, struct beep_dev, dev);
    char write_buf[8];
    size_t len = min(count, sizeof(write_buf) - 1);

    if (copy_from_user(write_buf, buf, len))
        return -EFAULT;
    
    write_buf[len] = '\0';

    if (strncmp(write_buf, "on", 2) == 0)
        beep_on(dev);
    else if (strncmp(write_buf, "off", 3) == 0)
        beep_off(dev);

    return count;
}


struct beep_dev beep = {
    .dev = {
        .minor = MISC_DYNAMIC_MINOR,
        .name = NAME,
        .fops = &beep_fops,
    },
};

static void beep_on(struct beep_dev *dev)
{
    gpio_set_value(dev->beep_gpio, 0);
    dev->beep_state = 1;
}

static void beep_off(struct beep_dev *dev)
{
    gpio_set_value(dev->beep_gpio, 1);
    dev->beep_state = 0;
}

static void beep_toggle(struct beep_dev *dev)
{
    if (dev->beep_state == 1) {
        beep_off(dev);
    } else {
        beep_on(dev);
    }
}

// probe
static int beep_probe(struct platform_device *pdev)
{
    int ret;
    int rc;

    printk(NAME " probe\n");

    // find node by platform
    beep.node = pdev->dev.of_node;

    // get beep gpio
    beep.beep_gpio = of_get_named_gpio(beep.node, "beep-gpios", 0);
    if (beep.beep_gpio < 0) {
        printk("beep gpio not found\n");
        ret = -ENODEV;
        goto err_find_node;
    }
    printk("beep gpio: %d\n", beep.beep_gpio);

    // request gpio
    rc = gpio_request(beep.beep_gpio, "beep");
    if (rc) {
        printk("gpio request failed\n");
        ret = -EINVAL;
        goto err_request_gpio;
    }

    // set gpio direction
    if(gpio_direction_output(beep.beep_gpio, 1))
    {
        printk("gpio direction output failed\n");
        ret = -EINVAL;
        goto err_set_gpio;
    }

    // beep off - default off
    beep_off(&beep);

    // register misc device
    ret = misc_register(&beep.dev);
    if (ret) {
        printk("misc register failed\n");
        goto err_reg_misc_dev;
    }


    // Link platform device with our led structure
    platform_set_drvdata(pdev, &beep);
 
	return 0;
err_reg_misc_dev:
    beep_off(&beep);
err_set_gpio:
    gpio_free(beep.beep_gpio);
err_request_gpio:
err_find_node:
    return ret;
}

// remove
static int beep_remove(struct platform_device *pdev)
{
    struct beep_dev *dev = platform_get_drvdata(pdev);
    printk(NAME " remove\n");

    // beep off
    beep_off(dev);
    
    // unregister misc device
    misc_deregister(&dev->dev);

    //free gpio
    gpio_free(dev->beep_gpio);

    return 0;
}

struct of_device_id beep_of_match[] = {
    {.compatible = "alpha-beep"},
    {},
};

static struct platform_driver beep_driver = {
    .driver = {
        .name = "imx6ull-beep",
        .of_match_table = beep_of_match,
    },
    .probe = beep_probe,
    .remove = beep_remove,
};

static int __init beep_driver_init(void)
{
    printk(NAME " init\n");

    return platform_driver_register(&beep_driver);
}

static void __exit beep_driver_exit(void)
{
    printk(NAME " exit\n");
    platform_driver_unregister(&beep_driver);
}

module_init(beep_driver_init);
module_exit(beep_driver_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("platform - misc led");
MODULE_LICENSE("GPL");