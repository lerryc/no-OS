/***************************************************************************//**
 *   @file   iio_ad5933.c
 *   @brief  Implementation of the AD5933 IIO driver.
 *   @author Mark John Lerry Casero (markjohnlerry.casero@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES, INC. "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL ANALOG DEVICES, INC. BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include "iio_ad5933.h"
#include "ad5933.h"
#include "iio.h"
#include "no_os_alloc.h"
#include "no_os_util.h"
#include <string.h>


/* Temperature scale is 1/32 degC/LSB = 0.031250. */
#define AD5933_TEMP_SCALE_INT		0
#define AD5933_TEMP_SCALE_MICRO		31250

/** Per-channel selector carried through iio_channel.address. */
static enum ad5933_iio_chan {
	AD5933_CH_REAL,
	AD5933_CH_IMAG,
	AD5933_CH_TEMP,
};

/** Device-attribute selector carried through iio_attribute.priv. */
static enum ad5933_iio_attr_priv {
	AD5933_ATTR_PGA_GAIN,
	AD5933_ATTR_PGA_GAIN_AVAILABLE,
	AD5933_ATTR_OUTPUT_RANGE,
	AD5933_ATTR_OUTPUT_RANGE_AVAILABLE,
	AD5933_ATTR_START_FREQ,
	AD5933_ATTR_FREQ_INCREMENT,
	AD5933_ATTR_FREQ_POINTS,
	AD5933_ATTR_SETTLING_CYCLES,
	AD5933_ATTR_SETTLING_CYCLES_MULTIPLIER,
	AD5933_ATTR_SETTLING_CYCLES_MULTIPLIER_AVAILABLE,
	AD5933_ATTR_HEARTBEAT,
	AD5933_ATTR_SWEEP_INITIALIZED,
	AD5933_ATTR_SWEEP_STARTED,
	AD5933_ATTR_MEASURE_MODE,
	AD5933_ATTR_MEASURE_MODE_AVAILABLE,
	AD5933_ATTR_CURRENT_OUTPUT_FREQ,
	AD5933_ATTR_REPEAT_MEASUREMENT,
	AD5933_ATTR_INCREMENTED_MEASUREMENT
};

static char* ad5933_pga_gain_available[] = {
	[AD5933_GAIN_X5] = "gain_5x",
	[AD5933_GAIN_X1] = "gain_1x",
};

static char* ad5933_output_range_available[] = {
	[AD5933_RANGE_2000mVpp] = "range_2000mvpp",
	[AD5933_RANGE_200mVpp] = "range_200mvpp",
	[AD5933_RANGE_400mVpp] = "range_400mvpp",
	[AD5933_RANGE_1000mVpp] = "range_1000mvpp"
};

static char* ad5933_settling_cycles_multiplier_available[] = {
	[AD5933_SETTLING_X1] = "settling_1x",
	[AD5933_SETTLING_X2] = "settling_2x",
	[AD5933_SETTLING_X4] = "settling_4x"
};

static char* ad5933_measure_mode_available[] = {
	[AD5933_MEASURE_MODE_SINGLE] = "single",
	[AD5933_MEASURE_MODE_SWEEP] = "sweep"
};

static int16_t ad5933_channel_data[1024];

static int ad5933_iio_read_raw(void *dev, char *buf, uint32_t len,
			       const struct iio_ch_info *channel, intptr_t priv);
static int ad5933_iio_read_scale(void *dev, char *buf, uint32_t len,
				 const struct iio_ch_info *channel, intptr_t priv);
static int ad5933_iio_read_dev_attr(void *dev, char *buf, uint32_t len,
				    const struct iio_ch_info *channel,
				    intptr_t priv);
static int ad5933_iio_write_dev_attr(void *dev, char *buf, uint32_t len,
				     const struct iio_ch_info *channel,
				     intptr_t priv);
static int ad5933_iio_pre_enable(void *dev, uint32_t mask);
static int ad5933_iio_submit(struct iio_device_data *dev_data);

static int ad5933_iio_reg_read(void *device, uint32_t reg_addr,
			       uint32_t *reg_data);
static int ad5933_iio_reg_write(void *device, uint32_t reg_addr,
				uint32_t reg_data);

static struct iio_attribute ad5933_temp_attrs[] = {
	{ .name = "raw", .show = ad5933_iio_read_raw },
	{ .name = "scale", .show = ad5933_iio_read_scale },
	END_ATTRIBUTES_ARRAY
};

static struct iio_attribute ad5933_raw_attrs[] = {
	{ .name = "raw", .show = ad5933_iio_read_raw },
	END_ATTRIBUTES_ARRAY
};

static struct scan_type ad5933_sweep_scan_type = {
	.sign = 's',
	.realbits = 16,
	.storagebits = 16,
	.shift = 0,
	.is_big_endian = false,
};

static struct iio_channel ad5933_channels[] = {

	{
		.ch_type = IIO_VOLTAGE,
		.name = "real",
		.channel = 0,
		.indexed = true,
		.address = AD5933_CH_REAL,
		.attributes = ad5933_raw_attrs,
		.ch_out = false,
		.scan_index = 0,
		.scan_type = &ad5933_sweep_scan_type,
	},
	{
		.ch_type = IIO_VOLTAGE,
		.name = "imaginary",
		.channel = 1,
		.indexed = true,
		.address = AD5933_CH_IMAG,
		.attributes = ad5933_raw_attrs,
		.ch_out = false,
		.scan_index = 1,
		.scan_type = &ad5933_sweep_scan_type,
	},
	{
		.ch_type = IIO_TEMP,
		.channel = 0,
		.name = "temperature",
		.address = AD5933_CH_TEMP,
		.attributes = ad5933_temp_attrs,
		.ch_out = false,
		.scan_index = -1,
	},
};

static struct iio_attribute ad5933_iio_dev_attrs[] = {
	{
		.name = "pga_gain",
		.show = ad5933_iio_read_dev_attr,
		.store = ad5933_iio_write_dev_attr,
		.priv = AD5933_ATTR_PGA_GAIN,
	},
	{
		.name = "pga_gain_available",
		.show = ad5933_iio_read_dev_attr,
		.priv = AD5933_ATTR_PGA_GAIN_AVAILABLE,
	},
	{
		.name = "output_range",
		.show = ad5933_iio_read_dev_attr,
		.store = ad5933_iio_write_dev_attr,
		.priv = AD5933_ATTR_OUTPUT_RANGE,
	},
	{
		.name = "output_range_available",
		.show = ad5933_iio_read_dev_attr,
		.priv = AD5933_ATTR_OUTPUT_RANGE_AVAILABLE,
	},
	{
		.name = "start_frequency",
		.show = ad5933_iio_read_dev_attr,
		.store = ad5933_iio_write_dev_attr,
		.priv = AD5933_ATTR_START_FREQ,
	},
	{
		.name = "frequency_increment",
		.show = ad5933_iio_read_dev_attr,
		.store = ad5933_iio_write_dev_attr,
		.priv = AD5933_ATTR_FREQ_INCREMENT,
	},
	{
		.name = "frequency_points",
		.show = ad5933_iio_read_dev_attr,
		.store = ad5933_iio_write_dev_attr,
		.priv = AD5933_ATTR_FREQ_POINTS,
	},
	{
		.name = "settling_cycles",
		.show = ad5933_iio_read_dev_attr,
		.store = ad5933_iio_write_dev_attr,
		.priv = AD5933_ATTR_SETTLING_CYCLES,
	},
	{
		.name = "settling_cycles_multiplier",
		.show = ad5933_iio_read_dev_attr,
		.store = ad5933_iio_write_dev_attr,
		.priv = AD5933_ATTR_SETTLING_CYCLES_MULTIPLIER,
	},
	{
		.name = "settling_cycles_multiplier_available",
		.show = ad5933_iio_read_dev_attr,
		.priv = AD5933_ATTR_SETTLING_CYCLES_MULTIPLIER_AVAILABLE,
	},
	{
		.name = "heartbeat",
		.show = ad5933_iio_read_dev_attr,
		.priv = AD5933_ATTR_HEARTBEAT,
	},
	{
		.name = "sweep_initialized",
		.show = ad5933_iio_read_dev_attr,
		.store = ad5933_iio_write_dev_attr,
		.priv = AD5933_ATTR_SWEEP_INITIALIZED,
	},
	{
		.name = "sweep_started",
		.show = ad5933_iio_read_dev_attr,
		.store = ad5933_iio_write_dev_attr,
		.priv = AD5933_ATTR_SWEEP_STARTED,
	},
	{
		.name = "measure_mode",
		.show = ad5933_iio_read_dev_attr,
		.store = ad5933_iio_write_dev_attr,
		.priv = AD5933_ATTR_MEASURE_MODE,
	},
	{
		.name = "measure_mode_available",
		.show = ad5933_iio_read_dev_attr,
		.priv = AD5933_ATTR_MEASURE_MODE_AVAILABLE,
	},
	{
		.name = "current_output_frequency",
		.show = ad5933_iio_read_dev_attr,
		.priv = AD5933_ATTR_CURRENT_OUTPUT_FREQ,
	},
	{
		.name = "repeat_measurement",
		.store = ad5933_iio_write_dev_attr,
		.show = ad5933_iio_read_dev_attr,
		.priv = AD5933_ATTR_REPEAT_MEASUREMENT,
	},
	{
		.name = "incremented_measurement",
		.store = ad5933_iio_write_dev_attr,
		.show = ad5933_iio_read_dev_attr,
		.priv = AD5933_ATTR_INCREMENTED_MEASUREMENT,
	},
	END_ATTRIBUTES_ARRAY
};

static struct iio_device ad5933_iio_dev = {
	.num_ch = NO_OS_ARRAY_SIZE(ad5933_channels),
	.channels = ad5933_channels,
	.attributes = ad5933_iio_dev_attrs,
	.debug_attributes = NULL,
	.buffer_attributes = NULL,
	.pre_enable = ad5933_iio_pre_enable,
	.submit = ad5933_iio_submit,
	.debug_reg_read = ad5933_iio_reg_read,
	.debug_reg_write = ad5933_iio_reg_write,
};

static int ad5933_iio_reg_read(void *device, uint32_t reg_addr,
			       uint32_t *reg_data)
{
	struct ad5933_dev *ad5933 = device;
	int ret;
	uint8_t reg_val;

	if (!device || !reg_data)
		return -EINVAL;

	if (!reg_addr)
		return 0;

	ret = ad5933_reg_read(ad5933, (uint8_t)reg_addr, &reg_val);

	if (ret)
		return ret;

	*reg_data = (uint32_t)reg_val;

	return 0;
}

static int ad5933_iio_reg_write(void *device, uint32_t reg_addr,
				uint32_t reg_data)
{
	struct ad5933_dev *ad5933 = device;
	uint8_t reg_val;

	if (!device)
		return -EINVAL;

	return ad5933_reg_write(ad5933, (uint8_t)reg_addr, (uint8_t)reg_data);
}

/**
 * @brief Read the "raw" attribute of a channel (temperature / real / imag).
 */
static int ad5933_iio_read_raw(void *dev, char *buf, uint32_t len,
			       const struct iio_ch_info *channel, intptr_t priv)
{
	struct ad5933_iio_dev *iio_ad5933;
	struct ad5933_dev *ad5933;
	int32_t raw;
	int32_t temp_raw;
	int16_t real;
	int16_t imag;
	int ret;

	if (!dev)
		return -EINVAL;

	iio_ad5933 = dev;
	ad5933 = iio_ad5933->ad5933_dev;

	switch (channel->address) {
	case AD5933_CH_TEMP:
		ret = ad5933_get_raw_temperature(ad5933, &temp_raw);
		if (ret)
			return ret;
		raw = temp_raw;
		break;
	case AD5933_CH_REAL:
	case AD5933_CH_IMAG:
		ret = ad5933_get_current_data(ad5933, &real, &imag);
		if (ret)
			return ret;
		raw = (channel->address == AD5933_CH_REAL) ? real : imag;
		break;
	default:
		return -EINVAL;
	}

	return iio_format_value(buf, len, IIO_VAL_INT, 1, &raw);
}

/**
 * @brief Read the "scale" attribute (temperature channel only).
 */
static int ad5933_iio_read_scale(void *dev, char *buf, uint32_t len,
				 const struct iio_ch_info *channel, intptr_t priv)
{
	int32_t vals[2];

	if (!dev)
		return -EINVAL;

	switch (channel->address) {
	case AD5933_CH_TEMP:
		vals[0] = AD5933_TEMP_SCALE_INT;
		vals[1] = AD5933_TEMP_SCALE_MICRO;
		return iio_format_value(buf, len, IIO_VAL_INT_PLUS_MICRO, 2,
					vals);
	default:
		return -EINVAL;
	}
}


/**
 * @brief Read a device-global attribute.
 */
static int ad5933_iio_read_dev_attr(void *dev, char *buf, uint32_t len,
				    const struct iio_ch_info *channel,
				    intptr_t priv)
{
	struct ad5933_iio_dev *iio_ad5933;
	struct ad5933_dev *ad5933;
	int32_t val;
	int length = 0;
	int ret;

	if (!dev)
		return -EINVAL;

	iio_ad5933 = dev;
	ad5933 = iio_ad5933->ad5933_dev;

	switch (priv) {
	case AD5933_ATTR_PGA_GAIN:
		val = ad5933->pga_gain;
		return snprintf(buf, len, "%s", ad5933_pga_gain_available[val]);
	case AD5933_ATTR_PGA_GAIN_AVAILABLE:
		for (int i = 0; i < NO_OS_ARRAY_SIZE(ad5933_pga_gain_available); i++){
			ret = snprintf(buf + length, len - length, "%s ", ad5933_pga_gain_available[i]);
			if (ret < 0 || ret >= (int)(len - length))
				return -ENOMEM;
			length += ret;
		}
		return length;
	case AD5933_ATTR_OUTPUT_RANGE:
		val = ad5933->output_range;
		return snprintf(buf, len, "%s", ad5933_output_range_available[val]);
	case AD5933_ATTR_OUTPUT_RANGE_AVAILABLE:
		for (int i = 0; i < NO_OS_ARRAY_SIZE(ad5933_output_range_available); i++){
			ret = snprintf(buf + length, len - length, "%s ", ad5933_output_range_available[i]);
			if (ret < 0 || ret >= (int)(len - length))
				return -ENOMEM;
			length += ret;
		}
		return length;
	case AD5933_ATTR_START_FREQ:
		val = ad5933->start_freq;
		return iio_format_value(buf, len, IIO_VAL_INT, 1, &val);
	case AD5933_ATTR_FREQ_INCREMENT:
		val = ad5933->freq_increment;
		return iio_format_value(buf, len, IIO_VAL_INT, 1, &val);
	case AD5933_ATTR_FREQ_POINTS:
		val = ad5933->num_increments;
		return iio_format_value(buf, len, IIO_VAL_INT, 1, &val);
	case AD5933_ATTR_SETTLING_CYCLES:
		val = ad5933->settling_cycles;
		return iio_format_value(buf, len, IIO_VAL_INT, 1, &val);
	case AD5933_ATTR_SETTLING_CYCLES_MULTIPLIER:
		val = ad5933->settling_cycle_multiplier;
		return snprintf(buf, len, "%s", ad5933_settling_cycles_multiplier_available[val]);
	case AD5933_ATTR_SETTLING_CYCLES_MULTIPLIER_AVAILABLE:
		for (int i = 0; i < NO_OS_ARRAY_SIZE(ad5933_settling_cycles_multiplier_available); i++){
			if (i == 2)
				continue;  // Skip the missing enum index 2
			ret = snprintf(buf + length, len - length, "%s ", ad5933_settling_cycles_multiplier_available[i]);
			if (ret < 0 || ret >= (int)(len - length))
				return -ENOMEM;
			length += ret;
		}
		return length;
	case AD5933_ATTR_HEARTBEAT:
		val = iio_ad5933->heartbeat;
		return iio_format_value(buf, len, IIO_VAL_INT, 1, &val);
	case AD5933_ATTR_SWEEP_INITIALIZED:
		val = iio_ad5933->sweep_initialized;
		return iio_format_value(buf, len, IIO_VAL_INT, 1, &val);
	case AD5933_ATTR_SWEEP_STARTED:
		val = iio_ad5933->sweep_started;
		return iio_format_value(buf, len, IIO_VAL_INT, 1, &val);
	case AD5933_ATTR_MEASURE_MODE:
		val = iio_ad5933->measure_mode;
		return snprintf(buf, len, "%s", ad5933_measure_mode_available[val]);
	case AD5933_ATTR_MEASURE_MODE_AVAILABLE:
		for (int i = 0; i < NO_OS_ARRAY_SIZE(ad5933_measure_mode_available); i++){
			ret = snprintf(buf + length, len - length, "%s ", ad5933_measure_mode_available[i]);
			if (ret < 0 || ret >= (int)(len - length))
				return -ENOMEM;
			length += ret;
		}
		return length;
	case AD5933_ATTR_CURRENT_OUTPUT_FREQ:
		val = ad5933->current_output_freq;
		return iio_format_value(buf, len, IIO_VAL_INT, 1, &val);
	case AD5933_ATTR_REPEAT_MEASUREMENT:
		val = ad5933->current_output_freq;
		return snprintf(buf, len, "1=repeat measurement at current frequency");
	case AD5933_ATTR_INCREMENTED_MEASUREMENT:
		val = ad5933->current_output_freq;
		return snprintf(buf, len, "1=measure after incrementing frequency to next point in sweep");
	default:
		return -EINVAL;
	}
}

/**
 * @brief Write a device-global attribute.
 */
static int ad5933_iio_write_dev_attr(void *dev, char *buf, uint32_t len,
				     const struct iio_ch_info *channel,
				     intptr_t priv)
{
	struct ad5933_iio_dev *iio_ad5933;
	struct ad5933_dev *ad5933;
	int32_t val;
	int ret;

	if (!dev)
		return -EINVAL;

	iio_ad5933 = dev;
	ad5933 = iio_ad5933->ad5933_dev;

	ret = iio_parse_value(buf, IIO_VAL_INT, &val, NULL);

	if (ret < 0)
		return ret;

	switch (priv) {
	case AD5933_ATTR_PGA_GAIN:
		for (int i = 0; i < NO_OS_ARRAY_SIZE(ad5933_pga_gain_available); i++) {
			if (!strcmp(buf, ad5933_pga_gain_available[i])) {
				ret = ad5933_set_gain(ad5933, i);
				if (ret)
					return ret;
				break;
			}
		}
		break;
	case AD5933_ATTR_OUTPUT_RANGE:
		for (int i = 0; i < NO_OS_ARRAY_SIZE(ad5933_output_range_available); i++) {
			if (!strcmp(buf, ad5933_output_range_available[i])) {
				ret = ad5933_set_range(ad5933, i);
				if (ret)
					return ret;
				return len;
			}
		}
		return -EINVAL;
	case AD5933_ATTR_START_FREQ:
		ret = ad5933_config_sweep(ad5933, val,
					  ad5933->freq_increment,
					  ad5933->num_increments);
		if (ret)
			return ret;
		break;
	case AD5933_ATTR_FREQ_INCREMENT:
		ret = ad5933_config_sweep(ad5933, ad5933->start_freq,
					  val,
					  ad5933->num_increments);
		if (ret)
			return ret;
		break;
	case AD5933_ATTR_FREQ_POINTS:
		ret = ad5933_config_sweep(ad5933, ad5933->start_freq,
					  ad5933->freq_increment,
					  val);
		if (ret)
			return ret;
		break;
	case AD5933_ATTR_SETTLING_CYCLES:
		ret = ad5933_set_settling_time(ad5933,
					       ad5933->settling_cycle_multiplier,
					       val);
		if (ret)
			return ret;
		break;
	case AD5933_ATTR_SETTLING_CYCLES_MULTIPLIER:
		for (int i = 0; i < NO_OS_ARRAY_SIZE(ad5933_settling_cycles_multiplier_available); i++) {
			if (i == 2)
				continue; // Skip the missing enum index 2

			if (!strcmp(buf, ad5933_settling_cycles_multiplier_available[i])) {
				ret = ad5933_set_settling_time(ad5933,
							       i,
							       ad5933->settling_cycles);
				if (ret)
					return ret;
				return len;
			}
		}
		return -EINVAL;
	case AD5933_ATTR_SWEEP_INITIALIZED:
		if (val == 1) {
			ret = ad5933_initialize_sweep(ad5933);
			if (ret)
				return ret;
			iio_ad5933->sweep_initialized = val;
		}
		break;
	case AD5933_ATTR_SWEEP_STARTED:
		if (val == 1) {
			ret = ad5933_start_sweep(ad5933);
			if (ret)
				return ret;
			iio_ad5933->sweep_started = val;
		}
		break;
	case AD5933_ATTR_MEASURE_MODE:
		for (int i = 0; i < NO_OS_ARRAY_SIZE(ad5933_measure_mode_available); i++) {
			if (!strcmp(buf, ad5933_measure_mode_available[i])) {
				iio_ad5933->measure_mode = i;
				return len;
			}
		}
		return -EINVAL;
	case AD5933_ATTR_REPEAT_MEASUREMENT:
		ret = ad5933_repeat_freq(ad5933);
		if (ret)
			return ret;
		ret = ad5933_wait_status(ad5933, AD5933_STAT_DATA_VALID, NULL);
		if (ret)
			return ret;
		break;
	case AD5933_ATTR_INCREMENTED_MEASUREMENT:
		ret = ad5933_increment_freq(ad5933);
		if (ret)
			return ret;
		ret = ad5933_wait_status(ad5933, AD5933_STAT_DATA_VALID, NULL);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		return ret;

	return len;
}

/**
 * @brief Buffer pre-enable: record the active scan-channel mask.
 * @param dev  - The AD5933 IIO device descriptor.
 * @param mask - Active-channel mask (bit position == channel array index).
 * @return 0 in case of success, negative error code otherwise.
 */
static int ad5933_iio_pre_enable(void *dev, uint32_t mask)
{
	struct ad5933_iio_dev *iio_ad5933 = dev;

	if (!iio_ad5933)
		return -EINVAL;

	iio_ad5933->active_channels = mask;
	iio_ad5933->no_of_active_channels = no_os_hweight32(mask);

	if (iio_ad5933->measure_mode != AD5933_MEASURE_MODE_SWEEP) {
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief Buffer submit: copy the collected sweep points into the ring.
 *
 * @param dev_data - The IIO device data structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int ad5933_iio_submit(struct iio_device_data *dev_data)
{
	struct ad5933_iio_dev *iio_ad5933;
	struct ad5933_dev *ad5933;
	struct iio_buffer *buffer;
	uint32_t mask;
	uint32_t points;
	uint32_t max_samples;
	uint32_t i;
	int16_t scan[3];
	int16_t real, imag;
	int ret;

	if (!dev_data)
		return -EINVAL;

	iio_ad5933 = dev_data->dev;
	ad5933 = iio_ad5933->ad5933_dev;
	buffer = dev_data->buffer;

	mask = buffer->active_mask;
	points = buffer->samples;
	max_samples = ad5933->num_increments + 1;

	for (i = 0; i < points; i++) {
		uint8_t k = 0;

		if (mask & NO_OS_BIT(AD5933_CH_REAL))
			scan[k++] = i < max_samples ? ad5933_channel_data[i * 2] : 0;
		if (mask & NO_OS_BIT(AD5933_CH_IMAG))
			scan[k++] = i < max_samples ? ad5933_channel_data[i * 2 + 1] : 0;
		ret = iio_buffer_push_scan(buffer, scan);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * @brief Advance the non-blocking sweep by one point.
 *
 * Registered by the project as iio_app post_step_callback and invoked on every
 * iiod loop iteration.
 *
 * @param arg - The AD5933 IIO device descriptor (struct ad5933_iio_dev *).
 * @return 0 always.
 */
int ad5933_iio_sweep_step(void *arg)
{
	struct ad5933_iio_dev *iio_ad5933 = arg;
	int ret;

	if (iio_ad5933->measure_mode == AD5933_MEASURE_MODE_SWEEP
		&& iio_ad5933->sweep_started && iio_ad5933->sweep_initialized
	) {
			if (iio_ad5933->ad5933_dev->sweep_point <= iio_ad5933->ad5933_dev->num_increments){
				ad5933_wait_status(iio_ad5933->ad5933_dev, AD5933_STAT_DATA_VALID, NULL);
				ad5933_get_current_data(iio_ad5933->ad5933_dev, &ad5933_channel_data[iio_ad5933->ad5933_dev->sweep_point * 2], &ad5933_channel_data[iio_ad5933->ad5933_dev->sweep_point * 2 + 1]);
				ad5933_increment_freq(iio_ad5933->ad5933_dev);
			}else{
				iio_ad5933->sweep_started = false;
			}
	}
	
	iio_ad5933->heartbeat++;

	if(iio_ad5933->heartbeat > 1000) {
		iio_ad5933->heartbeat = 1;
	}

	return 0;
}

/**
 * @brief Initialize the AD5933 IIO driver.
 * @param iio_dev    - The AD5933 IIO device descriptor.
 * @param init_param - The AD5933 IIO device initialization parameters.
 * @return 0 in case of success, negative error code otherwise.
 */
int ad5933_iio_init(struct ad5933_iio_dev **iio_dev,
		    struct ad5933_iio_dev_init_param *init_param)
{
	struct ad5933_iio_dev *desc;
	int ret;

	if (!iio_dev || !init_param || !init_param->ad5933_dev_ip)
		return -EINVAL;

	desc = no_os_calloc(1, sizeof(*desc));
	if (!desc)
		return -ENOMEM;

	desc->iio_dev = &ad5933_iio_dev;
	desc->measure_mode = AD5933_MEASURE_MODE_SINGLE;
	desc->heartbeat = 0;

	ret = ad5933_init(&desc->ad5933_dev, init_param->ad5933_dev_ip);

	if (ret) {
		goto error_desc;
	}

	ret = ad5933_setup(desc->ad5933_dev);

	if (ret)
		goto error_dev;

	ret = ad5933_config_sweep(desc->ad5933_dev, init_param->start_freq,
				  init_param->freq_increment, init_param->freq_points);

	if (ret)
		goto error_dev;

	*iio_dev = desc;

	return 0;

error_dev:
	ad5933_remove(desc->ad5933_dev);
error_desc:
	no_os_free(desc);
	return ret;
}

/**
 * @brief Free the resources allocated by ad5933_iio_init().
 * @param desc - The AD5933 IIO device descriptor.
 * @return 0 in case of success, negative error code otherwise.
 */
int ad5933_iio_remove(struct ad5933_iio_dev *desc)
{
	int ret;

	if (!desc)
		return -EINVAL;

	ret = ad5933_remove(desc->ad5933_dev);
	if (ret)
		return ret;

	no_os_free(desc);

	return 0;
}
