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

#define NAME "dts_led"
#define CHAR_DEV_BASE_MAJOR 100

#define DTS_LED_COUNT   1
#define DTS_LED_NODE_PATH   "/dts_led"


#if 0
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

struct dts_led_dev{
    dev_t devid;    // device id
    int major;      // major number
    struct cdev cdev;   // character device
    struct class *led_class;    // class
    struct device *led_device;  // device
    struct device_node *np;     // device node
};


struct dts_led_dev dts_led;

// remap addr
static void __iomem *ccm_ccgr1;
static void __iomem *sw_mux_gpio1_io03;
static void __iomem *sw_pad_gpio1_io03;
static void __iomem *gpio1_dr;
static void __iomem *gpio1_gdir;



char led_data[] = "kernel data - led\n";

char read_buf[100];
char write_buf[100];

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
    filp->private_data = &dts_led;
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


#if 0

	dts_led {
		compatible = "alpha-dts_led";
		status = "okay";
		#address-cells = <1>;
		#size-cells = <1>;
		dts_led_0: led@0 {
			compatible = "dts_led,led";
			reg = < 0X020C406C 0x4 /* CCM_CCGR1_BASE */
					0X020E0068 0x4 /* SW_MUX_GPIO1_IO03 */
					0X020E02F4 0x4 /* SW_PAD_GPIO1_IO03 */
					0X0209C000 0x4 /* GPIO1_DR */
					0X0209C004 0x4 /* GPIO1_GDIR */

			>;
		};
	};

#endif

static int __init led_init(void)
{
    uint32_t reg = 0;
    int rc;
    int i;
    const char *str;

	// printk(KERN_DEBUG NAME " initializing\n");
	printk(NAME " initializing\n");

    // find node
    dts_led.np = of_find_node_by_path(DTS_LED_NODE_PATH);
    if (!dts_led.np)
    {
        printk(NAME "node not found\n");
        goto err_find_node;
    }

    // compatible
    if(of_property_read_string(dts_led.np, "compatible", &str))
    {
        printk(NAME " compatible property read failed\n");
        return -ENODEV;
    }
    printk(NAME " compatible: %s\n", str);

    // status
    if(of_property_read_string(dts_led.np, "status", &str))
    {
        printk(NAME " status property read failed\n");
        return -ENODEV;
    }
    printk(NAME " status: %s\n", str);

    // get child node led@0
    struct device_node *led_node = of_get_child_by_name(dts_led.np, "led");
    if (!led_node) {
        printk(NAME " led@0 child node not found\n");
        return -ENODEV;
    }
    
    // get reg property from led@0 node
    const __be32 *reg_prop;
    int reg_len;
    reg_prop = of_get_property(led_node, "reg", &reg_len);
    if (!reg_prop) {
        printk(NAME " reg property not found\n");
        of_node_put(led_node);
        return -ENODEV;
    }
    
    // parse reg values (address, size pairs)
    int num_regs = reg_len / (sizeof(u32) * 2);
    printk(NAME " found %d register entries\n", num_regs);


    for (i = 0; i < num_regs; i++) {
        u32 addr = be32_to_cpu(reg_prop[i * 2]);
        u32 size = be32_to_cpu(reg_prop[i * 2 + 1]);
        printk(NAME " reg[%d]: addr=0x%08x, size=0x%x\n", i, addr, size);
    }
    
    of_node_put(led_node);

    if(num_regs != 5)
    {
        printk(NAME " reg property read failed\n");
        return -ENODEV;
    }


    // remap addr
    ccm_ccgr1 = ioremap(be32_to_cpu(reg_prop[0]), be32_to_cpu(reg_prop[1]));
    if (!ccm_ccgr1) goto err_ioremap;
    sw_mux_gpio1_io03 = ioremap(be32_to_cpu(reg_prop[2]), be32_to_cpu(reg_prop[3]));
    if (!sw_mux_gpio1_io03) goto err_ioremap;
    sw_pad_gpio1_io03 = ioremap(be32_to_cpu(reg_prop[4]), be32_to_cpu(reg_prop[5]));
    if (!sw_pad_gpio1_io03) goto err_ioremap;
    gpio1_dr = ioremap(be32_to_cpu(reg_prop[6]), be32_to_cpu(reg_prop[7]));
    if (!gpio1_dr) goto err_ioremap;
    gpio1_gdir = ioremap(be32_to_cpu(reg_prop[8]), be32_to_cpu(reg_prop[9]));
    if (!gpio1_gdir) goto err_ioremap;


    // clk
    reg = ioread32(ccm_ccgr1);
    reg &= ~(3 << 26);	/* bit27~26清0，时钟关闭 */
    reg |= (3 << 26);	/* bit27~26置1，时钟打开 */
    iowrite32(reg, ccm_ccgr1);

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

    // device id
    dts_led.major = 0;
    if(dts_led.major)
    {
        dts_led.devid = MKDEV(dts_led.major, 0);
        rc = register_chrdev_region(dts_led.devid, DTS_LED_COUNT, NAME);
        if(rc < 0) {
            printk(NAME " register_chrdev_region failed\n");
            goto err_ioremap;
        }
    }
    else
    {
        rc = alloc_chrdev_region(&dts_led.devid, 0, DTS_LED_COUNT, NAME);
        if(rc < 0) {
            printk(NAME " alloc_chrdev_region failed\n");
            goto err_ioremap;
        }

        dts_led.major = MAJOR(dts_led.devid);
    }

    printk(NAME " major: %d\n", dts_led.major);
    printk(NAME " minor: %d\n", MINOR(dts_led.devid));

    // register chrdev
    cdev_init(&dts_led.cdev, &led_fops);
    rc = cdev_add(&dts_led.cdev, dts_led.devid, 1);
    if(rc < 0) {
        printk(NAME " cdev_add failed\n");
        goto err_chrdev;
    }

    // class_create
    dts_led.led_class = class_create(THIS_MODULE, NAME);
    if(IS_ERR(dts_led.led_class)) {
        printk(NAME " class_create failed\n");
        goto err_cdev;
    }

    // device_create
    dts_led.led_device = device_create(dts_led.led_class, NULL, dts_led.devid, NULL, NAME);
    if(IS_ERR(dts_led.led_device)) {
        printk(NAME " device_create failed\n");
        goto err_class;
    }

	return 0;

err_class:
    class_destroy(dts_led.led_class);
err_cdev:
    cdev_del(&dts_led.cdev);
err_chrdev:
    unregister_chrdev_region(dts_led.devid, DTS_LED_COUNT);
err_ioremap:
    if(ccm_ccgr1) iounmap(ccm_ccgr1);
    if(sw_mux_gpio1_io03) iounmap(sw_mux_gpio1_io03);
    if(sw_pad_gpio1_io03) iounmap(sw_pad_gpio1_io03);
    if(gpio1_dr) iounmap(gpio1_dr);
    if(gpio1_gdir) iounmap(gpio1_gdir);

    return -EIO;

err_find_node:
    return -ENODEV;
}

static void __exit led_exit(void)
{
	// printk(KERN_DEBUG NAME " exit\n");
	printk(NAME " exit\n");

    led_off();

    // cleanup in reverse order of initialization
    device_destroy(dts_led.led_class, dts_led.devid);
    class_destroy(dts_led.led_class);
    cdev_del(&dts_led.cdev);
    unregister_chrdev_region(dts_led.devid, DTS_LED_COUNT);

    // unmap addr
    iounmap(ccm_ccgr1);
    iounmap(sw_mux_gpio1_io03);
    iounmap(sw_pad_gpio1_io03);
    iounmap(gpio1_dr);
    iounmap(gpio1_gdir);

}

module_init(led_init);
module_exit(led_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("led - alloc_chrdev_region");
MODULE_LICENSE("GPL");