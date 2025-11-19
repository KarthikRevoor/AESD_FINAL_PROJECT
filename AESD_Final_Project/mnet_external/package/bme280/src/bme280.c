#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/kernel.h>

#include "bme280.h"

/* Helper register access */

static int bme280_read_reg(struct i2c_client *client, u8 reg)
{
	int ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "reg 0x%02x read failed (%d)\n", reg, ret);

	return ret;
}

static int bme280_read_block(struct i2c_client *client, u8 reg, u8 *buf, int len)
{
	int ret = i2c_smbus_read_i2c_block_data(client, reg, len, buf);

	if (ret < 0)
		dev_err(&client->dev, "block read @0x%02x len=%d failed (%d)\n",
			reg, len, ret);

	return ret;
}

/* Calibration read */

static int bme280_read_calibration(struct bme280_data *data)
{
	struct i2c_client *client = data->client;
	struct bme280_calib *c = &data->calib;
	int ret;
	u8 buf[26];

	/* T1–T3, P1–P9 (0x88..0xA1) */
	ret = bme280_read_block(client, 0x88, buf, 24);
	if (ret < 0)
		return ret;

	c->dig_T1 = (u16)(buf[1] << 8 | buf[0]);
	c->dig_T2 = (s16)(buf[3] << 8 | buf[2]);
	c->dig_T3 = (s16)(buf[5] << 8 | buf[4]);

	c->dig_P1 = (u16)(buf[7] << 8 | buf[6]);
	c->dig_P2 = (s16)(buf[9] << 8 | buf[8]);
	c->dig_P3 = (s16)(buf[11] << 8 | buf[10]);
	c->dig_P4 = (s16)(buf[13] << 8 | buf[12]);
	c->dig_P5 = (s16)(buf[15] << 8 | buf[14]);
	c->dig_P6 = (s16)(buf[17] << 8 | buf[16]);
	c->dig_P7 = (s16)(buf[19] << 8 | buf[18]);
	c->dig_P8 = (s16)(buf[21] << 8 | buf[20]);
	c->dig_P9 = (s16)(buf[23] << 8 | buf[22]);

	/* H1 from 0xA1 */
	ret = bme280_read_reg(client, 0xA1);
	if (ret < 0)
		return ret;
	c->dig_H1 = (u8)ret;

	/* Remaining humidity calib data from 0xE1..0xE7 */
	ret = bme280_read_block(client, 0xE1, buf, 7);
	if (ret < 0)
		return ret;

	c->dig_H2 = (s16)(buf[1] << 8 | buf[0]);
	c->dig_H3 = buf[2];
	c->dig_H4 = (s16)((buf[3] << 4) | (buf[4] & 0x0f));
	c->dig_H5 = (s16)((buf[5] << 4) | (buf[4] >> 4));
	c->dig_H6 = (s8)buf[6];

	return 0;
}

/* Raw reading helpers                                                        */

static int bme280_read_raw_temp(struct bme280_data *data, s32 *adc_T)
{
	u8 buf[3];
	int ret;

	ret = bme280_read_block(data->client, BME280_REG_TEMP_MSB, buf, 3);
	if (ret < 0)
		return ret;

	*adc_T = ((s32)buf[0] << 12) |
	         ((s32)buf[1] << 4)  |
	         ((s32)buf[2] >> 4);

	return 0;
}

static int bme280_read_raw_press(struct bme280_data *data, s32 *adc_P)
{
	u8 buf[3];
	int ret;

	ret = bme280_read_block(data->client, BME280_REG_PRESS_MSB, buf, 3);
	if (ret < 0)
		return ret;

	*adc_P = ((s32)buf[0] << 12) |
	         ((s32)buf[1] << 4)  |
	         ((s32)buf[2] >> 4);

	return 0;
}

static int bme280_read_raw_hum(struct bme280_data *data, s32 *adc_H)
{
	u8 buf[2];
	int ret;

	ret = bme280_read_block(data->client, BME280_REG_HUM_MSB, buf, 2);
	if (ret < 0)
		return ret;

	*adc_H = ((s32)buf[0] << 8) | buf[1];
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Compensation formulas (fixed-point)              USED CHATGPT FOR THIS     */
/* Based on BME280 datasheet integer algorithm                               */
/* -------------------------------------------------------------------------- */

static int bme280_compensate_temp(struct bme280_data *data,
				  s32 adc_T, s32 *temp_mdegc)
{
	s32 var1, var2;
	struct bme280_calib *c = &data->calib;

	var1 = ((((adc_T >> 3) - ((s32)c->dig_T1 << 1))) * (s32)c->dig_T2) >> 11;
	var2 = (((((adc_T >> 4) - (s32)c->dig_T1) *
		  ((adc_T >> 4) - (s32)c->dig_T1)) >> 12) *
		(s32)c->dig_T3) >> 14;

	data->t_fine = var1 + var2;

	/* T in 0.01 degC; convert to milli-degC (×10) */
	*temp_mdegc = (((data->t_fine * 5 + 128) >> 8) * 10);

	return 0;
}

static int bme280_compensate_press(struct bme280_data *data,
				   s32 adc_P, s32 *pressure_pa)
{
	s64 var1, var2, p;
	struct bme280_calib *c = &data->calib;

	if (!data->t_fine)
		return -EINVAL;

	var1 = (s64)data->t_fine - 128000;
	var2 = var1 * var1 * (s64)c->dig_P6;
	var2 = var2 + ((var1 * (s64)c->dig_P5) << 17);
	var2 = var2 + (((s64)c->dig_P4) << 35);

	var1 = ((var1 * var1 * (s64)c->dig_P3) >> 8) +
	       ((var1 * (s64)c->dig_P2) << 12);
	var1 = (((((s64)1) << 47) + var1) * (s64)c->dig_P1) >> 33;

	if (var1 == 0)
		return -EINVAL;

	p = 1048576 - adc_P;
	p = (((p << 31) - var2) * 3125) / var1;
	var1 = ((s64)c->dig_P9 * (p >> 13) * (p >> 13)) >> 25;
	var2 = ((s64)c->dig_P8 * p) >> 19;
	p = ((p + var1 + var2) >> 8) + (((s64)c->dig_P7) << 4);

	/* p is Q4.4 format (Pa * 16); convert to Pa */
	*pressure_pa = (s32)(p >> 4);
	return 0;
}

/*
 * Humidity compensation is quite involved; for now we provide a TODO stanza
 * and simply return raw humidity (not calibrated) as an integer.
 * You can later replace this with the full integer-compensation from the
 * BME280 datasheet.
 */
static int bme280_compensate_hum(struct bme280_data *data,
				 s32 adc_H, s32 *hum_raw)
{
	*hum_raw = adc_H; /* TODO: implement proper compensated RH */
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Sysfs attributes                                                           */
/* -------------------------------------------------------------------------- */

static ssize_t temp_mdegc_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bme280_data *data = i2c_get_clientdata(client);
	s32 adc_T, temp_mdegc;
	int ret;

	mutex_lock(&data->lock);

	ret = bme280_read_raw_temp(data, &adc_T);
	if (!ret)
		ret = bme280_compensate_temp(data, adc_T, &temp_mdegc);

	mutex_unlock(&data->lock);

	if (ret < 0)
		return ret;

	/* Print milli-degC as integer, e.g., 24870 = 24.870°C */
	return sysfs_emit(buf, "%d\n", temp_mdegc);
}

static ssize_t pressure_pa_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bme280_data *data = i2c_get_clientdata(client);
	s32 adc_T, adc_P, temp_mdegc, press_pa;
	int ret;

	mutex_lock(&data->lock);

	ret = bme280_read_raw_temp(data, &adc_T);
	if (!ret)
		ret = bme280_compensate_temp(data, adc_T, &temp_mdegc);
	if (!ret)
		ret = bme280_read_raw_press(data, &adc_P);
	if (!ret)
		ret = bme280_compensate_press(data, adc_P, &press_pa);

	mutex_unlock(&data->lock);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", press_pa);
}

static ssize_t humidity_raw_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bme280_data *data = i2c_get_clientdata(client);
	s32 adc_H, hum;
	int ret;

	mutex_lock(&data->lock);

	ret = bme280_read_raw_hum(data, &adc_H);
	if (!ret)
		ret = bme280_compensate_hum(data, adc_H, &hum);

	mutex_unlock(&data->lock);

	if (ret < 0)
		return ret;

	/* TODO: convert to 0.01 %RH etc when full compensation is added */
	return sysfs_emit(buf, "%d\n", hum);
}

static DEVICE_ATTR_RO(temp_mdegc);
static DEVICE_ATTR_RO(pressure_pa);
static DEVICE_ATTR_RO(humidity_raw);

static struct attribute *bme280_attrs[] = {
	&dev_attr_temp_mdegc.attr,
	&dev_attr_pressure_pa.attr,
	&dev_attr_humidity_raw.attr,
	NULL,
};

static const struct attribute_group bme280_attr_group = {
	.attrs = bme280_attrs,
};

/* -------------------------------------------------------------------------- */
/* Probe / remove                                                             */
/* -------------------------------------------------------------------------- */

static int bme280_chip_init(struct bme280_data *data)
{
	struct i2c_client *client = data->client;
	int ret, tries;
	u8 status;

	/* Soft reset */
	ret = i2c_smbus_write_byte_data(client, BME280_REG_RESET, 0xB6);
	if (ret < 0)
		return ret;

	/* Wait for NVM copy (status bit 0) to clear */
	for (tries = 0; tries < 10; tries++) {
		msleep(10);
		ret = bme280_read_reg(client, BME280_REG_STATUS);
		if (ret < 0)
			return ret;
		status = ret & 0x01;
		if (!status)
			break;
	}

	/* Humidity oversampling x1 */
	ret = i2c_smbus_write_byte_data(client, BME280_REG_CTRL_HUM, 0x01);
	if (ret < 0)
		return ret;

	/*
	 * Temp/Pressure oversampling x1, mode normal:
	 * osrs_t = 001, osrs_p = 001, mode = 11 -> 0b00100111 = 0x27
	 */
	ret = i2c_smbus_write_byte_data(client, BME280_REG_CTRL_MEAS, 0x27);
	if (ret < 0)
		return ret;

	return 0;
}

static int bme280_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct bme280_data *data;
	int ret, chip_id;

	chip_id = bme280_read_reg(client, BME280_REG_ID);
	if (chip_id < 0)
		return chip_id;

	if (chip_id != 0x60) {
		dev_err(&client->dev, "unexpected chip id 0x%02x\n", chip_id);
		return -ENODEV;
	}

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->lock);

	i2c_set_clientdata(client, data);

	ret = bme280_chip_init(data);
	if (ret)
		return ret;

	ret = bme280_read_calibration(data);
	if (ret)
		return ret;

	ret = sysfs_create_group(&client->dev.kobj, &bme280_attr_group);
	if (ret) {
		dev_err(&client->dev, "failed to create sysfs group\n");
		return ret;
	}

	dev_info(&client->dev,
		 "BME280 sensor probed (addr=0x%02x, chip-id=0x%02x)\n",
		 client->addr, chip_id);

	return 0;
}

static void bme280_remove(struct i2c_client *client)
{
    sysfs_remove_group(&client->dev.kobj, &bme280_attr_group);
}


/* -------------------------------------------------------------------------- */
/* I2C tables                                                                 */
/* -------------------------------------------------------------------------- */

static const struct i2c_device_id bme280_id[] = {
	{ "bme280", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bme280_id);

static const struct of_device_id bme280_of_match[] = {
	{ .compatible = "mnet,bme280" },
	{ }
};
MODULE_DEVICE_TABLE(of, bme280_of_match);

static struct i2c_driver bme280_driver = {
	.driver = {
		.name           = "bme280_mnet",
		.of_match_table = bme280_of_match,
	},
	.probe    = bme280_probe,
	.remove   = bme280_remove,
	.id_table = bme280_id,
};

module_i2c_driver(bme280_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kaif Shahid Shaikh");
MODULE_DESCRIPTION("BME280 I2C driver with sysfs temperature/pressure/humidity");
