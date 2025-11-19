#ifndef _BME280_H
#define _BME280_H

#include <linux/types.h>

/* BME280 register map */
#define BME280_REG_ID                0xD0
#define BME280_REG_RESET             0xE0
#define BME280_REG_CTRL_HUM          0xF2
#define BME280_REG_STATUS            0xF3
#define BME280_REG_CTRL_MEAS         0xF4
#define BME280_REG_CONFIG            0xF5
#define BME280_REG_PRESS_MSB         0xF7
#define BME280_REG_TEMP_MSB          0xFA
#define BME280_REG_HUM_MSB           0xFD

/* Calibration data as per datasheet */
struct bme280_calib {
    u16 dig_T1;
    s16 dig_T2;
    s16 dig_T3;

    u16 dig_P1;
    s16 dig_P2;
    s16 dig_P3;
    s16 dig_P4;
    s16 dig_P5;
    s16 dig_P6;
    s16 dig_P7;
    s16 dig_P8;
    s16 dig_P9;

    u8  dig_H1;
    s16 dig_H2;
    u8  dig_H3;
    s16 dig_H4;
    s16 dig_H5;
    s8  dig_H6;
};

/* Per-device driver data */
struct bme280_data {
    struct i2c_client   *client;
    struct bme280_calib calib;
    s32                 t_fine;
    struct mutex        lock;      /* protect accesses */
};

#endif /* _BME280_H */
