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
  * @file		SpiFunctions.c
  * @date		8/1/2019
  * @author		A. Nolan (alex.nolan@analog.com)
  * @author 	J. Chong (juan.chong@analog.com)
  * @brief		This file contains all generic SPI read/write function implementations.
 **/

#include "SpiFunctions.h"

/* Private function prototypes */
static void AdiBitBangSpiTransferCPHA0(uint8_t * MOSI, uint8_t* MISO, uint32_t BitCount, BitBangSpiConf config);
static void AdiBitBangSpiTransferCPHA1(uint8_t * MOSI, uint8_t* MISO, uint32_t BitCount, BitBangSpiConf config);
static CyU3PReturnStatus_t AdiBitBangSpiSetup(BitBangSpiConf config);
static void AdiWaitForSpiNotBusy();

/* Tell the compiler where to find the needed globals */
extern BoardState FX3State;
extern StreamState StreamThreadState;
extern CyU3PDmaBuffer_t ManualDMABuffer;
extern CyU3PDmaChannel ChannelToPC;
extern uint8_t USBBuffer[4096];
extern uint8_t BulkBuffer[12288];

/** Pointer to bit bang SPI SCLK pin */
static uvint32_t *SCLKPin;

/** Pointer to bit bang SPI CS pin */
static uvint32_t *CSPin;

/** Pointer to bit bang SPI MISO pin */
static uvint32_t *MISOPin;

/** Pointer to bit bang SPI MOSI pin */
static uvint32_t *MOSIPin;

/** SCLK active setting. Based on CPOL */
static uint32_t SCLKActiveMask;

/** SCLK idle setting. Based on CPOL */
static uint32_t SCLKInactiveMask;

/** SCLK low period offset */
static uint32_t SCLKLowTime;

/** Word length for a register based SPI tranfer */
static uint32_t SPIWordLen;

/**
  * @brief Set up the SPI for a register based transfer
  *
  * @returns void
  *
  * The process is as follows:
  *
  * (1) Disable SPI
  * (2) Disable interrupts
  * (3) Ensure Rx and Tx clear is disabled
  * (4) Clear FIFOs
  * (5) Set the file scope SPI word length variable
 **/
void AdiSpiPrepareForTransfer()
{
    /* Disable SPI block */
    SPI->lpp_spi_config &= ~(CY_U3P_LPP_SPI_ENABLE);

	/* Disable tx/rx clear flags */
    SPI->lpp_spi_config &= ~(CY_U3P_LPP_SPI_TX_CLEAR | CY_U3P_LPP_SPI_RX_CLEAR);

	/* Disable interrupts */
	SPI->lpp_spi_intr_mask = 0;

	/* Reset FIFO */
	AdiSpiResetFifo(CyTrue, CyTrue);

    /* Get the wordLen in bytes. Min. 1 byte */
    SPIWordLen = ((SPI->lpp_spi_config & CY_U3P_LPP_SPI_WL_MASK) >> CY_U3P_LPP_SPI_WL_POS);
    if ((SPIWordLen & 0x07) != 0)
    {
    	SPIWordLen = (SPIWordLen >> 3) + 1;
    }
    else
    {
    	SPIWordLen = (SPIWordLen >> 3);
    }
}

/**
  * @brief Bi-directional SPI transfer function, in register mode. Optimized for speed.
  *
  * @return void
  *
  * This function is used to allow for a reduced SPI stall time. Is fairly "unsafe" in that
  * all hardware has to be configured for correct operation, and free, before this function
  * can be called.
 **/
void AdiSpiTransferWord(uint8_t *txBuf, uint8_t *rxBuf)
{
	uint32_t temp;

    /* Enable the TX and RX bits. */
    SPI->lpp_spi_config |= CY_U3P_LPP_SPI_TX_ENABLE | CY_U3P_LPP_SPI_RX_ENABLE;

    /* Re-enable SPI block. */
    SPI->lpp_spi_config |= CY_U3P_LPP_SPI_ENABLE;

    /* Place data in egress register */
    temp = 0;
    switch (SPIWordLen)
    {
        case 4:
            temp |= (txBuf[3] << 24);
            //no break
        case 3:
            temp |= (txBuf[2] << 16);
            //no break
        case 2:
            temp |= (txBuf[1] << 8);
            //no break
        default:
            temp |= txBuf[0];
            break;
    }
    SPI->lpp_spi_egress_data = temp;

    /* Wait for tx/rx done interrupt */
    while ((SPI->lpp_spi_status & (CY_U3P_LPP_SPI_RX_DATA | CY_U3P_LPP_SPI_TX_SPACE)) != (CY_U3P_LPP_SPI_RX_DATA | CY_U3P_LPP_SPI_TX_SPACE));

    /* Get ingress data */
    temp = SPI->lpp_spi_ingress_data;

    /* Apply to buffer */
    switch (SPIWordLen)
    {
        case 4:
        	/* Word length of 4 bytes */
            rxBuf[3] = (uint8_t)((temp >> 24) & 0xFF);
            //no break
        case 3:
        	/* Word length of 3 bytes */
            rxBuf[2] = (uint8_t)((temp >> 16) & 0xFF);
            //no break
        case 2:
        	/* Word length of 2 bytes */
            rxBuf[1] = (uint8_t)((temp >> 8) & 0xFF);
            //no break
        default:
        	/* Word length of 0.5 - 1 bytes */
            rxBuf[0] = (uint8_t)(temp & 0xFF);
            break;
    }

    /* Disable the TX and RX. */
    SPI->lpp_spi_config &= ~(CY_U3P_LPP_SPI_TX_ENABLE | CY_U3P_LPP_SPI_RX_ENABLE);

    /* Disable SPI block */
    SPI->lpp_spi_config &= ~(CY_U3P_LPP_SPI_ENABLE);
}

/**
  * @brief This function restarts the SPI controller.
  *
  * @return A status code indicating the success of the setConfig call after re-initialization.
  *
  * This function can be used to restore hardware SPI functionality after overriding the SPI pins
  * to act as a bit-banged SPI port. This function can be called before or after the SPI controller
  * has been initialized without causing problems. This function may cause erroneous toggles on the
  * SPI lines during the initialization process - be careful to ensure that the connected DUT is not
  * particularly sensitive to extra toggles.
 **/
CyU3PReturnStatus_t AdiRestartSpi()
{
	/* Status code for SPI init */
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	/* Deactivate SPI controller */
	CyU3PSpiDeInit();
	/* Restore pins */
	CyU3PDeviceGpioRestore(53);
	CyU3PDeviceGpioRestore(54);
	CyU3PDeviceGpioRestore(55);
	CyU3PDeviceGpioRestore(56);
	/* Reinitialized */
	CyU3PSpiInit();
	/* Set the prior config */
	status = CyU3PSpiSetConfig(&FX3State.SpiConfig, NULL);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(SpiFunctions_c, __LINE__, status);
	}
	return status;
}


/**
  * @brief This function handles bit bang SPI requests from the control endpoint.
  *
  * @returns A status code indicating the success of the SPI bitbang operation.
  *
  * This function requires all data to have been retrieved from the control endpoint before being
  * called. It parses all the parameters about the current bit bang SPI operation to perform from
  * the transaction. The pins/timing/config is sent from the FX3 API to the firmware with each
  * bitbang SPI transaction.
 **/
CyU3PReturnStatus_t AdiBitBangSpiHandler()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	BitBangSpiConf config = {0};
	uint32_t bitsPerTransfer;
	uint32_t numTransfers;
	uint32_t transferCounter;
	uint32_t bitBangStallTime;
	register uvint32_t cycleTimer;

	/* Buffer pointers */
	uint8_t * MOSIPtr;
	uint8_t * MISOPtr;

	/* Parse data from the USB buffer */
	config.SCLK = USBBuffer[0];
	config.CS = USBBuffer[1];
	config.MOSI = USBBuffer[2];
	config.MISO = USBBuffer[3];
	config.HalfClockDelay = USBBuffer[4];
	config.HalfClockDelay |= (USBBuffer[5] << 8);
	config.HalfClockDelay |= (USBBuffer[6] << 16);
	config.HalfClockDelay |= (USBBuffer[7] << 24);
	config.CSLeadDelay = USBBuffer[8];
	config.CSLeadDelay |= (USBBuffer[9] << 8);
	config.CSLagDelay = USBBuffer[10];
	config.CSLagDelay |= (USBBuffer[11] << 8);
	bitBangStallTime = USBBuffer[12];
	bitBangStallTime |= (USBBuffer[13] << 8);
	bitBangStallTime |= (USBBuffer[14] << 16);
	bitBangStallTime |= (USBBuffer[15] << 24);
	config.CPHA = USBBuffer[16];
	config.CPOL = USBBuffer[17];
	bitsPerTransfer = USBBuffer[18];
	bitsPerTransfer |= (USBBuffer[19] << 8);
	bitsPerTransfer |= (USBBuffer[20] << 16);
	bitsPerTransfer |= (USBBuffer[21] << 24);
	numTransfers = USBBuffer[22];
	numTransfers |= (USBBuffer[23] << 8);
	numTransfers |= (USBBuffer[24] << 16);
	numTransfers |= (USBBuffer[25] << 24);

	/* apply offset to stall */
	if(bitBangStallTime > STALL_COUNT_OFFSET)
		bitBangStallTime -= STALL_COUNT_OFFSET;
	else
		bitBangStallTime = 0;

	/* Memclear the bulk buffer */
	CyU3PMemSet (BulkBuffer, 0, sizeof(BulkBuffer));

	/* Start MISO pointer at bulk buffer */
	MISOPtr = BulkBuffer;

	/* Start MOSI pointer at USBBuffer[26], first transmit data bit */
	MOSIPtr = USBBuffer;
	MOSIPtr += 26;

	/* Setup the GPIO selected */
	status = AdiBitBangSpiSetup(config);
	if(status == CY_U3P_SUCCESS)
	{
		/* Perform transfers */
		for(transferCounter = 0; transferCounter < numTransfers; transferCounter++)
		{
			/* Transfer data */
			if(config.CPHA)
				AdiBitBangSpiTransferCPHA1(MOSIPtr, MISOPtr, bitsPerTransfer, config);
			else
				AdiBitBangSpiTransferCPHA0(MOSIPtr, MISOPtr, bitsPerTransfer, config);
			/* Update buffer pointers */
			MOSIPtr += bitsPerTransfer;
			MISOPtr += bitsPerTransfer;
			/* Wait for stall time */
			cycleTimer = bitBangStallTime;
			while(cycleTimer > 0)
				cycleTimer--;
		}
	}

	/* Return MISO data over bulk buffer */
	ManualDMABuffer.buffer = BulkBuffer;
	ManualDMABuffer.size = sizeof(BulkBuffer);
	ManualDMABuffer.count = numTransfers * bitsPerTransfer;

	/* Send the data to PC */
	status = CyU3PDmaChannelSetupSendBuffer(&ChannelToPC, &ManualDMABuffer);
	if(status != CY_U3P_SUCCESS)
	{
		AdiLogError(SpiFunctions_c, __LINE__, status);
	}

	return status;
}

/**
  * @brief Configures all pins and timers needed to bitbang a SPI connection.
  *
  * @param config A structure containing all the relevant bit banged SPI configuration parameters.
  *
  * @returns A status code indicating the success of the bitbang SPI setup process.
 **/
static CyU3PReturnStatus_t AdiBitBangSpiSetup(BitBangSpiConf config)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

	if(!CyU3PIsGpioValid(config.MOSI))
	{
		AdiLogError(SpiFunctions_c, __LINE__, status);
		return CY_U3P_ERROR_BAD_ARGUMENT;
	}
	if(!CyU3PIsGpioValid(config.SCLK))
	{
		AdiLogError(SpiFunctions_c, __LINE__, status);
		return CY_U3P_ERROR_BAD_ARGUMENT;
	}
	if(!CyU3PIsGpioValid(config.CS))
	{
		AdiLogError(SpiFunctions_c, __LINE__, status);
		return CY_U3P_ERROR_BAD_ARGUMENT;
	}
	if(!CyU3PIsGpioValid(config.MISO))
	{
		AdiLogError(SpiFunctions_c, __LINE__, status);
		return CY_U3P_ERROR_BAD_ARGUMENT;
	}

	/* Output config */
	CyU3PGpioSimpleConfig_t gpioConfig;
	gpioConfig.outValue = config.CPOL;
	gpioConfig.inputEn = CyFalse;
	gpioConfig.driveLowEn = CyTrue;
	gpioConfig.driveHighEn = CyTrue;
	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;

	/* Set SCLK as output driven based on CPOL setting */
	status = CyU3PGpioSetSimpleConfig(config.SCLK, &gpioConfig);
	if(status != CY_U3P_SUCCESS)
	{
		/* Override the pin to act as simple GPIO */
		CyU3PDeviceGpioOverride(config.SCLK, CyTrue);
		/* Set the config again */
		status = CyU3PGpioSetSimpleConfig(config.SCLK, &gpioConfig);
		/* Verify that override was successful */
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(SpiFunctions_c, __LINE__, status);
			return status;
		}
	}

	/* Set MOSI, CS as output pins driven high. */
	gpioConfig.outValue = CyTrue;
	status = CyU3PGpioSetSimpleConfig(config.CS, &gpioConfig);
	if(status != CY_U3P_SUCCESS)
	{
		/* Override the pin to act as simple GPIO */
		CyU3PDeviceGpioOverride(config.CS, CyTrue);
		/* Set the config again */
		status = CyU3PGpioSetSimpleConfig(config.CS, &gpioConfig);
		/* Verify that override was successful */
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(SpiFunctions_c, __LINE__, status);
			return status;
		}
	}

	status = CyU3PGpioSetSimpleConfig(config.MOSI, &gpioConfig);
	if(status != CY_U3P_SUCCESS)
	{
		/* Override the pin to act as simple GPIO */
		CyU3PDeviceGpioOverride(config.MOSI, CyTrue);
		/* Set the config again */
		status = CyU3PGpioSetSimpleConfig(config.MOSI, &gpioConfig);
		/* Verify that override was successful */
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(SpiFunctions_c, __LINE__, status);
			return status;
		}
	}

	/* Set MISO as input pin */
	gpioConfig.outValue = CyFalse;
	gpioConfig.inputEn = CyTrue;
	gpioConfig.driveLowEn = CyFalse;
	gpioConfig.driveHighEn = CyFalse;
	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
	status = CyU3PGpioSetSimpleConfig(config.MISO, &gpioConfig);
	if(status != CY_U3P_SUCCESS)
	{
		/* Override the pin to act as simple GPIO */
		CyU3PDeviceGpioOverride(config.MISO, CyTrue);
		/* Set the config again */
		status = CyU3PGpioSetSimpleConfig(config.MISO, &gpioConfig);
		/* Verify that override was successful */
		if(status != CY_U3P_SUCCESS)
		{
			AdiLogError(SpiFunctions_c, __LINE__, status);
			return status;
		}
	}

	/* Set pin pointers */
	MOSIPin = &GPIO->lpp_gpio_simple[config.MOSI];
	MISOPin = &GPIO->lpp_gpio_simple[config.MISO];
	CSPin = &GPIO->lpp_gpio_simple[config.CS];
	SCLKPin = &GPIO->lpp_gpio_simple[config.SCLK];

	/* Calculate wait value for short half of period */
	SCLKLowTime = config.HalfClockDelay + BITBANG_HALFCLOCK_OFFSET;

	/* Set the sclk active/inactive masks based on CPOL */
	if(config.CPOL)
	{
		/* Idle high, active low */
		SCLKActiveMask = GPIO_LOW;
		SCLKInactiveMask = GPIO_HIGH;
	}
	else
	{
		/* Idle low, active high */
		SCLKActiveMask = GPIO_HIGH;
		SCLKInactiveMask = GPIO_LOW;
	}
	return status;
}

/**
  * @brief Performs a single bit banged SPI transfer. Pins must already be configured as needed.
  *
  * @param MOSI A pointer to the master out data buffer. This data will be transmitted MSB first, over the MOSI line in config.
  *
  * @param MISO A pointer to the data receive (rx) buffer. The data received from a slave will be placed here.
  *
  * @param BitCount The number of bits to transfer.
  *
  * @param config The configuration settings to use for the transfer.
  *
  * This function allows a user to use any pins on the FX3 as a low speed SPI master. The API
  * provided is similar to the Cypress API for the hardware SPI. This function is fixed to operate in
  * CPHA mode 1 (update data on idle-active edge, sample on active-idle edge).
 **/
static void AdiBitBangSpiTransferCPHA1(uint8_t * MOSI, uint8_t* MISO, uint32_t BitCount, BitBangSpiConf config)
{
	/* Track the number of bits clocked */
	uint32_t bitCounter;
	register uvint32_t cycleTimer;

	/* Drop chip select */
	*CSPin = GPIO_LOW;

	/* Wait for CS lead delay */
	cycleTimer = config.CSLeadDelay;
	while(cycleTimer > 0)
		cycleTimer--;

	/* main transmission loop */
	for(bitCounter = 0; bitCounter < (BitCount - 1); bitCounter++)
	{
		/* Place output data bit on MOSI pin (approx. 150ns) */
		*MOSIPin = GPIO_LOW | MOSI[bitCounter];

		/* Toggle SCLK active */
		*SCLKPin = SCLKActiveMask;

		/* Wait HalfClock period (w/ added offset to make duty cycle 50%)*/
		cycleTimer = SCLKLowTime;
		while(cycleTimer > 0)
			cycleTimer--;

		/* Toggle SCLK inactive */
		*SCLKPin = SCLKInactiveMask;

		/* Sample MISO pin */
		MISO[bitCounter] = *MISOPin;

		/* Wait HalfClock period */
		cycleTimer = config.HalfClockDelay;
		while(cycleTimer > 0)
			cycleTimer--;
	}

	/* Perform last bit outside the loop to save some time */

	/* Place output data bit on MOSI pin */
	*MOSIPin = GPIO_LOW | MOSI[BitCount - 1];

	/* Toggle SCLK active */
	*SCLKPin = SCLKActiveMask;

	/* Wait HalfClock period (w/ added offset to make duty cycle 50%)*/
	cycleTimer = SCLKLowTime;
	while(cycleTimer > 0)
		cycleTimer--;

	/* Toggle SCLK inactive */
	*SCLKPin = SCLKInactiveMask;

	/* Sample MISO pin */
	MISO[BitCount - 1] = *MISOPin;

	/* Wait for CS lag delay */
	cycleTimer = config.CSLagDelay;
	while(cycleTimer > 0)
	{
		cycleTimer--;
	}

	/* Restore CS, MOSI to high */
	*CSPin = GPIO_HIGH;
	*MOSIPin = GPIO_HIGH;
}

/**
  * @brief Performs a single bit banged SPI transfer. Pins must already be configured as needed.
  *
  * @param MOSI A pointer to the master out data buffer. This data will be transmitted MSB first, over the MOSI line in config.
  *
  * @param MISO A pointer to the data receive (rx) buffer. The data received from a slave will be placed here.
  *
  * @param BitCount The number of bits to transfer.
  *
  * @param config The configuration settings to use for the transfer.
  *
  * This function allows a user to use any pins on the FX3 as a low speed SPI master. The API
  * provided is similar to the Cypress API for the hardware SPI. This function is fixed to operate in
  * CPHA mode 0 (sample data on idle-active edge, update data on active-idle edge).
 **/
static void AdiBitBangSpiTransferCPHA0(uint8_t * MOSI, uint8_t* MISO, uint32_t BitCount, BitBangSpiConf config)
{
	/* Track the number of bits clocked */
	uint32_t bitCounter;
	register uvint32_t cycleTimer;

	/* Drop chip select */
	*CSPin = GPIO_LOW;

	/* Load initial data bit to output */
	*MOSIPin = GPIO_LOW | MOSI[0];

	/* Wait for CS lead delay */
	cycleTimer = config.CSLeadDelay;
	while(cycleTimer > 0)
		cycleTimer--;

	/* main transmission loop */
	for(bitCounter = 0; bitCounter < BitCount; bitCounter++)
	{
		/* Toggle SCLK to active state */
		*SCLKPin = SCLKActiveMask;

		/* Sample MISO pin */
		MISO[bitCounter] = *MISOPin;

		/* Wait HalfClock period */
		cycleTimer = config.HalfClockDelay;
		while(cycleTimer > 0)
			cycleTimer--;

		/* Toggle SCLK inactive */
		*SCLKPin = SCLKInactiveMask;

		/* Place output data bit on MOSI pin (approx. 150ns) */
		*MOSIPin = GPIO_LOW | MOSI[bitCounter + 1];

		/* Wait HalfClock period (w/ added offset to make duty cycle 50%)*/
		cycleTimer = SCLKLowTime;
		while(cycleTimer > 0)
			cycleTimer--;
	}

	/* Wait for CS lag delay */
	cycleTimer = config.CSLagDelay;
	while(cycleTimer > 0)
	{
		cycleTimer--;
	}

	/* Restore CS, MOSI to high */
	*CSPin = GPIO_HIGH;
	*MOSIPin = GPIO_HIGH;
}

/**
  * @brief This function parses the SPI control registers into an easier to work with config struct.
  *
  * @return The current SPI config, as set in the SPI controller hardware.
  *
  * This function can be used to ensure synchronization between the SPI controller and the SPI
  * settings in firmware, without having to perform a SPI controller reset operation. Resetting
  * the SPI controller can cause undesired effects on the SPI lines.
 **/
CyU3PSpiConfig_t AdiGetSpiConfig()
{
	CyU3PSpiConfig_t conf = {0};
	uint32_t config_reg;

	AdiWaitForSpiNotBusy();

	/* Read SPI config register */
	config_reg = SPI->lpp_spi_config;

	/* Parse out SPI config */
	conf.wordLen = (config_reg >> CY_U3P_LPP_SPI_WL_POS) & 0x3F;
	conf.ssnPol = (config_reg >> 16) & 0x1;
	conf.lagTime = (config_reg >> CY_U3P_LPP_SPI_LAG_POS) & 0x3;
	conf.leadTime = (config_reg >> CY_U3P_LPP_SPI_LEAD_POS) & 0x3;
	conf.cpha = (config_reg >> 11) & 0x1;
	conf.cpol = (config_reg >> 10) & 0x1;
	conf.ssnCtrl = (config_reg >> CY_U3P_LPP_SPI_SSNCTRL_POS) & 0x3;
	conf.isLsbFirst = (config_reg >> 3) & 0x1;

	/* use existing clock setting */
	conf.clock = FX3State.SpiConfig.clock;
	return conf;
}

/**
  * @brief Prints a given SPI config over the UART debug port.
  *
  * @param config The SPI config structure to print out.
  *
  * @returns void
 **/
void AdiPrintSpiConfig(CyU3PSpiConfig_t config)
{
	CyU3PDebugPrint (4, "SPI Config: \r\nSCLK Freq: %d\r\n", config.clock);
	CyU3PDebugPrint (4, "CPHA: %d\r\n", config.cpha);
	CyU3PDebugPrint (4, "CPOL: %d\r\n", config.cpol);
	CyU3PDebugPrint (4, "LSB First: %d\r\n", config.isLsbFirst);
	CyU3PDebugPrint (4, "CS Lag Time: %d\r\n", config.lagTime);
	CyU3PDebugPrint (4, "CS Lead Time: %d\r\n", config.leadTime);
	CyU3PDebugPrint (4, "CS Control Mode: %d\r\n", config.ssnCtrl);
	CyU3PDebugPrint (4, "CS Polarity: %d\r\n", config.ssnPol);
	CyU3PDebugPrint (4, "Word Length: %d\r\n", config.wordLen);
}

/**
  * @brief This function performs a protocol agnostic SPI bi-directional SPI transfer of (1, 2, 4) bytes
  *
  * @param writeData The data to transmit on the MOSI line.
  *
  * @return A status code indicating the success of the function.
  *
  * This function performs a bi-directional SPI transfer, on up to 4 bytes of data. The transfer length is
  * determined by the current SPI config word length setting. The data received on the MISO line is placed
  * in USBBuffer[4 - 7] following the transfer.
 **/
CyU3PReturnStatus_t AdiTransferBytes(uint32_t writeData)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint8_t writeBuffer[4];
	uint8_t readBuffer[4] = {0};

	/* populate the writebuffer */
	writeBuffer[0] = writeData & 0xFF;
	writeBuffer[1] = (writeData & 0xFF00) >> 8;
	writeBuffer[2] = (writeData & 0xFF0000) >> 16;
	writeBuffer[3] = (writeData & 0xFF000000) >> 24;

	/* perform SPI transfer */
	AdiWaitForSpiNotBusy();
	AdiSpiTransferWord(writeBuffer, readBuffer);

	/* Load read data to be sent back via control endpoint */
	USBBuffer[4] = readBuffer[0];
	USBBuffer[5] = readBuffer[1];
	USBBuffer[6] = readBuffer[2];
	USBBuffer[7] = readBuffer[3];

	/* Return status code  */
	return status;
}

/**
  * @brief This function reads a single 16 bit SPI word from a slave device.
  *
  * @param addr The address to send to the DUT in the first SPI transaction.
  *
  * @return A status code indicating the success of the function.
  *
  * This function reads a single word over SPI. Note that reads are not "full duplex"
  * and will require a discrete read to set the address to be read from (two 16 bit transactions per read).
 **/
CyU3PReturnStatus_t AdiReadRegBytes(uint16_t addr)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint8_t tempBuffer[2];

	/* Set the second byte to 0's */
	tempBuffer[0] = 0;
	/* Set the address to read from */
	tempBuffer[1] = (0x7F) & addr;
	/* Send SPI Read command */
	status = CyU3PSpiTransmitWords(tempBuffer, 2);
	/* Check that the transfer was successful and end function if failed */
	if (status != CY_U3P_SUCCESS)
	{
		AdiLogError(SpiFunctions_c, __LINE__, status);
	}

	/* Stall for user-specified time */
	AdiSleepForMicroSeconds(FX3State.StallTime);

	/* Receive the data requested */
	status = CyU3PSpiReceiveWords(tempBuffer, 2);
	/* Check that the transfer was successful and end function if failed */
	if (status != CY_U3P_SUCCESS)
	{
		AdiLogError(SpiFunctions_c, __LINE__, status);
	}

	/* Send status and data back via control endpoint */
	USBBuffer[0] = status & 0xFF;
	USBBuffer[1] = (status & 0xFF00) >> 8;
	USBBuffer[2] = (status & 0xFF0000) >> 16;
	USBBuffer[3] = (status & 0xFF000000) >> 24;
	USBBuffer[4] = tempBuffer[0];
	USBBuffer[5] = tempBuffer[1];
	CyU3PUsbSendEP0Data (6, USBBuffer);

	return status;
}

/**
  * @brief This function writes a single byte of data over the SPI bus
  *
  * @param addr The DUT address to write data to (7 bits).
  *
  * @param data The byte of data to write to the address
  *
  * @return A status code indicating the success of the function.
  *
  * This function uses  the standard iSensor SPI protocol to issue a write command.
  * For the standard iSensor SPI parts, a write is performed in a single 16 bit command,
  * where the first bit clocked out is the write bit (high) followed by the address and data.
 **/
CyU3PReturnStatus_t AdiWriteRegByte(uint16_t addr, uint8_t data)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	uint8_t tempBuffer[2];
	tempBuffer[0] = data;
	tempBuffer[1] = 0x80 | addr;
	status = CyU3PSpiTransmitWords(tempBuffer, 2);
	/* Check that the transfer was successful and end function if failed */
	if (status != CY_U3P_SUCCESS)
	{
		AdiLogError(SpiFunctions_c, __LINE__, status);
	}
	/* Send write status over the control endpoint */
	USBBuffer[0] = status & 0xFF;
	USBBuffer[1] = (status & 0xFF00) >> 8;
	USBBuffer[2] = (status & 0xFF0000) >> 16;
	USBBuffer[3] = (status & 0xFF000000) >> 24;
	CyU3PUsbSendEP0Data (4, USBBuffer);

	return status;
}

/**
  * @brief Sets the SPI controller word length (4 - 32 bits)
  *
  * @param wordLength The number of bits to transfer in a single SPI transaction
  *
  * This function writes directly to the SPI control register. It does not cause the toggle
  * on the chip select line seen using the cypress API for setting the SPI word length.
 **/
void AdiSetSpiWordLength(uint8_t wordLength)
{
	uint32_t spiConf;
	/* Truncate input to 6 bits */
	wordLength &= 0x3F;
	/* Wait for any previous transactions */
	AdiWaitForSpiNotBusy();
	/* Read the SPI configuration register */
	spiConf = SPI->lpp_spi_config;
	/* Clear word length field*/
	spiConf &= ~(0x3F << CY_U3P_LPP_SPI_WL_POS);
	/* Sets bits 17 - 22 to wordlength */
	spiConf |= wordLength << CY_U3P_LPP_SPI_WL_POS;
	/* Write the new config value */
	SPI->lpp_spi_config = spiConf;
}

/**
  * @brief Waits for the SPI controller busy bit to be not set
  *
  * @returns void
 **/
static void AdiWaitForSpiNotBusy()
{
	while(SPI->lpp_spi_status & 1<<28);
}

/**
  * @brief This function resets the SPI FIFO and disables the SPI block after completion.
  *
  * @param isTx Boolean to indicate if you're clearing the TX FIFO
  *
  * @param isRx Boolean to indicate if you're clearing the RX FIFO
  *
  * @return The success of the SPI reset FIFO operation.
  *
  * It is a copy of the private CyU3PSpiResetFifo() function which bypasses some input sanitization which the Cypress
  * libraries perform. This is required due to our high-speed, register-initiated transfers.
 **/
CyU3PReturnStatus_t AdiSpiResetFifo(CyBool_t isTx, CyBool_t isRx)
{
	uint32_t intrMask;
	uint32_t ctrlMask = 0;
	uint32_t temp;

	/* No lock is acquired or error checked */

	/* Temporarily disable interrupts. */
	intrMask = SPI->lpp_spi_intr_mask;
	SPI->lpp_spi_intr_mask = 0;

	if (isTx)
	{
		ctrlMask = CY_U3P_LPP_SPI_TX_CLEAR;
	}
	if (isRx)
	{
		ctrlMask |= CY_U3P_LPP_SPI_RX_CLEAR;
	}

	/* Disable the SPI block and reset. */
	temp = ~(CY_U3P_LPP_SPI_RX_ENABLE | CY_U3P_LPP_SPI_TX_ENABLE |
		CY_U3P_LPP_SPI_DMA_MODE | CY_U3P_LPP_SPI_ENABLE);
	SPI->lpp_spi_config &= temp;
	while ((SPI->lpp_spi_config & CY_U3P_LPP_SPI_ENABLE) != 0);

	/* Clear the FIFOs and wait until they have been cleared. */
	SPI->lpp_spi_config |= ctrlMask;
	if (isTx)
	{
		while ((SPI->lpp_spi_status & CY_U3P_LPP_SPI_TX_DONE) == 0);
	}
	if (isRx)
	{
		while ((SPI->lpp_spi_status & CY_U3P_LPP_SPI_RX_DATA) != 0);
	}
	SPI->lpp_spi_config &= ~ctrlMask;

	/* Clear all interrupts and re-enable them. */
	SPI->lpp_spi_intr |= CY_U3P_LPP_SPI_TX_DONE;
	SPI->lpp_spi_intr_mask = intrMask;

	return CY_U3P_SUCCESS;
}

/**
  * @brief This function handles vendor commands to get the current SPI configuration from the FX3
  *
  * @return A status code indicating the success of the function.
  *
  * This function allows the FX3 API to verify that the FX3 board has the same SPI settings as the
  * current FX3 connection instance. The current configuration is sent to the PC via EP0.
 **/
CyU3PReturnStatus_t AdiGetSpiSettings()
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	/* Clock first */
	USBBuffer[0] = FX3State.SpiConfig.clock & 0xFF;
	USBBuffer[1] = (FX3State.SpiConfig.clock & 0xFF00) >> 8;
	USBBuffer[2] = (FX3State.SpiConfig.clock & 0xFF0000) >> 16;
	USBBuffer[3] = (FX3State.SpiConfig.clock & 0xFF000000) >> 24;
	USBBuffer[4] = FX3State.SpiConfig.cpha;
	USBBuffer[5] = FX3State.SpiConfig.cpol;
	USBBuffer[6] = FX3State.SpiConfig.isLsbFirst;
	USBBuffer[7] = FX3State.SpiConfig.lagTime;
	USBBuffer[8] = FX3State.SpiConfig.leadTime;
	USBBuffer[9] = FX3State.SpiConfig.ssnCtrl;
	USBBuffer[10] = FX3State.SpiConfig.ssnPol;
	USBBuffer[11] = FX3State.SpiConfig.wordLen;
	USBBuffer[12] = FX3State.StallTime & 0xFF;
	USBBuffer[13] = (FX3State.StallTime & 0xFF00) >> 8;
	USBBuffer[14] = FX3State.DutType;
	USBBuffer[15] = (CyBool_t) FX3State.DrActive;
	USBBuffer[16] = (CyBool_t) FX3State.DrPolarity;
	USBBuffer[17] = FX3State.DrPin & 0xFF;
	USBBuffer[18] = (FX3State.DrPin & 0xFF00) >> 8;
	USBBuffer[19] = S_TO_TICKS_MULT & 0xFF;
	USBBuffer[20] = (S_TO_TICKS_MULT & 0xFF00) >> 8;
	USBBuffer[21] = (S_TO_TICKS_MULT & 0xFF0000) >> 16;
	USBBuffer[22] = (S_TO_TICKS_MULT & 0xFF000000) >> 24;
	status = CyU3PUsbSendEP0Data (23, USBBuffer);
	return status;
}

/**
  * @brief This function handles a vendor command request to update the SPI/DR Pin configuration.
  *
  * @param index The wIndex from the control endpoint transaction which indicates which parameter to update
  *
  * @param value The wValue from the control endpoint transaction which holds the SPI value to set for the selected parameter.
  *
  * @param length The length of the Data In phase of the control endpoint transaction
  *
  * @return A boolean indicating if the SPI configuration was a success
  *
  * This function provides an API for maintaining synchronicity in SPI and data ready triggering settings between the FX3 API and
  * the firmware. Any time a setting is changed on the FX3 API, this function will be invoked to reflect that change.
 **/
CyBool_t AdiSpiUpdate(uint16_t index, uint16_t value, uint16_t length)
{
    uint32_t clockFrequency;
    uint16_t *bytesRead = 0;
	CyBool_t isHandled = CyTrue;
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	CyU3PUsbGetEP0Data(length, USBBuffer, bytesRead);
	switch(index)
	{
	case 0:
		/* Clock setting */
		if(length != 4)
		{
			/* Reasonable Default if data frame isn't set properly */
			FX3State.SpiConfig.clock = 2000000;
		}
		else
		{
			clockFrequency = USBBuffer[3];
			clockFrequency += USBBuffer[2] << 8;
			clockFrequency += USBBuffer[1] << 16;
			clockFrequency += USBBuffer[0] << 24;
			FX3State.SpiConfig.clock = clockFrequency;
			status = CyU3PSpiSetConfig (&FX3State.SpiConfig, NULL);
#ifdef VERBOSE_MODE
			CyU3PDebugPrint (4, "SCLK = %d\r\n", clockFrequency);
#endif
		}
		break;

	case 1:
		/* cpol */
		FX3State.SpiConfig.cpol = (CyBool_t) value;
		status = CyU3PSpiSetConfig (&FX3State.SpiConfig, NULL);
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "cpol = %d\r\n", value);
#endif
		break;

	case 2:
		/* cpha */
		FX3State.SpiConfig.cpha = (CyBool_t) value;
		status = CyU3PSpiSetConfig (&FX3State.SpiConfig, NULL);
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "cpha = %d\r\n", value);
#endif
		break;

	case 3:
		/* Chip Select Polarity */
		FX3State.SpiConfig.ssnPol = (CyBool_t) value;
		status = CyU3PSpiSetConfig (&FX3State.SpiConfig, NULL);
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "ssnPol = %d\r\n", value);
#endif
		break;

	case 4:
		/* Chip Select Control */
		FX3State.SpiConfig.ssnCtrl = (CyU3PSpiSsnCtrl_t) value;
		status = CyU3PSpiSetConfig (&FX3State.SpiConfig, NULL);
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "ssnCtrl = %d\r\n", value);
#endif
		break;

	case 5:
		/* Lead Time */
		FX3State.SpiConfig.leadTime = (CyU3PSpiSsnLagLead_t) value;
		status = CyU3PSpiSetConfig (&FX3State.SpiConfig, NULL);
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "leadTime = %d\r\n", value);
#endif
		break;

	case 6:
		/* Lag Time */
		FX3State.SpiConfig.lagTime = (CyU3PSpiSsnLagLead_t) value;
		status = CyU3PSpiSetConfig (&FX3State.SpiConfig, NULL);
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "lagTime = %d\r\n", value);
#endif
		break;

	case 7:
		/* Is LSB First */
		FX3State.SpiConfig.isLsbFirst = (CyBool_t) value;
		status = CyU3PSpiSetConfig (&FX3State.SpiConfig, NULL);
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "isLsbFirst = %d\r\n", value);
#endif
		break;

	case 8:
		/* Word Length */
		FX3State.SpiConfig.wordLen = value & 0xFF;
		status = CyU3PSpiSetConfig (&FX3State.SpiConfig, NULL);
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "wordLen = %d\r\n", value);
#endif
		break;

	case 9:
		/* Stall time in ticks (received in ticks from the PC, each tick = 1us) */
		FX3State.StallTime = value;
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "stallTime = %d\r\n", value);
#endif
		break;

	case 10:
		/* DUT type */
		FX3State.DutType = value;
		switch(FX3State.DutType)
		{
		case ADcmXL3021:
			/* (32 word x 3 axis) + 4 word status/counter/etc */
			StreamThreadState.BytesPerFrame = 200;
			break;
		case ADcmXL2021:
			/* (32 word x 2 axis) + 8 word padding + 4 word status/counter/etc */
			StreamThreadState.BytesPerFrame = 152;
			break;
		case ADcmXL1021:
			/* 32 word + 8 word padding + 4 word status/counter/etc */
			StreamThreadState.BytesPerFrame = 88;
			break;
		case IMU:
		case LegacyIMU:
			/* Falls into default case */
		default:
			/* Default to  3021 - shouldn't reach here during normal operation */
			StreamThreadState.BytesPerFrame = 200;
			break;
		}
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "bytesPerFrame = %d\r\n", StreamThreadState.BytesPerFrame);
#endif
		break;

	case 11:
		/* DR polarity */
		FX3State.DrPolarity = (CyBool_t) value;
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "DrPolarity = %d\r\n", value);
#endif
		break;

	case 12:
		/* DR active */
		FX3State.DrActive = (CyBool_t) value;
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "DrActive = %d\r\n", value);
#endif
		break;

	case 13:
		/* Ready pin */
		FX3State.DrPin = value;
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "DrPin = %d\r\n", value);
#endif
		break;

	case 14:
		/* enable watchdog */
		FX3State.WatchDogEnabled = CyTrue;
		FX3State.WatchDogPeriodMs = 1000 * value;
		AdiConfigureWatchdog();
		break;

	case 15:
		/* disable watchdog */
		FX3State.WatchDogEnabled = CyFalse;
		FX3State.WatchDogPeriodMs = 1000 * value;
		AdiConfigureWatchdog();
		break;

	default:
		/* Invalid Command */
		isHandled = CyFalse;
#ifdef VERBOSE_MODE
		CyU3PDebugPrint (4, "ERROR: Invalid SPI config command!\r\n");
#endif
		break;
	}

	/* Check that the configuration was successful */
	if(status != CY_U3P_SUCCESS)
	{
		isHandled = CyFalse;
	}

	return isHandled;
}
