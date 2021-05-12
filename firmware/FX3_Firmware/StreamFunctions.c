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
  * @file		StreamFunctions.c
  * @date		8/1/2019
  * @author		A. Nolan (alex.nolan@analog.com)
  * @author 	J. Chong (juan.chong@analog.com)
  * @brief		This file contains all functions used to start/stop data streams from a DUT.
 **/

#include "StreamFunctions.h"

/* Tell the compiler where to find the needed globals */
extern CyU3PEvent EventHandler;
extern CyU3PDmaChannel StreamingChannel;
extern CyU3PDmaChannel MemoryToSPI;
extern CyU3PDmaBuffer_t SpiDmaBuffer;
extern BoardState FX3State;
extern volatile CyBool_t KillStreamEarly;
extern StreamState StreamThreadState;

/** Global USB Buffer (Control Endpoint) */
extern uint8_t USBBuffer[4096];

/** Global USB Buffer (Bulk Endpoints) */
extern uint8_t BulkBuffer[12288];

/**
  * @brief Configures 10MHz timer to control stall time for generic or transfer streams.
  *
  * @return void
  *
  * This function sets the timer period and enables the timer interrupt as required for the stream. For
  * stall times less than the min (5 microseconds) the timer threshold is set to one timer tick.
 **/
void AdiConfigStreamStallTimer()
{
	/* Enable timer interrupts */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status &= (~CY_U3P_LPP_GPIO_INTRMODE_MASK);
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status |= CY_U3P_GPIO_INTR_TIMER_THRES << CY_U3P_LPP_GPIO_INTRMODE_POS;

	/* Set the timer pin threshold to correspond with the stall time */
	if((FX3State.StallTime * 10) < ADI_GENERIC_STALL_OFFSET)
	{
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].threshold = 1;
		/* Set the timer pin period (useful for error case, timer register is manually reset) */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].period = 2;
	}
	else
	{
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].threshold = (FX3State.StallTime * 10) - ADI_GENERIC_STALL_OFFSET;
		/* Set the timer pin period (useful for error case, timer register is manually reset) */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].period = (FX3State.StallTime * 10) - ADI_GENERIC_STALL_OFFSET + 1;
	}
}

/**
  * @brief This function sets a flag to notify the streaming thread that the user requested to cancel streaming.
  *
  * @return A status code indicating the success of the function.
  *
  * This function can be used to stop any stream operation. The variable KillStreamEarly is defined as
  * volatile to prevent any compiler optimizations which may remove it.
 **/
CyU3PReturnStatus_t AdiStopAnyDataStream()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

	/* Set the event mask to the stream enable events */
	uint32_t eventMask = ADI_GENERIC_STREAM_ENABLE|ADI_RT_STREAM_ENABLE|ADI_BURST_STREAM_ENABLE|ADI_TRANSFER_STREAM_ENABLE|ADI_I2C_STREAM_ENABLE;

	/* Variable to receive the event arguments into */
	uint32_t eventFlags = 0;

	/* Check if any streams are enabled */
	CyU3PEventGet(&EventHandler, eventMask, CYU3P_EVENT_OR, &eventFlags, CYU3P_NO_WAIT);

	/* If no events are set eventFlags will be 0 */
	if(eventFlags == 0)
	{
		status = CY_U3P_ERROR_NOT_STARTED;
	}

#ifdef VERBOSE_MODE
	CyU3PDebugPrint (4, "Stream cancel request received\r\n");
#endif

	/* Set kill stream early flag */
	KillStreamEarly = CyTrue;

	/* Return status over USB */
	AdiSendStatus(status, 4, CyTrue);

	/* return status code */
	return status;
}

/**
  * @brief This function prints all the stream state variables to the terminal if VERBOSE_MODE is defined
  *
  * @return A status code indicating if the print occurred (returns false if VERBOSE_MODE is not defined).
  *
  * This function prints a significant amount of data, and as such is fairly slow in VERBOSE_MODE. The return value of
  * this function can be used to determine if the running application firmware is set to verbose mode.
 **/
CyBool_t AdiPrintStreamState()
{
	CyBool_t verboseMode = CyFalse;

#ifdef VERBOSE_MODE
	verboseMode = CyTrue;
	CyU3PDebugPrint (4, "Endpoint Transfer Size: %d\r\n", StreamThreadState.TransferByteLength);
	CyU3PDebugPrint (4, "NumCaptures: %d NumBuffers: %d Bytes Per USB Packet: %d\r\n", StreamThreadState.NumCaptures, StreamThreadState.NumBuffers, StreamThreadState.BytesPerUsbPacket);
	CyU3PDebugPrint (4, "DrActive is %d, with the data ready pin set to GPIO[%d]. DrPolarity is %d\r\n", FX3State.DrActive, FX3State.DrPin, FX3State.DrPolarity);
	AdiPrintSpiConfig(AdiGetSpiConfig());
#endif

	return verboseMode;
}

/**
  * @brief Starts an I2C read stream.
  *
  * @return A status code indicating the success of the I2C stream start.
  *
  * This function reads the I2C stream start request in from the control
  * endpoint. It parses out the stream parameters from the buffer and
  * configures the Stream DMA channel for I2C -> USB, with an infinite
  * transfer.
 **/
CyU3PReturnStatus_t AdiI2CStreamStart()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint32_t timeout, index;
	uint16_t bytesRead;
	CyU3PDmaChannelConfig_t i2cDmaConfig;

	/* Get USB Data */
	CyU3PUsbGetEP0Data(StreamThreadState.TransferByteLength, USBBuffer, &bytesRead);

	/* Parse USB data (number of bytes placed in numcaptures) */
	index = I2CParseUSBBuffer(&timeout, &StreamThreadState.NumCaptures, &StreamThreadState.I2CStreamPreamble);

	/* Number of buffers to capture follows after I2C read stream request data */
	StreamThreadState.NumBuffers = USBBuffer[index];
	StreamThreadState.NumBuffers |= (USBBuffer[index + 1] << 8);
	StreamThreadState.NumBuffers |= (USBBuffer[index + 2] << 16);
	StreamThreadState.NumBuffers |= (USBBuffer[index + 3] << 24);

	/* Disable VBUS ISR */
	CyU3PVicDisableInt(CY_U3P_VIC_GCTL_PWR_VECTOR);

	/* Disable GPIO interrupt before attaching interrupt to pin */
	CyU3PVicDisableInt(CY_U3P_VIC_GPIO_CORE_VECTOR);

	/* Re-init I2C block in DMA mode */
	AdiI2CInit(FX3State.I2CBitRate, CyTrue);

	/* Configure data ready interrupts */
	if(FX3State.DrActive)
		AdiConfigureDrPin();

	/* Configure StreamChannel for I2C to USB automatic DMA */
    CyU3PMemSet ((uint8_t *)&i2cDmaConfig, 0, sizeof(i2cDmaConfig));
    i2cDmaConfig.size           = StreamThreadState.NumCaptures;
    i2cDmaConfig.count          = 16;
    i2cDmaConfig.prodAvailCount = 0;
    i2cDmaConfig.dmaMode        = CY_U3P_DMA_MODE_BYTE;
    i2cDmaConfig.prodHeader     = 0;
    i2cDmaConfig.prodFooter     = 0;
    i2cDmaConfig.consHeader     = 0;
    i2cDmaConfig.notification   = 0;
    i2cDmaConfig.cb             = NULL;
    i2cDmaConfig.prodSckId = CY_U3P_LPP_SOCKET_I2C_PROD;
    i2cDmaConfig.consSckId = CY_U3P_UIB_SOCKET_CONS_1;
    status = CyU3PDmaChannelCreate(&StreamingChannel, CY_U3P_DMA_TYPE_AUTO, &i2cDmaConfig);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Log stream state in vebose mode */
	AdiPrintStreamState();

	/* Flush streaming endpoint */
	CyU3PUsbFlushEp(ADI_STREAMING_ENDPOINT);

	/* Enable an infinite DMA transfer on the streaming channel */
	status = CyU3PDmaChannelSetXfer(&StreamingChannel, 0);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Set the burst stream flag to notify the streaming thread it should take over */
	CyU3PEventSet (&EventHandler, ADI_I2C_STREAM_ENABLE, CYU3P_EVENT_OR);

	return status;
}

/**
  * @brief Cleans up an I2C read stream.
  *
  * @return A status code indicating the success of the I2C stream clean up.
  *
  * This function detroys the StreamingChannel DMA which was set up to allow
  * I2C -> USB DMA. It also flushes the relevant endpoint and reconfigures the
  * I2C to operate in register mode.
 **/
CyU3PReturnStatus_t AdiI2CStreamFinished()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

    /* Destroy the stream DMA channel */
    status = CyU3PDmaChannelDestroy(&StreamingChannel);

	/* Flush the streaming end point */
	status |= CyU3PUsbFlushEp(ADI_STREAMING_ENDPOINT);

	/* Re-init I2C in register mode */
	AdiI2CInit(FX3State.I2CBitRate, CyFalse);

	/* Clear all interrupt flags */
	CyU3PVicClearInt();

	/* Re-enable relevant ISRs */
	CyU3PVicEnableInt(CY_U3P_VIC_GPIO_CORE_VECTOR);
	CyU3PVicEnableInt(CY_U3P_VIC_GCTL_PWR_VECTOR);

	/* Clear stream kill flag */
	KillStreamEarly = CyFalse;

	return status;
}

/**
  * @brief Starts a protocol agnostic SPI transfer stream.
  *
  * @return A status code indicating the success of the transfer stream start.
  *
  * This is used to implement the ISpi32Interface. The stream info is read in from EP0 into
  * the USBBuffer. This includes stream parameters and the MOSI data. Any error encountered
  * during stream setup will result in a system reboot, after the error data is logged
  * to flash memory.
 **/
CyU3PReturnStatus_t AdiTransferStreamStart()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint16_t bytesRead;
	CyU3PDmaChannelConfig_t dmaConfig =  {0};
	uint16_t initialMOSIByteCount;
	CyBool_t initialDrActive;
	uint8_t * initialTxData;
	uint8_t rxBuf[4];

	/* Get the data from the control endpoint */
	status = CyU3PUsbGetEP0Data(StreamThreadState.TransferByteLength, USBBuffer, &bytesRead);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Parse control endpoint data. The data is formatted as follows
	 * NumCaptures[0-3], NumBuffers[4-7], BytesPerUSBBuffer[8-11], MOSIData.Count()[12-13], MOSIData[14 - ...] */

	/* Number of times to transfer the MOSI data list per data ready*/
	StreamThreadState.NumCaptures = USBBuffer[0];
	StreamThreadState.NumCaptures |= (USBBuffer[1] << 8);
	StreamThreadState.NumCaptures |= (USBBuffer[2] << 16);
	StreamThreadState.NumCaptures |= (USBBuffer[3] << 24);

	/* Total number of buffers to transfer (one buffer is going through MOSIData numCaptures times)*/
	StreamThreadState.NumBuffers = USBBuffer[4];
	StreamThreadState.NumBuffers |= (USBBuffer[5] << 8);
	StreamThreadState.NumBuffers |= (USBBuffer[6] << 16);
	StreamThreadState.NumBuffers |= (USBBuffer[7] << 24);

	/* Number of bytes to place in a single USB packet before transmitting */
	StreamThreadState.BytesPerUsbPacket = USBBuffer[8];
	StreamThreadState.BytesPerUsbPacket |= (USBBuffer[9] << 8);
	StreamThreadState.BytesPerUsbPacket |= (USBBuffer[10] << 16);
	StreamThreadState.BytesPerUsbPacket |= (USBBuffer[11] << 24);

	/* This is just the number of bytes in MOSI data */
	StreamThreadState.BytesPerBuffer = USBBuffer[12];
	StreamThreadState.BytesPerBuffer |= (USBBuffer[13] << 8);

	/* Get initial MOSI byte count and DR active setting */
	initialMOSIByteCount = USBBuffer[14];
	initialMOSIByteCount |= (USBBuffer[15] << 8);
	initialDrActive = (CyBool_t)USBBuffer[16];

	AdiPrintStreamState();

	/* Disable VBUS ISR */
	CyU3PVicDisableInt(CY_U3P_VIC_GCTL_PWR_VECTOR);

	/* Disable GPIO interrupt before attaching interrupt to pin */
	CyU3PVicDisableInt(CY_U3P_VIC_GPIO_CORE_VECTOR);

	/* If using DR triggering configure the selected pin as an input with the correct polarity */
	if(FX3State.DrActive || initialDrActive)
	{
		/* Configure the pin as an input with interrupts enabled on the selected edge */
		AdiConfigureDrPin();
	}

	/* Flush the streaming endpoint */
	status = CyU3PUsbFlushEp(ADI_STREAMING_ENDPOINT);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Configure the StreamingChannel DMA (SPI to PC) */
	CyU3PMemSet ((uint8_t *)&dmaConfig, 0, sizeof(dmaConfig));
	dmaConfig.size 				= FX3State.UsbBufferSize;
	dmaConfig.count 			= 8;
	dmaConfig.prodSckId 		= CY_U3P_CPU_SOCKET_PROD;
	dmaConfig.consSckId 		= CY_U3P_UIB_SOCKET_CONS_1;
	dmaConfig.dmaMode 			= CY_U3P_DMA_MODE_BYTE;
	dmaConfig.prodHeader    	= 0;
	dmaConfig.prodFooter    	= 0;
	dmaConfig.consHeader    	= 0;
	dmaConfig.notification  	= 0;
	dmaConfig.cb            	= NULL;
	dmaConfig.prodAvailCount	= 0;

	CyU3PDmaChannelDestroy(&StreamingChannel);
	status = CyU3PDmaChannelCreate(&StreamingChannel, CY_U3P_DMA_TYPE_MANUAL_OUT, &dmaConfig);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Set DMA transfer mode */
	status = CyU3PDmaChannelSetXfer(&StreamingChannel, 0);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Enable timer hardware for stall */
	AdiConfigStreamStallTimer();

	/* Configure SPI port for transfer */
	AdiSpiPrepareForTransfer();

	/* Clear data ready interrupt */
	GPIO->lpp_gpio_simple[FX3State.DrPin] |= CY_U3P_LPP_GPIO_INTR;

	if(initialMOSIByteCount != 0)
	{
		/* Initially set to just USB buffer */
		initialTxData = USBBuffer;
		/* Increment to end of repeated MOSI data */
		initialTxData += (StreamThreadState.BytesPerBuffer + 17);
		/* Waiting for data ready? */
		if(initialDrActive)
		{
			/* Loop until interrupt is triggered */
			while(!(GPIO->lpp_gpio_intr0 & (1 << FX3State.DrPin)));
			/* Clear data ready interrupt */
			GPIO->lpp_gpio_simple[FX3State.DrPin] |= CY_U3P_LPP_GPIO_INTR;
		}
		/* Perform first transfer */
		AdiSpiTransferWord(initialTxData, rxBuf);
		/* Set the timer to 0 and clear interrupt flag */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].timer = 0;
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status |= CY_U3P_LPP_GPIO_INTR;
		initialTxData += 4;
		for(int txCnt = 4; txCnt < initialMOSIByteCount; txCnt+=4)
		{
			/* Wait for stall time */
			while(!(GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_INTR));
			/* Transmit data */
			AdiSpiTransferWord(initialTxData, rxBuf);
			/* Set the timer to 0 and clear interrupt flag */
			GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].timer = 0;
			GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status |= CY_U3P_LPP_GPIO_INTR;
			/* Increment pointer */
			initialTxData += 4;
		}
	}

	/* Enable generic data capture thread */
	status = CyU3PEventSet(&EventHandler, ADI_TRANSFER_STREAM_ENABLE, CYU3P_EVENT_OR);

	/* Return status code */
	return status;
}

/**
  * @brief Cleans up a protocol agnostic transfer stream.
  *
  * @return A status code indicating the success of the function.
  *
  * Resets the streaming endpoint, destroys the DMA channel, and restores the interrupt
  * source states to their standard operating condition. Must be explicitly invoked via a
  * vendor command when all "buffers" or captured, or indirectly via a TransferStream cancel.
  * This function currently just calls the GenericStreamFinished implementation.
 **/
CyU3PReturnStatus_t AdiTransferStreamFinished()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

	/* Call generic stream finished, since the same resources are used */
	status = AdiGenericStreamFinished();

	/* Return status code */
	return status;
}

/**
  * @brief Starts a real time stream for ADcmXLx021 DUTs
  *
  * @return The status of starting a real-time stream.
  *
  * This function kicks off a real-time stream by configuring interrupts, SPI, and end points.
  * It also optionally toggles the SYNC/RTS pin if requested. At the end of the function, the
  * bit assigned to enable the capture thread is toggled to signal the streaming thread to start producing data.
 **/
CyU3PReturnStatus_t AdiRealTimeStreamStart()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint16_t bytesRead;
	uint8_t tempWriteBuffer[2];
	uint8_t tempReadBuffer[2];
	CyU3PGpioSimpleConfig_t gpioConfig = {0};
	CyU3PDmaChannelConfig_t dmaConfig = {0};

	/* Disable GPIO ISR (Interrupt functionality still active) */
	CyU3PVicDisableInt(CY_U3P_VIC_GPIO_CORE_VECTOR);

	/* Disable VBUS ISR */
	CyU3PVicDisableInt(CY_U3P_VIC_GCTL_PWR_VECTOR);

	/* Clear all interrupt flags */
	CyU3PVicClearInt();

	/* Make sure the BUSY pin is configured as input (DIO2) */
	gpioConfig.outValue = CyFalse;
	gpioConfig.inputEn = CyTrue;
	gpioConfig.driveLowEn = CyFalse;
	gpioConfig.driveHighEn = CyFalse;
	gpioConfig.intrMode = CY_U3P_GPIO_INTR_POS_EDGE;
	CyU3PGpioSetSimpleConfig(FX3State.DrPin, &gpioConfig);

	/* Get number of frames to capture from control endpoint */
	CyU3PUsbGetEP0Data(5, USBBuffer, &bytesRead);
	StreamThreadState.NumRealTimeCaptures = USBBuffer[0];
	StreamThreadState.NumRealTimeCaptures += (USBBuffer[1] << 8);
	StreamThreadState.NumRealTimeCaptures += (USBBuffer[2] << 16);
	StreamThreadState.NumRealTimeCaptures += (USBBuffer[3] << 24);

	/* Get pin start setting. Pin exit setting already set here */
	StreamThreadState.PinStartEnable = (CyBool_t) USBBuffer[4];

	/* Flush streaming end point */
	CyU3PUsbFlushEp(ADI_STREAMING_ENDPOINT);

	/* Configure RTS channel DMA */
	CyU3PMemSet((uint8_t *)&dmaConfig, 0, sizeof(dmaConfig));
	dmaConfig.size 				= FX3State.UsbBufferSize;
	dmaConfig.count 			= 64;
	dmaConfig.prodSckId 		= CY_U3P_LPP_SOCKET_SPI_PROD;
	dmaConfig.consSckId 		= CY_U3P_UIB_SOCKET_CONS_1;
	dmaConfig.dmaMode 			= CY_U3P_DMA_MODE_BYTE;
	dmaConfig.prodHeader    	= 0;
	dmaConfig.prodFooter    	= 0;
	dmaConfig.consHeader    	= 0;
	dmaConfig.notification  	= 0;
	dmaConfig.cb            	= NULL;
	dmaConfig.prodAvailCount	= 0;

    /* Configure DMA for RealTimeStreamingChannel */
	CyU3PDmaChannelDestroy(&StreamingChannel);
    status = CyU3PDmaChannelCreate(&StreamingChannel, CY_U3P_DMA_TYPE_AUTO, &dmaConfig);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Clear the DMA buffers */
	CyU3PDmaChannelReset(&StreamingChannel);

	/* Handle start conditions
	 * Disable starting the capture by raising SYNC/RTS
	 * If this is not done before setting SYNC/RTS high, things will break */
	if(StreamThreadState.PinExitEnable && !StreamThreadState.PinStartEnable)
	{
		/* If pin start is disabled (we're starting the capture with GLOB_CMD) */
		/* Read MSC_CTRL register */
		tempReadBuffer[1] = (0x64);
		tempReadBuffer[0] = (0x00);
		status = CyU3PSpiTransmitWords(tempReadBuffer, 2);
		if (status != CY_U3P_SUCCESS)
		{
			AdiLogError(StreamFunctions_c, __LINE__, status);
			AdiAppErrorHandler(status);
		}
		AdiSleepForMicroSeconds(FX3State.StallTime);
		status = CyU3PSpiReceiveWords(tempReadBuffer, 2);
		AdiSleepForMicroSeconds(FX3State.StallTime);

		/* Clear bit 12 (bit enables/disables starting a capture using SYNC pin) */
		tempReadBuffer[1] = tempReadBuffer[1] & (0xEF);

		/* Write modified buffer to MSC_CTRL */
		tempWriteBuffer[1] = (0x80) | (0x64);
		tempWriteBuffer[0] = tempReadBuffer[0];
		status = CyU3PSpiTransmitWords(tempWriteBuffer, 2);
		AdiSleepForMicroSeconds(FX3State.StallTime);
		tempWriteBuffer[1] = (0x80) | (0x65);
		tempWriteBuffer[0] = tempReadBuffer[0];
		status = CyU3PSpiTransmitWords(tempWriteBuffer, 2);
		AdiSleepForMicroSeconds(FX3State.StallTime);

		/* Configure SYNC (DIO2) as an output and set high */
		gpioConfig.outValue = CyTrue;
		gpioConfig.inputEn = CyFalse;
		gpioConfig.driveLowEn = CyTrue;
		gpioConfig.driveHighEn = CyTrue;
		gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
		status = CyU3PGpioSetSimpleConfig(FX3State.PinMap.ADI_PIN_DIO1, &gpioConfig);

		/* If config fails try to override and reconfigure */
		if(status != CY_U3P_SUCCESS)
		{
			status = CyU3PDeviceGpioOverride(FX3State.PinMap.ADI_PIN_DIO1, CyTrue);
			status = CyU3PGpioSetSimpleConfig(FX3State.PinMap.ADI_PIN_DIO1, &gpioConfig);
		}

		/* Check that the pin is configured to act as an output, return if not */
		status = CyU3PGpioSimpleSetValue(FX3State.PinMap.ADI_PIN_DIO1, CyTrue);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(StreamFunctions_c, __LINE__, status);
			AdiAppErrorHandler(status);
		}
	}

	/* If pin start is enabled, set bit 12 in MISC_CTRL and toggle SYNC, otherwise send 0x0800 to COMMAND */
	if(StreamThreadState.PinStartEnable)
	{
		/* Read MSC_CTRL register */
		tempReadBuffer[1] = (0x64);
		tempReadBuffer[0] = (0x00);
		status = CyU3PSpiTransmitWords(tempReadBuffer, 2);
		AdiSleepForMicroSeconds(FX3State.StallTime);
		status = CyU3PSpiReceiveWords(tempReadBuffer, 2);
		AdiSleepForMicroSeconds(FX3State.StallTime);

		/* Set bit 12 (bit enables/disables starting a capture using SYNC pin) */
		tempReadBuffer[1] = tempReadBuffer[1] | (0x10);

		/* Write modified buffer to MSC_CTRL */
		tempWriteBuffer[1] = (0x80) | (0x64);
		tempWriteBuffer[0] = tempReadBuffer[0];
		status = CyU3PSpiTransmitWords(tempWriteBuffer, 2);
		AdiSleepForMicroSeconds(FX3State.StallTime);
		tempWriteBuffer[1] = (0x80) | (0x65);
		tempWriteBuffer[0] = tempReadBuffer[1];
		status = CyU3PSpiTransmitWords(tempWriteBuffer, 2);
		AdiSleepForMicroSeconds(FX3State.StallTime);

		/* Delay for setting to apply to DUT */
		AdiSleepForMicroSeconds(500);

		/* Configure SYNC/RTS as an output and set high */
		gpioConfig.outValue = CyTrue;
		gpioConfig.inputEn = CyFalse;
		gpioConfig.driveLowEn = CyTrue;
		gpioConfig.driveHighEn = CyTrue;
		gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
		status = CyU3PGpioSetSimpleConfig(FX3State.PinMap.ADI_PIN_DIO1, &gpioConfig);

		/* If config fails try to override and reconfigure */
		if(status != CY_U3P_SUCCESS)
		{
			status = CyU3PDeviceGpioOverride(FX3State.PinMap.ADI_PIN_DIO1, CyTrue);
			status = CyU3PGpioSetSimpleConfig(FX3State.PinMap.ADI_PIN_DIO1, &gpioConfig);
		}

		/* Check that the pin is configured to act as an output, return if not */
		status = CyU3PGpioSimpleSetValue(FX3State.PinMap.ADI_PIN_DIO1, CyTrue);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(StreamFunctions_c, __LINE__, status);
			AdiAppErrorHandler(status);
		}
	}
	else
	{
		/* Send COMMAND 0x800
		 * Command is Page 0, Address 62 */
		tempWriteBuffer[1] = (0x80) | (0x3E);
		tempWriteBuffer[0] = 0;
		status = CyU3PSpiTransmitWords(tempWriteBuffer, 2);
		AdiSleepForMicroSeconds(FX3State.StallTime);
		tempWriteBuffer[1] = (0x80) | (0x3F);
		tempWriteBuffer[0] = 0x08;
		status = CyU3PSpiTransmitWords(tempWriteBuffer, 2);
		AdiSleepForMicroSeconds(FX3State.StallTime);
	}

	/* Manually reset SPI Rx/Tx FIFO */
	AdiSpiResetFifo(CyTrue, CyTrue);

	/* Set the SPI config for streaming mode (8 bit transactions) */
	AdiSetSpiWordLength(8);

	/* Print the stream state if in verbose mode */
#ifdef VERBOSE_MODE
	AdiPrintStreamState();
#endif

	/* Set infinite DMA transfer on streaming channel */
	CyU3PDmaChannelSetXfer(&StreamingChannel, 0);

	/* Set the real-time data capture thread flag */
	CyU3PEventSet(&EventHandler, ADI_RT_STREAM_ENABLE, CYU3P_EVENT_OR);

	return status;
}

/**
  * @brief This function cleans up resources allocated for a real time stream.
  *
  * @return The status of the cancel operation.
  *
  * This function cleans up after a real-time stream by resetting the SPI port, triggering the
  * SYNC/RTS pin (if asked to do so), and notifying the host that the cancel operation was successful.
 **/
CyU3PReturnStatus_t AdiRealTimeStreamFinished()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

	/* Pull SYNC/RTS pin low to force x021 out of RT mode if pin exit flag is set */
	if(StreamThreadState.PinExitEnable)
	{
		/* Configure SYNC/RTS as an output and set high */
		CyU3PGpioSimpleConfig_t gpioConfig;
		gpioConfig.outValue = CyFalse;
		gpioConfig.inputEn = CyFalse;
		gpioConfig.driveLowEn = CyTrue;
		gpioConfig.driveHighEn = CyTrue;
		gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
		CyU3PGpioSetSimpleConfig(FX3State.PinMap.ADI_PIN_DIO1, &gpioConfig);

		/* Reset flag for next run - delete? */
		StreamThreadState.PinExitEnable = CyFalse;
	}

	/* Stop sync pin output drive and enable as input */
	CyU3PGpioSimpleConfig_t gpioConfig;
	gpioConfig.inputEn = CyTrue;
	gpioConfig.driveLowEn = CyFalse;
	gpioConfig.driveHighEn = CyFalse;
	CyU3PGpioSetSimpleConfig(FX3State.PinMap.ADI_PIN_DIO1, &gpioConfig);

	/* Disable SPI DMA mode */
	CyU3PSpiDisableBlockXfer(CyTrue, CyTrue);

	/* Reset the SPI controller */
	SPI->lpp_spi_config &= ~(CY_U3P_LPP_SPI_RX_ENABLE | CY_U3P_LPP_SPI_TX_ENABLE | CY_U3P_LPP_SPI_DMA_MODE | CY_U3P_LPP_SPI_ENABLE);
	while ((SPI->lpp_spi_config & CY_U3P_LPP_SPI_ENABLE) != 0);

    /* Tear down the RT streaming channel */
    status = CyU3PDmaChannelDestroy(&StreamingChannel);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
	}

	/* Flush streaming end point */
	CyU3PUsbFlushEp(ADI_STREAMING_ENDPOINT);

	/* Clear all interrupt flags */
	CyU3PVicClearInt();

	/* Re-enable relevant ISRs */
	CyU3PVicEnableInt(CY_U3P_VIC_GPIO_CORE_VECTOR);
	CyU3PVicEnableInt(CY_U3P_VIC_GCTL_PWR_VECTOR);

	/* Restore the SPI state */
	AdiSetSpiWordLength(FX3State.SpiConfig.wordLen);

	/* Reset KillStreamEarly flag in case the user wants to capture data again */
	KillStreamEarly = CyFalse;

	/* Return status code */
	return status;
}

/**
  * @brief Starts a burst stream for IMU products.
  *
  * @return The status of starting a real-time stream.
  *
  * This function kicks off a burst stream by configuring a pin interrupt on a user-specified pin, configuring
  * the SPI and USB DMAs to handle the incoming data, and enabling the streaming function.It can be configured
  * for "Blackfin" or "ADuC" burst using vendor requests.
 **/
CyU3PReturnStatus_t AdiBurstStreamStart()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint16_t bytesRead;
	uint16_t triggerLength;

	/* Disable VBUS ISR */
	CyU3PVicDisableInt(CY_U3P_VIC_GCTL_PWR_VECTOR);

	/* Disable GPIO interrupt before attaching interrupt to pin */
	CyU3PVicDisableInt(CY_U3P_VIC_GPIO_CORE_VECTOR);

	/* Make sure the global data ready pin is configured as an input and attach the interrupt to the correct edge */
	if(FX3State.DrActive)
		AdiConfigureDrPin();

	/* Get the number of buffers, trigger word, and transfer length from the control endpoint */
	CyU3PUsbGetEP0Data(StreamThreadState.TransferWordLength, USBBuffer, &bytesRead);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Parse number of buffers */
	StreamThreadState.NumBuffers = USBBuffer[0];
	StreamThreadState.NumBuffers |= (USBBuffer[1] << 8);
	StreamThreadState.NumBuffers |= (USBBuffer[2] << 16);
	StreamThreadState.NumBuffers |= (USBBuffer[3] << 24);

	/* Parse transfer byte length */
	StreamThreadState.TransferByteLength = USBBuffer[4];
	StreamThreadState.TransferByteLength |= (USBBuffer[5] << 8);
	StreamThreadState.TransferByteLength |= (USBBuffer[6] << 16);
	StreamThreadState.TransferByteLength |= (USBBuffer[7] << 24);

	/* Set regList memory to correct length plus trigger word */
	StreamThreadState.RegList = CyU3PDmaBufferAlloc(sizeof(uint8_t) * StreamThreadState.TransferByteLength);

	/* Clear (zero) contents of regList memory. Burst transfers are DNC, so we're sending zeros */
	CyU3PMemSet(StreamThreadState.RegList, 0, sizeof(uint8_t) * StreamThreadState.TransferByteLength);

	/* Calculate trigger length (USB transfer length - header size) */
	triggerLength = bytesRead - 8;

	/* Append burst trigger word to the first two bytes of regList */
	for(int i = 0; i< triggerLength; i++)
	{
		StreamThreadState.RegList[i] = USBBuffer[i + 8];
	}

	/* Calculate the required memory block (in bytes) to be a multiple of 16 */
	uint16_t remainder = StreamThreadState.TransferByteLength % 16;
	if (remainder == 0)
	{
		StreamThreadState.RoundedByteTransferLength = StreamThreadState.TransferByteLength;
	}
	else
	{
		StreamThreadState.RoundedByteTransferLength = StreamThreadState.TransferByteLength + 16 - remainder;
	}

#ifdef VERBOSE_MODE
	 CyU3PDebugPrint (4, "Starting burst stream!\r\n");
	 CyU3PDebugPrint (4, "burstTriggerUpper:  %d\r\n", StreamThreadState.RegList[0]);
	 CyU3PDebugPrint (4, "burstTriggerLower:  %d\r\n", StreamThreadState.RegList[1]);
	 CyU3PDebugPrint (4, "roundedTransferLength:  %d\r\n", StreamThreadState.RoundedByteTransferLength);
	 CyU3PDebugPrint (4, "transferByteLength:  %d\r\n", StreamThreadState.TransferByteLength);
	 CyU3PDebugPrint (4, "numBuffers:  %d\r\n", StreamThreadState.NumBuffers);
	 CyU3PDebugPrint (4, "USB Buffer Size:  %d\r\n", FX3State.UsbBufferSize);
#endif

	/* Configure the Burst DMA Streaming Channel (SPI to PC) for Auto DMA */
	CyU3PDmaChannelConfig_t dmaConfig;
	CyU3PMemSet ((uint8_t *)&dmaConfig, 0, sizeof(dmaConfig));
	dmaConfig.size 				= FX3State.UsbBufferSize;
	dmaConfig.count 			= 8;
	dmaConfig.prodSckId 		= CY_U3P_LPP_SOCKET_SPI_PROD;
	dmaConfig.consSckId 		= CY_U3P_UIB_SOCKET_CONS_1;
	dmaConfig.dmaMode 			= CY_U3P_DMA_MODE_BYTE;
	dmaConfig.prodHeader    	= 0;
	dmaConfig.prodFooter    	= 0;
	dmaConfig.consHeader    	= 0;
	dmaConfig.notification  	= 0;
	dmaConfig.cb            	= NULL;
	dmaConfig.prodAvailCount	= 0;

	/* Destroy and re-create streaming DMA channel */
	CyU3PDmaChannelDestroy(&StreamingChannel);
	status = CyU3PDmaChannelCreate(&StreamingChannel, CY_U3P_DMA_TYPE_AUTO, &dmaConfig);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Configure SPI TX DMA (CPU memory to SPI for burst mode)
	 * Transfer length must equal length of message to be sent
	 * Count not required since the DMA will be run in override mode */
	CyU3PMemSet ((uint8_t *)&dmaConfig, 0, sizeof(dmaConfig));
    dmaConfig.size 				= StreamThreadState.RoundedByteTransferLength;
    dmaConfig.count 			= 0;
    dmaConfig.prodSckId 		= CY_U3P_CPU_SOCKET_PROD;
    dmaConfig.consSckId 		= CY_U3P_LPP_SOCKET_SPI_CONS;
    dmaConfig.dmaMode 			= CY_U3P_DMA_MODE_BYTE;
    dmaConfig.prodHeader     	= 0;
    dmaConfig.prodFooter     	= 0;
    dmaConfig.consHeader     	= 0;
    dmaConfig.notification   	= 0;
    dmaConfig.cb             	= NULL;
    dmaConfig.prodAvailCount 	= 0;

    /* Destroy and re-create memory to SPI (Tx) channel */
    CyU3PDmaChannelDestroy(&MemoryToSPI);
    status = CyU3PDmaChannelCreate(&MemoryToSPI, CY_U3P_DMA_TYPE_MANUAL_OUT, &dmaConfig);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Flush the streaming endpoint */
	status = CyU3PUsbFlushEp(ADI_STREAMING_ENDPOINT);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Manually reset the SPI Rx/Tx FIFO */
	AdiSpiResetFifo(CyTrue, CyTrue);

	/* Set the SPI config for streaming mode (8 bit transactions) */
	AdiSetSpiWordLength(8);

	/* Configure SpiDmaBuffer and feed it regList*/
	CyU3PMemSet ((uint8_t *)&SpiDmaBuffer, 0, sizeof(SpiDmaBuffer));
	SpiDmaBuffer.count = StreamThreadState.TransferByteLength;
	SpiDmaBuffer.size = StreamThreadState.RoundedByteTransferLength;
	SpiDmaBuffer.buffer = StreamThreadState.RegList;
	SpiDmaBuffer.status = 0;

	/* Enable an infinite DMA transfer on the streaming channel */
	status = CyU3PDmaChannelSetXfer(&StreamingChannel, 0);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Set the burst stream flag to notify the streaming thread it should take over */
	status = CyU3PEventSet(&EventHandler, ADI_BURST_STREAM_ENABLE, CYU3P_EVENT_OR);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	return status;
}

/**
  * @brief Cleans up resources allocated for a IMU burst stream.
  *
  * @return The status of the cancel operation.
  *
  * This function cleans up after a burst stream by resetting the SPI port and notifying the host
  * that the cancel operation was successful.
 **/
CyU3PReturnStatus_t AdiBurstStreamFinished()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

	/* Reset the SPI controller */
	SPI->lpp_spi_config &= ~(CY_U3P_LPP_SPI_RX_ENABLE | CY_U3P_LPP_SPI_TX_ENABLE | CY_U3P_LPP_SPI_DMA_MODE | CY_U3P_LPP_SPI_ENABLE);
	while ((SPI->lpp_spi_config & CY_U3P_LPP_SPI_ENABLE) != 0);

	/* Remove the interrupt from the global data ready pin */
	CyU3PGpioSimpleConfig_t gpioConfig;
	gpioConfig.outValue = CyTrue;
	gpioConfig.inputEn = CyTrue;
	gpioConfig.driveLowEn = CyFalse;
	gpioConfig.driveHighEn = CyFalse;
	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
	CyU3PGpioSetSimpleConfig(FX3State.DrPin, &gpioConfig);

	/* Destroy MemoryToSpi DMA channel */
    status = CyU3PDmaChannelDestroy(&MemoryToSPI);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
	}

    /* Destroy the burst DMA channel (and recover a LOT of memory) */
    status = CyU3PDmaChannelDestroy(&StreamingChannel);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
	}

	/* Flush the streaming end point */
	CyU3PUsbFlushEp(ADI_STREAMING_ENDPOINT);

	/* Clear all interrupt flags */
	CyU3PVicClearInt();

	/* Re-enable relevant ISRs */
	CyU3PVicEnableInt(CY_U3P_VIC_GPIO_CORE_VECTOR);
	CyU3PVicEnableInt(CY_U3P_VIC_GCTL_PWR_VECTOR);

	/* Restore the SPI state */
	AdiSetSpiWordLength(FX3State.SpiConfig.wordLen);

	/* Reset KillStreamEarly flag in case the user wants to capture data again */
	KillStreamEarly = CyFalse;

	return status;
}

/**
  * @brief Starts a register read/write stream, with options to trigger on a data ready.
  *
  * @return The status of starting a generic stream.
  *
  * This function kicks off a generic data stream by configuring interrupts, SPI, and end points.
  * At the end of the function, the ADI_GENERIC_STREAM_ENABLE flag is set such that the
  * generic streaming thread knows to start producing data.
 **/
CyU3PReturnStatus_t AdiGenericStreamStart()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	CyU3PDmaChannelConfig_t dmaConfig = {0};

	/* Disable VBUS ISR */
	CyU3PVicDisableInt(CY_U3P_VIC_GCTL_PWR_VECTOR);

	/* Disable GPIO interrupt before attaching interrupt to pin */
	CyU3PVicDisableInt(CY_U3P_VIC_GPIO_CORE_VECTOR);

	if(FX3State.DrActive)
	{
		status = AdiConfigureDrPin();
	}
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
	}

	/* Get the number of buffers (number of times to read each set of registers) */
	StreamThreadState.NumBuffers = USBBuffer[0];
	StreamThreadState.NumBuffers += (USBBuffer[1] << 8);
	StreamThreadState.NumBuffers += (USBBuffer[2] << 16);
	StreamThreadState.NumBuffers += (USBBuffer[3] << 24);

	/* Get the number of captures of the address list (number of times to capture the list of registers per buffer) */
	StreamThreadState.NumCaptures = USBBuffer[4];
	StreamThreadState.NumCaptures += (USBBuffer[5] << 8);
	StreamThreadState.NumCaptures += (USBBuffer[6] << 16);
	StreamThreadState.NumCaptures += (USBBuffer[7] << 24);

	/* Calculate the number of bytes per buffer */
	/* Number of times to read each set of registers * (number of registers - control registers) */
	StreamThreadState.BytesPerBuffer = StreamThreadState.NumCaptures * (StreamThreadState.TransferByteLength - 8);

	/* Set the reglist (just use the Bulk buffer - gives defined behavior)*/
	StreamThreadState.RegList = BulkBuffer;

	/* Copy the register list */
	CyU3PMemCopy(StreamThreadState.RegList, USBBuffer + 8, StreamThreadState.TransferByteLength - 8);

	/* Zero the last values */
	StreamThreadState.RegList[StreamThreadState.TransferByteLength - 7] = 0;
	StreamThreadState.RegList[StreamThreadState.TransferByteLength - 8] = 0;

	/* Find number of register "buffers" which fit in a USB buffer */
	if(StreamThreadState.BytesPerBuffer > FX3State.UsbBufferSize)
	{
		StreamThreadState.BytesPerUsbPacket = FX3State.UsbBufferSize;
	}
	else
	{
		StreamThreadState.BytesPerUsbPacket = ((FX3State.UsbBufferSize / StreamThreadState.BytesPerBuffer) * StreamThreadState.BytesPerBuffer);
	}

	/* Flush the streaming endpoint */
	status = CyU3PUsbFlushEp(ADI_STREAMING_ENDPOINT);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Configure the StreamingChannel DMA (SPI to PC) */
	dmaConfig.size 				= FX3State.UsbBufferSize;
	dmaConfig.count 			= 16;
	dmaConfig.prodSckId 		= CY_U3P_CPU_SOCKET_PROD;
	dmaConfig.consSckId 		= CY_U3P_UIB_SOCKET_CONS_1;
	dmaConfig.dmaMode 			= CY_U3P_DMA_MODE_BYTE;
	dmaConfig.prodHeader    	= 0;
	dmaConfig.prodFooter    	= 0;
	dmaConfig.consHeader    	= 0;
	dmaConfig.notification  	= 0;
	dmaConfig.cb            	= NULL;
	dmaConfig.prodAvailCount	= 0;

	/* Destroy then create channel */
	CyU3PDmaChannelDestroy(&StreamingChannel);
	status = CyU3PDmaChannelCreate(&StreamingChannel, CY_U3P_DMA_TYPE_MANUAL_OUT, &dmaConfig);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

	/* Set DMA transfer mode */
	status = CyU3PDmaChannelSetXfer(&StreamingChannel, 0);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
		AdiAppErrorHandler(status);
	}

#ifdef VERBOSE_MODE
	/* Print stream state after all config */
	AdiPrintStreamState();
#endif

	/* Enable timer for stall */
	AdiConfigStreamStallTimer();

	/* Configure SPI port */
	AdiSpiPrepareForTransfer();

	/* Clear data ready interrupt */
	GPIO->lpp_gpio_simple[FX3State.DrPin] |= CY_U3P_LPP_GPIO_INTR;

	/* Enable generic data capture thread */
	status = CyU3PEventSet (&EventHandler, ADI_GENERIC_STREAM_ENABLE, CYU3P_EVENT_OR);

	return status;
}

/**
  * @brief This function cleans up after a generic stream and notifies the host that the cancel operation was successful if requested.
  *
  * @return The status of the cancel operation.
  *
  * This function must be explicitly invoked via a vendor command after the PC has finished reading all data from the FX3. This is done
  * to ensure data consistency and prevent a race condition between the FX3 API and the firmware.
 **/
CyU3PReturnStatus_t AdiGenericStreamFinished()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

	/* Remove the interrupt from the global data ready pin */
	CyU3PGpioSimpleConfig_t gpioConfig;
	gpioConfig.outValue = CyTrue;
	gpioConfig.inputEn = CyTrue;
	gpioConfig.driveLowEn = CyFalse;
	gpioConfig.driveHighEn = CyFalse;
	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
	CyU3PGpioSetSimpleConfig(FX3State.DrPin, &gpioConfig);

    /* Destroy the StreamingChannel channel (and recover a LOT of memory) */
    status = CyU3PDmaChannelDestroy(&StreamingChannel);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
	}

	/* Flush the streaming endpoint */
	status = CyU3PUsbFlushEp(ADI_STREAMING_ENDPOINT);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamFunctions_c, __LINE__, status);
	}

	/* Clear all interrupt flags */
	CyU3PVicClearInt();

	/* Re-enable relevant ISRs */
	CyU3PVicEnableInt(CY_U3P_VIC_GPIO_CORE_VECTOR);
	CyU3PVicEnableInt(CY_U3P_VIC_GCTL_PWR_VECTOR);

	/* Reset KillStreamEarly flag in case the user wants to capture data again */
	KillStreamEarly = CyFalse;

	/* return status code */
	return status;
}

/**
  * @brief Configures the data ready pin as an input with edge interrupt triggering enabled.
  *
  * @return The status of the pin configure operation.
  *
  * Configures the data ready pin (FX3State.DrPin) as an input, with edge interrupts enabled
  * based on FX3State.DrPolarity. It is advisable to disable the GPIO interrupt vector before
  * calling this function. If you do not, the GPIO ISR may be triggered.
 **/
CyU3PReturnStatus_t AdiConfigureDrPin()
{
	return AdiConfigurePinInterrupt(FX3State.DrPin, FX3State.DrPolarity);
}
