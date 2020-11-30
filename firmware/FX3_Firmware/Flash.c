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
  * @file		Flash.c
  * @date		04/30/2020
  * @author		A. Nolan (alex.nolan@analog.com)
  * @brief		FX3 I2C EEPROM (model m24m02-dr) interfacing module.
 **/

#include "Flash.h"

/* Private function prototypes */
static uint16_t GetFlashDeviceAddress(uint32_t ByteAddress);
static CyU3PReturnStatus_t FlashTransfer(uint32_t Address, uint16_t NumBytes, uint8_t* Buf, CyBool_t isRead);

/** Global USB Buffer, from main */
extern uint8_t USBBuffer[4096];

/** FX3 state (from main) */
extern BoardState FX3State;

/** I2C Tx DMA channel handle */
static CyU3PDmaChannel flashTxHandle;

/** I2C Rx DMA channel handle */
static CyU3PDmaChannel flashRxHandle;

/**
  * @brief Initializes flash memory interface module
  *
  * @return Status code indication the success of the flash init operation
  *
  * The FX3 board features a ST m24m02-dr I2C EEPROM. This function
  * initializes the FX3 I2C block to operate in DMA mode with the max
  * supported I2C clock. It then configures the I2C Rx and Tx channels
  * to perform a transfer
 **/
CyU3PReturnStatus_t AdiFlashInit()
{
    CyU3PI2cConfig_t i2cConfig = {0};
    CyU3PDmaChannelConfig_t i2cDmaConfig = {0};
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

    /* Initialize and configure the I2C master module. */
    CyU3PI2cDeInit();
    status = CyU3PI2cInit();
    if (status != CY_U3P_SUCCESS)
    {
#ifdef VERBOSE_MODE
    	CyU3PDebugPrint (4, "I2C init failed! 0x%x\r\n", status);
#endif
        return status;
    }

    /* Start the I2C master block. Set i2c clock of 100KHz, DMA mode */
    CyU3PMemSet ((uint8_t *)&i2cConfig, 0, sizeof(i2cConfig));
    i2cConfig.bitRate    = 1000000;
    i2cConfig.busTimeout = 0xFFFFFFFF;
    i2cConfig.dmaTimeout = 0xFFFF;
    i2cConfig.isDma      = CyTrue;

    status = CyU3PI2cSetConfig (&i2cConfig, NULL);
    if (status != CY_U3P_SUCCESS)
    {
#ifdef VERBOSE_MODE
    	CyU3PDebugPrint (4, "Setting I2C configuration failed! 0x%x\r\n", status);
#endif
        return status;
    }

    /* Now create the DMA channels required for read and write. */
    CyU3PMemSet ((uint8_t *)&i2cDmaConfig, 0, sizeof(i2cDmaConfig));
    i2cDmaConfig.size           = FLASH_PAGE_SIZE;
    i2cDmaConfig.count          = 0;
    i2cDmaConfig.prodAvailCount = 0;
    i2cDmaConfig.dmaMode        = CY_U3P_DMA_MODE_BYTE;
    i2cDmaConfig.prodHeader     = 0;
    i2cDmaConfig.prodFooter     = 0;
    i2cDmaConfig.consHeader     = 0;
    i2cDmaConfig.notification   = 0;
    i2cDmaConfig.cb             = NULL;

    /* Create a channel to write to the EEPROM. */
    i2cDmaConfig.prodSckId = CY_U3P_CPU_SOCKET_PROD;
    i2cDmaConfig.consSckId = CY_U3P_LPP_SOCKET_I2C_CONS;
    status = CyU3PDmaChannelCreate(&flashTxHandle, CY_U3P_DMA_TYPE_MANUAL_OUT, &i2cDmaConfig);
    if (status != CY_U3P_SUCCESS)
    {
#ifdef VERBOSE_MODE
    	CyU3PDebugPrint (4, "Setting I2C Tx DMA channel failed! 0x%x\r\n", status);
#endif
        return status;
    }

    /* Create a channel to read from the EEPROM. */
    i2cDmaConfig.prodSckId = CY_U3P_LPP_SOCKET_I2C_PROD;
    i2cDmaConfig.consSckId = CY_U3P_CPU_SOCKET_CONS;
    status = CyU3PDmaChannelCreate (&flashRxHandle, CY_U3P_DMA_TYPE_MANUAL_IN, &i2cDmaConfig);
    if (status != CY_U3P_SUCCESS)
    {
#ifdef VERBOSE_MODE
    	CyU3PDebugPrint (4, "Setting I2C Rx DMA channel failed! 0x%x\r\n", status);
#endif
        return status;
    }

    /* Return status code */
    return status;
}

/**
  * @brief De-init the flash memory interfacing module
  *
  * @return void
  *
  * This functions destroys the DMA channels used for interfacing
  * with the I2C module, and re-inits the I2C controller to operate
  * in register mode, with the previously selected bitrate.
 **/
void AdiFlashDeInit()
{
	CyU3PI2cDeInit();
	CyU3PDmaChannelDestroy(&flashTxHandle);
	CyU3PDmaChannelDestroy(&flashRxHandle);
	/* Re-init I2C for use in register mode */
	AdiI2CInit(FX3State.I2CBitRate, CyFalse);
}

/**
  * @brief Write a block of memory to flash, at the specified byte address
  *
  * @param Address The start address (in flash) to perform the write operation to
  *
  * @param NumBytes The number of bytes to write to flash
  *
  * @param WriteBuf RAM buffer containing data to be written to flash
  *
  * @return void
  *
  * This function controls the flash write enable signal. This write enable
  * signal is used to prevent un-intended writes the flash from user space,
  * via I2C functions. This write enable signal is only present on the
  * iSensor FX3 board rev C or newer, but there shouldn't be any downside to
  * asserting it on older hardware models.
 **/
void AdiFlashWrite(uint32_t Address, uint16_t NumBytes, uint8_t* WriteBuf)
{
	/* Enable flash for write */
	CyU3PGpioSimpleSetValue(ADI_FLASH_WRITE_ENABLE_PIN, CyFalse);
	/* Perform write */
	FlashTransfer(Address, NumBytes, WriteBuf, CyFalse);
	/* Lock flash */
	CyU3PGpioSimpleSetValue(ADI_FLASH_WRITE_ENABLE_PIN, CyTrue);
}

/**
  * @brief Read a block of memory from flash, at the specified byte address
  *
  * @param Address The start address (in flash) to perform the read operation from
  *
  * @param NumBytes The number of bytes to read from the flash
  *
  * @param ReadBuf RAM buffer to read flash data into
  *
  * @return void
  *
  * This function leaves the I2C EEPROM write functionality disabled. This prevents
  * inadvertent writes from being processed.
 **/
void AdiFlashRead(uint32_t Address, uint16_t NumBytes, uint8_t* ReadBuf)
{
	FlashTransfer(Address, NumBytes, ReadBuf, CyTrue);
}

/**
  * @brief Handles flash read requests from control endpoint
  *
  * @param Address The byte address in flash to start reading at
  *
  * @param NumBytes The number of bytes to read. Max 4KB
  *
  * @return void
  *
  * The data read from flash is returned over the control endpoint.
  * This limits a single read to 4KB. If greater than a 4KB read
  * is needed, multiple transactions should be sent. Managing
  * this limit is left to the higher level software sending USB
  * commands.
 **/
void AdiFlashReadHandler(uint32_t Address, uint16_t NumBytes)
{
	/* Cap number of bytes to read at 4096 */
	if(NumBytes > 4096)
		NumBytes = 4096;
	/* Perform transfer */
	FlashTransfer(Address, NumBytes, USBBuffer, CyTrue);
	/* Return over USB control endpoint */
	CyU3PUsbSendEP0Data(NumBytes, USBBuffer);
}

/**
  * @brief Performs a transfer from the I2C flash memory
  *
  * @param Address The flash byte address to start the read/write operation from. Valid range 0x0 - 0x40000
  *
  * @param NumBytes The number of data bytes to transfer to/from the flash
  *
  * @param Buf RAM data buffer. Write data must be placed here. Read data is returned here.
  *
  * @param isRead Bool indicating if operation is read or write
  *
  * @return Status code indicating the success of the flash read/write operation
  *
  * This function performs all interfacing with the ST m24m02-dr I2C EEPROM which is
  * included on the iSensor FX3 board (and FX3 explorer kit). Before each transaction,
  * AdiFlashInit is called to ensure the flash and DMA are configured properly. Then, for
  * each transaction, the read/write is split into 64 byte (or less) chunks, which are
  * each performed using a single I2C<->Mem DMA transfer. Once all required transfers have
  * been performed, the flash is de-initialized.
 **/
static CyU3PReturnStatus_t FlashTransfer(uint32_t Address, uint16_t NumBytes, uint8_t* Buf, CyBool_t isRead)
{
    CyU3PDmaBuffer_t buf_p = {0};
    CyU3PI2cPreamble_t preamble = {.buffer = {0}};
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
    uint16_t dmaCount;
    uint16_t lastCount;

    /* device address (upper two address bits encoded into device address) */
    uint16_t device_address;

    /* Calculate page count */
    uint16_t pageCount = (NumBytes / FLASH_PAGE_SIZE);

    /* Return for zero length transfer */
    if(NumBytes == 0)
        return CY_U3P_SUCCESS;

    /* Check if extra bytes which fall onto another page */
    lastCount = (NumBytes % FLASH_PAGE_SIZE);
    if(lastCount != 0)
        pageCount ++;
    else
    	lastCount = FLASH_PAGE_SIZE;

    /* Init flash */
    AdiFlashInit();

    /* Update the buffer status. */
    buf_p.status = 0;
	/* Update buffer address */
	buf_p.buffer = Buf;

    while (pageCount != 0)
    {
    	/* Get device addr */
    	device_address = GetFlashDeviceAddress(Address);
    	/* Get transfer count */
    	if(pageCount > 1)
    		dmaCount = FLASH_PAGE_SIZE;
    	else
    		dmaCount = lastCount;

#ifdef VERBOSE_MODE
    	CyU3PDebugPrint (4, "I2C access: Dev addr: 0x%x Byte Addr: 0x%x, size: 0x%x, pages: 0x%x read: %d\r\n", device_address, Address, dmaCount, pageCount, isRead);
#endif

    	if(isRead)
    	{
            /* Update the preamble information. */
            preamble.length    = 4;
            preamble.buffer[0] = device_address;
            preamble.buffer[1] = (uint8_t)((Address & 0xFF00) >> 8);
            preamble.buffer[2] = (uint8_t)(Address & 0xFF);
            preamble.buffer[3] = (device_address | 0x01);
            preamble.ctrlMask  = 0x0004;

            buf_p.size = FLASH_PAGE_SIZE;
            buf_p.count = dmaCount;

            /* Send read command */
            status = CyU3PI2cSendCommand(&preamble, dmaCount, CyTrue);
#ifdef VERBOSE_MODE
            if(status != CY_U3P_SUCCESS)
            	CyU3PDebugPrint (4, "I2C send read command failed: 0x%x\r\n", status);
#endif
            /* Set up DMA to receive read data */
            status = CyU3PDmaChannelSetupRecvBuffer (&flashRxHandle, &buf_p);
#ifdef VERBOSE_MODE
            if(status != CY_U3P_SUCCESS)
            	CyU3PDebugPrint (4, "I2C DMA Rx channel setup failed: 0x%x\r\n", status);
#endif
    	}
    	else
    	{
            /* Update the preamble information. */
            preamble.length    = 3;
            preamble.buffer[0] = device_address;
            preamble.buffer[1] = (uint8_t)((Address & 0xFF00) >> 8);
            preamble.buffer[2] = (uint8_t)(Address & 0xFF);
            preamble.ctrlMask  = 0x0000;

            buf_p.size = FLASH_PAGE_SIZE;
            buf_p.count = dmaCount;

            /* Setup DMA transmit buffer */
            status = CyU3PDmaChannelSetupSendBuffer (&flashTxHandle, &buf_p);
#ifdef VERBOSE_MODE
            if(status != CY_U3P_SUCCESS)
            	CyU3PDebugPrint (4, "I2C DMA Tx channel setup failed: 0x%x\r\n", status);
#endif
            /* Send write command */
            status = CyU3PI2cSendCommand (&preamble, dmaCount, CyFalse);
#ifdef VERBOSE_MODE
            if(status != CY_U3P_SUCCESS)
            	CyU3PDebugPrint (4, "I2C send write command failed: 0x%x\r\n", status);
#endif
    	}
        /* Stall for 20ms */
        CyU3PThreadSleep(20);
        /* Wait for finish */
        status = CyU3PI2cWaitForBlockXfer(isRead);
#ifdef VERBOSE_MODE
        if(status != CY_U3P_SUCCESS)
        	CyU3PDebugPrint (4, "I2C DMA wait for completion failed: 0x%x\r\n", status);
#endif

        /* decrement page count */
        pageCount --;
        /* Increment address */
        Address += dmaCount;
        buf_p.buffer += dmaCount;
    }

#ifdef VERBOSE_MODE
    CyU3PDebugPrint (4, "Flash transfer complete!\r\n", status);
#endif

    /* De-Init flash */
    AdiFlashDeInit();

    /* Return the status code */
    return status;
}

/**
  * @brief Gets the flash devices address, based on the requested byte address
  *
  * @return void The 8-bit flash device address
  *
  * For the EEPROM, byte address bits 16-17 are encoded into the device address
 **/
static uint16_t GetFlashDeviceAddress(uint32_t ByteAddress)
{
	uint16_t address = 0xA0;
	/* mask out all but bits 17 - 16 in the byte address and shift down (leave one bit up) */
	ByteAddress = ByteAddress >> 15;
	ByteAddress &= 0x6;
	address |= ByteAddress;
	return address;
}
