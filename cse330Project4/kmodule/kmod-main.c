/* General headers */
#include <linux/kthread.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/skbuff.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>

/* File IO-related headers */
#include <linux/fs.h>
#include <linux/bio.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/version.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adil Ahmad");
MODULE_DESCRIPTION("A Block Abstraction Read/Write for a USB device.");
MODULE_VERSION("1.0");

/* USB storage device name (to be received as module argument) */
char* device = "";
module_param(device, charp, S_IRUGO);

/* USB storage disk-related data structures */
static struct block_device*     bdevice = NULL;
static struct bio*              bdevice_bio;

bool kmod_ioctl_init(void);
void kmod_ioctl_teardown(void);

static bool open_usb_disk(void) {
    /* Open the USB storage disk)*/
     bdevice = blkdev_get_by_path(device, FMODE_READ | FMODE_WRITE, NULL);
    if (IS_ERR(bdevice)) {
        printk(KERN_ERR "Failed to open device %s\n", device);
        return false;
    }
    bdevice_bio = bio_alloc(bdevice, 1, REQ_OP_READ, GFP_NOIO);
    if (!bdevice_bio) {
        printk(KERN_ERR "Failed to allocate bio\n");
        blkdev_put(bdevice, FMODE_READ | FMODE_WRITE);
        return false;
    }
    return true;
}

static void close_usb_disk(void) {
    /* Close the USB storage disk (Hint: use blkdev_put(..);)*/
    if (bdevice_bio) {
        bio_put(bdevice_bio);
    }
    if (bdevice) {
        blkdev_put(bdevice, FMODE_READ | FMODE_WRITE);
    }
}

static int __init kmod_init(void) {
    printk("Hello World!\n");
    open_usb_disk();
    kmod_ioctl_init();
    return 0;
}

static void __exit kmod_fini(void) {
    close_usb_disk();
    kmod_ioctl_teardown();
    printk("Goodbye, World!\n");
}

module_init(kmod_init);
module_exit(kmod_fini);
