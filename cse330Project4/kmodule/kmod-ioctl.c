#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/dcache.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/kthread.h>
#include <linux/limits.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>
#include <linux/cdev.h>
#include <linux/nospec.h>

#include "../ioctl-defines.h"

/* Device-related definitions */
static dev_t            dev = 0;
static struct class*    kmod_class;
static struct cdev      kmod_cdev;
static struct block_device *g_bdevice;


/* Buffers for different operation requests */
struct block_rw_ops         rw_request;
struct block_rwoffset_ops   rwoffset_request;

static int perform_block_operation(int op, char *user_buffer, int size, char *kernel_buffer, int offset) {
    int ret;
    int current_offset;
    int current_size;
    int copy_size;
    int count = 0;
    struct bio *bdevice_bio;
    printk("op: %d, size: %d, offset: %d\n", op, size, offset);
    /* Set bio parameters */
    
    bdevice_bio = bio_alloc(g_bdevice, 1, op, GFP_NOIO);
    if (!bdevice_bio) {
        printk(KERN_ERR "Failed to allocate bio\n");
        return -EIO;
    }
    bio_set_dev(bdevice_bio, g_bdevice);
    
    current_size = size;
    current_offset = offset;
    while(current_size)
    {
	copy_size = min(current_size, 512);
	bdevice_bio->bi_iter.bi_sector = (current_offset/512);
    	bdevice_bio->bi_opf = op;
    	bio_add_page(bdevice_bio, vmalloc_to_page(kernel_buffer+current_offset), copy_size, (count*512)); //investigate, count offset from 0
    	//printk("copy size: %d, current offset: %d, sector: %d\n",copy_size, current_offset, bdevice_bio->bi_iter.bi_sector);
    	submit_bio_wait(bdevice_bio);
    	if(op == REQ_OP_READ) {
    		copy_to_user(user_buffer+current_offset, kernel_buffer+current_offset, copy_size);
    
    	}
    	bio_reset(bdevice_bio, g_bdevice, op);  
    	
    	count = (count+1)%8;
    	//kfree(kernel_buffer);
    	current_size -= copy_size;
    	current_offset += copy_size;
    }
    
    
    
    
    
    
   
    
    bio_put(bdevice_bio);
#if 0
    /* Add pages to bio */
    while (size > 0) {
        int bytes = min(size, PAGE_SIZE);

        if (kernel_buffer) {
            page = vmalloc_to_page(kernel_buffer); 
        } else {
            page = virt_to_page(user_buffer); 
        }
        
        if (!bio_add_page(bdevice_bio, page, bytes, offset_in_page(user_buffer))) {
            printk(KERN_ERR "Failed to add page to bio\n");
            return -EFAULT;
        }

        size -= bytes;
        user_buffer += bytes;
        if (kernel_buffer) kernel_buffer += bytes;
    }

    /* Submit the bio and wait for completion */
    ret = submit_bio_wait(bdevice_bio);
    if (ret < 0) {
        printk(KERN_ERR "Failed to submit bio: %d\n", ret);
        return ret;
    }
	#endif
    return ret;
}

static long kmod_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
    void __user *argp = (void __user *)arg;
    int ret = 0;

    switch (cmd) {
        case BREAD:
        case BWRITE: {
            /* Get request from user */
            if (copy_from_user(&rw_request, argp, sizeof(rw_request))) {
                return -EFAULT;
            }

            /* Allocate a kernel buffer to read/write user data */
            char *kernel_buffer = kmalloc(max(rw_request.size,PAGE_SIZE), GFP_KERNEL);
            if (!kernel_buffer) {
                return -ENOMEM;
            }

            /* Perform the block operation */
            if (cmd == BREAD) {
                ret = perform_block_operation(REQ_OP_READ, rw_request.data, rw_request.size, kernel_buffer, 0);
            } else {
                if (copy_from_user(kernel_buffer, rw_request.data, rw_request.size)) {
                    kfree(kernel_buffer);
                    return -EFAULT;
                }
                //printk("kernel buffer: %s \n",kernel_buffer);
                ret = perform_block_operation(REQ_OP_WRITE, rw_request.data, rw_request.size, kernel_buffer, 0);
            }

            kfree(kernel_buffer);
            break;
        }
        case BREADOFFSET:
        case BWRITEOFFSET: {
            /* Get request from user */
            if (copy_from_user(&rwoffset_request, argp, sizeof(rwoffset_request))) {
                return -EFAULT;
            }

            /* Allocate a kernel buffer to read/write user data */
            char *kernel_buffer = kmalloc(max(rwoffset_request.size, PAGE_SIZE), GFP_KERNEL);
            if (!kernel_buffer) {
                return -ENOMEM;
            }

            /* Perform the block operation */
            if (cmd == BREADOFFSET) {
                ret = perform_block_operation(REQ_OP_READ, rwoffset_request.data, rwoffset_request.size, kernel_buffer, rwoffset_request.offset);
            } else {
                if (copy_from_user(kernel_buffer, rwoffset_request.data, rwoffset_request.size)) {
                    kfree(kernel_buffer);
                    return -EFAULT;
                }
                ret = perform_block_operation(REQ_OP_WRITE, rwoffset_request.data, rwoffset_request.size, kernel_buffer, rwoffset_request.offset);
            }

            kfree(kernel_buffer);
            break;
        }
        default:
            printk("Error: incorrect operation requested, returning.\n");
            return -ENOTTY;
    }

    return ret;
}
 



static int kmod_open(struct inode* inode, struct file* file) {
    printk("Opened kmod. \n");
    return 0;
}

static int kmod_release(struct inode* inode, struct file* file) {
    printk("Closed kmod. \n");
    return 0;
}

static struct file_operations fops = 
{
    .owner          = THIS_MODULE,
    .open           = kmod_open,
    .release        = kmod_release,
    .unlocked_ioctl = kmod_ioctl,
};

/* Initialize the module for IOCTL commands */
bool kmod_ioctl_init(struct block_device *bdevice) {
	g_bdevice = bdevice;
    /* Allocate a character device. */
    if (alloc_chrdev_region(&dev, 0, 1, "usbaccess") < 0) {
        printk("error: couldn't allocate \'usbaccess\' character device.\n");
        return false;
    }

    /* Initialize the chardev with my fops. */
    cdev_init(&kmod_cdev, &fops);
    if (cdev_add(&kmod_cdev, dev, 1) < 0) {
        printk("error: couldn't add kmod_cdev.\n");
        goto cdevfailed;
    }

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6,2,16)
    if ((kmod_class = class_create(THIS_MODULE, "kmod_class")) == NULL) {
        printk("error: couldn't create kmod_class.\n");
        goto cdevfailed;
    }
#else
    if ((kmod_class = class_create("kmod_class")) == NULL) {
        printk("error: couldn't create kmod_class.\n");
        goto cdevfailed;
    }
#endif

    if ((device_create(kmod_class, NULL, dev, NULL, "kmod")) == NULL) {
        printk("error: couldn't create device.\n");
        goto classfailed;
    }

    printk("[*] IOCTL device initialization complete.\n");
    return true;

classfailed:
    class_destroy(kmod_class);
cdevfailed:
    unregister_chrdev_region(dev, 1);
    return false;
}

void kmod_ioctl_teardown(void) {
    /* Destroy the classes too (IOCTL-specific). */
    if (kmod_class) {
        device_destroy(kmod_class, dev);
        class_destroy(kmod_class);
    }
    cdev_del(&kmod_cdev);
    unregister_chrdev_region(dev,1);
    printk("[*] IOCTL device teardown complete.\n");
}
