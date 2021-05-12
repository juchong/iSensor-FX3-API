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
  * @file		StreamThread.c
  * @date		8/1/2019
  * @author		A. Nolan (alex.nolan@analog.com)
  * @author 	J. Chong (juan.chong@analog.com)
  * @brief		This file contains the functions directly associated with StreamThread.
 **/

#include "StreamThread.h"

/* Private worker functions for each of the stream modes */
static CyU3PReturnStatus_t AdiGenericStreamWork();
static CyU3PReturnStatus_t AdiRealTimeStreamWork();
static CyU3PReturnStatus_t AdiBurstStreamWork();
static CyU3PReturnStatus_t AdiTransferStreamWork();
static CyU3PReturnStatus_t AdiI2CStreamWork();

/* Tell the compiler where to find the needed globals */
extern CyU3PEvent EventHandler;
extern CyU3PDmaChannel StreamingChannel;
extern CyU3PDmaChannel MemoryToSPI;
extern CyU3PDmaBuffer_t SpiDmaBuffer;
extern BoardState FX3State;
extern volatile CyBool_t KillStreamEarly;
extern StreamState StreamThreadState;
extern uint8_t USBBuffer[4096];

/**
  * @brief The entry point function for the StreamThread. Handles all streaming data captures.
  *
  * @param input Unused input required by the RTOS thread manager
  *
  * This function runs in its own thread and handles real-time, burst, generic, and transfer streaming processes.
  * Either type of stream can be kicked off by executing the appropriate set-up routine and then
  * triggering the corresponding event flag.
 **/
void AdiStreamThreadEntry(uint32_t input)
{
	UNUSED(input);

	/* Set the event mask to the stream enable events */
	uint32_t eventMask = ADI_GENERIC_STREAM_ENABLE|ADI_RT_STREAM_ENABLE|ADI_BURST_STREAM_ENABLE|ADI_TRANSFER_STREAM_ENABLE|ADI_I2C_STREAM_ENABLE;

	/* Variable to receive the event arguments into */
	uint32_t eventFlag;

	for (;;)
	{
		/* Wait indefinitely for any flag to be set */
		if (CyU3PEventGet(&EventHandler, eventMask, CYU3P_EVENT_OR_CLEAR, &eventFlag, CYU3P_WAIT_FOREVER) == CY_U3P_SUCCESS)
		{
			/* Real-time (ADcmXL) stream case */
			if (eventFlag & ADI_RT_STREAM_ENABLE)
			{
				AdiRealTimeStreamWork();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Finished real time stream work\r\n");
#endif
			}
			/* Transfer stream case */
			else if(eventFlag & ADI_TRANSFER_STREAM_ENABLE)
			{
				AdiTransferStreamWork();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Finished transfer stream work\r\n");
#endif
			}
			/* Generic register stream case */
			else if (eventFlag & ADI_GENERIC_STREAM_ENABLE)
			{
				AdiGenericStreamWork();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Finished generic stream work\r\n");
#endif
			}
			/* Burst stream case */
			else if (eventFlag & ADI_BURST_STREAM_ENABLE)
			{
				AdiBurstStreamWork();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Finished burst stream work\r\n");
#endif
			}
			/* I2C stream case */
			else if (eventFlag & ADI_I2C_STREAM_ENABLE)
			{
				AdiI2CStreamWork();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Finished I2C stream work\r\n");
#endif
			}
			else
			{
				/* Shouldnt be able to get here */
				AdiLogError(StreamThread_c, __LINE__, eventFlag);
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "ERROR: Unhandled StreamThread event generated. eventFlag: 0x%x\r\n", eventFlag);
#endif
			}
		}
        /* Allow other ready threads to run. */
        CyU3PThreadRelinquish();
	}
}

/**
  * @brief This is the worker function for the I2C read stream.
  *
  * @return A status code representing the success of the I2C stream operation.
  *
  * This function performs all the I2C and USB transfers for a single "buffer" of an I2C read stream.
  * The size of each buffer is the number of read bytes requested in the stream start
 **/
static CyU3PReturnStatus_t AdiI2CStreamWork()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

	/* Track the number of buffers read */
	static uint32_t numBuffersRead = 0;

	/* Wait for DR if enabled */
	if (FX3State.DrActive)
	{
		/* Clear GPIO interrupts */
		GPIO->lpp_gpio_simple[FX3State.DrPin] |= CY_U3P_LPP_GPIO_INTR;
		/* Loop until interrupt is triggered */
		while(!(GPIO->lpp_gpio_intr0 & (1 << FX3State.DrPin)));
	}

	/* Start new I2C DMA transfer */
	CyU3PI2cSendCommand(&StreamThreadState.I2CStreamPreamble, StreamThreadState.NumCaptures, CyTrue);

	/* Wait for completion */
	CyU3PI2cWaitForBlockXfer(CyTrue);

	/* Check to see if we've captured enough buffers or if we were asked to stop data capture early */
	if ((numBuffersRead >= (StreamThreadState.NumBuffers - 1)) || KillStreamEarly)
	{
		/* Reset values */
		numBuffersRead = 0;

		/* Set channel wrap up */
		CyU3PDmaChannelSetWrapUp(&StreamingChannel);

		/* Set stream done flag if kill early event was processed (otherwise must be explicitly invoked by FX3 API) */
		if(KillStreamEarly)
		{
			CyU3PEventSet(&EventHandler, ADI_I2C_STREAM_DONE, CYU3P_EVENT_OR);
		}
	}
	else
	{
		/* Increment buffer counter */
		numBuffersRead++;

		/* Reset flag */
		CyU3PEventSet(&EventHandler, ADI_I2C_STREAM_ENABLE, CYU3P_EVENT_OR);
	}

	return status;
}

/**
  * @brief This is the worker function for the generic stream.
  *
  * @return A status code representing the success of the generic stream operation.
  *
  * This function performs all the SPI and USB transfers for a single "buffer" of a generic stream.
  * One buffer is considered to be numCapture reads of the register list provided.
 **/
static CyU3PReturnStatus_t AdiGenericStreamWork()
{
	/* Track the number of buffers read */
	static uint32_t numBuffersRead = 0;

	/* Track the number of bytes read into the current DMA buffer */
	static int32_t byteCounter = 0;

	/* Track the current position within the MISO (streaming DMA) buffer*/
	static uint8_t *MISOPtr;

	/* DMA buffer structure for the active buffer for the streaming DMA channel */
	static CyU3PDmaBuffer_t StreamChannelBuffer = {0};

	/* Index variables */
	uint16_t regIndex, captureCount;

	/* Operation status */
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

	/* track the current position within the MOSI (reglist) buffer */
	uint8_t* MOSIPtr;

	/* If the stream channel buffer has not been set, get a new buffer */
	if (MISOPtr == 0)
	{
		status = CyU3PDmaChannelGetBuffer(&StreamingChannel, &StreamChannelBuffer, CYU3P_WAIT_FOREVER);
		if (status != CY_U3P_SUCCESS)
		{
			AdiLogError(StreamThread_c, __LINE__, status);
		}
		MISOPtr = StreamChannelBuffer.buffer;
	}

	/* Wait for DR if enabled */
	if (FX3State.DrActive)
	{
		/* Loop until interrupt is triggered */
		while(!(GPIO->lpp_gpio_intr0 & (1 << FX3State.DrPin)));
		/* Clear data ready interrupt */
		GPIO->lpp_gpio_simple[FX3State.DrPin] |= CY_U3P_LPP_GPIO_INTR;
	}

	/* Run through the register list numCaptures times - this is one buffer */
	for(captureCount = 0; captureCount < StreamThreadState.NumCaptures; captureCount++)
	{
		/* Set the MOSI pointer to the bottom of the register list */
		MOSIPtr = StreamThreadState.RegList;

		/* Transmit the first words without reading back */
		AdiSpiTransferWord(MOSIPtr, MISOPtr);

		/* Increment the MOSI pointer*/
		MOSIPtr += 2;

		/* Set the timer value to 0 */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].timer = 0;
		/* clear interrupt flag */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status |= CY_U3P_LPP_GPIO_INTR;

		/* Iterate through the rest of the register list */
		for(regIndex = 0; regIndex < (StreamThreadState.TransferByteLength - 8); regIndex += 2)
		{
			/* Wait for the complex GPIO timer to reach the stall time */
			while(!(GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_INTR));

			/* transfer words */
			AdiSpiTransferWord(MOSIPtr, MISOPtr);

			/* Set the pin timer to 0 */
			GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].timer = 0;
			/* clear interrupt flag */
			GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status |= CY_U3P_LPP_GPIO_INTR;

			/* Check if a readback is needed for the last transfer */
			if(regIndex == (StreamThreadState.TransferByteLength - 12))
			{
				/* If the write bit was set skip the read back*/
				if(MOSIPtr[1] & 0x80)
				{
					regIndex += 2;
					MOSIPtr += 2;
					MISOPtr += 2;
					byteCounter += 2;
				}
			}

			/* Update counters */
			MOSIPtr += 2;
			MISOPtr += 2;
			byteCounter += 2;

			/* Check if a transmission is needed */
			if (byteCounter >= (StreamThreadState.BytesPerUsbPacket - 1))
			{
				status = CyU3PDmaChannelCommitBuffer (&StreamingChannel, FX3State.UsbBufferSize, 0);
				if (status != CY_U3P_SUCCESS)
				{
					AdiLogError(StreamThread_c, __LINE__, status);
				}

				status = CyU3PDmaChannelGetBuffer (&StreamingChannel, &StreamChannelBuffer, CYU3P_WAIT_FOREVER);
				if (status != CY_U3P_SUCCESS)
				{
					AdiLogError(StreamThread_c, __LINE__, status);
				}
				MISOPtr = StreamChannelBuffer.buffer;
				byteCounter = 0;
			}
		}

		/* Wait for the complex GPIO timer to reach the stall time */
		while(!(GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_INTR));

		/* Set the pin timer to 0 */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].timer = 0;
		/* Clear interrupt flag */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status |= CY_U3P_LPP_GPIO_INTR;
	}

	/* Check to see if we've captured enough buffers or if we were asked to stop data capture early */
	if((numBuffersRead >= (StreamThreadState.NumBuffers - 1)) || KillStreamEarly)
	{
		/* Reset values */
		numBuffersRead = 0;
		/* Signal getting a new buffer */
		MISOPtr = 0;
		if (byteCounter)
		{
#ifdef VERBOSE_MODE
			CyU3PDebugPrint (4, "Commiting last USB buffer with %d bytes.\r\n", byteCounter);
#endif
			status = CyU3PDmaChannelCommitBuffer (&StreamingChannel, FX3State.UsbBufferSize, 0);
			if (status != CY_U3P_SUCCESS)
			{
				AdiLogError(StreamThread_c, __LINE__, status);
			}
			byteCounter = 0;
		}

		/* Clear GPIO interrupts */
		GPIO->lpp_gpio_simple[FX3State.DrPin] |= CY_U3P_LPP_GPIO_INTR;

		/* Clear timer interrupt */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status |= CY_U3P_LPP_GPIO_INTR;

		/* Update the threshold and period */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].threshold = 0xFFFFFFFF;
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].period = 0xFFFFFFFF;

		/* Disable interrupts */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status &= ~(CY_U3P_LPP_GPIO_INTRMODE_MASK);

#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "Exiting stream thread, %d generic stream buffers read.\r\n", numBuffersRead + 1);
#endif

		/* Set stream done flag if kill early event was processed (otherwise must be explicitly invoked by FX3 API) */
		if(KillStreamEarly)
		{
			CyU3PEventSet (&EventHandler, ADI_GENERIC_STREAM_DONE, CYU3P_EVENT_OR);
		}
	}
	else
	{
		/* Increment buffer counter */
		numBuffersRead++;
		/* Wait for the complex GPIO timer to reach the stall time if no data ready */
		if(!FX3State.DrActive)
		{
			while(!(GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_INTR));
		}
		/* Reset flag */
		CyU3PEventSet (&EventHandler, ADI_GENERIC_STREAM_ENABLE, CYU3P_EVENT_OR);
	}
	/* Return status code */
	return status;
}

/**
  * @brief This is the worker function for the ADcmXL real time stream.
  *
  * @return A status code representing the success of the real time stream operation.
  *
  * The operation of this function is very similar to the Burst Stream function. This implementation
  * is slightly more stream lined to allow for the very tight tolerances on the ADcmXL3021 stream modes.
 **/
static CyU3PReturnStatus_t AdiRealTimeStreamWork()
{
	/* Static variables persist through function calls, are initialized to 0 */
	static uint32_t numFramesCaptured = 0;

	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	CyBool_t interruptTriggered = CyFalse;

	/* Clear GPIO interrupts */
	GPIO->lpp_gpio_simple[FX3State.DrPin] |= CY_U3P_LPP_GPIO_INTR;
	/* Wait for GPIO interrupt flag to be set and pin to be positive (interrupt configured for positive edge) */
	while(!interruptTriggered)
	{
		interruptTriggered = ((CyBool_t)(GPIO->lpp_gpio_intr0 & (1 << FX3State.DrPin)) && (CyBool_t)(GPIO->lpp_gpio_simple[FX3State.DrPin] & CY_U3P_LPP_GPIO_IN_VALUE));
	}

	/* Set the config for DMA mode */
	SPI->lpp_spi_config |= CY_U3P_LPP_SPI_DMA_MODE;

	/* Set the Tx/Rx count */
	SPI->lpp_spi_tx_byte_count = 0;
	SPI->lpp_spi_rx_byte_count = StreamThreadState.BytesPerFrame;

	/* Enable Rx and Tx as required */
	SPI->lpp_spi_config |= CY_U3P_LPP_SPI_RX_ENABLE;

	/* Enable the SPI block */
	SPI->lpp_spi_config |= CY_U3P_LPP_SPI_ENABLE;

	/* Wait for transfer to finish */
	status = CyU3PSpiWaitForBlockXfer(CyTrue);
	if (status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamThread_c, __LINE__, status);
	}

	/* Check that we haven't captured the desired number of frames or were asked to kill the thread early */
	if((numFramesCaptured >= (StreamThreadState.NumRealTimeCaptures - 1)) || KillStreamEarly)
	{
		/* Disable SPI DMA transfer */
		status = CyU3PSpiDisableBlockXfer(CyTrue, CyTrue);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(StreamThread_c, __LINE__, status);
		}
		/* Clear GPIO interrupts */
		GPIO->lpp_gpio_simple[FX3State.DrPin] |= CY_U3P_LPP_GPIO_INTR;

		/* Send whatever is in the buffer over to the PC */
		status = CyU3PDmaChannelSetWrapUp(&StreamingChannel);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(StreamThread_c, __LINE__, status);
		}

		/* Reset frame counter */
		numFramesCaptured = 0;

		/* Set stream done flag if kill early event was processed (otherwise must be explicitly invoked by FX3 API) */
		if(KillStreamEarly)
		{
			CyU3PEventSet(&EventHandler, ADI_RT_STREAM_DONE, CYU3P_EVENT_OR);
		}

#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "Exiting stream thread, %d real time frames read.\r\n", numFramesCaptured + 1);
#endif
	}
	else
	{
		/* Increment the frame counter */
		numFramesCaptured++;
		/* Reset real-time data capture thread flag */
		CyU3PEventSet(&EventHandler, ADI_RT_STREAM_ENABLE, CYU3P_EVENT_OR);
	}
	return status;
}

/**
  * @brief This is the worker function for the burst stream.
  *
  * @return A status code representing the success of the burst stream operation.
  *
  * This function performs all the SPI and USB transfers for a single burst in IMU
  * burst mode. It can be configured to transfer an arbitrary number of bytes in a single
  * SPI transaction, with optional data ready triggering.
 **/
static CyU3PReturnStatus_t AdiBurstStreamWork()
{
	/* Static variables persist through function calls, are initialized to 0*/
	static uint32_t numBuffersRead = 0;

	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	CyBool_t interruptTriggered;

#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "Burst stream thread entered.\r\n");
#endif

	/* Set up DMA to read registers from CPU memory */
	status = CyU3PDmaChannelSetupSendBuffer(&MemoryToSPI, &SpiDmaBuffer);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamThread_c, __LINE__, status);
	}

	/* Wait for DR if enabled */
	if (FX3State.DrActive)
	{
		/* Clear GPIO interrupts */
		GPIO->lpp_gpio_simple[FX3State.DrPin] |= CY_U3P_LPP_GPIO_INTR;
		/* Loop until interrupt is triggered */
		interruptTriggered = CyFalse;
		while(!interruptTriggered)
		{
			interruptTriggered = ((CyBool_t)(GPIO->lpp_gpio_intr0 & (1 << FX3State.DrPin)) || ((numBuffersRead == 0) && (GPIO->lpp_gpio_simple[FX3State.DrPin] & CY_U3P_LPP_GPIO_IN_VALUE)));
		}
	}

	/* Set the config for DMA mode with RX and TX enabled */
	SPI->lpp_spi_config |= CY_U3P_LPP_SPI_DMA_MODE;

	/* Set the Tx/Rx count */
	SPI->lpp_spi_tx_byte_count = StreamThreadState.TransferByteLength;
	SPI->lpp_spi_rx_byte_count = StreamThreadState.TransferByteLength;

	/* Enable SPI Rx and Tx */
	SPI->lpp_spi_config |= (CY_U3P_LPP_SPI_RX_ENABLE | CY_U3P_LPP_SPI_TX_ENABLE);

	/* Enable the SPI block */
	SPI->lpp_spi_config |= CY_U3P_LPP_SPI_ENABLE;

	/* Wait for SPI transfer to finish */
	status = CyU3PSpiWaitForBlockXfer(CyTrue);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(StreamThread_c, __LINE__, status);
	}

	/* Check that we haven't captured the desired number of frames or that we were asked to kill the thread early */
	if((numBuffersRead >= (StreamThreadState.NumBuffers - 1)) || KillStreamEarly)
	{
		/* Disable the SPI DMA transfer */
		status = CyU3PSpiDisableBlockXfer(CyTrue, CyTrue);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(StreamThread_c, __LINE__, status);
		}

		/* Send whatever is in the buffer over to the PC */
		status = CyU3PDmaChannelSetWrapUp(&StreamingChannel);
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(StreamThread_c, __LINE__, status);
		}

		/* Clear GPIO interrupts */
		GPIO->lpp_gpio_simple[FX3State.DrPin] |= CY_U3P_LPP_GPIO_INTR;
		/* Reset frame counter */
		numBuffersRead = 0;

		/* Set stream done flag if kill early event was processed (otherwise must be explicitly invoked by FX3 API) */
		if(KillStreamEarly)
		{
			CyU3PEventSet(&EventHandler, ADI_BURST_STREAM_DONE, CYU3P_EVENT_OR);
		}

#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "Exiting stream thread, %d burst stream buffers read.\r\n", numBuffersRead + 1);
#endif

	}
	else
	{
		/* Increment the frame counter */
		numBuffersRead++;
		/* Reset the real-time data capture thread flag */
		CyU3PEventSet (&EventHandler, ADI_BURST_STREAM_ENABLE, CYU3P_EVENT_OR);
	}
	return status;
}

/**
  * @brief This is the worker function for the transfer stream.
  *
  * @return A status code representing the success of the transfer stream operation.
  *
  * Transfer stream is used to implement protocol agnostic SPI transfers. This is useful for sensors which
  * implement a non-standard SPI protocol (CRC/Metadata/Weird bit lengths, etc). The MOSI data to be
  * sent is stored in USBBuffer[14 ...] prior to this function being called
 **/
static CyU3PReturnStatus_t AdiTransferStreamWork()
{
	/* Track the current position within the DMA buffer*/
	static uint8_t *bufPtr = 0;

	/* Track the number of buffers read */
	static uint32_t numBuffersRead = 0;

	/* Track the number of bytes read into the current DMA buffer */
	static int32_t byteCounter = 0;

	/* DMA buffer structure for the active buffer for the streaming DMA channel */
	static CyU3PDmaBuffer_t StreamChannelBuffer = {0};

	/* Return status code */
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

	/* Track index within the USBBuffer */
	uint32_t MOSIDataCount;

	/* Track current capture count */
	uint32_t captureCount;

	/* array to hold the MOSI data */
	uint8_t* MOSIData;

	/* Flag to track first run */
	CyBool_t firstTransfer = CyTrue;

	/* If the stream channel buffer has not been set, get a new buffer */
	if(bufPtr == 0)
	{
		/* get the buffer */
		status = CyU3PDmaChannelGetBuffer (&StreamingChannel, &StreamChannelBuffer, CYU3P_WAIT_FOREVER);
		if (status != CY_U3P_SUCCESS)
		{
			AdiLogError(StreamThread_c, __LINE__, status);
		}
		bufPtr = StreamChannelBuffer.buffer;
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "Got the first transfer stream DMA buffer, address = 0x%x\r\n", bufPtr);
#endif
	}

	/* Wait for DR if enabled */
	if (FX3State.DrActive)
	{
		/* Loop until interrupt is triggered */
		while(!(GPIO->lpp_gpio_intr0 & (1 << FX3State.DrPin)));
		/* Clear data ready interrupt */
		GPIO->lpp_gpio_simple[FX3State.DrPin] |= CY_U3P_LPP_GPIO_INTR;
	}

	/* Set the pin timer to 0 */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].timer = 0;
	/* clear interrupt flag */
	GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status |= CY_U3P_LPP_GPIO_INTR;

	for(captureCount = 0; captureCount < StreamThreadState.NumCaptures; captureCount++)
	{
		/* Set the MOSI pointer to the base address of the USB Buffer*/
		MOSIData = USBBuffer;
		/* Increment by 17 so it now points at first MOSI data value */
		MOSIData += 17;
		for(MOSIDataCount = 0; MOSIDataCount < StreamThreadState.BytesPerBuffer; MOSIDataCount += 4)
		{
			/* Wait for the complex GPIO timer to reach the stall time (except on first word) */
			if(!firstTransfer)
				while(!(GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_INTR));
			else
				firstTransfer = CyFalse;

			/* Transfer data */
			AdiSpiTransferWord(MOSIData, bufPtr);

			/* Set the pin timer to 0 */
			GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].timer = 0;

			/* clear timer interrupt flag */
			GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status |= CY_U3P_LPP_GPIO_INTR;

			/* Update counters and buffer pointers */
			bufPtr += 4;
			byteCounter += 4;
			MOSIData += 4;

			/* Check if a transmission is needed */
			if (byteCounter >= (StreamThreadState.BytesPerUsbPacket - 1))
			{
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Transfer steam DMA transmit started. Buffers Read = %d\r\n", numBuffersRead);
#endif
				/* Commit DMA buffer */
				status = CyU3PDmaChannelCommitBuffer (&StreamingChannel, FX3State.UsbBufferSize, 0);
				if (status != CY_U3P_SUCCESS)
				{
					AdiLogError(StreamThread_c, __LINE__, status);
				}

				/* Get new buffer */
				status = CyU3PDmaChannelGetBuffer (&StreamingChannel, &StreamChannelBuffer, CYU3P_WAIT_FOREVER);
				if (status != CY_U3P_SUCCESS)
				{
					AdiLogError(StreamThread_c, __LINE__, status);
				}
				bufPtr = StreamChannelBuffer.buffer;
				byteCounter = 0;
			}
		}
	}

	/* Check to see if we've captured enough buffers or if we were asked to stop data capture early */
	if ((numBuffersRead >= (StreamThreadState.NumBuffers - 1)) || KillStreamEarly)
	{

#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "Exiting stream thread, %d transfer stream buffers read.\r\n", numBuffersRead + 1);
#endif

		/* Reset values */
		numBuffersRead = 0;
		/* Signal getting a new buffer */
		bufPtr = 0;
		if (byteCounter)
		{
			status = CyU3PDmaChannelCommitBuffer (&StreamingChannel, FX3State.UsbBufferSize, 0);
			if (status != CY_U3P_SUCCESS)
			{
				AdiLogError(StreamThread_c, __LINE__, status);
			}
			byteCounter = 0;
		}

		/* Clear GPIO interrupts */
		GPIO->lpp_gpio_simple[FX3State.DrPin] |= CY_U3P_LPP_GPIO_INTR;

		/* Clear timer interrupt */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status |= CY_U3P_LPP_GPIO_INTR;

		/* Update the threshold and period */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].threshold = 0xFFFFFFFF;
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].period = 0xFFFFFFFF;

		/* Disable interrupts */
		GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status &= ~(CY_U3P_LPP_GPIO_INTRMODE_MASK);

		/* Set stream done flag if kill early event was processed (otherwise must be explicitly invoked by FX3 API) */
		if(KillStreamEarly)
		{
			CyU3PEventSet (&EventHandler, ADI_TRANSFER_STREAM_DONE, CYU3P_EVENT_OR);
		}
	}
	else
	{
		/* Increment buffer counter */
		numBuffersRead++;
		/* Wait for the complex GPIO timer to reach the stall time if no data ready */
		if(!FX3State.DrActive)
		{
			while(!(GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & CY_U3P_LPP_GPIO_INTR));
		}
		/* Reset flag */
		CyU3PEventSet (&EventHandler, ADI_TRANSFER_STREAM_ENABLE, CYU3P_EVENT_OR);
	}
	return status;
}


