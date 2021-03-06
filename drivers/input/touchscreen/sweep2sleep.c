/*
 * Sweep2Wake driver for OnePlus One Bacon with multiple gestures support
 * 
 * Author: andip71, 24.02.2016
 * 
 * Version 1.1.0
 *
 * Credits for initial implementation to Dennis Rassmann <showp1984@gmail.com>
 * 
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/lcd_notify.h>
#include <linux/hrtimer.h>
#include <linux/jiffies.h>
#include <linux/boeffla_powerkey_helper.h>


/*****************************************/
/* Module/driver data */
/*****************************************/

#define DRIVER_AUTHOR "andip71 (Lord Boeffla)"
#define DRIVER_DESCRIPTION "Sweep2sleep for OnePlus One bacon"
#define DRIVER_VERSION "1.1.0"
#define LOGTAG "Boeffla s2s: "

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPLv2");


/*****************************************/
/* general defaults */
/*****************************************/

#define STATIC_BANKS			4
#define DYNAMIC_BANKS			1
#define BANKS_MAX				4

#define S2S_Y_BUTTONLIMIT     	1900

#define STATUSBAR_Y_MIN			0
#define STATUSBAR_Y_MAX			80
#define STATUSBAR_DT_TIME_MS	800

#define BIT_STATIC_GESTURE_1	0x0001
#define BIT_STATIC_GESTURE_2	0x0002
#define BIT_STATIC_GESTURE_3	0x0004
#define BIT_STATIC_GESTURE_4	0x0008
#define BIT_DYNAMIC_GESTURE_1	0x0010
#define BIT_DYNAMIC_GESTURE_2	0x0020
#define BIT_DYNAMIC_GESTURE_3	0x0040
#define BIT_DYNAMIC_GESTURE_4	0x0080
#define BIT_STATUSBAR_DTSLEEP	0x0100

#define IMPLEMENTED_FUNCTIONS	BIT_DYNAMIC_GESTURE_1 | BIT_STATIC_GESTURE_3 | BIT_STATIC_GESTURE_4
								
#define INPUT_DEV_NAME			"synaptics"


// Gestures - static definitions
//
// Bank1 = right->left on softkeys, Bank2 = left->right on softkeys
// Bank3 = right->left on launcher dock, Bank4 = left->right on launcher dock
//
//									Bank	1	  2
static int STATIC_Y_MIN1[STATIC_BANKS] = {1900, 1900, 1700, 1700};
static int STATIC_Y_MAX1[STATIC_BANKS] = {2400, 2400, 1920, 1920};
static int STATIC_X_MAX1[STATIC_BANKS] = {1280, 300,  1280, 300 };
static int STATIC_X_MIN1[STATIC_BANKS] = {700,  0,     700, 0   };

static int STATIC_Y_MIN2[STATIC_BANKS] = {1900, 1900, 1700, 1700};
static int STATIC_Y_MAX2[STATIC_BANKS] = {2400, 2400, 1920, 1920};
static int STATIC_X_MAX2[STATIC_BANKS] = {650,  650,  650,  650 };
static int STATIC_X_MIN2[STATIC_BANKS] = {350,  350,  350,  350 };

static int STATIC_Y_MIN3[STATIC_BANKS] = {1900, 1900, 1700, 1700};
static int STATIC_Y_MAX3[STATIC_BANKS] = {2400, 2400, 1920, 1920};
static int STATIC_X_MAX3[STATIC_BANKS] = {300,  1280, 300,  1280};
static int STATIC_X_MIN3[STATIC_BANKS] = {0,    700,  0,    700 };

// Gestures - dynamic definitions
//
// Note: Sizes must ALWAYS be positive values !!!
//
// Bank1 = right top, left down (diagonal 30 degrees)
//
//									Bank	1	
static int DYNAMIC_Y_MIN1[DYNAMIC_BANKS] = {800};
static int DYNAMIC_Y_MAX1[DYNAMIC_BANKS] = {1600};
static int DYNAMIC_X_MAX1[DYNAMIC_BANKS] = {1280};
static int DYNAMIC_X_MIN1[DYNAMIC_BANKS] = {800};

static int DYNAMIC_Y_OFFSET2[DYNAMIC_BANKS] = {100};
static int DYNAMIC_Y_SIZE2[DYNAMIC_BANKS] = {200};
static int DYNAMIC_X_OFFSET2[DYNAMIC_BANKS] = {-300};
static int DYNAMIC_X_SIZE2[DYNAMIC_BANKS] = {200};

static int DYNAMIC_Y_OFFSET3[DYNAMIC_BANKS] = {100};
static int DYNAMIC_Y_SIZE3[DYNAMIC_BANKS] = {200};
static int DYNAMIC_X_OFFSET3[DYNAMIC_BANKS] = {-300};
static int DYNAMIC_X_SIZE3[DYNAMIC_BANKS] = {200};


/*****************************************/
/* Variables, structures and pointers */
/*****************************************/

int s2s = 0;
static int debug = 0;
static int touch_x = 0;
static int touch_y = 0;
static int statusBarWithinTime = 0;

static bool touch_x_called = false;
static bool touch_y_called = false;
static bool scr_suspended = false;
static bool exec_count = true;
static bool scr_on_touch = false;

static bool static_bank_active[BANKS_MAX] = {false, false, false, false};
static bool static_barrier1[STATIC_BANKS] = {false, false};
static bool static_barrier2[STATIC_BANKS] = {false, false};

static bool dynamic_bank_active[BANKS_MAX] = {false, false, false, false};
static bool dynamic_barrier1[DYNAMIC_BANKS] = {false};
static bool dynamic_barrier2[DYNAMIC_BANKS] = {false};

static int dynamic_next_x_min = 0;
static int dynamic_next_x_max = 0;
static int dynamic_next_y_min = 0;
static int dynamic_next_y_max = 0;

static struct notifier_block s2s_lcd_notif;
static struct workqueue_struct *s2s_input_wq;
static struct work_struct s2s_input_work;
static struct delayed_work statusBarTimer;


/*****************************************/
// Internal functions
/*****************************************/


// status bar timer work
static void statusBarTimer_work(struct work_struct *work)
{
	// reset timer active flag
	statusBarWithinTime = 0;
}


/* Reset sweep2sleep */
static void sweep2sleep_reset(void) 
{
	int i;

	exec_count = true;
	scr_on_touch = false;
	
	for (i = 0; i < STATIC_BANKS; i++)
	{
		static_barrier1[i] = false;
		static_barrier2[i] = false;
	}

	for (i = 0; i < DYNAMIC_BANKS; i++)
	{
		dynamic_barrier1[i] = false;
		dynamic_barrier2[i] = false;
	}

}


/* Handling for double tap on status bar */
static void doubleTapStatusBar(int x, int y)
{
	if (debug)
		pr_info(LOGTAG"doubleTapStatusBar x: %d, y: %d, timer flag: %d\n", x, y, statusBarWithinTime);
	
	// if we are not on status bar or screen is off, exit
	if ((y < STATUSBAR_Y_MIN) || (y > STATUSBAR_Y_MAX) || (scr_suspended))
		return;
	
	// if last tap was still within the active time limit, switch off
	// screen and reset the flag; otherwise restart timer
	if (statusBarWithinTime)
	{
		boeffla_press_powerkey();
		statusBarWithinTime = 0;
	}
	else
	{
		cancel_delayed_work_sync(&statusBarTimer);
		schedule_delayed_work(&statusBarTimer, msecs_to_jiffies(STATUSBAR_DT_TIME_MS));
		statusBarWithinTime = 1;
	}
}


/* sweep2sleep main function */
static void detect_sweep2sleep(int x, int y)
{
	int i;

	if (debug)
		pr_info(LOGTAG"x: %d, y: %d\n", x, y);

	if ((scr_suspended == false) && (s2s != 0)) 
	{
		scr_on_touch=true;
	
		// Static gestures
		for (i = 0; i < STATIC_BANKS; i++)
		{
			if (static_bank_active[i])
				if ((static_barrier1[i] == true) ||
				   ((x < STATIC_X_MAX1[i]) && (x > STATIC_X_MIN1[i]) && 
					(y < STATIC_Y_MAX1[i]) && (y > STATIC_Y_MIN1[i])))
				{
					static_barrier1[i] = true;
				
					if ((static_barrier2[i] == true) ||
					   ((x < STATIC_X_MAX2[i]) && (x > STATIC_X_MIN2[i]) && 
						(y < STATIC_Y_MAX2[i]) && (y > STATIC_Y_MIN2[i]))) 
					{
						static_barrier2[i] = true;
					
						if ((x < STATIC_X_MAX3[i]) && (x > STATIC_X_MIN3[i]) && 
							(y < STATIC_Y_MAX3[i]) && (y > STATIC_Y_MIN3[i])) 
						{
							if (exec_count) 
							{
								pr_info(LOGTAG"Sweep2sleep static activated\n");
								boeffla_press_powerkey();
								exec_count = false;
								sweep2sleep_reset();
							}
						}
					}
				}
		} // end of static banks
		
		// Dynamic gestures
		for (i = 0; i < DYNAMIC_BANKS; i++)
		{
			if (dynamic_bank_active[i])
			{
				if ((dynamic_barrier1[i] == true) ||
				   ((x < DYNAMIC_X_MAX1[i]) && (x > DYNAMIC_X_MIN1[i]) && 
					(y < DYNAMIC_Y_MAX1[i]) && (y > DYNAMIC_Y_MIN1[i])))
				{
					if (dynamic_barrier1[i] == false)
					{
						dynamic_barrier1[i] = true;
						dynamic_next_x_min = x + DYNAMIC_X_OFFSET2[i];
						dynamic_next_x_max = dynamic_next_x_min + DYNAMIC_X_SIZE2[i];
						dynamic_next_y_min = y + DYNAMIC_Y_OFFSET2[i];
						dynamic_next_y_max = dynamic_next_y_min + DYNAMIC_Y_SIZE2[i];

						if (debug)
							pr_info(LOGTAG"new target 1: x %d-%d y %d-%d\n",
							dynamic_next_x_min, dynamic_next_x_max, 
							dynamic_next_y_min, dynamic_next_y_max);
					}
					
					if ((dynamic_barrier2[i] == true) ||
					   ((x < dynamic_next_x_max) && (x > dynamic_next_x_min) && 
						(y < dynamic_next_y_max) && (y > dynamic_next_y_min)))
					{
						if (dynamic_barrier2[i] == false)
						{
							dynamic_barrier2[i] = true;
							dynamic_next_x_min = x + DYNAMIC_X_OFFSET3[i];
							dynamic_next_x_max = dynamic_next_x_min + DYNAMIC_X_SIZE3[i];
							dynamic_next_y_min = y + DYNAMIC_Y_OFFSET3[i];
							dynamic_next_y_max = dynamic_next_y_min + DYNAMIC_Y_SIZE3[i];

							if (debug)
								pr_info(LOGTAG"new target 2: x %d-%d y %d-%d\n",
								dynamic_next_x_min, dynamic_next_x_max, 
								dynamic_next_y_min, dynamic_next_y_max);
						}
						
						if ((x < dynamic_next_x_max) && (x > dynamic_next_x_min) && 
							(y < dynamic_next_y_max) && (y > dynamic_next_y_min)) 
	 
						{
							if (exec_count) 
							{
								pr_info(LOGTAG"Sweep2sleep dynamic activated\n");
								boeffla_press_powerkey();
								exec_count = false;
								sweep2sleep_reset();
							}
						}
					}
				}
			}
		} // end of dynamic banks
	}
}


/* input callback function */
static void s2s_input_callback(struct work_struct *unused) 
{
	if (s2s)
		detect_sweep2sleep(touch_x, touch_y);

	return;
}


/* input event dispatcher */
static void s2s_input_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value)
{
	if (!s2s)
		return;

	if (code == ABS_MT_SLOT) 
	{
		sweep2sleep_reset();
		if (debug)
			pr_info(LOGTAG"sweep ABS_MT_SLOT\n");
		
		return;
	}

	if (code == ABS_MT_TRACKING_ID && value == -1) 
	{
		if (debug)
			pr_info(LOGTAG"sweep ABS_MT_TRACKING_ID\n");
		
		// double tap to status bar to sleep
		if (s2s & BIT_STATUSBAR_DTSLEEP)
			doubleTapStatusBar(touch_x, touch_y);
		
		// only reset due to finger taken off when not on soft keys
		// (on soft keys it is normal as it interrupts the touch screen area)
		if (touch_y < S2S_Y_BUTTONLIMIT)
		{
			sweep2sleep_reset();
			if (debug)
				pr_info(LOGTAG"sweep reset\n");
		}
		return;
	}

	if (code == ABS_MT_POSITION_X)
	{
		touch_x = value;
		touch_x_called = true;
	}

	if (code == ABS_MT_POSITION_Y) 
	{
		touch_y = value;
		touch_y_called = true;
	}

	if (touch_x_called && touch_y_called) 
	{
		touch_x_called = false;
		touch_y_called = false;
		queue_work_on(0, s2s_input_wq, &s2s_input_work);
	}
}

/* input filter function */
static int input_dev_filter(struct input_dev *dev) 
{
	if (strstr(dev->name, "touch") || strstr(dev->name, INPUT_DEV_NAME)) 
		return 0;

	return 1;
}


/* connect to input stream */
static int s2s_input_connect(struct input_handler *handler,
				struct input_dev *dev, const struct input_device_id *id) 
{
	struct input_handle *handle;
	int error;

	if (input_dev_filter(dev))
		return -ENODEV;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "s2s";

	error = input_register_handle(handle);
	if (error)
		goto err1;

	error = input_open_device(handle);
	if (error)
		goto err2;

	return 0;

err2:
	input_unregister_handle(handle);
err1:
	kfree(handle);
	return error;
}


/* disconnect from input stream */
static void s2s_input_disconnect(struct input_handle *handle) 
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}


static const struct input_device_id s2s_ids[] = 
{
	{ .driver_info = 1 },
	{ },
};


static struct input_handler s2s_input_handler = 
{
	.event		= s2s_input_event,
	.connect	= s2s_input_connect,
	.disconnect	= s2s_input_disconnect,
	.name		= "s2s_inputreq",
	.id_table	= s2s_ids,
};


/* callback function for lcd notifier */
static int lcd_notifier_callback(struct notifier_block *this,
								unsigned long event, void *data)
{
	switch (event) 
	{
		case LCD_EVENT_ON_END:
			scr_suspended = false;
			break;
			
		case LCD_EVENT_OFF_END:
			scr_suspended = true;
			break;
			
		default:
			break;
	}

	return 0;
}


/*****************************************/
// Sysfs definitions 
/*****************************************/

static ssize_t sweep2sleep_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", s2s);
}


static ssize_t sweep2sleep_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val, i;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	// validate against implemented functions
	val &= IMPLEMENTED_FUNCTIONS;
		
	// store if valid data
	if (((val >= 0x00) && (val <= 0xFFFF)))
	{
		s2s = val;

		// bits 0...3 are for static gestures, 4...7 for dynamic gestures
		for (i=0; i < STATIC_BANKS; i++)
			static_bank_active[i] = ((s2s >> i) & 0x01);

		for (i=0; i < DYNAMIC_BANKS; i++)
			dynamic_bank_active[i] = ((s2s >> (i+4)) & 0x01);
	}

	return count;
}

static DEVICE_ATTR(sweep2sleep, (S_IWUSR|S_IRUGO),
	sweep2sleep_show, sweep2sleep_store);


static ssize_t sweep2sleep_implemented_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", IMPLEMENTED_FUNCTIONS);
}

static DEVICE_ATTR(sweep2sleep_implemented, (S_IWUSR|S_IRUGO),
	sweep2sleep_implemented_show, NULL);


static ssize_t debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", debug);
}

static ssize_t debug_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;
		
	// store if valid data
	if (((val == 0) || (val == 1)))
		debug = val;

	return count;
}

static DEVICE_ATTR(sweep2sleep_debug, (S_IWUSR|S_IRUGO),
	debug_show, debug_store);


static ssize_t version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", DRIVER_VERSION);
}

static DEVICE_ATTR(sweep2sleep_version, (S_IWUSR|S_IRUGO),
	version_show, NULL);


/*****************************************/
// Driver init and exit functions
/*****************************************/

struct kobject *android_touch_kobj;
EXPORT_SYMBOL_GPL(android_touch_kobj);

static int __init sweep2sleep_init(void)
{
	int rc = 0;

	s2s_input_wq = create_workqueue("s2siwq");
	if (!s2s_input_wq) 
	{
		pr_err(LOGTAG"%s: Failed to create s2siwq workqueue\n", __func__);
		goto err1;
	}
	
	INIT_WORK(&s2s_input_work, s2s_input_callback);
	rc = input_register_handler(&s2s_input_handler);
	if (rc)
	{
		pr_err(LOGTAG"%s: Failed to register s2s_input_handler\n", __func__);
		goto err2;
	}
	
	s2s_lcd_notif.notifier_call = lcd_notifier_callback;
	if (lcd_register_client(&s2s_lcd_notif) != 0) 
	{
		pr_err(LOGTAG"%s: Failed to register lcd callback\n", __func__);
		goto err2;
	}

	android_touch_kobj = kobject_create_and_add("android_touch", NULL) ;
	if (android_touch_kobj == NULL) 
	{
		pr_err(LOGTAG"%s: android_touch_kobj create_and_add failed\n", __func__);
		goto err2;
	}

	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2sleep.attr);
	if (rc) 
	{
		pr_warn(LOGTAG"%s: sysfs_create_file failed for sweep2sleep\n", __func__);
		goto err2;
	}
	
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2sleep_debug.attr);
	if (rc) 
	{
		pr_warn(LOGTAG"%s: sysfs_create_file failed for sweep2sleep_debug\n", __func__);
		goto err2;
	}
	
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2sleep_version.attr);
	if (rc) 
	{
		pr_warn(LOGTAG"%s: sysfs_create_file failed for sweep2sleep_version\n", __func__);
		goto err2;
	}

	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2sleep_implemented.attr);
	if (rc) 
	{
		pr_warn(LOGTAG"%s: sysfs_create_file failed for sweep2sleep_implemented\n", __func__);
		goto err2;
	}

	// Initialize delayed work for status bar timer
	INIT_DELAYED_WORK(&statusBarTimer, statusBarTimer_work);
	
	return 0;

err2:
	input_unregister_handler(&s2s_input_handler);
err1:
	destroy_workqueue(s2s_input_wq);
	return -EFAULT;
}


static void __exit sweep2sleep_exit(void)
{
	input_unregister_handler(&s2s_input_handler);
	destroy_workqueue(s2s_input_wq);

	return;
}


late_initcall(sweep2sleep_init);
module_exit(sweep2sleep_exit);
