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
  * @file		I2cFunctions.c
  * @date		5/13/2020
  * @author		A. Nolan (alex.nolan@analog.com)
  * @brief 		Implementation file for USB-I2C interfacing module
 **/

#include "I2cFunctions.h"

/** Reference to needed globals (defined in main) */
extern uint8_t USBBuffer[4096];
extern uint8_t BulkBuffer[12288];
extern CyU3PDmaBuffer_t ManualDMABuffer;
extern CyU3PDmaChannel ChannelToPC;
extern BoardState FX3State;

/**
  * @brief Handler for I2C read command from control endpoint
  *
  * @param RequestLength Number of bytes received over control endpoint
  *
  * @return A status code indicating the success of the I2C read command
  *
  * This function uses the I2C peripheral in register mode to perform a
  * single transfer. The number of bytes read in a single transfer
  * can be 0 bytes - 12KB.
 **/
CyU3PReturnStatus_t AdiI2CReadHandler(uint16_t RequestLength)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint16_t bytesRead;
	uint32_t timeout, numBytes;
	CyU3PI2cPreamble_t preamble = {.buffer = {0}};

	/* Get data from control endpoint */
	status = CyU3PUsbGetEP0Data(RequestLength, USBBuffer, &bytesRead);
	if(status != CY_U3P_SUCCESS)
		return status;

	/* Parse USB Buffer */
	I2CParseUSBBuffer(&timeout, &numBytes, &preamble);

	/* Clamp numbytes */
	if(numBytes > 12288)
		numBytes = 12288;

	/* Apply I2C timeout (arguments are in microseconds) */
	timeout = timeout * 1000;
	CyU3PI2cSetTimeout(timeout, timeout, timeout);

	/* Perform transfer, starting at offset 4 in bulk buffer */
	status = CyU3PI2cReceiveBytes(&preamble, BulkBuffer, numBytes, FX3State.I2CRetryCount);

	/* Put status in first 4 bytes sent back */
	BulkBuffer[0] = status & 0xFF;
	BulkBuffer[1] = (status & 0xFF00) >> 8;
	BulkBuffer[2] = (status & 0xFF0000) >> 16;
	BulkBuffer[3] = (status & 0xFF000000) >> 24;

	/* Send data to PC */
	ManualDMABuffer.buffer = BulkBuffer;
	ManualDMABuffer.size = 12288;
	ManualDMABuffer.count = numBytes;
	CyU3PDmaChannelSetupSendBuffer(&ChannelToPC, &ManualDMABuffer);

	/* Return status code */
	return status;
}

/**
  * @brief Handler for I2C write command from control endpoint
  *
  * @param RequestLength Number of bytes received over control endpoint
  *
  * @return A status code indicating the success of the I2C write command
  *
  * This function uses the I2C peripheral in register mode to perform a
  * single transfer. The number of bytes written in a single transfer is
  * limited to ~4070.
 **/
CyU3PReturnStatus_t AdiI2CWriteHandler(uint16_t RequestLength)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint16_t bytesRead = 0;
	uint32_t timeout, numBytes, index;
	CyU3PI2cPreamble_t preamble = {.buffer = {0}};
	uint8_t * bufIndex;

	/* Get data from control endpoint */
	status = CyU3PUsbGetEP0Data(RequestLength, USBBuffer, &bytesRead);
	if(status != CY_U3P_SUCCESS)
		return status;

	/* Parse USB Buffer */
	index = I2CParseUSBBuffer(&timeout, &numBytes, &preamble);

	/* Get index within USB buffer where write data starts */
	bufIndex = USBBuffer + index;

	/* Apply I2C timeout (arguments are in microseconds) */
	timeout = timeout * 1000;
	CyU3PI2cSetTimeout(timeout, timeout, timeout);

	status = CyU3PI2cTransmitBytes(&preamble, bufIndex, numBytes, FX3State.I2CRetryCount);
	if(status != CY_U3P_SUCCESS)
		AdiLogError(I2cFunctions_c, __LINE__, status);

	return status;
}

/**
  * @brief Init I2C peripheral
  *
  * @param BitRate Bit rate to configure I2C peripheral for (100KHz - 1MHz)
  *
  * @param isDMA If the I2C peripheral should be configured for DMA
  *
  * @return A status code indicating the success of I2C init operation.
 **/
CyU3PReturnStatus_t AdiI2CInit(uint32_t BitRate, CyBool_t isDMA)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
    CyU3PI2cConfig_t i2cConfig = {0};

    /* Filter bit rate */
    if(BitRate < 100000)
    	BitRate = 100000;

    if(BitRate > 1000000)
    	BitRate = 1000000;

	/* De-init */
	CyU3PI2cDeInit();
	/* Initialize and configure the I2C master module. */
    status = CyU3PI2cInit();
    if (status != CY_U3P_SUCCESS)
    {
    	/* Try and log error to flash - needs I2C to log, so might not actually work */
    	AdiLogError(I2cFunctions_c, __LINE__, status);
    	return status;
    }

    /* Start the I2C master block. Set i2c clock of 100KHz, DMA mode */
    CyU3PMemSet((uint8_t *)&i2cConfig, 0, sizeof(i2cConfig));
    i2cConfig.bitRate    = BitRate;
    i2cConfig.busTimeout = 0xFFFFFFFF;
    i2cConfig.dmaTimeout = 0xFFFF;
    i2cConfig.isDma      = isDMA;
    status = CyU3PI2cSetConfig (&i2cConfig, NULL);

    /* Save bit rate */
    FX3State.I2CBitRate = BitRate;

    /* Return status code */
	return status;
}

/**
  * @brief Parses I2C command data from the USB Buffer. Used for read/write/stream
  *
  * @param timeout Timeout value for I2C transaction. Return by reference.
  *
  * @param numBytes Number of bytes field in USBBuffer. Return by reference
  *
  * @param preamble I2C preamble struct stored in USBBuffer. Return by reference
  *
  * @return Index for the start of the I2C transmit data (if it exists)
 **/
uint32_t I2CParseUSBBuffer(uint32_t * timeout, uint32_t * numBytes, CyU3PI2cPreamble_t * preamble)
{
	uint32_t index = 0;

	/* Parse num bytes */
	*numBytes = USBBuffer[0];
	*numBytes |= (USBBuffer[1] << 8);
	*numBytes |= (USBBuffer[2] << 16);
	*numBytes |= (USBBuffer[3] << 24);

	/* Parse timeout */
	*timeout = USBBuffer[4];
	*timeout |= (USBBuffer[5] << 8);
	*timeout |= (USBBuffer[6] << 16);
	*timeout |= (USBBuffer[7] << 24);

	/* Parse pre-amble */
	preamble->length = USBBuffer[8];
	preamble->ctrlMask = USBBuffer[9];
	preamble->ctrlMask |= (USBBuffer[10] << 8);
	for(int i = 0; i < preamble->length; i++)
	{
		index = 11 + i;
		preamble->buffer[i] = USBBuffer[index];
	}
	/* Increment index to point at first write data */
	index++;

	return index;
}




