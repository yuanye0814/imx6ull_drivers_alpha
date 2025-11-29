#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
#include <linux/uaccess.h>
#else
#include <asm/uaccess.h>
#endif
#include <linux/string.h> // strlen
#include <linux/types.h>  // size_t
#include <linux/errno.h> // -EFAULT
#include <linux/io.h>    // ioremap, iounmap, iowrite32, ioread32
#include <linux/ioport.h> // request_region, iounmap
#include <linux/cdev.h>   // cdev_init, cdev_ad
#include <linux/device.h> // class_create, device_create
#include <linux/proc_fs.h> // create_proc_entry
#include <linux/seq_file.h> // seq_printf
#include <linux/of.h>
#include <linux/of_address.h> // of_iomap
#include <linux/slab.h> // kmalloc, kfree
#include <linux/gpio.h>
#include <linux/of_gpio.h>


#define NAME "gpio_led"
#define CHAR_DEV_BASE_MAJOR 100

#define DTS_LED_COUNT   1
#define DTS_LED_NODE_PATH   "/gpio_led"


#if 0 // reg addr
// CCM
#define CCM_CCGR0_BASE  (0X020C4068)
#define CCM_CCGR1_BASE  (0X020C406C)

// IOMUX
#define SW_MUX_GPIO1_IO03 	(0X020E0068)
#define SW_PAD_GPIO1_IO03 	(0X020E02F4)

// GPIO1
#define GPIO1_DR 			(0X0209C000)
#define GPIO1_GDIR 			(0X0209C004)
#define GPIO1_PSR 			(0X0209C008)
#define GPIO1_ICR1 			(0X0209C00C)
#define GPIO1_ICR2 			(0X0209C010)
#define GPIO1_IMR 			(0X0209C014)
#define GPIO1_ISR 			(0X0209C018)
#define GPIO1_EDGE_SEL 		(0X0209C01C)
#endif

struct gpio_led_dev{
    dev_t devid;    // device id
    int major;      // major number
    struct cdev cdev;   // character device
    struct class *led_class;    // class
    struct device *led_device;  // device
    struct device_node *np;     // device node
    int led_gpio; // led gpio
    int led_state; // led state: 1=on, 0=off
};

struct gpio_led_dev led;

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

/* The various file operations we support. */
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
    return copy_len;  // 返回实际读取的字节数
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


static int __init led_init(void)
{
    int rc;
    const char *str;
    struct device_node *child;

	// printk(KERN_DEBUG NAME " initializing\n");
	printk(NAME " initializing\n");

    // find node
    led.np = of_find_node_by_path(DTS_LED_NODE_PATH);
    if (!led.np)
    {
        printk(NAME "node not found\n");
        goto err_find_node;
    }

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

static void __exit led_exit(void)
{
	// printk(KERN_DEBUG NAME " exit\n");
	printk(NAME " exit\n");

    led_off();

    // cleanup in reverse order of initialization
    device_destroy(led.led_class, led.devid);
    class_destroy(led.led_class);
    cdev_del(&led.cdev);
    unregister_chrdev_region(led.devid, DTS_LED_COUNT);

    // gpio free
    gpio_free(led.led_gpio);


}

module_init(led_init);
module_exit(led_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("led - alloc_chrdev_region");
MODULE_LICENSE("GPL");