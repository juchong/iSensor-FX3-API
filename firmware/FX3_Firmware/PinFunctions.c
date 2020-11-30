/**
  * Copyright (c) 2018-2020 Analog Devices, Inc. All Rights Reserved.
  * This software is proprietary to Analog Devices, Inc. and its licensors.
  * 
  * THIS SOFTWARE UTILIZES LIBRARIES DEVELOPED
  * AND MAINTAINED BY CYPRESS INC. THE LICENSE INCLUDED IN
  * THIS REPOSITORY DOES NOT EXTEND TO CYPRESS PROPERTY.
  * 
  * Use of this file is governed by the license agreement
  * included in this repository.
  * 
  * @file		PinFunctions.c
  * @date		8/1/2019
  * @author		A. Nolan (alex.nolan@analog.com)
  * @author 	J. Chong (juan.chong@analog.com)
  * @brief		This file contains all pin and timing function implementations.
 **/

#include "PinFunctions.h"

/* Tell the compiler where to find the needed globals */
extern BoardState FX3State;
extern CyU3PEvent GpioHandler;
extern uint8_t USBBuffer[4096];
extern uint8_t BulkBuffer[12288];

/**
  * @brief Gets the programmed board type and pin mapping info
  *
  * @param outBuf Byte array which pin map and board type are placed into
  *
  * @return void
  *
  * outBuf contains BoardType(4), ResetPin(2), DIO(2 each), GPIO(2 each).
  * Total size of 4 + 2 + 8 + 8 = 22 bytes
 **/
void AdiGetBoardPinInfo(uint8_t * outBuf)
{
	outBuf[0] = FX3State.BoardType & 0xFF;
	outBuf[1] = (FX3State.BoardType & 0xFF00) >> 8;
	outBuf[2] = (FX3State.BoardType & 0xFF0000) >> 16;
	outBuf[3] = (FX3State.BoardType & 0xFF000000) >> 24;
	outBuf[4] = FX3State.PinMap.ADI_PIN_RESET & 0xFF;
	outBuf[5] = (FX3State.PinMap.ADI_PIN_RESET & 0xFF00) >> 8;
	outBuf[6] = FX3State.PinMap.ADI_PIN_DIO1 & 0xFF;
	outBuf[7] = (FX3State.PinMap.ADI_PIN_DIO1 & 0xFF00) >> 8;
	outBuf[8] = FX3State.PinMap.ADI_PIN_DIO2 & 0xFF;
	outBuf[9] = (FX3State.PinMap.ADI_PIN_DIO2 & 0xFF00) >> 8;
	outBuf[10] = FX3State.PinMap.ADI_PIN_DIO3 & 0xFF;
	outBuf[11] = (FX3State.PinMap.ADI_PIN_DIO3 & 0xFF00) >> 8;
	outBuf[12] = FX3State.PinMap.ADI_PIN_DIO4 & 0xFF;
	outBuf[13] = (FX3State.PinMap.ADI_PIN_DIO4 & 0xFF00) >> 8;
	outBuf[14] = FX3State.PinMap.FX3_PIN_GPIO1 & 0xFF;
	outBuf[15] = (FX3State.PinMap.FX3_PIN_GPIO1 & 0xFF00) >> 8;
	outBuf[16] = FX3State.PinMap.FX3_PIN_GPIO2 & 0xFF;
	outBuf[17] = (FX3State.PinMap.FX3_PIN_GPIO2 & 0xFF00) >> 8;
	outBuf[18] = FX3State.PinMap.FX3_PIN_GPIO3 & 0xFF;
	outBuf[19] = (FX3State.PinMap.FX3_PIN_GPIO3 & 0xFF00) >> 8;
	outBuf[20] = FX3State.PinMap.FX3_PIN_GPIO4 & 0xFF;
	outBuf[21] = (FX3State.PinMap.FX3_PIN_GPIO4 & 0xFF00) >> 8;
}

/**
  * @brief Determine state of an input pin (high, low, high Z)
  *
  * @param pin The GPIO matrix index for the pin to measure (0 - 31)
  *
  * @return The current pin state, as a PinState
  *
  * This function first configures the selected pin to act as an input.
  * It then enables a weak pull down resistor and records the value on the
  * input stage of the pin. It enables a weak pull up resistor and records
  * the value on the input stage of the pin. If both values match, then the pin
  * is at that level (high/low). If the value changes to follow the pull up, then
  * the pin is High-Z (tristated). The pin resistor is then disabled.
 **/
PinState AdiGetPinState(uint16_t pin)
{
	uint32_t read0, read1;
	CyU3PGpioSimpleConfig_t gpioConfig = {0};

	/* Configure pin to read input stage values */
	gpioConfig.outValue = CyFalse;
	gpioConfig.inputEn = CyTrue;
	gpioConfig.driveLowEn = CyFalse;
	gpioConfig.driveHighEn = CyFalse;
	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
	CyU3PGpioSetSimpleConfig(pin, &gpioConfig);

	/* Clear pull up/down */
	GCTL_WPU_CFG &= ~(1 << pin);
	GCTL_WPD_CFG &= ~(1 << pin);

	/* Sleep 5us */
	AdiSleepForMicroSeconds(5);

	/* Enable pull down */
	GCTL_WPD_CFG |= (1 << pin);

	/* Sleep 5us */
	AdiSleepForMicroSeconds(5);

	/* Read pin */
	read0 = (GPIO->lpp_gpio_simple[pin] >> 1) & 0x1;

	/* Disable pull down */
	GCTL_WPD_CFG &= ~(1 << pin);

	/* Sleep 5us */
	AdiSleepForMicroSeconds(5);

	/* Enable pull up */
	GCTL_WPU_CFG |= (1 << pin);

	/* Sleep 5us */
	AdiSleepForMicroSeconds(5);

	/* Read pin */
	read1 = (GPIO->lpp_gpio_simple[pin] >> 1) & 0x1;

	/* Disable pull up */
	GCTL_WPU_CFG &= ~(1 << pin);

	/* Sleep 5us */
	AdiSleepForMicroSeconds(5);

	/* If pins are at a fixed value, return that value */
	if(read1 == read0)
	{
		return read0;
	}
	else
	{
		/* If changed by resistor then is probably floating */
		return HighZ;
	}
}

/**
  * @brief Configure GPIO input stage pull up / pull down resistor
  *
  * @param pin The GPIO matrix index for the pin to configure (0 - 63)
  *
  * @param setting The PinResistorSetting to apply to the selected pin
  *
  * @return A status code indicating the success of the operation
  *
  * This function configures the "weak" pull up or pull down setting which the FX3 micro
  * provides for each GPIO.
 **/
CyU3PReturnStatus_t AdiSetPinResistor(uint16_t pin, PinResistorSetting setting)
{
	/* Check that pin number is valid */
	if(!AdiIsValidGPIO(pin))
		return CY_U3P_ERROR_BAD_ARGUMENT;

#ifdef VERBOSE_MODE
	CyU3PDebugPrint (4, "Starting GPIO Resistor Config for pin: %d with setting: %d\r\n", pin, setting);
#endif

	/* If pin is in lower 32 bits */
	if(pin < 32)
	{
		/* Clear pull up/down first */
		GCTL_WPU_CFG &= ~(1 << pin);
		GCTL_WPD_CFG &= ~(1 << pin);

		/* Sleep 5us */
		AdiSleepForMicroSeconds(5);

		/* Apply setting */
		if(setting == PullDown)
		{
			GCTL_WPD_CFG |= (1 << pin);
		}
		else if(setting == PullUp)
		{
			GCTL_WPU_CFG |= (1 << pin);
		}
	}
	/* If pin is in upper 32 bits */
	else
	{
		/* Offset the pin value by 32*/
		pin = pin - 32;

		/* Clear pull up/down first */
		GCTL_WPU_CFG_UPPR &= ~(1 << pin);
		GCTL_WPD_CFG_UPPR &= ~(1 << pin);

		/* Sleep 5us */
		AdiSleepForMicroSeconds(5);

		/* Apply setting */
		if(setting == PullDown)
		{
			GCTL_WPD_CFG_UPPR |= (1 << pin);
		}
		else if(setting == PullUp)
		{
			GCTL_WPU_CFG_UPPR |= (1 << pin);
		}
	}

	/* Sleep 5us */
	AdiSleepForMicroSeconds(5);

	/* return success code */
	return CY_U3P_SUCCESS;
}

/**
  * @brief Determines if a GPIO pin is valid for the FX3 Application firmware to set
  *
  * @param GpioId The GPIO matrix index for the pin to check
  *
  * @return A boolean indicating if the GPIO is valid or not.
  *
  * This function is called before all interfacing functions. This prevents invalid GPIO access,
  * or unintended modifications to the power supply management pins.
 **/
CyBool_t AdiIsValidGPIO(uint16_t GpioId)
{
	/* Power management control pins reserved */
	if(GpioId == ADI_3_3V_EN)
		return CyFalse;
	if(GpioId == ADI_5V_EN)
		return CyFalse;

	/* ID pins are reserved for system */
	if(GpioId == ADI_ID_PIN_0)
		return CyFalse;
	if(GpioId == ADI_ID_PIN_1)
		return CyFalse;

	/* Flash enable pin reserved */
	if(GpioId == ADI_FLASH_WRITE_ENABLE_PIN)
		return CyFalse;

	/* I2C pins reserved */
	if(GpioId == ADI_I2C_SCL_PIN)
		return CyFalse;
	if(GpioId == ADI_I2C_SDA_PIN)
		return CyFalse;

	/* Timer pin is reserved */
	if(GpioId == ADI_TIMER_PIN)
		return CyFalse;

	/* GPIO must be less than 64 */
	if(GpioId > 63)
		return CyFalse;

	/* Else, should be good */
	return CyTrue;
}

/**
  * @brief Measures the delay from a trigger pin edge (sync) to a busy pin edge.
  *
  * @param transferLength The amount of data (in bytes) to read from the USB buffer
  *
  * @return A status code indicating the success of the pin delay measure operation
  *
  * This function is approx. microsecond accurate. It can be used for timing measurements which
  * require a high degree of accuracy since it avoids the overhead of having a USB transaction (200us)
  * between the initial pin drive condition and the pulse measurement. This function is primarily
  * intended to be used for measuring the latency between a sync edge and data ready toggle on the
  * ADIS IMU series of products.
 **/
CyU3PReturnStatus_t AdiMeasurePinDelay(uint16_t transferLength)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint16_t *bytesRead = 0;
	uint16_t busyPin, triggerPin;
	CyBool_t busyInitialValue, busyCurrentValue, triggerDrivePolarity, exitCondition;
	uint32_t currentTime, lastTime, timeout, rollOverCount;
	CyU3PGpioSimpleConfig_t gpioConfig = {0};

	/* Read config data into USBBuffer */
	status = CyU3PUsbGetEP0Data(transferLength, USBBuffer, bytesRead);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(PinFunctions_c, __LINE__, status);
		/* Return error code up - timeout will occur on PC side */
		return CY_U3P_ERROR_INVALID_SEQUENCE;
	}

	/* Parse config */
	triggerPin = USBBuffer[0];
	triggerPin = triggerPin + (USBBuffer[1] << 8);
	triggerDrivePolarity = (CyBool_t) USBBuffer[2];
	busyPin = USBBuffer[3];
	busyPin = busyPin + (USBBuffer[4] << 8);
	timeout = USBBuffer[5];
	timeout = timeout + (USBBuffer[6] << 8);
	timeout = timeout + (USBBuffer[7] << 16);
	timeout = timeout + (USBBuffer[8] << 24);

	/* Convert ms to timer ticks */
	timeout = timeout * MS_TO_TICKS_MULT;

	/* Verify pins */
	if(!AdiIsValidGPIO(busyPin) || !AdiIsValidGPIO(triggerPin))
	{
		status = CY_U3P_ERROR_BAD_ARGUMENT;
		/* Send status to the PC (alerting them of invalid GPIO selection) */
		AdiReturnBulkEndpointData(status, 12);
		return status;
	}

	/* Check that busy pin specified is configured as input */
	status = CyU3PGpioSimpleGetValue(busyPin, &busyInitialValue);
	if(status != CY_U3P_SUCCESS)
	{
		/* If initial pin read fails try and configure as input */
		gpioConfig.outValue = CyFalse;
		gpioConfig.inputEn = CyTrue;
		gpioConfig.driveLowEn = CyFalse;
		gpioConfig.driveHighEn = CyFalse;
		gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
		status = CyU3PGpioSetSimpleConfig(busyPin, &gpioConfig);

		/* get the pin value again after configuring */
		status = CyU3PGpioSimpleGetValue(busyPin, &busyInitialValue);

		/* If pin setup not successful skip wait operation and return -1 */
		if(status != CY_U3P_SUCCESS)
		{
			/* Send status to PC */
			AdiReturnBulkEndpointData(status, 12);
			return status;
		}
	}

	/* Drive trigger pin to drive polarity */
	gpioConfig.outValue = triggerDrivePolarity;
	gpioConfig.inputEn = CyFalse;
	gpioConfig.driveLowEn = CyTrue;
	gpioConfig.driveHighEn = CyTrue;
	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
	status = CyU3PGpioSetSimpleConfig(triggerPin, &gpioConfig);

	/* Reset the pin timer register to 0 */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].timer = 0;
	/* Disable interrupts on the timer pin */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status &= ~(CY_U3P_LPP_GPIO_INTRMODE_MASK);
	/* Set the pin timer period to 0xFFFFFFFF; */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].period = 0xFFFFFFFF;

	lastTime = 0;
	currentTime = 0;
	rollOverCount = 0;
	exitCondition = CyFalse;

	/* Begin wait operation (for edge transition on busy pin)*/
	while(!exitCondition)
	{
		/* Store previous time */
		lastTime = currentTime;

		/* Set the pin config for sample now mode */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status = (FX3State.TimerPinConfig | (CY_U3P_GPIO_MODE_SAMPLE_NOW << CY_U3P_LPP_GPIO_MODE_POS));
		/* wait for sample to finish */
		while (GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_MODE_MASK);
		/* read timer value */
		currentTime = GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].threshold;

		/* Read the pin value */
		busyCurrentValue = ((GPIO->lpp_gpio_simple[busyPin] & CY_U3P_LPP_GPIO_IN_VALUE) >> 1);

		/* Check if rollover occurred */
		if(currentTime < lastTime)
		{
			rollOverCount++;
		}

		/* update the exit condition */
		exitCondition = (busyCurrentValue != busyInitialValue);
		if(timeout)
		{
			exitCondition |= (currentTime >= timeout);
		}
	}

	/*Restore trigger pin GPIO value*/
	if(triggerDrivePolarity)
		CyU3PGpioSetValue(triggerPin, CyFalse);
	else
		CyU3PGpioSetValue(triggerPin, CyTrue);

	/*Add 0.5us (calibrated using DSLogic Pro)*/
	if(currentTime < (0xFFFFFFFF - 5))
	{
		currentTime = currentTime + 5;
	}
	else
	{
		currentTime = 0;
		rollOverCount++;
	}

	/* Populate bulk buffer with result */
	BulkBuffer[4] = currentTime & 0xFF;
	BulkBuffer[5] = (currentTime & 0xFF00) >> 8;
	BulkBuffer[6] = (currentTime & 0xFF0000) >> 16;
	BulkBuffer[7] = (currentTime & 0xFF000000) >> 24;
	BulkBuffer[8] = rollOverCount & 0xFF;
	BulkBuffer[9] = (rollOverCount & 0xFF00) >> 8;
	BulkBuffer[10] = (rollOverCount & 0xFF0000) >> 16;
	BulkBuffer[11] = (rollOverCount & 0xFF000000) >> 24;

	/* Return pulse wait data over ChannelToPC */
	AdiReturnBulkEndpointData(status, 12);

	return status;
}

/**
  * @brief Sets a user configurable trigger condition and then measures the following GPIO pulse.
  *
  * @param transferLength The amount of data (in bytes) to read from the USB buffer
  *
  * @return A status code indicating the success of the measure pulse operation
  *
  * This function is accurate to approximately 1/10th a microsecond. It can be used for timing measurements which
  * require a high degree of accuracy since it avoids the overhead of having a USB transaction (~100us)
  * between the initial trigger condition and the pulse measurement. Note, this function utilizes the
  * complex GPIO block on the FX3 to perform the busy pulse measurement. This gives very reliable
  * accuracy of measurement, since the measurement is performed entirely by hardware. However, this
  * does limit which pins can perform a busy pulse measurement, if there are PWM signals being driven
  * which also use the complex GPIO block.
 **/
CyU3PReturnStatus_t AdiMeasureBusyPulse(uint16_t transferLength)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint16_t *bytesRead = 0;
	uint8_t * spiBuf;
	uint16_t busyPin, triggerPin, SpiTriggerWordCount;
	CyBool_t exitCondition, SpiTriggerMode, busyPolarity, triggerPolarity;
	uint32_t currentTime, timeout, driveTime, result;
	CyU3PGpioSimpleConfig_t gpioConfig;
	CyU3PGpioComplexConfig_t busyPinConfig;

	/* Ensure variables are initialized to stop compiler from complainging */
	driveTime = 0;
	triggerPolarity = CyTrue;

	/* Read config data into USBBuffer */
	status = CyU3PUsbGetEP0Data(transferLength, USBBuffer, bytesRead);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(PinFunctions_c, __LINE__, status);
		return status;
	}

	/* Parse general request data from USBBuffer */
	busyPin = USBBuffer[0];
	busyPin |= (USBBuffer[1] << 8);
	busyPolarity = (CyBool_t) USBBuffer[2];
	timeout = USBBuffer[3];
	timeout |= (USBBuffer[4] << 8);
	timeout |= (USBBuffer[5] << 16);
	timeout |= (USBBuffer[6] << 24);

	/* Check that busy pin is valid GPIO */
	if(!AdiIsValidGPIO(busyPin))
	{
		status = CY_U3P_ERROR_BAD_ARGUMENT;
		/* Send status to the PC (alerting them of invalid GPIO selection) */
		AdiReturnBulkEndpointData(status, 8);
		return status;
	}

	/* Get the trigger mode */
	SpiTriggerMode = USBBuffer[7];

	/* Convert timeout (in ms) to timer ticks */
	if((timeout == 0) || (timeout > 426000))
	{
		/* Set max timeout */
		timeout = 0xFFFFFFFF;
	}
	else
	{
		timeout = timeout * MS_TO_TICKS_MULT;
	}

	/* Set default for trigger pin to invalid value */
	triggerPin = 0xFFFF;

	/* Set default result to invalid value */
	result = 0xFFFFFFFF;

	/* Set up busy pin config */
	busyPinConfig.driveHighEn = CyFalse;
	busyPinConfig.driveLowEn = CyFalse;
	busyPinConfig.inputEn = CyTrue;
	busyPinConfig.intrMode = CY_U3P_GPIO_NO_INTR;
	busyPinConfig.timerMode = CY_U3P_GPIO_TIMER_LOW_FREQ; /* 10MHz */
	busyPinConfig.pinMode = CY_U3P_GPIO_MODE_STATIC;
	busyPinConfig.period = 0xFFFFFFFF;
	busyPinConfig.pinMode = CY_U3P_GPIO_MODE_STATIC;
	CyU3PGpioDisable(busyPin);
	CyU3PDeviceGpioOverride(busyPin, CyFalse);
	status = CyU3PGpioSetComplexConfig(busyPin, &busyPinConfig);
	/* If setting up pulse measure hardware fails then return */
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(PinFunctions_c, __LINE__, status);
    	AdiReturnBulkEndpointData(status, 8);
    }

    /* Start measure operation */
    if(busyPolarity == CyFalse)
    	status = CyU3PGpioComplexMeasureOnce(busyPin, CY_U3P_GPIO_MODE_MEASURE_LOW_ONCE);
    else
    	status = CyU3PGpioComplexMeasureOnce(busyPin, CY_U3P_GPIO_MODE_MEASURE_HIGH_ONCE);

    /* If measure start fails then return */
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(PinFunctions_c, __LINE__, status);
    	AdiReturnBulkEndpointData(status, 8);
    }

	/* parse the trigger specific data and trigger */
	if(SpiTriggerMode)
	{
		/* Get the SPI trigger word count */
		SpiTriggerWordCount = USBBuffer[8];
		SpiTriggerWordCount |= (USBBuffer[9] << 8);

		/* Set the SPI buffer */
		spiBuf = USBBuffer + 10;

		/* Transmit the SPI words */
		CyU3PSpiTransmitWords(spiBuf, SpiTriggerWordCount);
	}
	else
	{
		/* parse parameters from USB Buffer */
		triggerPin = USBBuffer[8];
		triggerPin = triggerPin + (USBBuffer[9] << 8);

		/* Get drive polarity */
		triggerPolarity = USBBuffer[10];

		/* Get drive time (in ms) */
		driveTime = USBBuffer[11];
		driveTime = driveTime + (USBBuffer[12] << 8);
		driveTime = driveTime + (USBBuffer[13] << 16);
		driveTime = driveTime + (USBBuffer[14] << 24);

		/* convert drive time (ms) to ticks */
		driveTime = driveTime * MS_TO_TICKS_MULT;

		/* want to configure the trigger pin to act as an output */
		status = CyU3PDeviceGpioOverride(triggerPin, CyTrue);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(PinFunctions_c, __LINE__, status);
		}

		/* Disable the GPIO */
		status = CyU3PGpioDisable(triggerPin);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(PinFunctions_c, __LINE__, status);
		}

		/* Configure the pin to act as an output and drive */
		gpioConfig.outValue = triggerPolarity;
		gpioConfig.inputEn = CyFalse;
		gpioConfig.driveLowEn = CyTrue;
		gpioConfig.driveHighEn = CyTrue;
		gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
		status = CyU3PGpioSetSimpleConfig(triggerPin, &gpioConfig);
		if (status != CY_U3P_SUCCESS)
		{
			AdiLogError(PinFunctions_c, __LINE__, status);
		}
	}

	/* Reset the timeout timer register to 0 */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].timer = 0;
	/* Disable interrupts on the timer pin */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status &= ~(CY_U3P_LPP_GPIO_INTRMODE_MASK);
	/* Set the pin timer period to 0xFFFFFFFF */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].period = 0xFFFFFFFF;

	/* Reset state variables */
	currentTime = 0;
	exitCondition = CyFalse;

	/* Wait for the GPIO pin the reach the desired level or timeout */
	while(!exitCondition)
	{
		/* Set the pin config for sample now mode */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status = (FX3State.TimerPinConfig | (CY_U3P_GPIO_MODE_SAMPLE_NOW << CY_U3P_LPP_GPIO_MODE_POS));
		/* wait for sample to finish */
		while (GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_MODE_MASK);
		/* read timer value */
		currentTime = GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].threshold;

		/* Get result from pulse measure hardware */
		status = CyU3PGpioComplexWaitForCompletion(busyPin, &result, CyFalse);

		/* update the exit condition */
		exitCondition = ((currentTime >= timeout) || (status == CY_U3P_SUCCESS));

		/* Check if the pin drive can stop */
		if(!SpiTriggerMode)
		{
			if(currentTime > driveTime)
			{
				/* drive the opposite polarity */
				CyU3PGpioSimpleSetValue(triggerPin, ~triggerPolarity);
			}
			/* Set trigger mode to true to prevent this loop from hitting again */
			SpiTriggerMode = CyTrue;
		}
	}

	/* Add 0.1us onto measured time (calibrated using DSLogic Pro) */
	if(status == CY_U3P_SUCCESS)
		result += 1;

	/* Reset busy pin to simple input */
	gpioConfig.outValue = CyFalse;
	gpioConfig.inputEn = CyTrue;
	gpioConfig.driveLowEn = CyFalse;
	gpioConfig.driveHighEn = CyFalse;
	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
	CyU3PGpioDisable(busyPin);
	CyU3PDeviceGpioRestore(busyPin);
	CyU3PDeviceGpioOverride(busyPin, CyTrue);
	CyU3PGpioSetSimpleConfig(busyPin, &gpioConfig);
	/* Reset trigger pin to input if needed */
	if(triggerPin != 0xFFFF)
	{
		CyU3PGpioDisable(triggerPin);
		CyU3PGpioSetSimpleConfig(triggerPin, &gpioConfig);
	}

	/* Populate buffer with result data */
	BulkBuffer[4] = result & 0xFF;
	BulkBuffer[5] = (result & 0xFF00) >> 8;
	BulkBuffer[6] = (result & 0xFF0000) >> 16;
	BulkBuffer[7] = (result & 0xFF000000) >> 24;

	/* Send the data to PC */
	AdiReturnBulkEndpointData(status, 8);

	return status;
}

/**
  * @brief This function configures the FX3 PWM outputs (enable or disable).
  *
  * @param EnablePWM If the PWM should be enabled or disabled.
  *
  * @return A status code indicating the success of the function.
  *
  * The pin number, threshold value, and period are provided in the USBBuffer, and are
  * calculated in the FX3Api. This mitigates any potential math/rounding errors. The
  * PWM pins are driven by a 100MHz clock. The output is artificially capped at 10MHz.
 **/
CyU3PReturnStatus_t AdiConfigurePWM(CyBool_t EnablePWM)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint16_t pinNumber;
	uint32_t threshold, period;
	CyU3PGpioComplexConfig_t gpioComplexConfig = {0};
	CyU3PGpioSimpleConfig_t gpioConfig = {0};

	/* Get the pin number */
	pinNumber = USBBuffer[0];
	pinNumber |= (USBBuffer[1] << 8);

	/* Check that GPIO is valid */
	if(!AdiIsValidGPIO(pinNumber))
	{
		return CY_U3P_ERROR_BAD_ARGUMENT;
	}

	if(EnablePWM)
	{
		/* get the period */
		period = USBBuffer[2];
		period |= (USBBuffer[3] << 8);
		period |= (USBBuffer[4] << 16);
		period |= (USBBuffer[5] << 24);

		/* get the threshold */
		threshold = USBBuffer[6];
		threshold |= (USBBuffer[7] << 8);
		threshold |= (USBBuffer[8] << 16);
		threshold |= (USBBuffer[9] << 24);

#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "Setting up PWM with period %d, threshold %d, for pin %d\r\n", period, threshold, pinNumber);
#endif

		/* Override the selected pin to run as a complex GPIO */
		status = CyU3PDeviceGpioOverride(pinNumber, CyFalse);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(PinFunctions_c, __LINE__, status);
			return status;
		}

		/* configure the selected pin in PWM mode */
		CyU3PMemSet((uint8_t *)&gpioComplexConfig, 0, sizeof (gpioComplexConfig));
		gpioComplexConfig.outValue = CyFalse;
		gpioComplexConfig.inputEn = CyFalse;
		gpioComplexConfig.driveLowEn = CyTrue;
		gpioComplexConfig.driveHighEn = CyTrue;
		gpioComplexConfig.pinMode = CY_U3P_GPIO_MODE_PWM;
		gpioComplexConfig.intrMode = CY_U3P_GPIO_NO_INTR;
		gpioComplexConfig.timerMode = CY_U3P_GPIO_TIMER_HIGH_FREQ;
		gpioComplexConfig.timer = 0;
		gpioComplexConfig.period = period;
		gpioComplexConfig.threshold = threshold;
		status = CyU3PGpioSetComplexConfig(pinNumber, &gpioComplexConfig);
	    if (status != CY_U3P_SUCCESS)
	    {
	    	AdiLogError(PinFunctions_c, __LINE__, status);
	    	return status;
	    }
	}
	else
	{
		/* Disable the GPIO */
		status = CyU3PGpioDisable(pinNumber);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(PinFunctions_c, __LINE__, status);
			return status;
		}

		CyU3PDeviceGpioRestore(pinNumber);

		/* want to reset the specified pin to simple state without output driven */
		status = CyU3PDeviceGpioOverride(pinNumber, CyTrue);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(PinFunctions_c, __LINE__, status);
			return status;
		}

		/* Set the GPIO configuration for the GPIO that was just overridden */
		CyU3PMemSet ((uint8_t *)&gpioConfig, 0, sizeof (gpioConfig));
		gpioConfig.outValue = CyFalse;
		gpioConfig.inputEn = CyTrue;
		gpioConfig.driveLowEn = CyFalse;
		gpioConfig.driveHighEn = CyFalse;
		gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;

		status = CyU3PGpioSetSimpleConfig(pinNumber, &gpioConfig);
	    if (status != CY_U3P_SUCCESS)
	    {
	    	AdiLogError(PinFunctions_c, __LINE__, status);
	    	return status;
	    }
	}
	return status;
}

/**
  * @brief This function drives a GPIO pin for a specified number of milliseconds, then returns it to the starting polarity.
  *
  * @return A status code indicating the success of the function.
  *
  * If the selected GPIO pin is not configured as an output, this function configures the pin. If you want the pin to stay at a
  * given logic level, use AdiSetPin() instead. The arguments to this function are passed in through USBBuffer.
  * pin: The GPIO pin number to drive
  * polarity: The polarity of the pin (True - High, False - Low)
  * driveTime: The number of milliseconds to drive the pin for
 **/
CyU3PReturnStatus_t AdiPulseDrive()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint16_t pinNumber;
	CyBool_t polarity, exit;
	uint32_t timerTicks, timerRollovers, rolloverCount, currentTime, lastTime;
	CyU3PGpioSimpleConfig_t gpioConfig = {0};

	/* Parse request data from USBBuffer */
	pinNumber = USBBuffer[0];
	pinNumber = pinNumber + (USBBuffer[1] << 8);
	polarity = (CyBool_t) USBBuffer[2];
	timerTicks = USBBuffer[3];
	timerTicks = timerTicks + (USBBuffer[4] << 8);
	timerTicks = timerTicks + (USBBuffer[5] << 16);
	timerTicks = timerTicks + (USBBuffer[6] << 24);
	timerRollovers = USBBuffer[7];
	timerRollovers = timerRollovers + (USBBuffer[8] << 8);
	timerRollovers = timerRollovers + (USBBuffer[9] << 16);
	timerRollovers = timerRollovers + (USBBuffer[10] << 24);

	/*Verify the pin number */
	if(!AdiIsValidGPIO(pinNumber))
	{
		CyU3PDebugPrint (4, "Error! Invalid GPIO pin number: %d\r\n", pinNumber);
		return CY_U3P_ERROR_BAD_ARGUMENT;
	}

	/* Configure the GPIO pin as a driven output */
	gpioConfig.outValue = polarity;
	gpioConfig.inputEn = CyFalse;
	gpioConfig.driveLowEn = CyTrue;
	gpioConfig.driveHighEn = CyTrue;
	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
	status = CyU3PGpioSetSimpleConfig(pinNumber, &gpioConfig);

	/* Reset the pin timer register to 0 */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].timer = 0;
	/* Disable interrupts on the timer pin */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status &= ~(CY_U3P_LPP_GPIO_INTRMODE_MASK);
	/* Set the pin timer period to 0xFFFFFFFF */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].period = 0xFFFFFFFF;

	/* If config fails try to disable and reconfigure */
	if(status != CY_U3P_SUCCESS)
	{
		CyU3PGpioDisable(pinNumber);
		CyU3PDeviceGpioOverride(pinNumber, CyTrue);
		status = CyU3PGpioSetSimpleConfig(pinNumber, &gpioConfig);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(PinFunctions_c, __LINE__, status);
			return status;
		}
	}

	exit = CyFalse;
	rolloverCount = 0;
	currentTime = 0;
	lastTime = 0;
	while(!exit)
	{
		/* Store previous time */
		lastTime = currentTime;

		/* Set the pin config for sample now mode */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status = (FX3State.TimerPinConfig | (CY_U3P_GPIO_MODE_SAMPLE_NOW << CY_U3P_LPP_GPIO_MODE_POS));
		/* wait for sample to finish */
		while (GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_MODE_MASK);
		/* read timer value */
		currentTime = GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].threshold;

		/* Check if roll over occurred */
		if(currentTime < lastTime)
		{
			rolloverCount++;
		}

		exit = (currentTime >= timerTicks) && (rolloverCount >= timerRollovers);
	}

	/* Set the pin to opposite polarity */
	CyU3PGpioSetValue(pinNumber, !polarity);

	/* Configure the selected pin as input and tristate */
	CyU3PDeviceGpioOverride(pinNumber, CyTrue);

	/* Disable the GPIO */
	CyU3PGpioDisable(pinNumber);

	gpioConfig.outValue = CyFalse;
	gpioConfig.inputEn = CyTrue;
	gpioConfig.driveLowEn = CyFalse;
	gpioConfig.driveHighEn = CyFalse;
	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;

	status = CyU3PGpioSetSimpleConfig(pinNumber, &gpioConfig);

	/* return the status */
	return status;
}

/**
  * @brief This function waits for a pin to reach a selected logic level. The PulseWait parameters
  * are passed in the USB buffer, and are retrieved with a call to GetEP0Data.
  *
  * @param transferLength How many bytes to read from the USBBuffer.
  *
  * @return A status code indicating the success of the function.
  *
  * The time waited and status information are sent to the PC over the bulk out endpoint at the
  * end of this function. If you want to collect very accurate timing measurements using the FX3,
  * consider using the AdiMeasureBusyPulse function instead.
  * pin is the GPIO pin number to poll
  * polarity is the pin polarity which will trigger an exit condition
  * delay is the wait time (in ms) from when the function starts before pin polling starts
  * timeout is the time (in ms) to wait for the pin level before exiting
 **/
CyU3PReturnStatus_t AdiPulseWait(uint16_t transferLength)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint16_t pin;
    uint16_t *bytesRead = 0;
	CyBool_t polarity, pinValue, exitCondition;
	uint32_t currentTime, lastTime, delay, timeoutTicks, timeoutRollover, rollOverCount;
	CyU3PGpioSimpleConfig_t gpioConfig = {0};

	/* Disable interrupts on the timer pin */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status &= ~(CY_U3P_LPP_GPIO_INTRMODE_MASK);
	/* Set the pin timer period to 0xFFFFFFFF */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].period = 0xFFFFFFFF;
	/* Reset the pin timer register to 0 */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].timer = 0;

	/* Read config data into USBBuffer */
	CyU3PUsbGetEP0Data(transferLength, USBBuffer, bytesRead);

	/* Parse request data from USBBuffer */
	pin = USBBuffer[0];
	pin = pin + (USBBuffer[1] << 8);
	polarity = (CyBool_t) USBBuffer[2];
	delay = USBBuffer[3];
	delay = delay + (USBBuffer[4] << 8);
	delay = delay + (USBBuffer[5] << 16);
	delay = delay + (USBBuffer[6] << 24);
	timeoutTicks = USBBuffer[7];
	timeoutTicks = timeoutTicks + (USBBuffer[8] << 8);
	timeoutTicks = timeoutTicks + (USBBuffer[9] << 16);
	timeoutTicks = timeoutTicks + (USBBuffer[10] << 24);
	timeoutRollover = USBBuffer[11];
	timeoutRollover = timeoutRollover + (USBBuffer[12] << 8);
	timeoutRollover = timeoutRollover + (USBBuffer[13] << 16);
	timeoutRollover = timeoutRollover + (USBBuffer[14] << 24);

	/* Convert ms to timer ticks */
	delay = delay * MS_TO_TICKS_MULT;

	/* Check that input pin specified is configured as input */
	status = CyU3PGpioSimpleGetValue(pin, &pinValue);
	if(status != CY_U3P_SUCCESS)
	{
		/* If initial pin read fails try and configure as input */
		gpioConfig.outValue = CyFalse;
		gpioConfig.inputEn = CyTrue;
		gpioConfig.driveLowEn = CyFalse;
		gpioConfig.driveHighEn = CyFalse;
		gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
		status = CyU3PGpioSetSimpleConfig(pin, &gpioConfig);

		/* get the pin value again after configuring */
		status = CyU3PGpioSimpleGetValue(pin, &pinValue);

		/* If pin setup not successful skip wait operation and return -1 */
		if(status != CY_U3P_SUCCESS)
		{
			/* Send status code back to PC */
			AdiReturnBulkEndpointData(status, 12);
			return status;
		}
	}

	/* Wait for the delay, if needed */
	currentTime = 0;
	rollOverCount = 0;
	lastTime = 0;
	exitCondition = CyFalse;
	if(delay > 0)
	{
		while(currentTime < delay)
		{
			/* Set the pin config for sample now mode */
			GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status = (FX3State.TimerPinConfig | (CY_U3P_GPIO_MODE_SAMPLE_NOW << CY_U3P_LPP_GPIO_MODE_POS));
			/* wait for sample to finish */
			while (GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_MODE_MASK);
			/* read timer value */
			currentTime = GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].threshold;
		}
	}

	/* Wait for the GPIO pin the reach the desired level or timeout */
	while(!exitCondition)
	{
		/* Store previous time */
		lastTime = currentTime;

		/* Set the pin config for sample now mode */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status = (FX3State.TimerPinConfig | (CY_U3P_GPIO_MODE_SAMPLE_NOW << CY_U3P_LPP_GPIO_MODE_POS));
		/* wait for sample to finish */
		while (GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_MODE_MASK);
		/* read timer value */
		currentTime = GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].threshold;

		/* Check if rollover occured */
		if(currentTime < lastTime)
		{
			rollOverCount++;
		}

		/* Read the pin value */
		pinValue = ((GPIO->lpp_gpio_simple[pin] & CY_U3P_LPP_GPIO_IN_VALUE) >> 1);

		/* update the exit condition (will always have valid timeout)
		 * exits when pin reaches the desired polarity or timer reaches timeout */
		exitCondition = ((pinValue == polarity) || ((currentTime >= timeoutTicks) && (rollOverCount >= timeoutRollover)));
	}

	/* Catch potential out of bounds status code */
	if(status > CY_U3P_ERROR_MEDIA_FAILURE)
	{
		status = CY_U3P_ERROR_NOT_SUPPORTED;
	}

	/* Populate bulk buffer with the function results */
	BulkBuffer[4] = currentTime & 0xFF;
	BulkBuffer[5] = (currentTime & 0xFF00) >> 8;
	BulkBuffer[6] = (currentTime & 0xFF0000) >> 16;
	BulkBuffer[7] = (currentTime & 0xFF000000) >> 24;
	BulkBuffer[8] = rollOverCount & 0xFF;
	BulkBuffer[9] = (rollOverCount & 0xFF00) >> 8;
	BulkBuffer[10] = (rollOverCount & 0xFF0000) >> 16;
	BulkBuffer[11] = (rollOverCount & 0xFF000000) >> 24;

	/* Send the data to PC via bulk endpoint */
	AdiReturnBulkEndpointData(status, 12);

	/* return status code */
	return status;
}

/**
  * @brief This function configures the specified pin as an output and drives it with the desired value.
  *
  * @param pinNumber The GPIO index of the pin to be set
  *
  * @param polarity The polarity of the pin to be set (True - High, False - Low)
  *
  * @return A status code indicating the success of the pin set operation.
  *
  * Take care when using this function. If a valid GPIO which has not been configured as an output
  * is selected, that pin will be forced to act as an output. This will cause pins which have
  * functionality beyond just a simple GPIO can lose that functionality (e.g. UART Debug, SPI, etc).
 **/
CyU3PReturnStatus_t AdiSetPin(uint16_t pinNumber, CyBool_t polarity)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	CyU3PGpioSimpleConfig_t gpioConfig = {0};

	if(!AdiIsValidGPIO(pinNumber))
		return CY_U3P_ERROR_BAD_ARGUMENT;

	/*Sanitize polarity */
	if(polarity)
		polarity = CyTrue;
	else
		polarity = CyFalse;

#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "Setting pin %d to %d\r\n", pinNumber, polarity);
#endif

	/* Configure pin as output and set the drive value */
	gpioConfig.outValue = polarity;
	gpioConfig.inputEn = CyFalse;
	gpioConfig.driveLowEn = CyTrue;
	gpioConfig.driveHighEn = CyTrue;
	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
	status = CyU3PGpioSetSimpleConfig(pinNumber, &gpioConfig);
	if(status != CY_U3P_SUCCESS)
	{
		CyU3PGpioDisable(pinNumber);
		CyU3PDeviceGpioOverride(pinNumber, CyTrue);
		status = CyU3PGpioSetSimpleConfig(pinNumber, &gpioConfig);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(PinFunctions_c, __LINE__, status);
		}
	}
	return status;
}

/**
  * @brief This function blocks the execution of the current thread until an event happens on the
  * specified GPIO pin.
  *
  * @param pinNumber The GPIO pin number to poll
  *
  * @param interruptSetting The simple GPIO interrupt mode that the selected pin is configured with.
  *
  * @param timeoutTicks The number of GPIO timer ticks (10MHz) to wait for before timing out and returning.
  *
  * @return A status code indicating the success of the pin wait function.
 **/
CyU3PReturnStatus_t AdiWaitForPin(uint32_t pinNumber, CyU3PGpioIntrMode_t interruptSetting, uint32_t timeoutTicks)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint32_t gpioEventFlag = 0;
	CyU3PGpioSimpleConfig_t gpioConfig = {0};

	/* Configure the specified pin as an input and attach the correct pin interrupt */
	gpioConfig.outValue = CyTrue;
	gpioConfig.inputEn = CyTrue;
	gpioConfig.driveLowEn = CyFalse;
	gpioConfig.driveHighEn = CyFalse;
	gpioConfig.intrMode = interruptSetting;
	status = CyU3PGpioSetSimpleConfig(pinNumber, &gpioConfig);

	/* Catch unspecified timeout */
	if (timeoutTicks == 0)
	{
		timeoutTicks = CYU3P_WAIT_FOREVER;
	}

	if (status == CY_U3P_SUCCESS)
	{
		/* Enable GPIO interrupts (in case it's not enabled) */
		CyU3PVicEnableInt(CY_U3P_VIC_GPIO_CORE_VECTOR);
		/* Wait for GPIO interrupt flag */
		status = CyU3PEventGet(&GpioHandler, pinNumber, CYU3P_EVENT_OR_CLEAR, &gpioEventFlag, timeoutTicks);
		/* Disable GPIO interrupts until we need them again */
		CyU3PVicDisableInt(CY_U3P_VIC_GPIO_CORE_VECTOR);
	}
	return status;
}

/**
  * @brief Converts milliseconds to number of ticks and adjusts the resulting offset if below the measurable minimum.
  *
  * @param timeInMS: The real stall time (in ms) desired.
  *
  * @return The number of timer ticks representing that MS value.
 **/
uint32_t AdiMStoTicks(uint32_t timeInMS)
{
	return timeInMS * MS_TO_TICKS_MULT;
}

/**
  * @brief This function handles Pin read control end point requests.
  *
  * @param pin The GPIO matrix number of the pin to read
  *
  * @return The success of the pin read operation
  *
  * This function reads the value of a specified GPIO pin, and sends that value over the
  * control endpoint to the host PC. The pin read status is also attached.
 **/
CyU3PReturnStatus_t AdiPinRead(uint16_t pin)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	CyBool_t pinValue = CyFalse;
	CyU3PGpioSimpleConfig_t gpioConfig = {0};

	if(AdiIsValidGPIO(pin))
	{
		/* Configure pin as input and sample the pin value */
		gpioConfig.outValue = CyFalse;
		gpioConfig.inputEn = CyTrue;
		gpioConfig.driveLowEn = CyFalse;
		gpioConfig.driveHighEn = CyFalse;
		gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
		status = CyU3PGpioSetSimpleConfig(pin, &gpioConfig);
		/* If the config is successful, read the pin value */
		if(status == CY_U3P_SUCCESS)
		{
			status = CyU3PGpioSimpleGetValue(pin, &pinValue);
		}
		else
		{
			CyU3PGpioDisable(pin);
			CyU3PDeviceGpioOverride(pin, CyTrue);
			status = CyU3PGpioSetSimpleConfig(pin, &gpioConfig);
			if(status == CY_U3P_SUCCESS)
			{
				status = CyU3PGpioSimpleGetValue(pin, &pinValue);
			}
			else
				AdiLogError(PinFunctions_c, __LINE__, status);
		}
	}
	else
	{
		status = CY_U3P_ERROR_BAD_ARGUMENT;
	}

#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "Pin %d value: %d\r\n", pin, pinValue);
#endif

	/* Put pin register value in output buffer */
	USBBuffer[0] = pinValue;
	USBBuffer[1] = status & 0xFF;
	USBBuffer[2] = (status & 0xFF00) >> 8;
	USBBuffer[3] = (status & 0xFF0000) >> 16;
	USBBuffer[4] = (status & 0xFF000000) >> 24;
	/* Send the pin value */
	CyU3PUsbSendEP0Data (5, (uint8_t *)USBBuffer);
	return status;
}

/**
  * @brief Reads the current value of the 10MHz timer (32 bit)
  *
  * @return The number of elapsed timer ticks
 **/
uint32_t AdiReadTimerRegValue()
{
	/* Set config for sample now mode */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status = (FX3State.TimerPinConfig | (CY_U3P_GPIO_MODE_SAMPLE_NOW << CY_U3P_LPP_GPIO_MODE_POS));
	/* Wait for sample to finish */
	while (GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_MODE_MASK);
	/* Return the threshold value (timer register value) */
	return GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].threshold;
}

/**
  * @brief Reads the current value from the complex GPIO timer and then sends the value over the control endpoint.
  *
  * @return The success of the timer read operation.
  *
  * This function handles timer read control endpoint requests specifically. May be changed to return the actual
  * timer value eventually.
 **/
CyU3PReturnStatus_t AdiReadTimerValue()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint32_t timerValue;

	status = CyU3PGpioComplexSampleNow(ADI_TIMER_PIN, &timerValue);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(PinFunctions_c, __LINE__, status);
	}
	USBBuffer[4] = timerValue & 0xFF;
	USBBuffer[5] = (timerValue & 0xFF00) >> 8;
	USBBuffer[6] = (timerValue & 0xFF0000) >> 16;
	USBBuffer[7] = (timerValue & 0xFF000000) >> 24;
	return status;
}

/**
  * @brief Measure the data ready frequency for a user specified pin
  *
  * @return The status of the pin drive operation
  *
  * This function measures two data ready pulses on a user-specified pin and reports
  * back the delta-time in ticks. The function also transmits the tick scale factor
  * and a timeout counter to notify the interface of timeouts that may have occurred
  * due to missing pulses. Data is transmitted over USB via the bulk endpoint. Inputs
  * are provided through the control endpoint. This function can be expanded to capture
  * as many samples as required.
  * pin: The GPIO pin number to measure
  * polarity: The polarity of the pin (1 - Low-to-High, 0 - High-to-Low)
  * timeoutInMs: The specified timeout in milliseconds
 **/
CyU3PReturnStatus_t AdiMeasurePinFreq()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	CyBool_t polarity, timeoutOccurred, interruptTriggered, exitCondition;
	uint16_t pin, numPeriods, periodCount;
	uint32_t timeoutTicks, timeoutRollovers, currentTime, lastTime, rollovers;
	CyU3PGpioSimpleConfig_t gpioConfig = {0};

	/* Parse data from the USB Buffer */
	pin = USBBuffer[0];
	pin |= (USBBuffer[1] << 8);
	polarity = USBBuffer[2];
	timeoutTicks = USBBuffer[3];
	timeoutTicks |= (USBBuffer[4] << 8);
	timeoutTicks |= (USBBuffer[5] << 16);
	timeoutTicks |= (USBBuffer[6] << 24);
	timeoutRollovers = USBBuffer[7];
	timeoutRollovers |= (USBBuffer[8] << 8);
	timeoutRollovers |= (USBBuffer[9] << 16);
	timeoutRollovers |= (USBBuffer[10] << 24);
	numPeriods = USBBuffer[11];
	numPeriods |= (USBBuffer[12] << 8);

	/* Disable relevant interrupts */
	CyU3PVicDisableInt(CY_U3P_VIC_GCTL_PWR_VECTOR);
	CyU3PVicDisableInt(CY_U3P_VIC_GPIO_CORE_VECTOR);

	/* Configure pin as an input, with interrupts set on the desired polarity */
	AdiConfigurePinInterrupt(pin, polarity);

	/* Reset timer value */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].timer = 0;
	/* Disable interrupts on the timer pin */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status &= ~(CY_U3P_LPP_GPIO_INTRMODE_MASK);
	/* Set the pin timer period to 0xFFFFFFFF */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].period = 0xFFFFFFFF;

	currentTime = 0;
	lastTime = 0;
	rollovers = 0;
	timeoutOccurred = CyFalse;
	interruptTriggered = CyFalse;
	/* Clear GPIO interrupts */
	GPIO->lpp_gpio_simple[FX3State.DrPin] |= CY_U3P_LPP_GPIO_INTR;
	/* Wait for edge, checking timeout as well */
	while(!(interruptTriggered | timeoutOccurred))
	{
		interruptTriggered = GPIO->lpp_gpio_intr0 & (1 << pin);
		if(interruptTriggered)
		{
			/* Reset the timer and clear bit*/
			GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].timer = 0;
			GPIO->lpp_gpio_simple[pin] |= CY_U3P_LPP_GPIO_INTR;
		}
		else
		{
			/* Get the new time values */
			lastTime = currentTime;

			/* Set the pin config for sample now mode */
			GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status = (FX3State.TimerPinConfig | (CY_U3P_GPIO_MODE_SAMPLE_NOW << CY_U3P_LPP_GPIO_MODE_POS));
			/* Wait for sample to finish */
			while (GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_MODE_MASK);
			/* Read timer value */
			currentTime = GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].threshold;

			/* Check if roll over occurred */
			if(currentTime < lastTime)
			{
				rollovers++;
			}

			/* Determine if a timeout has occurred */
			timeoutOccurred = (currentTime >= timeoutTicks) && (rollovers >= timeoutRollovers);
		}
	}

	/* Reset counters */
	currentTime = 0;
	lastTime = 0;
	rollovers = 0;
	periodCount = 0;
	interruptTriggered = CyFalse;

	/* Set initial exit condition */
	exitCondition = timeoutOccurred;

	/* Wait for specified number of edges, or timeout */
	while(!exitCondition)
	{
		/* Check interrupt status */
		interruptTriggered = GPIO->lpp_gpio_intr0 & (1 << pin);
		if(interruptTriggered)
		{
			/*Increment counter and clear the interrupt bit */
			periodCount++;
			GPIO->lpp_gpio_simple[pin] |= CY_U3P_LPP_GPIO_INTR;
		}

		/* Save the last timer value */
		lastTime = currentTime;

		/* Get the new timer value */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status = (FX3State.TimerPinConfig | (CY_U3P_GPIO_MODE_SAMPLE_NOW << CY_U3P_LPP_GPIO_MODE_POS));
		while (GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_MODE_MASK);
		currentTime = GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].threshold;

		/* Check if rollover occured */
		if(currentTime < lastTime)
		{
			rollovers++;
		}

		/* Determine if a timeout has occurred */
		timeoutOccurred = (currentTime >= timeoutTicks) && (rollovers >= timeoutRollovers);

		/* Determine the exit condition */
		exitCondition = timeoutOccurred || (periodCount >= numPeriods);
	}

	/* add 0.8us to current time (fudge factor, calibrated using DSLogic Pro) */
	if(currentTime < (0xFFFFFFFF - 8))
	{
		currentTime = currentTime + 8;
	}
	else
	{
		currentTime = 0;
		rollovers++;
	}

	/* Set error flags if a timeout occurred (status = timeout error) */
	if(timeoutOccurred)
	{
		status = CY_U3P_ERROR_TIMEOUT;
	}

	/* Disable interrupt mode on the pin */
	gpioConfig.outValue = CyTrue;
	gpioConfig.inputEn = CyTrue;
	gpioConfig.driveLowEn = CyFalse;
	gpioConfig.driveHighEn = CyFalse;
	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
	CyU3PGpioSetSimpleConfig(pin, &gpioConfig);

	/* Re-enable relevant ISRs */
	CyU3PVicEnableInt(CY_U3P_VIC_GPIO_CORE_VECTOR);
	CyU3PVicEnableInt(CY_U3P_VIC_GCTL_PWR_VECTOR);

	/* Populate bulk buffer with function results */
	BulkBuffer[4] = currentTime & 0xFF;
	BulkBuffer[5] = (currentTime & 0xFF00) >> 8;
	BulkBuffer[6] = (currentTime & 0xFF0000) >> 16;
	BulkBuffer[7] = (currentTime & 0xFF000000) >> 24;
	BulkBuffer[8] = rollovers & 0xFF;
	BulkBuffer[9] = (rollovers & 0xFF00) >> 8;
	BulkBuffer[10] = (rollovers & 0xFF0000) >> 16;
	BulkBuffer[11] = (rollovers & 0xFF000000) >> 24;

	/* Send the data to PC */
	AdiReturnBulkEndpointData(status, 12);

	/* return status code */
	return status;
}

/**
  * @brief configures the selected pin as an interrupt with edge triggering based on polarity
  *
  * @param polarity The edge to trigger on (true -> rising edge, false -> falling edge)
  *
  * @param pin The GPIO pin number to configure
  *
  * @return The success of the pin configuration operation.
 **/
CyU3PReturnStatus_t AdiConfigurePinInterrupt(uint16_t pin, CyBool_t polarity)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	CyU3PGpioSimpleConfig_t gpioConfig = {0};

	/* Make sure the data ready pin is configured as an input and attach the correct pin interrupt */
	gpioConfig.outValue = CyTrue;
	gpioConfig.inputEn = CyTrue;
	gpioConfig.driveLowEn = CyFalse;
	gpioConfig.driveHighEn = CyFalse;
	if (polarity)
	{
		gpioConfig.intrMode = CY_U3P_GPIO_INTR_POS_EDGE;
	}
	else
	{
		gpioConfig.intrMode = CY_U3P_GPIO_INTR_NEG_EDGE;
	}

	status = CyU3PGpioSetSimpleConfig(pin, &gpioConfig);
	if(status != CY_U3P_SUCCESS)
	{
		/* Override the pin to act as simple GPIO */
		CyU3PDeviceGpioOverride(pin, CyTrue);
		/* Set the config again */
		status = CyU3PGpioSetSimpleConfig(pin, &gpioConfig);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(PinFunctions_c, __LINE__, status);
		}
	}
	return status;
}
