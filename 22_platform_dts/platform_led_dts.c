#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h> // strlen
#include <linux/types.h>  // size_t
#include <linux/errno.h> // -EFAULT
#include <linux/uaccess.h> // copy_to_user
#include <linux/fs.h> // file_operations
#include <linux/cdev.h> // cdev_init, cdev_add
#include <linux/platform_device.h> // platform_device
#include <linux/device.h> // class_create, device_create
#include <linux/io.h> // ioremap, ioread32, iowrite32
#include <linux/ioport.h> // resource functions
#include <linux/resource.h> // resource_size
#include <linux/gpio.h> // gpio_set_value, gpio_request
#include <linux/of.h> // of_find_node_by_path
#include <linux/of_gpio.h> // of_get_named_gpio

#define NAME "platform_led_dts"

#define CHAR_DEV_BASE_MAJOR 100

#define DTS_LED_COUNT   1
#define DTS_LED_NODE_PATH   "/gpio_led"


struct dts_led_dev{
    dev_t devid;    // device id
    int major;      // major number
    struct cdev cdev;   // character device
    struct class *led_class;    // class
    struct device *led_device;  // device
    struct device_node *np;     // device node
    int led_gpio; // led gpio
    int led_state; // led state: 1=on, 0=off
};

struct dts_led_dev led;

// Buffer declarations
char read_buf[100];
char write_buf[100];


void led_on(void)
{
    gpio_set_value(led.led_gpio, 0);
    led.led_state = 1;
}

void led_off(void)
{
    gpio_set_value(led.led_gpio, 1);
    led.led_state = 0;
}

int led_open (struct inode *inode, struct file *filp)
{
    printk(NAME " open\n");
    filp->private_data = &led;
    return 0;
}

int led_release (struct inode *inode, struct file *filp)
{
    printk(NAME " release\n");
    filp->private_data = NULL;
    return 0;
}

ssize_t led_read (struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    int data_len = 0;
    int copy_len = 0;
    int gpio_val;
    
    printk(NAME " read\n");

    // read led status - use internal state instead of gpio_get_value for output pins
    gpio_val = gpio_get_value(led.led_gpio);
    printk(NAME " gpio_get_value: %d, led_state: %d\n", gpio_val, led.led_state);
    
    if(led.led_state == 1){
        printk(NAME " led is ON\n");
        snprintf(read_buf, sizeof(read_buf), "LED is ON\n");
    } else {
        printk(NAME " led is OFF\n");
        snprintf(read_buf, sizeof(read_buf), "LED is OFF\n");
    }

    data_len = strlen(read_buf);
    copy_len = count < data_len ? count : data_len;

    ret = copy_to_user(buf, read_buf, copy_len); // ret fail bytes
    if(ret != 0) {
        printk(NAME " copy_to_user failed, %d bytes not copied\n", ret);
        return -EFAULT;
    }
    
    printk(NAME " read %d bytes successfully\n", copy_len);
    return copy_len; 
}
ssize_t led_write (struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    printk(NAME " write %d\n", count);

    if(count >= sizeof(write_buf)) count = sizeof(write_buf) - 1;
    
    ret = copy_from_user(write_buf, buf, count); // ret fail bytes
    if(ret != 0) {
        printk(NAME " copy_from_user failed %d\n", ret);
        return -EFAULT;
    }

    write_buf[count] = '\0'; // null terminate the string
    printk(NAME " write buf ok, count: %d, string: %s\n", ret, write_buf);

    if(strncmp(write_buf, "on", 2) == 0) {
        led_on();
        printk(NAME " led on\n");
    } else if(strncmp(write_buf, "off", 3) == 0) {
        led_off();
        printk(NAME " led off\n");
    } else {
        printk(NAME " invalid command\n");
    }

    return count;
}


static const struct file_operations led_fops = {
	.owner		= THIS_MODULE,
    .open      = led_open,
    .release   = led_release,
    .read      = led_read,
    .write     = led_write
};



// probe
static int led_probe(struct platform_device *pdev)
{
    int rc;
    const char *str;
    struct device_node *child;

    
    printk(NAME " probe\n");

    // Link platform device with our led structure
    platform_set_drvdata(pdev, &led);

#if 0
    // find node
    led.np = of_find_node_by_path(DTS_LED_NODE_PATH);
    if (!led.np)
    {
        printk(NAME "node not found\n");
        goto err_find_node;
    }
#endif

    // find node by platform
    led.np = pdev->dev.of_node;

#if 0
    // compatible
    if(of_property_read_string(led.np, "compatible", &str))
    {
        printk(NAME " compatible property read failed\n");
        return -ENODEV;
    }
    printk(NAME " compatible: %s\n", str);

    // status
    if(of_property_read_string(led.np, "status", &str))
    {
        printk(NAME " status property read failed\n");
        return -ENODEV;
    }
    printk(NAME " status: %s\n", str);

    
    // debug: list all child nodes
    printk(NAME " listing all child nodes:\n");
    for_each_child_of_node(led.np, child) {
        printk(NAME " child node: %s\n", child->name);
    }
#endif   
    // get led gpio
    led.led_gpio = of_get_named_gpio(led.np, "led-gpios", 0);
    if(led.led_gpio < 0)
    {
        printk(NAME " led gpio not found, error: %d\n", led.led_gpio);
        goto err_find_node;
    }
    printk(NAME " led gpio: %d\n", led.led_gpio);

    // check if gpio is valid
    if(!gpio_is_valid(led.led_gpio))
    {
        printk(NAME " gpio %d is not valid\n", led.led_gpio);
        goto err_find_node;
    }

    // try to free gpio first in case it's already requested
    // gpio_free(led.led_gpio);
    
    // request gpio
    rc = gpio_request(led.led_gpio, "led");
    if(rc)
    {
        printk(NAME " gpio request failed, error: %d (EBUSY means already in use)\n", rc);
        goto err_find_node;
    }

    // set gpio direction
    if(gpio_direction_output(led.led_gpio, 1))
    {
        printk(NAME " gpio direction output failed\n");
        goto err_gpio_request;
    }

    // set gpio value
    gpio_set_value(led.led_gpio, 0);


    led_on();
    

    // device id
    led.major = 0;
    if(led.major)
    {
        led.devid = MKDEV(led.major, 0);
        rc = register_chrdev_region(led.devid, DTS_LED_COUNT, NAME);
        if(rc < 0) {
            printk(NAME " register_chrdev_region failed\n");
            goto err_gpio_request;
        }
    }
    else
    {
        rc = alloc_chrdev_region(&led.devid, 0, DTS_LED_COUNT, NAME);
        if(rc < 0) {
            printk(NAME " alloc_chrdev_region failed\n");
            goto err_gpio_request;
        }

        led.major = MAJOR(led.devid);
    }

    printk(NAME " major: %d\n", led.major);
    printk(NAME " minor: %d\n", MINOR(led.devid));

    // register chrdev
    cdev_init(&led.cdev, &led_fops);
    rc = cdev_add(&led.cdev, led.devid, DTS_LED_COUNT);
    if(rc < 0) {
        printk(NAME " cdev_add failed\n");
        goto err_chrdev;
    }

    // class_create
    led.led_class = class_create(THIS_MODULE, NAME);
    if(IS_ERR(led.led_class)) {
        printk(NAME " class_create failed\n");
        goto err_cdev;
    }

    // device_create
    led.led_device = device_create(led.led_class, NULL, led.devid, NULL, NAME);
    if(IS_ERR(led.led_device)) {
        printk(NAME " device_create failed\n");
        goto err_class;
    }

	return 0;

err_class:
    class_destroy(led.led_class);
err_cdev:
    cdev_del(&led.cdev);
err_chrdev:
    unregister_chrdev_region(led.devid, DTS_LED_COUNT);
err_gpio_request:
    gpio_free(led.led_gpio);

err_find_node:
    return -ENODEV;
}

// remove
static int led_remove(struct platform_device *pdev)
{
    struct dts_led_dev *dev = platform_get_drvdata(pdev);
    printk(NAME " remove\n");

    led_off();

    device_destroy(dev->led_class, dev->devid);
    class_destroy(dev->led_class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->devid, 1);

    // gpio free
    gpio_free(led.led_gpio);

    return 0;
}

struct of_device_id led_of_match[] = {
    {.compatible = "alpha-gpio_led"},
    {},
};

static struct platform_driver led_driver = {
    .driver = {
        .name = "imx6ull-led",
        .of_match_table = led_of_match,
    },
    .probe = led_probe,
    .remove = led_remove,
};

static int __init led_driver_init(void)
{
    printk(NAME " init\n");

    return platform_driver_register(&led_driver);
}

static void __exit led_driver_exit(void)
{
    printk(NAME " exit\n");
    platform_driver_unregister(&led_driver);
}

module_init(led_driver_init);
module_exit(led_driver_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("platform - led");
MODULE_LICENSE("GPL");