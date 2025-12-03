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
#include <linux/atomic.h> // atomic_t


#define NAME "key"
#define CHAR_DEV_BASE_MAJOR 100

#define DTS_KEY_COUNT   1
#define DTS_KEY_NODE_PATH   "/key"

struct key_dev{
    dev_t devid;    // device id
    int major;      // major number
    struct cdev cdev;   // character device
    struct class *key_class;    // class
    struct device *key_device;  // device
    struct device_node *np;     // device node
    int key_gpio; // key gpio

    atomic_t key_state; // 0: pressed; 1: not pressed
};

struct key_dev key;

char read_buf[100];
char write_buf[100];


/* The various file operations we support. */
int key_open (struct inode *inode, struct file *filp)
{
    printk(NAME " open\n");
    filp->private_data = &key;

    return 0;
}

int key_release (struct inode *inode, struct file *filp)
{
    printk(NAME " release\n");
    filp->private_data = NULL;

    return 0;
}

ssize_t key_read (struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    int data_len = 0;
    int copy_len = 0;
    int gpio_val;

    struct key_dev *dev = (struct key_dev *)filp->private_data;
    
    // read key status - use internal state instead of gpio_get_value for output pins
    if(gpio_get_value(dev->key_gpio) == 0)
    {
        while(gpio_get_value(dev->key_gpio) == 0);
        atomic_set(&key.key_state, 0);
    }
    else
    {
        atomic_set(&key.key_state, 1);
    }
    gpio_val = atomic_read(&key.key_state);

    data_len = sizeof(gpio_val);
    copy_len = count < data_len ? count : data_len;

    ret = copy_to_user(buf, &gpio_val, copy_len); // ret fail bytes
    if(ret != 0) {
        printk(NAME " copy_to_user failed, %d bytes not copied\n", ret);
        return -EFAULT;
    }
    
    // printk(NAME " read %d bytes successfully\n", copy_len);
    return copy_len;  // 返回实际读取的字节数
}

static const struct file_operations key_fops = {
	.owner		= THIS_MODULE,
    .open      = key_open,
    .release   = key_release,
    .read      = key_read
};


static int __init key_init(void)
{
    int rc;
    const char *str;
    struct device_node *child;

	// printk(KERN_DEBUG NAME " initializing\n");
	printk(NAME " initializing\n");

    // init atomic_t
    atomic_set(&key.key_state, 1);

    // find node
    key.np = of_find_node_by_path(DTS_KEY_NODE_PATH);
    if (!key.np)
    {
        printk(NAME "node not found\n");
        goto err_find_node;
    }

    // compatible
    if(of_property_read_string(key.np, "compatible", &str))
    {
        printk(NAME " compatible property read failed\n");
        return -ENODEV;
    }
    printk(NAME " compatible: %s\n", str);

    // status
    if(of_property_read_string(key.np, "status", &str))
    {
        printk(NAME " status property read failed\n");
        return -ENODEV;
    }
    printk(NAME " status: %s\n", str);
    
    
    // debug: list all child nodes
    printk(NAME " listing all child nodes:\n");
    for_each_child_of_node(key.np, child) {
        printk(NAME " child node: %s\n", child->name);
    }

    // get key gpio
    key.key_gpio = of_get_named_gpio(key.np, "key-gpios", 0);
    if(key.key_gpio < 0)
    {
        printk(NAME " key gpio not found, error: %d\n", key.key_gpio);
        goto err_find_node;
    }
    printk(NAME " key gpio: %d\n", key.key_gpio);

    // check if gpio is valid
    if(!gpio_is_valid(key.key_gpio))
    {
        printk(NAME " gpio %d is not valid\n", key.key_gpio);
        goto err_find_node;
    }

    // try to free gpio first in case it's already requested
    // gpio_free(key.key_gpio);
    
    // request gpio
    rc = gpio_request(key.key_gpio, "key");
    if(rc)
    {
        printk(NAME " gpio request failed, error: %d (EBUSY means already in use)\n", rc);
        goto err_find_node;
    }

    // set gpio direction
    if(gpio_direction_input(key.key_gpio))
    {
        printk(NAME " gpio direction input failed\n");
        goto err_gpio_request;
    }

    // set gpio value
    gpio_set_value(key.key_gpio, 0);

    // device id
    key.major = 0;
    if(key.major)
    {
        key.devid = MKDEV(key.major, 0);
        rc = register_chrdev_region(key.devid, DTS_KEY_COUNT, NAME);
        if(rc < 0) {
            printk(NAME " register_chrdev_region failed\n");
            goto err_gpio_request;
        }
    }
    else
    {
        rc = alloc_chrdev_region(&key.devid, 0, DTS_KEY_COUNT, NAME);
        if(rc < 0) {
            printk(NAME " alloc_chrdev_region failed\n");
            goto err_gpio_request;
        }

        key.major = MAJOR(key.devid);
    }

    printk(NAME " major: %d\n", key.major);
    printk(NAME " minor: %d\n", MINOR(key.devid));

    // register chrdev
    cdev_init(&key.cdev, &key_fops);
    rc = cdev_add(&key.cdev, key.devid, DTS_KEY_COUNT);
    if(rc < 0) {
        printk(NAME " cdev_add failed\n");
        goto err_chrdev;
    }

    // class_create
    key.key_class = class_create(THIS_MODULE, NAME);
    if(IS_ERR(key.key_class)) {
        printk(NAME " class_create failed\n");
        goto err_cdev;
    }

    // device_create
    key.key_device = device_create(key.key_class, NULL, key.devid, NULL, NAME);
    if(IS_ERR(key.key_device)) {
        printk(NAME " device_create failed\n");
        goto err_class;
    }

	return 0;

err_class:
    class_destroy(key.key_class);
err_cdev:
    cdev_del(&key.cdev);
err_chrdev:
    unregister_chrdev_region(key.devid, DTS_KEY_COUNT);
err_gpio_request:
    gpio_free(key.key_gpio);

err_find_node:
    return -ENODEV;
}

static void __exit key_exit(void)
{
	// printk(KERN_DEBUG NAME " exit\n");
	printk(NAME " exit\n");

    // cleanup in reverse order of initialization
    device_destroy(key.key_class, key.devid);
    class_destroy(key.key_class);
    cdev_del(&key.cdev);
    unregister_chrdev_region(key.devid, DTS_KEY_COUNT);

    // gpio free
    gpio_free(key.key_gpio);
}

module_init(key_init);
module_exit(key_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("key - chardev");
MODULE_LICENSE("GPL");