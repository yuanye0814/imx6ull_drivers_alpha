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

#define NAME "platform_led_driver"

#define DEV_NAME "imx6ull-led"

struct dts_led_dev{
    dev_t devid;    // device id
    int major;      // major number
    struct cdev cdev;   // character device
    struct class *led_class;    // class
    struct device *led_device;  // device
    // struct device_node *np;     // device node
};

struct dts_led_dev led;

// Buffer declarations
char read_buf[100];
char write_buf[100];

// remap addr
static void __iomem *ccm_ccgr1;
static void __iomem *sw_mux_gpio1_io03;
static void __iomem *sw_pad_gpio1_io03;
static void __iomem *gpio1_dr;
static void __iomem *gpio1_gdir;

void led_on(void)
{
    uint32_t reg = ioread32(gpio1_dr);
    reg &= ~(1 << 3); // gpio1_io03 low level
    iowrite32(reg, gpio1_dr);
}

void led_off(void)
{
    uint32_t reg = ioread32(gpio1_dr);
    reg |= (1 << 3); // gpio1_io03 high level
    iowrite32(reg, gpio1_dr);
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
    uint32_t reg;
    // int copy_len = min(count, data_len);
    
    printk(NAME " read\n");

    // read led status
    reg = ioread32(gpio1_dr);
    if((reg & (1 << 3)) == 0) {
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

// probe
static int led_probe(struct platform_device *pdev)
{
    uint32_t reg = 0;
    int rc;
    int i;
    struct resource *res[5];
    
    printk(NAME " probe\n");

    // Link platform device with our led structure
    platform_set_drvdata(pdev, &led);

    // get device resource
    for(i=0;i<5;i++)
    {
        res[i] = platform_get_resource(pdev, IORESOURCE_MEM, i);
        if (res[i] == NULL)
        {
            printk("get resource failed\n");
            return -EFAULT;
        }
        printk("res[%d]->start = 0x%08x\n", i, res[i]->start);
    }

    // remap addr
    ccm_ccgr1 = ioremap(res[0]->start, resource_size(res[0]));
    sw_mux_gpio1_io03 = ioremap(res[1]->start, resource_size(res[1]));
    sw_pad_gpio1_io03 = ioremap(res[2]->start, resource_size(res[2]));
    gpio1_dr = ioremap(res[3]->start, resource_size(res[3]));
    gpio1_gdir = ioremap(res[4]->start, resource_size(res[4]));

    // clk
    reg = ioread32(ccm_ccgr1);
    reg &= ~(3 << 26);	/* bit27~26清0，时钟关闭 */
    reg |= (3 << 26);	/* bit27~26置1，时钟打开 */
    iowrite32(reg, ccm_ccgr1);

    // init io
    iowrite32(0x5, sw_mux_gpio1_io03);

    // configure gpio1_io03
    iowrite32(0x10B0, sw_pad_gpio1_io03);

    // set gpio1_io03 output
    iowrite32(0X0000008, gpio1_gdir); 

    led_on();


    // get id for char device with alloc_chrdev_register
    if(led.major)
    {
        led.devid = MKDEV(led.major, 0);
        rc = register_chrdev_region(led.devid, 1, NAME);
        if(rc < 0) {
            printk(NAME " register_chrdev_region failed\n");
            goto err_ioremap;
        }
        printk(NAME " major: %d\n", led.major);
        printk(NAME " minor: %d\n", MINOR(led.devid));

    }
    else
    {
        rc = alloc_chrdev_region(&led.devid, 0, 1, NAME);
        if(rc < 0) {
            printk(NAME " alloc_chrdev_region failed\n");
            goto err_ioremap;
        }
        led.major = MAJOR(led.devid);
        printk(NAME " major: %d\n", led.major);
        printk(NAME " minor: %d\n", MINOR(led.devid));
    }

    // register
    cdev_init(&led.cdev, &led_fops);
    rc = cdev_add(&led.cdev, led.devid, 1);
    if(rc < 0) {
        printk(NAME " cdev_add failed\n");
        goto err_chrdev;
    }

    // mknod class_create
    led.led_class = class_create(THIS_MODULE, NAME);
    if(IS_ERR(led.led_class)) {
        printk(NAME " class_create failed\n");
        goto err_cdev;
    }
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
    unregister_chrdev_region(led.devid, 1);
err_ioremap:
    if(ccm_ccgr1) iounmap(ccm_ccgr1);
    if(sw_mux_gpio1_io03) iounmap(sw_mux_gpio1_io03);
    if(sw_pad_gpio1_io03) iounmap(sw_pad_gpio1_io03);
    if(gpio1_dr) iounmap(gpio1_dr);
    if(gpio1_gdir) iounmap(gpio1_gdir);
    return -EIO;
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

    // unmap
    if(ccm_ccgr1) iounmap(ccm_ccgr1);
    if(sw_mux_gpio1_io03) iounmap(sw_mux_gpio1_io03);
    if(sw_pad_gpio1_io03) iounmap(sw_pad_gpio1_io03);
    if(gpio1_dr) iounmap(gpio1_dr);
    if(gpio1_gdir) iounmap(gpio1_gdir);

    return 0;
}

static struct platform_driver led_driver = {
    .driver = {
        .name = DEV_NAME,
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