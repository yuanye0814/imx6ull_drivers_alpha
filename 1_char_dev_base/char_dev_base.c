#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h> // copy_to_user copy_from_user
#include <linux/string.h> // strlen
#include <linux/types.h>  // size_t
#include <linux/errno.h> // -EFAULT

#define NAME "char_dev_base"
#define CHAR_DEV_BASE_MAJOR 100

char char_dev_base_data[] = "kernel data - hello\n";

char read_buf[100];
char write_buf[100];

/* The various file operations we support. */


int char_dev_base_open (struct inode *inode, struct file *filp)
{
    printk(NAME " open\n");
    return 0;
}
int char_dev_base_release (struct inode *inode, struct file *filp)
{
    printk(NAME " release\n");
    return 0;
}
ssize_t char_dev_base_read (struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    int data_len = strlen(char_dev_base_data);
    int copy_len = count < data_len ? count : data_len;
    // int copy_len = min(count, data_len);
    
    printk(NAME " read\n");

    ret = copy_to_user(buf, char_dev_base_data, copy_len); // ret fail bytes
    if(ret != 0) {
        printk(NAME " copy_to_user failed, %d bytes not copied\n", ret);
        return -EFAULT;
    }
    
    printk(NAME " read %d bytes successfully\n", copy_len);
    return copy_len;  // 返回实际读取的字节数
}
ssize_t char_dev_base_write (struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    printk(NAME " write %d\n", count);

    ret = copy_from_user(write_buf, buf, count); // ret fail bytes
    if(ret != 0) {
        printk(NAME " copy_from_user failed %d\n", ret);
        return -EFAULT;
    }

    write_buf[count] = '\0'; // null terminate the string
    printk(NAME " write buf ok, count: %d, string: %s\n", ret, write_buf);

    return count;
}


static const struct file_operations char_dev_base_fops = {
	.owner		= THIS_MODULE,
    .open      = char_dev_base_open,
    .release   = char_dev_base_release,
    .read      = char_dev_base_read,
    .write     = char_dev_base_write
};

static int __init char_dev_base_init(void)
{
	// printk(KERN_DEBUG NAME " initializing\n");
	printk(NAME " initializing\n");

    if(register_chrdev(CHAR_DEV_BASE_MAJOR, NAME, &char_dev_base_fops) < 0) {
        printk(KERN_INFO "%s: unable to get major %d\n", NAME, CHAR_DEV_BASE_MAJOR);
        return -1;
    }

	return 0;
}

static void __exit char_dev_base_exit(void)
{
	// printk(KERN_DEBUG NAME " exit\n");
	printk(NAME " exit\n");

    unregister_chrdev(CHAR_DEV_BASE_MAJOR, NAME);
}

module_init(char_dev_base_init);
module_exit(char_dev_base_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("test");
MODULE_LICENSE("GPL");