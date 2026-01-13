#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/blk-mq.h>
#include <linux/slab.h>

#define NAME "ram_disk_req"
#define RAM_DISK_COUNT 3
#define RAM_DISK_SIZE (8 * 1024 * 1024)  // 8MB disk size
#define RAM_DISK_MINOR_START 0


// ram disk struct
struct ram_disk_dev {
    dev_t devid;
    int major;
    char *data;
    // ram address
    void *ram_address;
    struct request_queue *request_queue;
};

struct ram_disk_dev ram_disk;

static struct gendisk *ram_disk_gendisk;
static DEFINE_SPINLOCK(ram_disk_lock);


static int ram_disk_open(struct block_device *bdev, fmode_t mode)
{
    printk(NAME " ram disk open\n");
    return 0;
}

static void ram_disk_release(struct gendisk *disk, fmode_t mode)
{
    printk(NAME " ram disk release\n");
    // nothing to do
}

static int ram_disk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
    geo->heads = 4;
    geo->sectors = 16;
    geo->cylinders = RAM_DISK_SIZE / (geo->heads * geo->sectors * 512);
    geo->start = 0;
    return 0;
}

static const struct block_device_operations ram_disk_fops =
{
	.owner		= THIS_MODULE,
	.open		= ram_disk_open,
	.release	= ram_disk_release,
    .getgeo 	= ram_disk_getgeo,
};

static struct kobject *ram_disk_find(dev_t dev, int *part, void *data)
{
	*part = 0;
	return get_disk(ram_disk_gendisk);
}

static void do_ram_disk_request(struct request_queue *q)
{
    struct ram_disk_dev *dev = q->queuedata;
    struct request *req;

    req = blk_fetch_request(q);
    while (req != NULL) {
        sector_t start_sector = blk_rq_pos(req);
        unsigned int sector_cnt = blk_rq_sectors(req);
        unsigned long offset = start_sector * 512; // sector size 512 bytes
        unsigned long nbytes = sector_cnt * 512;

        unsigned long len  = blk_rq_cur_bytes(req);
        //unsigned long len = nbytes;

        void *buffer;
        struct bio_vec bv;
        struct req_iterator iter;
        int err = 0;

        if ((offset + len) > RAM_DISK_SIZE) {
            printk(NAME " Request out of bounds: start_sector=%llu, sector_cnt=%u\n",
                   (unsigned long long)start_sector, sector_cnt);
            goto done;
        }

        buffer = bio_data(req->bio);

        if (rq_data_dir(req) == READ) {
            memcpy(buffer, dev->ram_address + offset, len);
        } else {
            memcpy(dev->ram_address + offset, buffer, len);
        }


done:
		if (!__blk_end_request_cur(req, err))
			req = blk_fetch_request(q);
    }
}

static int __init ram_disk_driver_init(void)
{
    int ret = 0;
    printk(NAME " ram disk with request init\n");

    // allocate ram
    ram_disk.ram_address = kzalloc(RAM_DISK_SIZE, GFP_KERNEL);
    if (!ram_disk.ram_address) {
        printk(NAME " failed to allocate ram disk memory\n");
        ret = -ENOMEM;
        goto error_alloc_ram;
    }
    memset(ram_disk.ram_address, 0, RAM_DISK_SIZE);

    // register block device
    ram_disk.major = register_blkdev(0, NAME);
    if (ram_disk.major < 0) {
        printk(NAME " failed to register block device\n");
        ret = -ENODEV;
        goto error_register_block_dev;
    }
    printk(NAME " registered block device with major number: %d\n", ram_disk.major);

    // request queue
    ram_disk.request_queue = blk_init_queue(do_ram_disk_request, &ram_disk_lock);
    if (!ram_disk.request_queue) {
        printk(NAME " failed to initialize request queue\n");
        ret = -ENOMEM;
        goto error_init_request_queue;
    }
    ram_disk.request_queue->queuedata = &ram_disk;

    // alloc disk
    ram_disk_gendisk = alloc_disk(RAM_DISK_COUNT);
    if (!ram_disk_gendisk) {
        printk(NAME " failed to allocate gendisk\n");
        ret = -ENOMEM;
        goto error_alloc_disk;
    }

    //blk_register_region
    ram_disk_gendisk->major = ram_disk.major;
    ram_disk_gendisk->first_minor = RAM_DISK_MINOR_START;
    ram_disk_gendisk->fops = &ram_disk_fops;
    ram_disk_gendisk->private_data = &ram_disk;
    ram_disk_gendisk->queue = ram_disk.request_queue;
    sprintf(ram_disk_gendisk->disk_name, NAME);
    set_capacity(ram_disk_gendisk, RAM_DISK_SIZE / 512); // sector size 512 bytes   
    add_disk(ram_disk_gendisk);
    blk_register_region(MKDEV(ram_disk.major, RAM_DISK_MINOR_START), RAM_DISK_COUNT, THIS_MODULE, ram_disk_find, NULL, NULL);
    
    printk(NAME " ram disk with request initialized successfully\n");

    return 0;

error_alloc_disk:
    blk_cleanup_queue(ram_disk.request_queue);

error_init_request_queue:
    unregister_blkdev(ram_disk.major, NAME);

error_register_block_dev:
    kfree(ram_disk.ram_address);

error_alloc_ram:
    return ret;
}

static void __exit ram_disk_driver_exit(void)
{
    printk(NAME " ram disk with request exit\n");

    del_gendisk(ram_disk_gendisk);
    put_disk(ram_disk_gendisk);
    blk_cleanup_queue(ram_disk.request_queue);
    unregister_blkdev(ram_disk.major, NAME);
    kfree(ram_disk.ram_address);
}

module_init(ram_disk_driver_init);
module_exit(ram_disk_driver_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("ram disk with request");
MODULE_LICENSE("GPL");