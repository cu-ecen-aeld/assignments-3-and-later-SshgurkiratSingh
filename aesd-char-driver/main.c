/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>    // file_operations
#include <linux/slab.h>  // For kmalloc, kfree
#include <linux/mutex.h> // For mutex
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Gurkirat Singh"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

static int aesd_major = AESD_MAJOR;
static int aesd_minor = AESD_MINOR;
static struct aesd_dev aesd_device;

static int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev; // Set private data

    return 0;
}

static int aesd_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset;
    ssize_t retval = 0;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circular_buffer, *f_pos, &entry_offset);
    if (entry)
    {
        size_t to_copy = min(count, entry->size - entry_offset);
        if (copy_to_user(buf, entry->buffptr + entry_offset, to_copy))
        {
            retval = -EFAULT;
            goto out;
        }
        retval = to_copy;
        *f_pos += to_copy;
    }

out:
    mutex_unlock(&dev->lock);
    return retval;
}

static ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = 0;
    const char *newline_ptr;
    struct aesd_buffer_entry new_entry;
    char *new_write;
    size_t to_copy;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    if (dev->write_entry.size == 0)
    {
        dev->write_entry.buffptr = kmalloc(count, GFP_KERNEL);
        if (dev->write_entry.buffptr == NULL)
        {
            retval = -ENOMEM;
            goto out;
        }
        dev->write_entry.size = 0;
    }
    else
    {
        char *temp = krealloc(dev->write_entry.buffptr, dev->write_entry.size + count, GFP_KERNEL);
        if (temp == NULL)
        {
            retval = -ENOMEM;
            goto out;
        }
        dev->write_entry.buffptr = temp;
    }

    if (copy_from_user((void *)(dev->write_entry.buffptr + dev->write_entry.size), buf, count))
    {
        retval = -EFAULT;
        goto out;
    }

    dev->write_entry.size += count;

    newline_ptr = memchr(dev->write_entry.buffptr, '\n', dev->write_entry.size);
    if (newline_ptr != NULL)
    {
        size_t new_entry_size = newline_ptr - dev->write_entry.buffptr + 1;
        new_write = kmalloc(new_entry_size, GFP_KERNEL);
        if (new_write == NULL)
        {
            retval = -ENOMEM;
            goto out;
        }

        memcpy(new_write, dev->write_entry.buffptr, new_entry_size);

        new_entry.buffptr = new_write;
        new_entry.size = new_entry_size;

        if (dev->circular_buffer.full)
        {
            kfree(dev->circular_buffer.entry[dev->circular_buffer.out_offs].buffptr);
        }
        aesd_circular_buffer_add_entry(&dev->circular_buffer, &new_entry);

        if (new_entry_size < dev->write_entry.size)
        {
            memmove(dev->write_entry.buffptr, dev->write_entry.buffptr + new_entry_size, dev->write_entry.size - new_entry_size);
            dev->write_entry.size -= new_entry_size;
        }
        else
        {
            kfree(dev->write_entry.buffptr);
            dev->write_entry.buffptr = NULL;
            dev->write_entry.size = 0;
        }
    }

    retval = count;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

static struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

static int __init aesd_init_module(void)
{
    dev_t dev = 0;
    int result;

    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }

    memset(&aesd_device, 0, sizeof(struct aesd_dev));
    aesd_circular_buffer_init(&aesd_device.circular_buffer);
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);
    if (result)
    {
        unregister_chrdev_region(dev, 1);
    }

    return result;
}

static void __exit aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    uint8_t index;
    struct aesd_buffer_entry *entry;

    cdev_del(&aesd_device.cdev);
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer, index)
    {
        kfree(entry->buffptr);
    }
    kfree(aesd_device.write_entry.buffptr);
    unregister_chrdev_region(devno, 1);
    mutex_destroy(&aesd_device.lock);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);