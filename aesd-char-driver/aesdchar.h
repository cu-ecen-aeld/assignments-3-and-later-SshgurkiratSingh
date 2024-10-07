#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#define AESD_DEBUG 1  // Remove comment on this line to enable debug

#undef PDEBUG             /* undef it, just in case */
#ifdef AESD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk(KERN_DEBUG "aesdchar: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#include "aesd-circular-buffer.h"


struct aesd_dev {
    struct cdev cdev;     /* Char device structure      */
    struct aesd_circular_buffer circular_buffer;
    struct aesd_buffer_entry write_entry;
    struct mutex lock;    /* Mutex for thread safety */
};

#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
