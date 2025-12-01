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


#define NAME "beep"
#define CHAR_DEV_BASE_MAJOR 100

#define DTS_LED_COUNT   1
#define DTS_LED_NODE_PATH   "/beep"


struct beep_dev{
    dev_t devid;    // device id
    int major;      // major number
    struct cdev cdev;   // character device
    struct class *beep_class;    // class
    struct device *beep_device;  // device
    struct device_node *np;     // device node
    int beep_gpio; // beep gpio
    int beep_state; // beep state: 1=on, 0=off
};

struct beep_dev beep;

char read_buf[100];
char write_buf[100];


void beep_on(void)
{
    gpio_set_value(beep.beep_gpio, 0);
    beep.beep_state = 1;
}

void beep_off(void)
{
    gpio_set_value(beep.beep_gpio, 1);
    beep.beep_state = 0;
}

/* The various file operations we support. */
int beep_open (struct inode *inode, struct file *filp)
{
    printk(NAME " open\n");
    filp->private_data = &beep;
    return 0;
}

int beep_release (struct inode *inode, struct file *filp)
{
    printk(NAME " release\n");
    filp->private_data = NULL;
    return 0;
}

ssize_t beep_read (struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    int data_len = 0;
    int copy_len = 0;
    int gpio_val;
    
    printk(NAME " read\n");

    // read beep status - use internal state instead of gpio_get_value for output pins
    gpio_val = gpio_get_value(beep.beep_gpio);
    printk(NAME " gpio_get_value: %d, beep_state: %d\n", gpio_val, beep.beep_state);
    
    if(beep.beep_state == 1){
        printk(NAME " beep is ON\n");
        snprintf(read_buf, sizeof(read_buf), "LED is ON\n");
    } else {
        printk(NAME " beep is OFF\n");
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
ssize_t beep_write (struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
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
        beep_on();
        printk(NAME " beep on\n");
    } else if(strncmp(write_buf, "off", 3) == 0) {
        beep_off();
        printk(NAME " beep off\n");
    } else {
        printk(NAME " invalid command\n");
    }

    return count;
}


static const struct file_operations beep_fops = {
	.owner		= THIS_MODULE,
    .open      = beep_open,
    .release   = beep_release,
    .read      = beep_read,
    .write     = beep_write
};


static int __init beep_init(void)
{
    int rc;
    const char *str;
    struct device_node *child;

	// printk(KERN_DEBUG NAME " initializing\n");
	printk(NAME " initializing\n");

    // find node
    beep.np = of_find_node_by_path(DTS_LED_NODE_PATH);
    if (!beep.np)
    {
        printk(NAME "node not found\n");
        goto err_find_node;
    }

    // compatible
    if(of_property_read_string(beep.np, "compatible", &str))
    {
        printk(NAME " compatible property read failed\n");
        return -ENODEV;
    }
    printk(NAME " compatible: %s\n", str);

    // status
    if(of_property_read_string(beep.np, "status", &str))
    {
        printk(NAME " status property read failed\n");
        return -ENODEV;
    }
    printk(NAME " status: %s\n", str);
    
    
    // debug: list all child nodes
    printk(NAME " listing all child nodes:\n");
    for_each_child_of_node(beep.np, child) {
        printk(NAME " child node: %s\n", child->name);
    }

    // get beep gpio
    beep.beep_gpio = of_get_named_gpio(beep.np, "beep-gpios", 0);
    if(beep.beep_gpio < 0)
    {
        printk(NAME " beep gpio not found, error: %d\n", beep.beep_gpio);
        goto err_find_node;
    }
    printk(NAME " beep gpio: %d\n", beep.beep_gpio);

    // check if gpio is valid
    if(!gpio_is_valid(beep.beep_gpio))
    {
        printk(NAME " gpio %d is not valid\n", beep.beep_gpio);
        goto err_find_node;
    }

    // try to free gpio first in case it's already requested
    // gpio_free(beep.beep_gpio);
    
    // request gpio
    rc = gpio_request(beep.beep_gpio, "beep");
    if(rc)
    {
        printk(NAME " gpio request failed, error: %d (EBUSY means already in use)\n", rc);
        goto err_find_node;
    }

    // set gpio direction
    if(gpio_direction_output(beep.beep_gpio, 1))
    {
        printk(NAME " gpio direction output failed\n");
        goto err_gpio_request;
    }

    // set gpio value
    gpio_set_value(beep.beep_gpio, 0);


    beep_on();
    

    // device id
    beep.major = 0;
    if(beep.major)
    {
        beep.devid = MKDEV(beep.major, 0);
        rc = register_chrdev_region(beep.devid, DTS_LED_COUNT, NAME);
        if(rc < 0) {
            printk(NAME " register_chrdev_region failed\n");
            goto err_gpio_request;
        }
    }
    else
    {
        rc = alloc_chrdev_region(&beep.devid, 0, DTS_LED_COUNT, NAME);
        if(rc < 0) {
            printk(NAME " alloc_chrdev_region failed\n");
            goto err_gpio_request;
        }

        beep.major = MAJOR(beep.devid);
    }

    printk(NAME " major: %d\n", beep.major);
    printk(NAME " minor: %d\n", MINOR(beep.devid));

    // register chrdev
    cdev_init(&beep.cdev, &beep_fops);
    rc = cdev_add(&beep.cdev, beep.devid, DTS_LED_COUNT);
    if(rc < 0) {
        printk(NAME " cdev_add failed\n");
        goto err_chrdev;
    }

    // class_create
    beep.beep_class = class_create(THIS_MODULE, NAME);
    if(IS_ERR(beep.beep_class)) {
        printk(NAME " class_create failed\n");
        goto err_cdev;
    }

    // device_create
    beep.beep_device = device_create(beep.beep_class, NULL, beep.devid, NULL, NAME);
    if(IS_ERR(beep.beep_device)) {
        printk(NAME " device_create failed\n");
        goto err_class;
    }

	return 0;

err_class:
    class_destroy(beep.beep_class);
err_cdev:
    cdev_del(&beep.cdev);
err_chrdev:
    unregister_chrdev_region(beep.devid, DTS_LED_COUNT);
err_gpio_request:
    gpio_free(beep.beep_gpio);

err_find_node:
    return -ENODEV;
}

static void __exit beep_exit(void)
{
	// printk(KERN_DEBUG NAME " exit\n");
	printk(NAME " exit\n");

    beep_off();

    // cleanup in reverse order of initialization
    device_destroy(beep.beep_class, beep.devid);
    class_destroy(beep.beep_class);
    cdev_del(&beep.cdev);
    unregister_chrdev_region(beep.devid, DTS_LED_COUNT);

    // gpio free
    gpio_free(beep.beep_gpio);


}

module_init(beep_init);
module_exit(beep_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("beep - chrdev");
MODULE_LICENSE("GPL");