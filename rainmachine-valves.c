/*
 *  rainmachine-valves - CD74HCT4094 8-bits shift register GPIO driver
 *
 *  Authors: Nicu Pavel <npavel@mini-box.com>
 *           Csenteri Barna <brown@mini-box.com>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/platform_device.h>


#define RMVALVES_AUTO_EXPORT 1
#define RMVALVES_DRIVER_NAME "rmvalves"

struct rmvalves_platform_data {
	int	gpio_base;
	int	gpio_pin_data;
	int	gpio_pin_clk;
	int	gpio_pin_oe;
	int	gpio_pin_strobe;
	int	gpio_pin_nl;
	int	gpio_pin_sc;
	int	gpio_pin_reset;
};

#ifdef DEV_V3
#define RMVALVES_NUM_GPIOS	16 /* Max number could be 12 or 16 real triacs */
#define RMVALVES_CHIP_BITS	16 /* word size for the gpio chip */
#define RMVALVES_OE_ACTIVE	1 /* OE it's active HIGH */
#warning "INFO: Using Sprinkler 3 GPIO definitions !"
static struct rmvalves_platform_data rmvalves_hardcoded_platform_data = 
{
	.gpio_base = 128,
	.gpio_pin_data = 19, /* GPIO0_19 */
	.gpio_pin_clk = 113, /* GPIO3_17 */ 
	.gpio_pin_oe = 110, /* GPIO3_14 */
	.gpio_pin_strobe = 111, /* GPIO3_15 */
	.gpio_pin_nl = -1, /* doesn't exist on V3 */
	.gpio_pin_sc = -1, /* doesn't exist on V3 */
	.gpio_pin_reset = 112 /* GPIO3_16 */
};
#else
#define RMVALVES_NUM_GPIOS	8
#define RMVALVES_CHIP_BITS	8 /* word size for the gpio chip */
#define RMVALVES_OE_ACTIVE	0 /* OE it's active LOW */
static struct rmvalves_platform_data rmvalves_hardcoded_platform_data = 
{
	.gpio_base = 40,
	.gpio_pin_data = 14,
	.gpio_pin_clk = 17,
	.gpio_pin_oe = 16,
	.gpio_pin_strobe = 15,
	.gpio_pin_nl = 27,
	.gpio_pin_sc = 26,
	.gpio_pin_reset = -1
};
#endif
#define RMVALVES_MASK	1 << (RMVALVES_CHIP_BITS - 1)

struct rmvalves_chip {
	struct device		*parent;
	struct gpio_chip	gpio_chip;
	struct mutex		lock;
	long			mask;
};

static void rmvalves_set_value(struct gpio_chip *, unsigned, int);

static struct rmvalves_chip *gpio_to_rmvalves_chip(struct gpio_chip *gc)
{
	return container_of(gc, struct rmvalves_chip, gpio_chip);
}

static int rmvalves_direction_input(struct gpio_chip *gc, unsigned offset)
{
	WARN_ON(1);
	return -EINVAL;
}

static int rmvalves_direction_output(struct gpio_chip *gc,
					unsigned offset, int val)
{
	rmvalves_set_value(gc, offset, val);
	return 0;
}

static int rmvalves_get_value(struct gpio_chip *gc, unsigned offset)
{
	struct rmvalves_chip *chip = gpio_to_rmvalves_chip(gc);
	int ret;

	mutex_lock(&chip->lock);
	ret = test_bit(offset, &chip->mask);
	mutex_unlock(&chip->lock);

	return ret;
}

static void rmvalves_set_value(struct gpio_chip *gc,
				  unsigned offset, int val)
{
	struct rmvalves_chip *chip;
	struct rmvalves_platform_data *pdata;
	long mask;
	int refresh;
	int i;

	chip = gpio_to_rmvalves_chip(gc);
	/* pdata = chip->parent->platform_data; */
	pdata = &rmvalves_hardcoded_platform_data;

	mutex_lock(&chip->lock);
	if (val)
		refresh = (test_and_set_bit(offset, &chip->mask) != val);
	else
		refresh = (test_and_clear_bit(offset, &chip->mask) != val);

	//printk(KERN_ERR "RMValves: mask: %ld refresh: %d\n", chip->mask, refresh);

	if (refresh) {
		mask = chip->mask;
		for (i = RMVALVES_CHIP_BITS; i > 0; --i, mask <<= 1) {
			//printk(KERN_ERR "RMValves: %d :0x%02x : 0x%02x !\n", i, RMVALVES_MASK, mask & RMVALVES_MASK);
			gpio_set_value(pdata->gpio_pin_data, mask & RMVALVES_MASK);
			gpio_set_value(pdata->gpio_pin_clk, 1);
			gpio_set_value(pdata->gpio_pin_clk, 0);
		}
		gpio_set_value(pdata->gpio_pin_strobe, 1);
		gpio_set_value(pdata->gpio_pin_strobe, 0);
		gpio_set_value(pdata->gpio_pin_oe, !RMVALVES_OE_ACTIVE);
	}
	mutex_unlock(&chip->lock);
}

static int rmvalves_probe(struct platform_device *pdev)
{
	struct rmvalves_platform_data *pdata;
	struct rmvalves_chip *chip;
	struct gpio_chip *gc;
	int err;

	printk(KERN_ERR "RMValves(%d gpios): Entering probing !\n", RMVALVES_NUM_GPIOS);

	/*pdata = pdev->dev.platform_data;*/
	pdata = &rmvalves_hardcoded_platform_data;
	if (pdata == NULL) {
		dev_dbg(&pdev->dev, "no platform data specified\n");
		return -EINVAL;
	}

	chip = kzalloc(sizeof(struct rmvalves_chip), GFP_KERNEL);
	if (chip == NULL) {
		dev_err(&pdev->dev, "no memory for private data\n");
		return -ENOMEM;
	}

	err = gpio_request(pdata->gpio_pin_clk, dev_name(&pdev->dev));
	if (err) {
		dev_err(&pdev->dev, "unable to claim clock gpio %u, err=%d\n",
			pdata->gpio_pin_clk, err);
		goto err_free_chip;
	}

	err = gpio_request(pdata->gpio_pin_data, dev_name(&pdev->dev));
	if (err) {
		dev_err(&pdev->dev, "unable to claim data gpio %u, err=%d\n",
			pdata->gpio_pin_data, err);
		goto err_free_clk;
	}

	err = gpio_request(pdata->gpio_pin_oe, dev_name(&pdev->dev));
	if (err) {
		dev_err(&pdev->dev, "unable to claim oe gpio %u, err=%d\n",
			pdata->gpio_pin_oe, err);
		goto err_free_oe;
	}

	err = gpio_request(pdata->gpio_pin_strobe, dev_name(&pdev->dev));
	if (err) {
		dev_err(&pdev->dev, "unable to claim strobe gpio %u, err=%d\n",
			pdata->gpio_pin_strobe, err);
		goto err_free_strobe;
	}

	err = gpio_direction_output(pdata->gpio_pin_clk, 0);
	if (err) {
		dev_err(&pdev->dev,
			"unable to set direction of gpio %u, err=%d\n",
			pdata->gpio_pin_clk, err);
		goto err_free_data;
	}

	err = gpio_direction_output(pdata->gpio_pin_data, 0);
	if (err) {
		dev_err(&pdev->dev,
			"unable to set direction of gpio %u, err=%d\n",
			pdata->gpio_pin_data, err);
		goto err_free_data;
	}

	err = gpio_direction_output(pdata->gpio_pin_oe, RMVALVES_OE_ACTIVE);
	if (err) {
		dev_err(&pdev->dev,
			"unable to set direction of oe gpio %u, err=%d\n",
			pdata->gpio_pin_oe, err);
		goto err_free_data;
	}

	err = gpio_direction_output(pdata->gpio_pin_strobe, 0);
	if (err) {
		dev_err(&pdev->dev,
			"unable to set direction of strobe gpio %u, err=%d\n",
			pdata->gpio_pin_strobe, err);
		goto err_free_data;
	}

	/* Reset for the GPIO extender chip */
	if (pdata->gpio_pin_reset > -1) {
		err = gpio_request(pdata->gpio_pin_reset, dev_name(&pdev->dev));
		if (err) {
			dev_err(&pdev->dev, "unable to claim reset gpio %u, err=%d\n",
				pdata->gpio_pin_reset, err);
			goto err_free_strobe;
		}

		err = gpio_direction_output(pdata->gpio_pin_reset, 0);
		if (err) {
			dev_err(&pdev->dev,
				"unable to set direction of reset gpio %u, err=%d\n",
				pdata->gpio_pin_reset, err);
			goto err_free_strobe;
		}

		/* Reset the CHIP */
		gpio_set_value(pdata->gpio_pin_reset, 0);
		msleep(5);
		gpio_set_value(pdata->gpio_pin_reset, 1);
		dev_info(&pdev->dev, "chip reset done\n");
	}


	chip->parent = &pdev->dev;

	mutex_init(&chip->lock);

	gc = &chip->gpio_chip;

	gc->direction_input  = rmvalves_direction_input;
	gc->direction_output = rmvalves_direction_output;
	gc->get = rmvalves_get_value;
	gc->set = rmvalves_set_value;
	gc->can_sleep = 1;

	gc->base = pdata->gpio_base;
	gc->ngpio = RMVALVES_NUM_GPIOS;
	/* gc->label = dev_name(chip->parent); */
	gc->label = "rmvalves";
	gc->dev = chip->parent;
	gc->owner = THIS_MODULE;

	err = gpiochip_add(&chip->gpio_chip);
	if (err) {
		dev_err(&pdev->dev, "unable to add gpio chip, err=%d\n", err);
		goto err_free_data;
	}

	platform_set_drvdata(pdev, chip);

#ifdef RMVALVES_AUTO_EXPORT
	char *gpio_name;
	int i;
	for (i = 0; i < RMVALVES_NUM_GPIOS; i++) {
		int gpio = pdata->gpio_base + i;
		gpio_name = kmalloc(14, GFP_KERNEL);
		sprintf(gpio_name, "%s%d", "valve", i);
		err = gpio_request(gpio, gpio_name);
		if (err)
			dev_err(&pdev->dev, "unable to claim gpio %u, err=%d\n", gpio, err);

		err = gpio_direction_output(gpio, 0);
		if (err)
			dev_err(&pdev->dev, "unable to set direction out for gpio %u, err=%d\n", gpio, err);

		gpio_export(gpio, 0);
	}
#endif

	printk(KERN_ERR "RMValves: Finished probing !\n");
	return 0;

err_free_strobe:
	gpio_free(pdata->gpio_pin_strobe);
err_free_oe:
	gpio_free(pdata->gpio_pin_oe);
err_free_data:
	gpio_free(pdata->gpio_pin_data);
err_free_clk:
	gpio_free(pdata->gpio_pin_clk);

err_free_chip:
	kfree(chip);
	return err;
}

static int rmvalves_remove(struct platform_device *pdev)
{
	printk(KERN_ERR "RMValves: Enter removing !\n");
	struct rmvalves_chip *chip = platform_get_drvdata(pdev);
	/*struct rmvalves_platform_data *pdata = pdev->dev.platform_data;*/
	struct rmvalves_platform_data *pdata = &rmvalves_hardcoded_platform_data;


	if (chip) {
		int err;
		int i;

#ifdef RMVALVES_AUTO_EXPORT
		for (i = 0; i < RMVALVES_NUM_GPIOS; i++) {
			int gpio = pdata->gpio_base + i;
			gpio_unexport(gpio);
			gpio_free(gpio);
		}
#endif
		gpio_free(pdata->gpio_pin_clk);
		gpio_free(pdata->gpio_pin_data);
		gpio_free(pdata->gpio_pin_oe);
		gpio_free(pdata->gpio_pin_strobe);

		if (pdata->gpio_pin_reset > -1)
			gpio_free(pdata->gpio_pin_reset);

		printk(KERN_ERR "RMValves: Removed gpios !\n");
		err = gpiochip_remove(&chip->gpio_chip);
		if (err) {
			dev_err(&pdev->dev, "unable to remove gpio chip, err=%d\n", err);
			return err;
		}

		kfree(chip);
		platform_set_drvdata(pdev, NULL);
	}

	return 0;
}

static struct platform_driver rmvalves_driver = {
	.probe		= rmvalves_probe,
	.remove		= rmvalves_remove,
	.driver = {
		.name	= RMVALVES_DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(rmvalves_driver);

MODULE_AUTHOR("Nicu Pavel <npavel <at> mini-box.com>");
MODULE_DESCRIPTION("CD74HCT595 16-bits shift register GPIO driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform: rmvalves");