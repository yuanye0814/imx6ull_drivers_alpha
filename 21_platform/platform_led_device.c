#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h> // strlen
#include <linux/types.h>  // size_t
#include <linux/errno.h> // -EFAULT
#include <linux/uaccess.h> // copy_to_user
#include <linux/fs.h> // file_operations
#include <linux/cdev.h> // cdev_init, cdev_add
#include <linux/platform_device.h> // platform_device

#define NAME "platform_led_device"

#define DEV_NAME "imx6ull-led"

// led gpio reg info

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

#define REG_LENTH   (4)

// resource
struct resource led_resources[] = {
    [0] = {
        .start = CCM_CCGR1_BASE,
        .end = CCM_CCGR1_BASE + REG_LENTH - 1,
        .flags = IORESOURCE_MEM,
    },
    [1] = {
        .start = SW_MUX_GPIO1_IO03,
        .end = SW_MUX_GPIO1_IO03 + REG_LENTH - 1,
        .flags = IORESOURCE_MEM,
    },
    [2] = {
        .start = SW_PAD_GPIO1_IO03,
        .end = SW_PAD_GPIO1_IO03 + REG_LENTH - 1,
        .flags = IORESOURCE_MEM,
    },
    [3] = {
        .start = GPIO1_DR,
        .end = GPIO1_DR + REG_LENTH - 1,
        .flags = IORESOURCE_MEM,
    },
    [4] = {
        .start = GPIO1_GDIR,
        .end = GPIO1_GDIR + REG_LENTH - 1,
        .flags = IORESOURCE_MEM,
    },
};

// release function
static void led_release(struct device *dev)
{
    printk(NAME " led release\n");
}

static struct platform_device led_device = {
    .name = DEV_NAME,
    .id = -1,
    .dev = {
        .release = led_release,
    },
    .num_resources = ARRAY_SIZE(led_resources),
    .resource = led_resources,
};

static int __init led_device_init(void)
{
    printk(NAME " init\n");

	return platform_device_register(&led_device);
}

static void __exit led_device_exit(void)
{
    printk(NAME " exit\n");
    platform_device_unregister(&led_device);
}

module_init(led_device_init);
module_exit(led_device_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("platform - led");
MODULE_LICENSE("GPL");