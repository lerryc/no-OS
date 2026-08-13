EVAL-AD5933ARDZ no-OS Example Project
=====================================

.. no-os-doxygen::

.. contents:: Table of Contents
    :depth: 3

Supported Evaluation Boards
---------------------------

* `EVAL-AD5933ARDZ <https://www.analog.com/EVAL-AD5933ARDZ>`_

Overview
--------

The **EVAL-AD5933ARDZ** is an Arduino-compatible evaluation board for the
:adi:`AD5933`, a high precision impedance converter system that combines an
on-board frequency generator with a 12-bit, 1 MSPS ADC and an on-chip DFT
engine. The frequency generator excites an external unknown impedance, and the
DFT engine returns a real and an imaginary data word for each measured
frequency point, from which the host computes magnitude, phase, and impedance.

The AD5933 is controlled over a 400 kHz I2C interface at slave address ``0x0D``.
The excitation frequency is swept from a programmable start frequency in a
programmable increment over up to 511 increments (512 points total), with a
software-selectable output range, PGA gain, and settling-time count. These
examples target the **SDP-CK1Z** controller board, which carries an STM32
microcontroller and mates with the EVAL-AD5933ARDZ through the SDP connector.

Applications
------------

* Electrochemical analysis
* Bioelectrical impedance analysis
* Complex impedance measurement
* Corrosion monitoring and protection equipment
* Biomedical and automotive sensors
* Proximity sensing
* Nondestructive testing and material property analysis

Hardware Specifications
-----------------------

Power Supply Requirements
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The EVAL-AD5933ARDZ is powered through the controller board over the SDP
connector; no external supply is required for the default configuration. The
UART console is available over the controller board's USB connection.

No-OS Supported Examples
------------------------

This project is organized around the no-OS variant based build flow. Selecting a
variant at build time (``--variant <name>``) chooses which application is
compiled. The platform ``main()`` is a thin dispatcher that calls
``example_main()``, provided by the selected example. Shared initialization data
is defined in
`src/common <https://github.com/analogdevicesinc/no-OS/tree/main/projects/eval-ad5933ardz/src/common>`__,
and platform-specific macros and extra init parameters are in
`src/platform <https://github.com/analogdevicesinc/no-OS/tree/main/projects/eval-ad5933ardz/src/platform>`__.

Basic Example
~~~~~~~~~~~~~

The basic example initializes the AD5933, runs a frequency sweep, and prints the
per-point measurements over UART. The sweep parameters (start frequency,
frequency increment, number of points, settling cycles, and the calibration
resistor value) are set at the top of
`iio_example.c <src/examples/iio_example/iio_example.c>`__ and
`common_data.c <src/common/common_data.c>`__.

IIO Example
~~~~~~~~~~~

The IIO example launches an IIOD server on the board so that any libiio client
can connect, configure the AD5933, run a frequency sweep, and read back the
collected real/imaginary data. The AD5933 sweep is exposed as a **non-blocking**
operation: the single-threaded IIOD server stays responsive to all clients while
a sweep runs. A client arms the sweep and remains free to issue any other
request; the sweep advances one point per IIOD main-loop iteration via the
application's ``post_step_callback`` (``ad5933_iio_sweep_step``), so no threading
is used and no changes to the IIO core were needed.

The whole flow uses standard libiio operations — there is no new protocol:

1. **Configure the sweep parameters** by writing the device attributes
   ``start_frequency``, ``frequency_increment``, ``frequency_points`` (the number
   of increments; points = ``frequency_points`` + 1), ``settling_cycles`` and
   ``settling_multiplier``.
2. **Arm the sweep** by writing a non-zero value to the ``sweep_start`` device
   attribute. This returns immediately (it does not block); it returns
   ``-EBUSY`` if a sweep is already running.
3. **Poll the** ``sweep_done`` **read-only attribute** until it reads ``1``. Each
   read is instant: ``1`` = the sweep has finished collecting, ``-1`` = the sweep
   aborted on error, ``0`` = idle or still running. While polling, the client and
   any other connected clients remain free to issue requests, because the server
   never blocks on the sweep.
4. **Read the buffer** to retrieve the collected points. The ``real`` and
   ``imaginary`` channels are scannable (signed 16-bit); enable them, open a
   buffer, and read it after ``sweep_done`` reads ``1``. Each point is delivered
   as two interleaved words — real then imaginary — for up to 512 points. The
   host performs any magnitude / phase / impedance math from the raw data.

To **abort** a running sweep, write ``0`` to ``sweep_start``; this stops the
sweep and returns the device to standby so a new sweep can be armed.

If you are not familiar with ADI IIO Application, please take a look at:
`IIO No-OS <https://wiki.analog.com/resources/tools-software/no-os-software/iio>`_

If you are not familiar with ADI IIO-Oscilloscope Client, please take a look at:
`IIO Oscilloscope <https://wiki.analog.com/resources/tools-software/linux-software/iio_oscilloscope>`_

No-OS Supported Platforms
-------------------------

STM32
~~~~~

Used Hardware
^^^^^^^^^^^^^

* `EVAL-AD5933ARDZ <https://www.analog.com/EVAL-AD5933ARDZ>`_
* SDP-CK1Z controller board (STM32)

Connections
^^^^^^^^^^^

The EVAL-AD5933ARDZ connects to the SDP-CK1Z through the SDP connector. The
AD5933 is accessed over I2C, and the example console is emitted over the
controller board's UART:

.. list-table::
   :header-rows: 1

   * - Function
     - STM32 Peripheral
     - Notes
   * - I2C (SCL/SDA)
     - I2C1
     - 400 kHz, AD5933 slave address 0x0D
   * - UART (debug console)
     - UART5
     - 57600 baud, 8N1

Build Command
^^^^^^^^^^^^^

The STM32 platform uses the CMake/Ninja build system via the ``no_os_build.py``
helper script. Available variants: ``basic``, ``iio_example``. Available boards:
``sdp-ck1z``.

For toolchain setup and prerequisites, see the
`STM32 CMake build guide <https://analogdevicesinc.github.io/no-OS/build_guides/build_stm32_cmake.html>`__.

.. code-block:: bash

	# point at the STM32 toolchain installations
	export STM32CUBEMX=</path/to/stm32cubemx>
	export STM32CUBEIDE=</path/to/stm32cubeide>
	# Windows (PowerShell) equivalent:
	#   $env:STM32CUBEMX = "C:\ST\STM32CubeMX"
	#   $env:STM32CUBEIDE = "C:\ST\STM32CubeIDE"

	cd no-OS

	# build the IIO example on the SDP-CK1Z board
	python tools/scripts/no_os_build.py build \
		--project eval-ad5933ardz --variant iio_example --board sdp-ck1z

	# build and flash (requires a connected debug probe)
	python tools/scripts/no_os_build.py build \
		--project eval-ad5933ardz --variant iio_example --board sdp-ck1z \
		--probe openocd --flash
