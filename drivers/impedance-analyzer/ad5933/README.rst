AD5933 no-OS driver
===================

.. no-os-doxygen::

Supported Devices
-----------------

- :adi:`AD5933`

Overview
--------

The AD5933 is a high precision impedance converter system that combines an
on-board frequency generator with a 12-bit, 1 MSPS analog-to-digital converter
(ADC). The frequency generator excites an unknown impedance with a programmable
output frequency; the response signal is sampled by the ADC and processed by an
on-chip discrete Fourier transform (DFT) engine that returns a real and an
imaginary data word for every measured frequency point.

Communication is over a 400 kHz I2C interface at slave address ``0x0D``. The
excitation frequency is swept from a programmable start frequency in a
programmable increment, over up to 511 increments (512 points total). The
output excitation range and an input-stage PGA gain (x1 / x5) are software
selectable, and a configurable settling-time count lets the excitation settle
before each DFT measurement. From the real/imaginary DFT output, the host
computes magnitude, phase, and — after calibration against a known resistor —
impedance.

Applications
------------

* Electrochemical analysis
* Bioelectrical impedance analysis
* Complex impedance measurement
* Corrosion monitoring and protection equipment
* Biomedical and automotive sensors
* Proximity sensing
* Nondestructive testing and material property analysis

AD5933 Device Configuration
---------------------------

Driver Initialization
~~~~~~~~~~~~~~~~~~~~~~~

In order to be able to use the device, you will have to provide the support
for the communication protocol (I2C).

The first API to be called is **ad5933_init**. Make sure that it returns 0,
which means that the driver was initialized correctly. It allocates the device
descriptor and brings up the I2C peripheral described by the init parameters.
**ad5933_setup** then programs the system clock source, the output range, and
the PGA gain, and **ad5933_remove** releases every resource allocated by
**ad5933_init**.

Register Access
~~~~~~~~~~~~~~~

Direct register access is available through **ad5933_reg_write** and
**ad5933_reg_read** for single-byte registers, and through
**ad5933_set_register_value** / **ad5933_get_register_value** for the
multi-byte, MSB-first frequency and increment registers. **ad5933_reset** issues
a device reset, and **ad5933_wait_status** polls the status register until a
requested status bit (temperature valid, data valid, sweep done) is set or a
timeout elapses.

Clock, Range and Gain Configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The system clock source (internal 16.776 MHz oscillator or an external clock)
is selected with **ad5933_set_system_clk**. The excitation output range and the
input PGA gain are configured together with **ad5933_set_range_and_gain**, or
individually with **ad5933_set_range** and **ad5933_set_gain**.
**ad5933_set_settling_time** programs the number of output-settling cycles
(and their x1 / x2 / x4 multiplier) that elapse before each DFT measurement.

Frequency Sweep Configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**ad5933_config_sweep** programs the sweep: the start frequency, the frequency
increment, and the number of increments (``inc_num``); the sweep therefore
covers ``inc_num + 1`` points, up to a maximum of 512. The classic blocking
sweep helpers remain available for non-IIO callers: **ad5933_start_sweep**
starts the sweep and waits for the first point to become valid,
**ad5933_get_current_data** reads the real/imaginary words of the current point,
**ad5933_sweep_next** advances to the next point and waits for it,
**ad5933_sweep_done** reports whether the sweep has completed, and
**ad5933_run_sweep** runs a whole sweep in one blocking call, filling the caller's
real/imaginary arrays.

Non-blocking Sweep Engine
~~~~~~~~~~~~~~~~~~~~~~~~~~~

For the single-threaded IIO daemon, the driver also provides a non-blocking
sweep state machine (``IDLE`` → ``RUNNING`` → ``DONE`` / ``ERROR``) that never
busy-waits on the device. This lets a cooperative server initiate a sweep and
remain responsive to all clients while it runs.

* **ad5933_sweep_arm** runs the start-sweep register sequence and enters the
  ``RUNNING`` state without blocking on the first data-valid. It returns
  ``-EBUSY`` if a sweep is already running.
* **ad5933_sweep_step** advances the sweep by at most one point per call and
  never blocks: it probes the status register once with **ad5933_status_probe**,
  and on data-valid it stores the point into the driver-owned staging buffer and
  issues the next frequency increment (or transitions to ``DONE`` when the sweep
  completes). A per-point timeout guard moves the state machine to ``ERROR`` if a
  point never becomes valid.
* **ad5933_sweep_abort** stops a running sweep, returns the device to standby,
  and resets the state to ``IDLE``.
* **ad5933_sweep_state** returns the current sweep state, and
  **ad5933_sweep_get_data** exposes the staging buffer (interleaved real then
  imaginary, two signed 16-bit words per point) together with the number of
  valid points collected.

Measurement and Calibration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**ad5933_get_data** reads the real and imaginary DFT words for a given frequency
function, and **ad5933_get_magnitude** / **ad5933_get_phase** derive the
magnitude and phase (radians) from them. Calibration against a known reference
impedance is performed with **ad5933_calculate_gain_factor**, **ad5933_calibrate**
(single point) or **ad5933_calibrate_2point** (two points across the sweep).
Once calibrated, **ad5933_calculate_impedance** returns the impedance magnitude
|Z| and **ad5933_calculate_complex_impedance** returns the complex impedance
(R + jX). The on-chip temperature sensor is read with
**ad5933_get_raw_temperature** (raw 14-bit two's complement) or
**ad5933_get_temperature** (degrees Celsius).

AD5933 Driver Initialization Example
------------------------------------

.. code-block:: bash

	struct ad5933_dev *dev;

	struct ad5933_init_param ad5933_ip = {
		.i2c_init = {
			.device_id = I2C_DEVICE_ID,
			.max_speed_hz = 400000,
			.slave_address = AD5933_ADDRESS,
			.platform_ops = I2C_OPS,
			.extra = I2C_EXTRA,
		},
		.current_sys_clk = 16000000,
		.current_clock_source = AD5933_CONTROL_EXT_SYSCLK,
		.current_gain = AD5933_GAIN_X1,
		.current_range = AD5933_RANGE_200mVpp,
		.current_settling_cycles_mult = AD5933_SETTLING_X1,
		.current_settling_cycles = AD5933_15_CYCLES,
	};

	ret = ad5933_init(&dev, &ad5933_ip);
	if (ret)
		goto error;

	ret = ad5933_setup(dev);
	if (ret)
		goto error;

AD5933 no-OS IIO support
------------------------

The AD5933 IIO driver comes on top of the AD5933 driver and offers support for
interfacing IIO clients through libiio.

The frequency sweep is exposed through the IIO layer as a **non-blocking**
operation, so a single-threaded IIO daemon stays responsive to every client
while a sweep runs. A client arms the sweep, polls a done flag, and then reads
the collected points from a buffer — all with standard libiio operations, with
no new protocol. The sweep is advanced one point per IIO main-loop iteration by
the application's post-step callback (``ad5933_iio_sweep_step``); no threading is
used and no changes to the IIO core were required.

AD5933 IIO Device Configuration
-------------------------------

Channel Attributes
~~~~~~~~~~~~~~~~~~

The temperature channel exposes:

* ``raw - the raw temperature reading from the on-chip sensor.``
* ``scale - the scale (1/32 °C per LSB) applied to raw to obtain degrees Celsius.``

The scannable ``real`` and ``imaginary`` voltage channels expose:

* ``raw - the real (or imaginary) DFT word of the current sweep point.``

The processed channels expose an ``input`` attribute derived from the current
point's real/imaginary data:

* ``magnitude - the DFT magnitude sqrt(real^2 + imag^2).``
* ``phase - the DFT phase in radians.``
* ``impedance - the calibrated impedance magnitude |Z| in ohms.``

Global Attributes
~~~~~~~~~~~~~~~~~

* ``pga_gain - the input PGA gain (x1 / x5).``
* ``output_range - the excitation output voltage range.``
* ``start_frequency - the sweep start frequency in Hz.``
* ``frequency_increment - the sweep frequency increment in Hz.``
* ``frequency_points - the number of frequency increments (inc_num); points = frequency_points + 1.``
* ``settling_cycles - the number of output settling cycles before each measurement.``
* ``settling_multiplier - the settling-cycle multiplier (x1 / x2 / x4).``
* ``calibration_impedance - the known reference impedance used for calibration, in ohms.``
* ``gain_factor - the gain factor computed by the last calibration (read-only).``
* ``system_phase - the system phase computed by the last calibration (read-only).``
* ``calibrate - single-point calibration trigger at the current sweep point.``
* ``calibrate_2point - two-point calibration trigger across the configured sweep.``
* ``sweep_start - non-blocking sweep control. Writing a non-zero value ARMS the sweep and returns immediately (it does not block); it returns -EBUSY if a sweep is already running. Writing 0 ABORTS the running sweep and returns the device to standby.``
* ``sweep_done - read-only poll target for the non-blocking sweep. Returns "1" once the sweep has finished collecting, "-1" if the sweep aborted on error, and "0" while idle or still running. Each read is instant.``

Buffered Capture
~~~~~~~~~~~~~~~~

The ``real`` and ``imaginary`` channels are scannable (signed 16-bit, little
endian). A sweep collects up to 512 points; each point contributes two 16-bit
words pushed in ascending ``scan_index`` order — real then imaginary — so the
buffer layout is interleaved ``real, imag`` per point.

Buffered capture is decoupled from the sweep itself: the driver collects sweep
points into its own staging buffer as the sweep runs, and the IIO buffer
``submit`` callback copies the collected points into the ring with a pure-RAM
copy (it never touches the device and never blocks). Because the two lifetimes
are independent, opening or reading the buffer cannot corrupt an in-flight
sweep. The typical client sequence is: configure the sweep parameters, write
``sweep_start`` = 1 to arm, poll ``sweep_done`` until it reads ``1``, then enable
and read the buffer to retrieve the interleaved ``real, imag`` samples. The copy
is clamped both to the number of points actually collected and to the number of
samples the client requested, so a buffer read issued before ``sweep_done`` reads
``1`` simply returns the points collected so far.

AD5933 IIO Driver Initialization Example
----------------------------------------

.. code-block:: bash

	int ret;

	struct ad5933_iio_dev *ad5933_iio_dev;
	struct ad5933_iio_dev_init_param ad5933_iio_ip = {
		.ad5933_dev_ip = &ad5933_ip,
		.start_freq = 30000,
		.freq_increment = 1000,
		.freq_points = 10,
		.settling_cycles = 15,
		.settling_multiplier = AD5933_SETTLING_X1,
		.calibration_impedance = 500,
		.calibration_freq = 30000,
		.calibration_freq2 = 40000,
	};

	struct iio_app_desc *app;
	struct iio_app_init_param app_init_param = {0};

	ret = ad5933_iio_init(&ad5933_iio_dev, &ad5933_iio_ip);
	if (ret)
		goto exit;

	struct iio_app_device iio_devices[] = {
		{
			.name = "ad5933",
			.dev = ad5933_iio_dev,
			.dev_descriptor = ad5933_iio_dev->iio_dev,
		},
	};

	app_init_param.devices = iio_devices;
	app_init_param.nb_devices = NO_OS_ARRAY_SIZE(iio_devices);
	app_init_param.uart_init_params = uip;
	/* Pump: advance the non-blocking sweep one point per iiod loop iteration. */
	app_init_param.post_step_callback = ad5933_iio_sweep_step;
	app_init_param.arg = ad5933_iio_dev;

	ret = iio_app_init(&app, app_init_param);
	if (ret)
		goto remove_iio_ad5933;

	return iio_app_run(app);
