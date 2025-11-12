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
#include <linux/slab.h> // kmalloc, kfree

#define NAME "dts_of"
#define CHAR_DEV_BASE_MAJOR 100

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

// remap addr
static void __iomem *ccm_ccgr0;
static void __iomem *ccm_ccgr1;
static void __iomem *sw_mux_gpio1_io03;
static void __iomem *sw_pad_gpio1_io03;
static void __iomem *gpio1_dr;
static void __iomem *gpio1_gdir;
static void __iomem *gpio1_psr;
static void __iomem *gpio1_icr1;
static void __iomem *gpio1_icr2;
static void __iomem *gpio1_imr;
static void __iomem *gpio1_isr;
static void __iomem *gpio1_edge_sel;


char led_data[] = "kernel data - led\n";

char read_buf[100];
char write_buf[100];

// static int major = CHAR_DEV_BASE_MAJOR;
static int major = 0;
static struct cdev led_cdev;
dev_t devid;

static struct class *led_class = NULL;
static struct device *led_device = NULL;

u32 *bright_values;


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

/* The various file operations we support. */
int led_open (struct inode *inode, struct file *filp)
{
    printk(NAME " open\n");
    return 0;
}

int led_release (struct inode *inode, struct file *filp)
{
    printk(NAME " release\n");
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


#if 0

	backlight {
		compatible = "pwm-backlight";
		pwms = <&pwm1 0 5000000>;
		brightness-levels = <0 4 8 16 32 64 128 255>;
		default-brightness-level = <6>;
		status = "okay";
	};

#endif

static int __init led_init(void)
{
    uint32_t reg = 0;
    int rc;

    struct device_node *backlight_node = NULL;
    // struct property *comp_prop;
    const char *compatible;
    const char *status;
    u32 default_brightness_level;
    int bright_level_count;
    int i;

	// printk(KERN_DEBUG NAME " initializing\n");
	printk(NAME " initializing\n");

    // find node - "/backlight" from dtb
    backlight_node = of_find_node_by_path("/backlight");
    if (!backlight_node)
    {
        printk(NAME " backlight node not found\n");
        return -ENODEV;
    }

    if(of_property_read_string(backlight_node, "compatible", &compatible))
    {
        printk(NAME " compatible property read failed\n");
        return -ENODEV;
    }
    printk(NAME " compatible: %s\n", compatible);

    if(of_property_read_string(backlight_node, "status", &status))
    {
        printk(NAME " status property read failed\n");
        return -ENODEV;
    }
    printk(NAME " status: %s\n", status);

    // default-brightness-level = <6>;
    if(of_property_read_u32(backlight_node, "default-brightness-level", &default_brightness_level))
    {
        printk(NAME " default-brightness-level read failed\n");
        return -ENODEV;
    }
    printk(NAME " default-brightness-level: %d\n", default_brightness_level);

    // brightness-levels = <0 4 8 16 32 64 128 255>;
    bright_level_count = of_property_count_elems_of_size(backlight_node, "brightness-levels", sizeof(u32));
    if (bright_level_count <= 0) {
        printk(NAME " brightness-levels count failed\n");
        return -ENODEV;
    }
    printk(NAME " brightness-levels count: %d\n", bright_level_count);

    bright_values = kzalloc(bright_level_count * sizeof(u32), GFP_KERNEL);
    if (!bright_values)
        return -ENOMEM;
        
    if (of_property_read_u32_array(backlight_node, "brightness-levels", bright_values, bright_level_count)) {
        kfree(bright_values);
        return -ENODEV;
    }
    
    for(i = 0; i < bright_level_count; i++)
        printk(NAME " [%d]: %d\n", i, bright_values[i]);
    
    kfree(bright_values);

    // remap addr
    ccm_ccgr0 = ioremap(CCM_CCGR0_BASE, 4);
    if (!ccm_ccgr0) goto err_ioremap;
    ccm_ccgr1 = ioremap(CCM_CCGR1_BASE, 4);
    if (!ccm_ccgr1) goto err_ioremap;
    sw_mux_gpio1_io03 = ioremap(SW_MUX_GPIO1_IO03, 4);
    if (!sw_mux_gpio1_io03) goto err_ioremap;
    sw_pad_gpio1_io03 = ioremap(SW_PAD_GPIO1_IO03, 4);
    if (!sw_pad_gpio1_io03) goto err_ioremap;
    gpio1_dr = ioremap(GPIO1_DR, 4);
    if (!gpio1_dr) goto err_ioremap;
    gpio1_gdir = ioremap(GPIO1_GDIR, 4);
    if (!gpio1_gdir) goto err_ioremap;
    gpio1_psr = ioremap(GPIO1_PSR, 4);
    if (!gpio1_psr) goto err_ioremap;
    gpio1_icr1 = ioremap(GPIO1_ICR1, 4);
    if (!gpio1_icr1) goto err_ioremap;
    gpio1_icr2 = ioremap(GPIO1_ICR2, 4);
    if (!gpio1_icr2) goto err_ioremap;
    gpio1_imr = ioremap(GPIO1_IMR, 4);
    if (!gpio1_imr) goto err_ioremap;
    gpio1_isr = ioremap(GPIO1_ISR, 4);
    if (!gpio1_isr) goto err_ioremap;
    gpio1_edge_sel = ioremap(GPIO1_EDGE_SEL, 4);
    if (!gpio1_edge_sel) goto err_ioremap;

    // clk
    reg = ioread32(ccm_ccgr0);
    reg &= ~(3 << 26);	/* bit27~26清0，时钟关闭 */
    reg |= (3 << 26);	/* bit27~26置1，时钟打开 */
    iowrite32(reg, ccm_ccgr0);

    // init io
    iowrite32(0x5, sw_mux_gpio1_io03);  /* 复用为GPIO1_IO03 */

    // configure gpio1_io03
    iowrite32(0X10B0, sw_pad_gpio1_io03);

    // set gpio1_io03 as output
    iowrite32(0X0000008, gpio1_gdir);

    // set gpio1_io03 output low level, turn on LED0
    // iowrite32(0X0, gpio1_dr);

    led_on();
    
    // register char device
    // if(register_chrdev(CHAR_DEV_BASE_MAJOR, NAME, &led_fops) < 0) {
    //     printk(KERN_INFO "%s: unable to get major %d\n", NAME, CHAR_DEV_BASE_MAJOR);
    //     return -EIO;
    // }

    // get id for char device with alloc_chrdev_register
    if(major)
    {
        devid = MKDEV(major, 0);
        rc = register_chrdev_region(devid, 1, NAME);
        if(rc < 0) {
            printk(NAME " register_chrdev_region failed\n");
            goto err_ioremap;
        }
        printk(NAME " major: %d\n", major);
        printk(NAME " minor: %d\n", MINOR(devid));

    }
    else
    {
        rc = alloc_chrdev_region(&devid, 0, 1, NAME);
        if(rc < 0) {
            printk(NAME " alloc_chrdev_region failed\n");
            goto err_ioremap;
        }
        major = MAJOR(devid);
        printk(NAME " major: %d\n", major);
        printk(NAME " minor: %d\n", MINOR(devid));
    }

    // register
    cdev_init(&led_cdev, &led_fops);
    rc = cdev_add(&led_cdev, devid, 1);
    if(rc < 0) {
        printk(NAME " cdev_add failed\n");
        goto err_chrdev;
    }

    // mknod class_create
    led_class = class_create(THIS_MODULE, NAME);
    if(IS_ERR(led_class)) {
        printk(NAME " class_create failed\n");
        goto err_cdev;
    }
    led_device = device_create(led_class, NULL, devid, NULL, NAME);
    if(IS_ERR(led_device)) {
        printk(NAME " device_create failed\n");
        goto err_class;
    }

	return 0;

err_class:
    class_destroy(led_class);
err_cdev:
    cdev_del(&led_cdev);
err_chrdev:
    unregister_chrdev_region(devid, 1);
err_ioremap:
    if(ccm_ccgr0) iounmap(ccm_ccgr0);
    if(ccm_ccgr1) iounmap(ccm_ccgr1);
    if(sw_mux_gpio1_io03) iounmap(sw_mux_gpio1_io03);
    if(sw_pad_gpio1_io03) iounmap(sw_pad_gpio1_io03);
    if(gpio1_dr) iounmap(gpio1_dr);
    if(gpio1_gdir) iounmap(gpio1_gdir);
    if(gpio1_psr) iounmap(gpio1_psr);
    if(gpio1_icr1) iounmap(gpio1_icr1);
    if(gpio1_icr2) iounmap(gpio1_icr2);
    if(gpio1_imr) iounmap(gpio1_imr);
    if(gpio1_isr) iounmap(gpio1_isr);
    if(gpio1_edge_sel) iounmap(gpio1_edge_sel);
    return -EIO;
}

static void __exit led_exit(void)
{
	// printk(KERN_DEBUG NAME " exit\n");
	printk(NAME " exit\n");

    led_off();

    // cleanup in reverse order of initialization
    device_destroy(led_class, devid);
    class_destroy(led_class);
    cdev_del(&led_cdev);
    unregister_chrdev_region(devid, 1);

    // unmap addr
    iounmap(ccm_ccgr0);
    iounmap(ccm_ccgr1);
    iounmap(sw_mux_gpio1_io03);
    iounmap(sw_pad_gpio1_io03);
    iounmap(gpio1_dr);
    iounmap(gpio1_gdir);
    iounmap(gpio1_psr);
    iounmap(gpio1_icr1);
    iounmap(gpio1_icr2);
    iounmap(gpio1_imr);
    iounmap(gpio1_isr);
    iounmap(gpio1_edge_sel);
}

module_init(led_init);
module_exit(led_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("led - alloc_chrdev_region");
MODULE_LICENSE("GPL");