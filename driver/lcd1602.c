/*
 * Copyright 2026 <Nikolay Chalkanov, aka PilotChalkanov>
 * I2C LCD1602 Driver - HD44780 LCD Controller via PCF8574 I2C Expander
 *
 * DATASHEET REFERENCES:
 * - HD44780U (LCD Controller): https://en.wikipedia.org/wiki/Hitachi_HD44780
 *   Official: https://www.alldatasheet.com/datasheet-pdf/pdf/50623/HITACHI/HD44780U.html
 * - PCF8574/PCF8574A (I2C I/O Expander): https://www.nxp.com/products/interfaces-and-connectivity/i2c/i2c-io-expanders/remote-8-bit-i2c-io-expander:PCF8574
 *   Datasheet: https://www.nxp.com/docs/en/data-sheet/PCF8574_PCF8574A.pdf
 *
 * HARDWARE ARCHITECTURE:
 * ┌─────────────┐         I2C            ┌──────────┐      Parallel        ┌──────────┐
 * │             │      (2 wires)         │          │     (8 wires)        │          │
 * │ Raspberry   │───SDA────────────────▶│ PCF8574T │────────────────────▶│ HD44780  │
 * │     Pi      │───SCL────────────────▶│  I2C to  │  P7,P6,P5,P4 → D7-D4 │   LCD    │
 * │             │                        │  Parallel│  P2 → EN (Enable)    │Controller│
 * │  (I2C       │      1 byte =          │ Expander │  P1 → RW             │          │
 * │  Master)    │   8 bits control       │          │  P0 → RS (Register)  │ 16x2     │
 * │             │   (Addr: 0x27)         │          │  P3 → BL (Backlight) │ Display  │
 * └─────────────┘                        └──────────┘                      └──────────┘
 *
 * PCF8574 DATASHEET SUMMARY:
 * - 8-bit quasi-bidirectional I/O expander
 * - I2C slave address: 0x27 (PCF8574T) or 0x3F (PCF8574AT)
 * - I2C speeds: 100kHz (Standard mode), 400kHz (Fast mode), 1MHz (Fast-plus)
 * - Pin P0-P7: Weak internal pull-ups, suitable for open-drain outputs
 * - Quasi-bidirectional mode: Acts as output with high-impedance input capability
 * - Single byte I/O: Read/write entire 8-bit port in one I2C transaction
 *
 * HD44780 LCD CONTROLLER DATASHEET SUMMARY:
 * - 16x2 character LCD (16 columns, 2 rows)
 * - 4-bit or 8-bit parallel interface (we use 4-bit)
 * - 4-bit mode: Data sent as two 4-bit nibbles (upper first, then lower)
 * - Control pins: RS (Register Select), RW (Read/Write), EN (Enable)
 * - EN pulse timing: High→Low transition latches data on falling edge
 * - Minimum timing: EN pulse width ≥450ns (we use 50µs for safety margin)
 * - Data setup time: ≥40ns before EN goes low
 * - Commands take 37-1530µs to execute (initialization needs delays)
 * - 4-bit initialization sequence: Must send 0x30 three times, then 0x20
 *
 * PIN MAPPING (PCF8574 P0-P7 to HD44780):
 * P0 = RS  (Register Select: 0=Command, 1=Data)
 * P1 = RW  (Read/Write: 0=Write, 1=Read) - held at 0 (write-only mode)
 * P2 = EN  (Enable: pulse high→low to latch data)
 * P3 = BL  (Backlight: 1=On, 0=Off)
 * P4 = D4  (Data bit 4)
 * P5 = D5  (Data bit 5)
 * P6 = D6  (Data bit 6)
 * P7 = D7  (Data bit 7)
 *
 * 4-BIT MODE OPERATION (from HD44780 datasheet page 45-46):
 * 1. Send upper 4 bits (D7-D4) of data/command to LCD via PCF8574 P7-P4
 * 2. Pulse EN pin (set EN=1, wait ≥1µs, set EN=0, wait ≥50µs)
 * 3. Send lower 4 bits (D7-D4) of data/command to LCD via PCF8574 P7-P4
 * 4. Pulse EN pin again
 * Note: Bits D3-D0 are ignored in 4-bit mode
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include "driver/lcd1602.h"

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

/* from product-manual CL Default I2C bus address:
0x3F for the PCF8574AT chip, 0x27 for the PCF8574T  */
#define LCD_I2C_ADDR 0x27

/* PCF8574 pin definitions*/
#define LCD_RS    0x01  /* Bit 0 */
#define LCD_RW    0x02  /* Bit 1 */
#define LCD_EN    0x04  /* Bit 2 */
#define LCD_BL    0x08  /* Bit 3 - Backlight */

/*
cmd ref: https://www.electronicwings.com/sensors-modules/lcd-16x2-display-module
*/

/* lcd cmds */
#define LCD_CLEAR           0x01
#define LCD_HOME            0x02
#define LCD_ENTRY_MODE      0x04
#define LCD_DISPLAY_CONTROL 0x08
#define LCD_FUNCTION_SET    0x20

/* cmd flags */
#define LCD_ENTRY_LEFT       0x02
#define LCD_DISPLAY_ON       0x04
#define LCD_CURSOR_OFF       0x00
#define LCD_BLINK_OFF        0x00
#define LCD_4BIT_MODE        0x00
#define LCD_2LINE            0x08
#define LCD_5x8DOTS          0x00


struct lcd1602_data {
    struct i2c_client *client;
    u8 backlight;
    struct miscdevice miscdev;
    struct mutex lock;
};

static int lcd_write_nibble(struct i2c_client *client, u8 nibble, u8 mode) {
    struct lcd1602_data *lcd = i2c_get_clientdata(client);
    u8 data;
    int ret;

    /* Prepare data byte to send to PCF8574 */
    data = (nibble & 0x0F) << 4; /* Place nibble on D7-D4 */
    if (mode)
        data |= LCD_RS;          /* Set RS for data mode */
    else
        data &= ~LCD_RS;         /* Clear RS for command mode */

    data |= lcd->backlight;      /* Set backlight state */

    /* Pulse EN high */
    data |= LCD_EN;
    ret = i2c_smbus_write_byte(client, data);
    if (ret < 0)
        return ret;

    udelay(1); /* Enable pulse width >= 450ns */

    /* Pulse EN low */
    data &= ~LCD_EN;
    ret = i2c_smbus_write_byte(client, data);
    if (ret < 0)
        return ret;

    udelay(50); /* Wait for command to process (>= 37µs) */

    return 0;
}

/* Write a byte to LCD (sends as two 4-bit nibbles) */
static int lcd_write_byte(struct i2c_client *client, u8 byte, u8 mode) {
    int ret;

    /*send upper nibble, by masking the byte with 11110000=0xF0 */
    ret = lcd_write_nibble(client, byte & 0xF0, mode);
    if (ret < 0)
        return ret;

    /*send lower nibble, shift and mask with 11110000=0xF0 */
    ret = lcd_write_nibble(client, (byte << 4) & 0xF0, mode);
    if (ret < 0)
        return ret;

    return 0;
}

/* Send command to LCD */
static int lcd_send_command(struct i2c_client *client, u8 cmd) {
    /* RS=0 for command */
    return lcd_write_byte(client, cmd, 0);
}

/* Send data (character) to LCD */
static int lcd_send_data(struct i2c_client *client, u8 data) {
    /* RS=1 for data */
    return lcd_write_byte(client, data, LCD_RS);
}


/* init lcd in 4-bit mode*/
static int lcd_init_display(struct i2c_client *client) {
    int ret;

    dev_info(&client->dev, "Initializing LCD display...\n");

    /* Wait for power up */
    msleep(50);

    /* Initialize to 4-bit mode - special sequence */
    /* Send 0x03 three times */
    ret = lcd_write_nibble(client, 0x30, 0);
    if (ret < 0) return ret;
    msleep(5);

    ret = lcd_write_nibble(client, 0x30, 0);
    if (ret < 0) return ret;
    msleep(1);

    ret = lcd_write_nibble(client, 0x30, 0);
    if (ret < 0) return ret;
    msleep(1);

    /* Switch to 4-bit mode */
    ret = lcd_write_nibble(client, 0x20, 0);
    if (ret < 0) return ret;
    msleep(1);

    /* Function set: 4-bit mode, 2 lines, 5x8 font */
    ret = lcd_send_command(client,
                          LCD_FUNCTION_SET | LCD_4BIT_MODE | LCD_2LINE | LCD_5x8DOTS);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to set function\n");
        return ret;
    }
    msleep(1);

    /* Display control: display on, cursor off, blink off */
    ret = lcd_send_command(client,
                          LCD_DISPLAY_CONTROL | LCD_DISPLAY_ON |
                          LCD_CURSOR_OFF | LCD_BLINK_OFF);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to set display control\n");
        return ret;
    }
    msleep(1);

    /* Clear display */
    ret = lcd_send_command(client, LCD_CLEAR);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to clear display\n");
        return ret;
    }
    msleep(2);

    /* Entry mode: increment cursor, no shift */
    ret = lcd_send_command(client, LCD_ENTRY_MODE | LCD_ENTRY_LEFT);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to set entry mode\n");
        return ret;
    }
    msleep(1);

    dev_info(&client->dev, "LCD initialized successfully\n");
    return 0;
}

/* Write a string to LCD */
static int lcd_write_string(struct i2c_client *client, const char *str) {
    int ret;

    while (*str) {
        ret = lcd_send_data(client, *str++);
        if (ret < 0)
            return ret;
    }

    return 0;
}


/* Write up to 2x16 chars from buffer to LCD. Excess is truncated. */
static int lcd_write_buffer(struct lcd1602_data *lcd, const char *buf, size_t len) {
    int ret;
    size_t i;
    size_t max_len = min(len, (size_t)32); /* 16 chars per line, 2 lines */

    /* Reset cursor and clear display */
    ret = lcd_send_command(lcd->client, LCD_CLEAR);
    if (ret < 0)
        return ret;
    msleep(2);

    ret = lcd_send_command(lcd->client, LCD_HOME);
    if (ret < 0)
        return ret;
    msleep(2);

    for (i = 0; i < max_len; i++) {
        if (i == 16) {
            /* Move to second line */
            ret = lcd_send_command(lcd->client, 0xC0);
            if (ret < 0)
                return ret;
            msleep(1);
        }

        ret = lcd_send_data(lcd->client, buf[i]);
        if (ret < 0)
            return ret;
    }

    return 0;
}

/* /dev/lcd1602 write handler */
static ssize_t lcd1602_write(struct file *file, const char __user *ubuf,
                             size_t count, loff_t *ppos) {
    struct lcd1602_data *lcd = container_of(file->private_data,
                                            struct lcd1602_data, miscdev);
    char kbuf[64]; /* holds up to 32 displayable chars plus padding */
    size_t to_copy;
    int ret;

    if (!count)
        return 0;

    to_copy = min(count, sizeof(kbuf) - 1);

    if (copy_from_user(kbuf, ubuf, to_copy))
        return -EFAULT;
    kbuf[to_copy] = '\0';

    mutex_lock(&lcd->lock);
    ret = lcd_write_buffer(lcd, kbuf, to_copy);
    mutex_unlock(&lcd->lock);

    if (ret < 0)
        return ret;

    return count; /* Report all bytes consumed */
}

static const struct file_operations lcd1602_fops = {
    .owner = THIS_MODULE,
    .write = lcd1602_write,
    .llseek = NULL,
};

/*
probe func - mandatory for i2c drivers
func is called when the driver is matched with a device.
check if dev is i2c capable
initialize the dev
*/
static int lcd1602_probe(struct i2c_client *client,
                         const struct i2c_device_id *id) {
    struct lcd1602_data *data;
    int ret;

    /*
    check if dev is i2c capable
    */

    PDEBUG("Probing LCD1602 driver\n");
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(&client->dev, "I2C functionality not supported\n");
        PDEBUG("I2C functionality not supported\n");
        return -EIO;
    }
    /*devm_kzalloc is managing memory deallocation automatically on disconnect*/
    lcd = devm_kzalloc(&client->dev, sizeof(*lcd), GFP_KERNEL);
    if (!lcd)
        return -ENOMEM;
    lcd->client = client;
        lcd->backlight = LCD_BL;  // Backlight ON
    mutex_init(&lcd->lock);
    i2c_set_clientdata(client, lcd);

    // ret = lcd_init_display(client);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to initialize LCD\n");
        PDEBUG("Failed to initialize LCD\n");
        return ret;
    }

    lcd->miscdev.minor = MISC_DYNAMIC_MINOR;
    lcd->miscdev.name = "lcd1602";
    lcd->miscdev.fops = &lcd1602_fops;
    lcd->miscdev.parent = &client->dev;

    ret = misc_register(&lcd->miscdev);
    if (ret) {
        dev_err(&client->dev, "Failed to register misc device: %d\n", ret);
        PDEBUG("Failed to register misc device: %d\n", ret);
        return ret;
    }

    /* display init mx*/
    lcd_write_string(client, "Hello from");
    lcd_send_command(client, 0xC0);  /* Move to second line */
    lcd_write_string(client, "Raspberry Pi!");
    dev_info(&client->dev, "LCD1602 driver loaded successfully\n");

    return 0;
}

static int lcd1602_remove(struct i2c_client *client) {
    struct lcd1602_data *lcd = i2c_get_clientdata(client);

    if (lcd)
        misc_deregister(&lcd->miscdev);
    lcd_send_command(lcd->client, LCD_CLEAR);
    kfree(lcd);
    dev_info(&client->dev, "LCD1602 driver removed\n");
    PDEBUG("LCD1602 driver removed\n");
    return 0;
}

static const struct i2c_device_id lcd1602_id[] = {
    { "lcd1602", 0 },
    { }
};

MODULE_DEVICE_TABLE(i2c, lcd1602_id);

/* Device Tree match table */
static const struct of_device_id lcd1602_of_match[] = {
    { .compatible = "hitachi,hd44780" },
    { }
};
MODULE_DEVICE_TABLE(of, lcd1602_of_match);

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
