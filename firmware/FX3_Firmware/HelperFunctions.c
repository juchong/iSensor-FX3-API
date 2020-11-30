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
  * @file		HelperFunctions.c
  * @date		6/17/2020
  * @author		A. Nolan (alex.nolan@analog.com)
  * @brief 		Implementation for a set of general purpose iSensor FX3 helper functions
 **/

#ifndef HELPERFUNCTIONS_C_
#define HELPERFUNCTIONS_C_

#include "HelperFunctions.h"

/* Private function prototypes */
static void WatchDogTimerCb (uint32_t nParam);

/* Tell compiler where to find needed globals */
extern CyU3PDmaChannel ChannelToPC;
extern CyU3PDmaBuffer_t ManualDMABuffer;
extern BoardState FX3State;
extern uint8_t USBBuffer[4096];
extern uint8_t BulkBuffer[12288];

/** Software timer called by RTOS to clear watchdog timer (if watchdog enabled) */
static CyU3PTimer WatchdogTimer = {0};

/**
  * @brief Sends a function result to the PC via the ChannelToPC endpoint
  *
  * @param status The status code to place in the BulkEndpointBuffer (0 - 3)
  *
  * @param length The number of bytes to send to the PC over the bulk endpoint
  *
  * @return void
  *
  * This function is used to allow early returns out of long functions in the
  * case where an invalid setting or operation is detected. Once this function
  * is called, and the result sent to the PC, the function can be safely exited.
 **/
void AdiReturnBulkEndpointData(CyU3PReturnStatus_t status, uint16_t length)
{
	/* Load status to BulkBuffer */
	BulkBuffer[0] = status & 0xFF;
	BulkBuffer[1] = (status & 0xFF00) >> 8;
	BulkBuffer[2] = (status & 0xFF0000) >> 16;
	BulkBuffer[3] = (status & 0xFF000000) >> 24;

	/* Configure manual DMA */
	ManualDMABuffer.buffer = BulkBuffer;
	ManualDMABuffer.size = sizeof(BulkBuffer);
	ManualDMABuffer.count = length;

	/* Send the data to PC */
	CyU3PDmaChannelSetupSendBuffer(&ChannelToPC, &ManualDMABuffer);
}

/**
  * @brief This function blocks thread execution for a specified number of microseconds.
  *
  * @param numMicroSeconds The number of microseconds to stall for.
  *
  * @return A status code indicating the success of the function.
 **/
CyU3PReturnStatus_t AdiSleepForMicroSeconds(uint32_t numMicroSeconds)
{
	if(numMicroSeconds < 2 || numMicroSeconds > 0xFFFFFFFF)
	{
		return CY_U3P_ERROR_BAD_ARGUMENT;
	}
	CyFx3BusyWait(numMicroSeconds - 2);
	return CY_U3P_SUCCESS;
}

/**
  * @brief This function configures the DUT supply voltage.
  *
  * @param SupplyMode The DUT Voltage to set (Off, 3.3V, 5V)
  *
  * @returns A status code indicating the success of the functions.
  *
  * This function sets the control pins on the LTC1470 power switch IC. This IC allows software to
  * power cycle a DUT or give it 3.3V/5V to Vdd. This feature only works on the in-house ADI iSensors
  * FX3 Eval board. It does not function on the iSensor FX3 Eval board based on the Cypress Explorer kit.
 **/
CyU3PReturnStatus_t AdiSetDutSupply(DutVoltage SupplyMode)
{
	CyU3PReturnStatus_t status0, status1 = CY_U3P_SUCCESS;
	CyU3PGpioSimpleConfig_t gpioConfig = {0};

	/* Set base config values */
	gpioConfig.outValue = CyTrue;
	gpioConfig.inputEn = CyFalse;
	gpioConfig.driveLowEn = CyTrue;
	gpioConfig.driveHighEn = CyTrue;
	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;

#ifdef VERBOSE_MODE
	CyU3PDebugPrint (4, "Setting power supply mode %d\r\n", SupplyMode);
#endif

	/* Check the DutVoltage value */
	switch(SupplyMode)
	{
	case Off:
		/* Set both high */
		status0 = CyU3PGpioSetSimpleConfig(ADI_5V_EN, &gpioConfig);
		status1 = CyU3PGpioSetSimpleConfig(ADI_3_3V_EN, &gpioConfig);
		break;

	case On3_3Volts:
		/* Set 5V high, 3.3V low */
		status0 = CyU3PGpioSetSimpleConfig(ADI_5V_EN, &gpioConfig);
		gpioConfig.outValue = CyFalse;
		status1 = CyU3PGpioSetSimpleConfig(ADI_3_3V_EN, &gpioConfig);
		break;

	case On5_0Volts:
		/* Set 3.3V high, 5V low */
		status1 = CyU3PGpioSetSimpleConfig(ADI_3_3V_EN, &gpioConfig);
		gpioConfig.outValue = CyFalse;
		status0 = CyU3PGpioSetSimpleConfig(ADI_5V_EN, &gpioConfig);
		break;

	default:
		/* Set both high to turn off power supply */
		status0 = CyU3PGpioSetSimpleConfig(ADI_5V_EN, &gpioConfig);
		status1 = CyU3PGpioSetSimpleConfig(ADI_3_3V_EN, &gpioConfig);
		/* Return invalid argument */
		AdiLogError(HelperFunctions_c, __LINE__, SupplyMode);
		return CY_U3P_ERROR_BAD_ARGUMENT;
	}

	/* Determine return code */
	if(status0)
		return status0;
	else if(status1)
		return status1;
	else
		return CY_U3P_SUCCESS;
}

/**
  * @brief Gets the firmware build date, followed by the build time
  *
  * @param outBuf Char array which build date/time is placed into
  *
  * @return void
 **/
void AdiGetBuildDate(uint8_t * outBuf)
{
	uint8_t date[11] = __DATE__;
	uint8_t time[8] = __TIME__;
	uint32_t index = 0;
	/* Assign date */
	for(index = 0; index < 11; index++)
	{
		outBuf[index] = date[index];
	}
	outBuf[11] = ' ';
	/* Assign time */
	for(index = 12; index < 20; index++)
	{
		outBuf[index] = time[index - 12];
	}
	outBuf[20] = '\0';
}

/**
  * @brief Sends status back to PC over control endpoint or manual bulk in endpoint
  *
  * @param status The status code to send back. Is placed in USBBuffer[0-3]
  *
  * @param count The number of bytes to send. Must be at least 4
  *
  * @param isControlEndpoint Bool indicating if data should be sent over control endpoint or bulk endpoint
  *
  * @returns void
  *
  * This function will overwrite the data in USBBuffer[0-3]. If you need to send extra
  * data along with the status, it must be placed starting at USBBuffer[4].
 **/
void AdiSendStatus(uint32_t status, uint16_t count, CyBool_t isControlEndpoint)
{
	/* Clamp count */
	if(count < 4)
		count = 4;

	/* Load status to USB Buffer */
	USBBuffer[0] = status & 0xFF;
	USBBuffer[1] = (status & 0xFF00) >> 8;
	USBBuffer[2] = (status & 0xFF0000) >> 16;
	USBBuffer[3] = (status & 0xFF000000) >> 24;

	/* Perform transfer */
	if(isControlEndpoint)
	{
		CyU3PUsbSendEP0Data(count, USBBuffer);
	}
	else
	{
		ManualDMABuffer.buffer = USBBuffer;
		ManualDMABuffer.size = 4096;
		ManualDMABuffer.count = count;
		CyU3PDmaChannelSetupSendBuffer(&ChannelToPC, &ManualDMABuffer);
	}
}

/**
  * @brief Configures the FX3 watchdog timer based on the current board state.
  *
  * @returns void
  *
  * The watchdog is cleared by a software timer managed in the threadX RTOS. The clear interval is set to 5 seconds
  * less than the watchdog period. If the watchdog timer elapses without being reset (software is locked up) then the
  * FX3 firmware undergoes a hard reset and will reboot onto the second stage bootloader. This will cause an
  * UnexpectedReset event to be raised in the running instance of the FX3Connection (if the FX3 board is connected).
 **/
void AdiConfigureWatchdog()
{
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

	/* configure the watchdog */
	CyU3PSysWatchDogConfigure(FX3State.WatchDogEnabled, FX3State.WatchDogPeriodMs);

	/* Calculate watchdog ticks */
	FX3State.WatchDogTicks = FX3State.WatchDogPeriodMs * 33;

	if(FX3State.WatchDogEnabled)
	{
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "Enabling Watchdog Timer, period %d ms\r\n", FX3State.WatchDogPeriodMs);
#endif
		/* Calculate the watchdog clear period - 5 seconds less than the watchdog timeout */
		uint32_t clearPeriod = FX3State.WatchDogPeriodMs - 5000;

		/* Destroy existing watchdog timer */
		CyU3PTimerDestroy(&WatchdogTimer);

		/* Create new watchdog timer with the correct parameters */
		status = CyU3PTimerCreate(&WatchdogTimer, WatchDogTimerCb, 0, clearPeriod, clearPeriod, CYU3P_AUTO_ACTIVATE);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(HelperFunctions_c, __LINE__, status);
			CyU3PSysWatchDogConfigure(CyFalse, FX3State.WatchDogPeriodMs);
		}
	}
	else
	{
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "Disabling Watchdog Timer\r\n");
#endif
		/* destroy timer */
		status = CyU3PTimerDestroy(&WatchdogTimer);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(HelperFunctions_c, __LINE__, status);
		}
	}
}

/**
  * @brief Timer callback function to clear the watchdog timer. Should not be called directly.
  *
  * @param nParam Callback argument, unused here.
  *
  * @return void
  *
  * This function is called periodically by the RTOS to reset the watchdog timer. If this function
  * is not called, then the FX3 will be rebooted onto the second stage bootloader.
 **/
static void WatchDogTimerCb(uint32_t nParam)
{
	/* Unused RTOS software timer arg */
	UNUSED(nParam);
	/* Reset the watchdog timer to the full period length */
	if (FX3State.WatchDogTicks & 0x01)
		FX3State.WatchDogTicks--;
	else
		FX3State.WatchDogTicks++;
	GCTLAON->watchdog_timer0 = FX3State.WatchDogTicks;
}

#endif /* HELPERFUNCTIONS_C_ */
