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
#include <linux/of_irq.h>     // irq_of_parse_and_map
#include <linux/slab.h> // kmalloc, kfree
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/atomic.h> // atomic_t
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/signal.h>


#define NAME "key_signal"
#define CHAR_DEV_BASE_MAJOR 100

#define KEY_COUNT   1
#define KEY_NODE_PATH   "/key"

#define KEY_DEBOUNCE_TIME_MS 10

struct key_desc{
    int gpio;
    int irq_num;
    // unsigned int key_value;

    atomic_t key_state; // 0: pressed; 1: not pressed
    atomic_t key_read_flag; // 0: no update; 1: update

    unsigned char name[32];
    irqreturn_t (*irq_handler)(int, void *);

    struct timer_list timer;    // timer for key debounce
};

struct key_dev{
    dev_t devid;    // device id
    int major;      // major number
    struct cdev cdev;   // character device
    struct class *key_class;    // class
    struct device *key_device;  // device
    struct device_node *np;     // device node

    struct key_desc key_descs[KEY_COUNT];
    wait_queue_head_t wq;       // wait queue head

    struct fasync_struct *fasync; // asynchronous notification

};

struct key_dev key;


static int key_fasync(int fd, struct file *filp, int on)
{
    struct key_dev *dev = (struct key_dev *)filp->private_data;

    return fasync_helper(fd, filp, on, &dev->fasync);
}


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
    
    // free asynchronous notification before clearing private_data
    key_fasync(-1, filp, 0);
    
    filp->private_data = NULL;

    return 0;
}

static int is_any_key_pressed(struct key_dev *dev)
{
    int i;
    for(i=0;i<KEY_COUNT;i++)
    {
        if(atomic_read(&dev->key_descs[i].key_read_flag) == 1)
        {
            return 1;
        }
    }

    return 0;
}

ssize_t key_read (struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    int data_len = 0;
    int copy_len = 0;
    int gpio_val[2]; //[0] - key id, [1] - key value

    int i;

    struct key_dev *dev = (struct key_dev *)filp->private_data;

    if(filp->f_flags & O_NONBLOCK)
    {
        if(!is_any_key_pressed(dev))
        {
            return -EAGAIN;
        }
    }
    else
    {
        // wait event
        // wait_event_interruptible(dev->wq, is_any_key_pressed(dev));
    }

    // printk(NAME " after wait_event\n");
    
    for(i=0;i<KEY_COUNT;i++)
    {
        if(atomic_read(&dev->key_descs[i].key_read_flag) == 1)
        {
            gpio_val[0] = i;
            gpio_val[1] = atomic_read(&dev->key_descs[i].key_state);
            atomic_set(&dev->key_descs[i].key_read_flag, 0);

            break;
        }
    }

    if(i == KEY_COUNT)
    {
        return 0;
    }

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

// key pool function
static unsigned int key_poll(struct file *filp, struct poll_table_struct *wait)
{
    int mask = 0;
    struct key_dev *dev = (struct key_dev *)filp->private_data;

    poll_wait(filp, &dev->wq, wait);

    if(is_any_key_pressed(dev))
    {
        mask |= POLLIN | POLLPRI;
    }   

    return mask;
}



static const struct file_operations key_fops = {
	.owner	   = THIS_MODULE,
    .open      = key_open,
    .release   = key_release,
    .read      = key_read,
    .poll      = key_poll,
    .fasync    = key_fasync,
};

// timer handler function
static void key_timer_handler(unsigned long data)
{
    struct key_desc *key_desc = (struct key_desc *)data;
    struct key_dev *key_dev = container_of(key_desc, struct key_dev, key_descs[0]);

    if(gpio_get_value(key_desc->gpio) == 0)
    {
        atomic_set(&key_desc->key_state, 0);
    }
    else
    {
        atomic_set(&key_desc->key_state, 1);
    }

    atomic_set(&key_desc->key_read_flag, 1);

    // wake up wait queue for poll/select
    wake_up_interruptible(&key_dev->wq);

    // send signal for fasync
    kill_fasync(&key_dev->fasync, SIGIO, POLL_IN);

    printk(NAME " timer handler, %s state: %d\n", key_desc->name, atomic_read(&key_desc->key_state));
}


// irq handler - key_irq_handler
static irqreturn_t key_irq_handler(int irq, void *key_desc)
{
    struct key_desc *key = (struct key_desc *)key_desc;

    // start timer
    mod_timer(&key->timer, jiffies + msecs_to_jiffies(KEY_DEBOUNCE_TIME_MS));
    // printk(NAME " %s irq, start timer\n", key->name);

    return IRQ_HANDLED;
}



static int __init key_signal_init(void)
{
    int rc;
    const char *str;
    struct device_node *child;
    int i;
    int j;

	// printk(KERN_DEBUG NAME " initializing\n");
	printk(NAME " initializing\n");

    // init atomic_t for each key
    for(i=0;i<KEY_COUNT;i++)
    {
        atomic_set(&key.key_descs[i].key_state, 1);
        atomic_set(&key.key_descs[i].key_read_flag, 0);
    }

    // init timer
    for(i=0;i<KEY_COUNT;i++)
    {
        init_timer(&key.key_descs[i].timer);
        key.key_descs[i].timer.function = key_timer_handler;
        key.key_descs[i].timer.data = (unsigned long)&key.key_descs[i];
    }

    // init wait queue head
    init_waitqueue_head(&key.wq);

    // find node
    key.np = of_find_node_by_path(KEY_NODE_PATH);
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


    // gpio - dts
    for(i=0;i<KEY_COUNT;i++)
    {
        // get key gpio
        key.key_descs[i].gpio = of_get_named_gpio(key.np, "key-gpios", i);
        if(key.key_descs[i].gpio < 0)
        {
            printk(NAME " key gpio not found, error: %d\n", key.key_descs[i].gpio);
            goto err_find_node;
        }
        printk(NAME " key gpio: %d\n", key.key_descs[i].gpio);

        // check if gpio is valid
        if(!gpio_is_valid(key.key_descs[i].gpio))
        {
            printk(NAME " gpio %d is not valid\n", key.key_descs[i].gpio);
            goto err_find_node;
        }

        // set name
        memset(key.key_descs[i].name, 0, sizeof(key.key_descs[i].name));
        sprintf(key.key_descs[i].name, "key%d", i);
    }

    // gpio - request
    for(i=0;i<KEY_COUNT;i++)
    {
        // request gpio
        rc = gpio_request(key.key_descs[i].gpio, key.key_descs[i].name);
        if(rc)
        {
            printk(NAME " gpio request failed, error: %d (EBUSY means already in use)\n", rc);
            goto err_gpio_request;
        }

        // set gpio direction
        if(gpio_direction_input(key.key_descs[i].gpio))
        {
            printk(NAME " gpio direction input failed\n");

            // release gpio
            gpio_free(key.key_descs[i].gpio);
            goto err_gpio_request;
        }

        // set gpio value
        gpio_set_value(key.key_descs[i].gpio, 0);
    }

    // irq
    for(i=0;i<KEY_COUNT;i++)
    {
        // set key irq handler
        key.key_descs[i].irq_handler = key_irq_handler;

        // get key irq
        key.key_descs[i].irq_num = irq_of_parse_and_map(key.np, i);
        if(!key.key_descs[i].irq_num)
        {
            printk(NAME " irq_of_parse_and_map failed for key %d\n", i);
            goto err_irq_request;
        }

        // key.key_descs[i].irq_num = gpio_to_irq(key.key_descs[i].gpio);

        // request irq
        rc = request_irq(key.key_descs[i].irq_num, 
                        key_irq_handler, 
                        irq_get_trigger_type(key.key_descs[i].irq_num), 
                        key.key_descs[i].name, 
                        &key.key_descs[i]);
        if(rc)
        {
            printk(NAME " irq request failed, error: %d\n", rc);
            goto err_irq_request;
        }
    }

    // device id
    key.major = 0;
    if(key.major)
    {
        key.devid = MKDEV(key.major, 0);
        rc = register_chrdev_region(key.devid, KEY_COUNT, NAME);
        if(rc < 0) {
            printk(NAME " register_chrdev_region failed\n");
            goto err_irq_request;
        }
    }
    else
    {
        rc = alloc_chrdev_region(&key.devid, 0, KEY_COUNT, NAME);
        if(rc < 0) {
            printk(NAME " alloc_chrdev_region failed\n");
            goto err_irq_request;
        }

        key.major = MAJOR(key.devid);
    }

    printk(NAME " major: %d\n", key.major);
    printk(NAME " minor: %d\n", MINOR(key.devid));

    // register chrdev
    cdev_init(&key.cdev, &key_fops);
    rc = cdev_add(&key.cdev, key.devid, KEY_COUNT);
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
    unregister_chrdev_region(key.devid, KEY_COUNT);
    // Fall through to free all IRQs and GPIOs
    i = KEY_COUNT; // Set i to cleanup all resources

err_irq_request:
    // Free all previous IRQs (i-1 and below)
    for(j = i - 1; j >= 0; j--)
    {
        free_irq(key.key_descs[j].irq_num, &key.key_descs[j]);
    }

    i = KEY_COUNT; // Set i to cleanup all resources
    
err_gpio_request:
    // Free all previous GPIOs (i-1 and below)
    for(j = i - 1; j >= 0; j--)
    {
        gpio_free(key.key_descs[j].gpio);
    }

err_find_node:
    // No resources allocated yet, just return error

    // free timer
    for(i=0;i<KEY_COUNT;i++)
    {
        del_timer_sync(&key.key_descs[i].timer);
    }

    return -ENODEV;
}

static void __exit key_signal_exit(void)
{
    int i;
    printk(NAME " exit\n");

    // wake up all waiting processes before cleanup
    // wake_up_all(&key.wq);

    // cleanup in reverse order of initialization
    device_destroy(key.key_class, key.devid);
    class_destroy(key.key_class);
    cdev_del(&key.cdev);
    unregister_chrdev_region(key.devid, KEY_COUNT);

    // free irq and gpio
    for(i=0;i<KEY_COUNT;i++)
    {
        if(key.key_descs[i].irq_num)
        {
            free_irq(key.key_descs[i].irq_num, &key.key_descs[i]);
        }
        if(key.key_descs[i].gpio)
        {
            gpio_free(key.key_descs[i].gpio);
        }
    }

    // free timer
    for(i=0;i<KEY_COUNT;i++)
    {
        del_timer_sync(&key.key_descs[i].timer);
    }
}

module_init(key_signal_init);
module_exit(key_signal_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("key - chardev");
MODULE_LICENSE("GPL");