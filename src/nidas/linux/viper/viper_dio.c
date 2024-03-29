/* -*- mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8; -*- */
/* vim: set shiftwidth=8 softtabstop=8 expandtab: */
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2007, Copyright University Corporation for Atmospheric Research
 **
 ** This program is free software; you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation; either version 2 of the License, or
 ** (at your option) any later version.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** The LICENSE.txt file accompanying this software contains
 ** a copy of the GNU General Public License. If it is not found,
 ** write to the Free Software Foundation, Inc.,
 ** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 **
 ********************************************************************
*/
/*

  Driver for Viper digital IO ports. There are 8 independent inputs, IN0-7,
  and 8 independent outputs OUT0-7.  The value of the inputs can be read,
  and the value of the outputs written or read.

Original author:	Gordon Maclean

*/

#include "viper_dio.h"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/uaccess.h>        /* access_ok */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
#include <mach/hardware.h>
#include <linux/gpio.h>
#include <mach/pxa2xx-regs.h>
#else
#include <asm/arch-pxa/hardware.h>
#include <asm/arch-pxa/pxa-regs.h>
#endif

#include <nidas/linux/ver_macros.h>
#include <nidas/linux/Revision.h>    // REPO_REVISION

// #define DEBUG
#include <nidas/linux/klog.h>
#include <nidas/linux/isa_bus.h>

#ifndef REPO_REVISION
#define REPO_REVISION "unknown"
#endif

MODULE_AUTHOR("Gordon Maclean <maclean@ucar.edu>");
MODULE_LICENSE("GPL");

MODULE_DESCRIPTION("Driver for VIPER DIO pins");
MODULE_VERSION(REPO_REVISION);


/*
 * Device structure.
 */
static struct VIPER_DIO viper_dio;

static int _ngpio;

static void set_douts(unsigned char bits)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
        GPSR(20) = (u32)bits << 20;
#else
        int i;
        unsigned char m = 1;
        for (i=0; i < 8; i++,m<<=1) {
                if (bits & m) gpio_set_value(VIPER_PL9_OUT0 + i,1);
        }
#endif
}

static void clear_douts(unsigned char bits)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
        GPCR(20) = (u32)bits << 20;
#else
        int i;
        unsigned char m = 1;
        for (i=0; i < 8; i++,m<<=1) {
                if (bits & m) gpio_set_value(VIPER_PL9_OUT0 + i,0);
        }
#endif
}

static void set_douts_val(unsigned char bits,unsigned char value)
{
        unsigned char vbits = bits & value;
        set_douts(vbits);
        vbits = bits & ~value;
        clear_douts(vbits);
}

static unsigned char get_douts(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
        return GPLR(20) >> 20;
#else
        int i;
        unsigned char m = 0;
        for (i=0; i < 8; i++,m<<=1) {
                m += gpio_get_value(VIPER_PL9_OUT0 + i) << i;
        }
        return m;
#endif
}

static unsigned char get_dins(void)
{
        /*
         * This memory access to VIPER_GPIO currently fails. I believe the addressing
         * is correct, and that the solution is to update the viper_io_desc structure
         * in the kernel. See arch/arm/mach-pxa/viper.c:
               {
                       .virtual = VIPER_CPLD_BASE,
                       .pfn     = __phys_to_pfn(VIPER_CPLD_PHYS),
                       .length  = 0x00300000,
                       .type    = MT_DEVICE,
               },
         * Change .length to (at least) 0x00500000 since the VIPER_GPIO register is
         * at 0x14500000,0x14500001. 
         * It is interesting to note that the COM5 and COM4 registers are at
         * 0x14300000-0x1430001F, which appear to be just beyond the length value above.
         */
        return VIPER_GPIO & 0xff;
}

/************ File Operations ****************/

/* More than one thread can open this device.  */
static int viper_dio_open(struct inode *inode, struct file *filp)
{
        int result = 0;

        /* Inform kernel that this device is not seekable */
        nonseekable_open(inode,filp);

        return result;
}

static int viper_dio_release(struct inode *inode, struct file *filp)
{
        int result = 0;
        return result;
}

static long viper_dio_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
        int result = -EINVAL,err = 0;

         /* don't even decode wrong cmds: better returning
          * ENOTTY than EFAULT */
        if (_IOC_TYPE(cmd) != VIPER_DIO_IOC_MAGIC) return -ENOTTY;
        if (_IOC_NR(cmd) > VIPER_DIO_IOC_MAXNR) return -ENOTTY;

        /*
         * the type is a bitmask, and VERIFY_WRITE catches R/W
         * transfers. Note that the type is user-oriented, while
         * verify_area is kernel-oriented, so the concept of "read" and
         * "write" is reversed
         */
        if (_IOC_DIR(cmd) & _IOC_READ)
                err = !portable_access_ok(VERIFY_WRITE, (void __user *)arg,
                    _IOC_SIZE(cmd));
        else if (_IOC_DIR(cmd) & _IOC_WRITE)
                err =  !portable_access_ok(VERIFY_READ, (void __user *)arg,
                    _IOC_SIZE(cmd));
        if (err) return -EFAULT;

        switch (cmd)
        {

        case VIPER_DIO_GET_NOUT:
        case VIPER_DIO_GET_NIN:
                result = VIPER_DIO_NOUT;
                break;
        case VIPER_DIO_CLEAR:	/* user set */
                {
                unsigned char bits;
                if (copy_from_user(&bits,(void __user *)arg,
                    sizeof(bits))) return -EFAULT;
                if ((result = mutex_lock_interruptible(&viper_dio.reg_mutex))) return result;
                clear_douts(bits);
                mutex_unlock(&viper_dio.reg_mutex);
                result = 0;
                }
                break;
        case VIPER_DIO_SET:	/* user set */
                {
                unsigned char bits;
                if (copy_from_user(&bits,(void __user *)arg,
                    sizeof(bits))) return -EFAULT;
                if ((result = mutex_lock_interruptible(&viper_dio.reg_mutex))) return result;
                set_douts(bits);
                mutex_unlock(&viper_dio.reg_mutex);
                result = 0;
                break;
                }
        case VIPER_DIO_SET_TO_VAL:      /* user set */
            {
                unsigned char bits[2];
                if (copy_from_user(bits,(void __user *)arg,
                        sizeof(bits))) return -EFAULT;
                if ((result = mutex_lock_interruptible(&viper_dio.reg_mutex))) return result;
                set_douts_val(bits[0],bits[1]);
                mutex_unlock(&viper_dio.reg_mutex);
                result = 0;
            }
	    break;
        case VIPER_DIO_GET_DOUT:      /* user get */
            {
                unsigned char bits;
                if ((result = mutex_lock_interruptible(&viper_dio.reg_mutex))) return result;
                bits = get_douts();
                mutex_unlock(&viper_dio.reg_mutex);
                if (copy_to_user((void __user *)arg,&bits,
                        sizeof(bits))) return -EFAULT;
                result = 0;
            }
	    break;
        case VIPER_DIO_GET_DIN:      /* user get */
            {
                unsigned char bits;
                if ((result = mutex_lock_interruptible(&viper_dio.reg_mutex))) return result;
                bits = get_dins();
                mutex_unlock(&viper_dio.reg_mutex);
                if (copy_to_user((void __user *)arg,&bits,
                        sizeof(bits))) return -EFAULT;
                result = 0;
            }
	    break;
        default:
                result = -ENOTTY;
                break;
        }
        return result;
}

static struct file_operations viper_dio_fops = {
        .owner   = THIS_MODULE,
        .open    = viper_dio_open,
        .unlocked_ioctl   = viper_dio_ioctl,
        .release = viper_dio_release,
        .llseek  = no_llseek,
};

/*-----------------------Module ------------------------------*/

/* Don't add __exit macro to the declaration of this cleanup function
 * since it is also called at init time, if init fails. */
static void viper_dio_cleanup(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
        int i;
        for (i = 0; i < _ngpio; i++)
                gpio_free(VIPER_PL9_OUT0 + i);
#endif

        if (viper_dio.vclass && !IS_ERR(viper_dio.vclass)) {
                if (viper_dio.device && !IS_ERR(viper_dio.device))
                        device_destroy(viper_dio.vclass, viper_dio.cdev.dev);
                class_destroy(viper_dio.vclass);
        }

        if (MAJOR(viper_dio.cdev.dev) != 0) {
                cdev_del(&viper_dio.cdev);
                viper_dio.cdev.dev =  MKDEV(0,0);
                unregister_chrdev_region(viper_dio.cdev.dev,1);
        }

        viper_dio.vclass = 0;

        KLOG_DEBUG("complete\n");
}

static int __init viper_dio_init(void)
{
        int result = -EINVAL;
        int i;
        dev_t devno = MKDEV(0,0);

        KLOG_NOTICE("version: %s\n",REPO_REVISION);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
        for (i = 0; i < 8; i++) {
                result = gpio_request(VIPER_PL9_OUT0 + i, "GPIO");
                if (result) {
                        KLOG_ERR("gpio_request failed for GPIO %d\n",VIPER_PL9_OUT0 + i);
                        goto err;
                }
                _ngpio = i + 1;
                result = gpio_direction_output(VIPER_PL9_OUT0, 0);
                if (result) {
                        KLOG_ERR("gpio_direction_output failed for GPIO %d\n",VIPER_PL9_OUT0 + i);
                        goto err;
                }
        }
#endif

        result = alloc_chrdev_region(&devno,0,1,"viper_dio");
        if (result < 0) goto err;

        mutex_init(&viper_dio.reg_mutex);

        // for informational messages only at this point
        sprintf(viper_dio.deviceName,"/dev/viper_dio%d",0);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,0,0)
        viper_dio.vclass = class_create(THIS_MODULE, "viper_dio");
#else
        viper_dio.vclass = class_create("viper_dio");
#endif
        if (IS_ERR(viper_dio.vclass)) {
                result = PTR_ERR(viper_dio.vclass);
                goto err;
        }

        cdev_init(&viper_dio.cdev,&viper_dio_fops);
        viper_dio.cdev.owner = THIS_MODULE;

        /* After calling cdev_all the device is "live"
         * and ready for user operation.
         */
        result = cdev_add(&viper_dio.cdev, devno, 1);
        if (result) goto err;

        viper_dio.device = device_create_x(viper_dio.vclass, NULL,
                        devno, "viper_dio%d", 0);
        if (IS_ERR(viper_dio.device)) {
                result = PTR_ERR(viper_dio.device);
                goto err;
        }

        KLOG_DEBUG("complete.\n");

        return 0;
err:
        viper_dio_cleanup();
        return result;
}

module_init(viper_dio_init);
module_exit(viper_dio_cleanup);

