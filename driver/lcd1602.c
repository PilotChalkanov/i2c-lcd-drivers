#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

MODULE_DEVICE_TABLE(i2c, lcd1602_id);

static struct i2c_driver lcd1602_driver = {
    .driver = {
        .name = "lcd1602",
        .of_match_table = lcd1602_of_match,
    },
    .probe = lcd1602_probe,
    .remove = lcd1602_remove,
    .id_table = lcd1602_id,
};

module_i2c_driver(lcd1602_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PilotChalkanov");
MODULE_DESCRIPTION("I2C 1602 LCD Driver");