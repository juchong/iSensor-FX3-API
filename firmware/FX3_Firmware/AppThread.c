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
  * @file		AppThread.c
  * @date		8/1/2019
  * @author		A. Nolan (alex.nolan@analog.com)
  * @author 	J. Chong (juan.chong@analog.com)
  * @brief		This file contains all functions associated with the AppThread.
 **/

#include "AppThread.h"

/* Private function prototypes */
static void AdiDebugInit();
static void AdiAppInit();

/* Tell the compiler where to find the needed globals */
extern CyU3PEvent EventHandler;

/** Global char buffer to store unique FX3 serial number */
extern char serial_number[];

/**
  * @brief This function initializes the UART controller to send debug messages.
  *
  * This function is called as part of the main application thread startup process.
  * The debug prints are routed to the UART and can be seen using a UART console
  * running at 115200 baud rate. The UART Tx and Rx must be connected to DQ30 and DQ31
  * on the Cypress FX3 Explorer board. On the ADI iSensor FX3 Board (small board), the
  * Rx and Tx are connected to pins 5 and 6 on the second 12 pin header.
  *
  * @returns void
 **/
static void AdiDebugInit()
{
    CyU3PUartConfig_t uartConfig = {0};
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

    /* Initialize the UART for printing debug messages */
    status = CyU3PUartInit();
    if (status != CY_U3P_SUCCESS)
    {
        /* Error handling */
    	AdiLogError(AppThread_c, __LINE__, status);
        AdiAppErrorHandler(status);
    }

    /* Set UART configuration */
    CyU3PMemSet ((uint8_t *)&uartConfig, 0, sizeof (uartConfig));
    uartConfig.baudRate = CY_U3P_UART_BAUDRATE_115200;
    uartConfig.stopBit = CY_U3P_UART_ONE_STOP_BIT;
    uartConfig.parity = CY_U3P_UART_NO_PARITY;
    uartConfig.txEnable = CyTrue;
    uartConfig.rxEnable = CyFalse;
    uartConfig.flowCtrl = CyFalse;
    uartConfig.isDma = CyTrue;

    /* Set the UART configuration */
    status = CyU3PUartSetConfig (&uartConfig, NULL);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

    /* Set the UART transfer to a really large value. */
    status = CyU3PUartTxSetBlockXfer (0xFFFFFFFF);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

    /* Initialize the debug module. */
    status = CyU3PDebugInit (CY_U3P_LPP_SOCKET_UART_CONS, 8);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

    /* Turn off the preamble to the debug messages. */
    CyU3PDebugPreamble(CyFalse);

    /* Send a success command over the newly-created debug port. */
    CyU3PDebugPrint (4, "\r\n");
    CyU3PDebugPrint (4, "Debugger initialized!\r\n");
}

/**
  * @brief This function initializes the USB module and attaches core event handlers.
  *
  * This function is called as part of the main application thread (AppThread) startup
  * process when the ThreadX RTOS first boots. This function also retrieves the unique
  * FX3 serial number from the EFUSE array.
  *
  * @returns void
 **/
static void AdiAppInit ()
{
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
    const char hex_digit[16] = "0123456789ABCDEF";
    uint32_t die_id[2] = {0};

    /* Get USB serial number from FX3 die id */
	CyU3PReadDeviceRegisters(EFUSE_DIE_ID, 2, die_id);
	/* Write FX3 die ID to USB serial number descriptor and a global variable */
	for (int i = 0; i < 2; i++)
	{
		/* Access via the USB descriptor */
		CyFxUSBSerialNumDesc[i*16+ 2] = hex_digit[(die_id[1-i] >> 28) & 0xF];
		CyFxUSBSerialNumDesc[i*16+ 4] = hex_digit[(die_id[1-i] >> 24) & 0xF];
		CyFxUSBSerialNumDesc[i*16+ 6] = hex_digit[(die_id[1-i] >> 20) & 0xF];
		CyFxUSBSerialNumDesc[i*16+ 8] = hex_digit[(die_id[1-i] >> 16) & 0xF];
		CyFxUSBSerialNumDesc[i*16+10] = hex_digit[(die_id[1-i] >> 12) & 0xF];
		CyFxUSBSerialNumDesc[i*16+12] = hex_digit[(die_id[1-i] >>  8) & 0xF];
		CyFxUSBSerialNumDesc[i*16+14] = hex_digit[(die_id[1-i] >>  4) & 0xF];
		CyFxUSBSerialNumDesc[i*16+16] = hex_digit[(die_id[1-i] >>  0) & 0xF];

		/* Access via a vendor command */
		serial_number[i*16+ 0] = hex_digit[(die_id[1-i] >> 28) & 0xF];
		serial_number[i*16+ 2] = hex_digit[(die_id[1-i] >> 24) & 0xF];
		serial_number[i*16+ 4] = hex_digit[(die_id[1-i] >> 20) & 0xF];
		serial_number[i*16+ 6] = hex_digit[(die_id[1-i] >> 16) & 0xF];
		serial_number[i*16+ 8] = hex_digit[(die_id[1-i] >> 12) & 0xF];
		serial_number[i*16+10] = hex_digit[(die_id[1-i] >>  8) & 0xF];
		serial_number[i*16+12] = hex_digit[(die_id[1-i] >>  4) & 0xF];
		serial_number[i*16+14] = hex_digit[(die_id[1-i] >>  0) & 0xF];
	}

	/* Start the USB functionality. */
    status = CyU3PUsbStart();
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
        AdiAppErrorHandler(status);
    }
    else
    {
    	CyU3PDebugPrint (4, "USB OK\r\n");
    }

    /* The fast enumeration is the easiest way to setup a USB connection,
     * where all enumeration phase is handled by the library. Only the
     * class / vendor requests need to be handled by the application. */
    CyU3PUsbRegisterSetupCallback(AdiControlEndpointHandler, CyTrue);

    /* Setup the callback to handle the USB events */
    CyU3PUsbRegisterEventCallback(AdiUSBEventHandler);

    /* Register a callback to handle LPM requests from the USB host */
    CyU3PUsbRegisterLPMRequestCallback(AdiLPMRequestHandler);

    /* Set the USB Enumeration descriptors */

    /* Super speed device descriptor. */
    status = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_DEVICE_DESCR, 0, (uint8_t *)CyFxUSB30DeviceDscr);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
        AdiAppErrorHandler(status);
    }

    /* Full speed configuration descriptor */
    status = CyU3PUsbSetDesc(CY_U3P_USB_SET_FS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBFSConfigDscr);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
        AdiAppErrorHandler(status);
    }

    /* Super speed configuration descriptor */
    status = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBSSConfigDscr);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
        AdiAppErrorHandler(status);
    }

    /* BOS descriptor */
    status = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_BOS_DESCR, 0, (uint8_t *)CyFxUSBBOSDscr);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
        AdiAppErrorHandler(status);
    }

    /* High speed device descriptor. */
    status = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_DEVICE_DESCR, 0, (uint8_t *)CyFxUSB20DeviceDscr);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
        AdiAppErrorHandler(status);
    }

    /* Device qualifier descriptor */
    status = CyU3PUsbSetDesc(CY_U3P_USB_SET_DEVQUAL_DESCR, 0, (uint8_t *)CyFxUSBDeviceQualDscr);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
        AdiAppErrorHandler(status);
    }

    /* High speed configuration descriptor */
    status = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBHSConfigDscr);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
        AdiAppErrorHandler(status);
    }

    /* String descriptor 0 */
    status = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 0, (uint8_t *)CyFxUSBStringLangIDDscr);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
        AdiAppErrorHandler(status);
    }

    /* String descriptor 1 */
    status = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 1, (uint8_t *)CyFxUSBManufactureDscr);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
        AdiAppErrorHandler(status);
    }

    /* String descriptor 2 */
    status = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 2, (uint8_t *)CyFxUSBProductDscr);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
        AdiAppErrorHandler(status);
    }

    /* Serial number descriptor */
    status = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 3, (uint8_t *)CyFxUSBSerialNumDesc);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

    /* Connect the USB Pins with high speed operation enabled (USB 2.0 for better compatibility) */
    status = CyU3PConnectState (CyTrue, CyFalse);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(AppThread_c, __LINE__, status);
        AdiAppErrorHandler(status);
    }
}

/**
  * @brief This is the entry point for the primary iSensors firmware application thread.
  *
  * @param input Unused input argument required by the thread manager
  *
  * @return void
  *
  * This function performs device initialization and then handles streaming start/stop commands for the various streaming methods.
  * The actual work done for the streaming is performed in the StreamThread - seperating the two allows for better control and
  * responsiveness to cancellation commands.
 **/
void AdiAppThreadEntry(uint32_t input)
{
	UNUSED(input);
	/* Events to trigger from */
    uint32_t eventMask =
    		ADI_RT_STREAM_DONE |
    		ADI_RT_STREAM_START |
    		ADI_RT_STREAM_STOP |
    		ADI_GENERIC_STREAM_DONE |
    		ADI_GENERIC_STREAM_START |
    		ADI_GENERIC_STREAM_STOP |
    		ADI_BURST_STREAM_DONE |
    		ADI_BURST_STREAM_START |
    		ADI_BURST_STREAM_STOP |
    		ADI_TRANSFER_STREAM_DONE |
    		ADI_TRANSFER_STREAM_START |
    		ADI_TRANSFER_STREAM_STOP |
    		ADI_I2C_STREAM_DONE |
    		ADI_I2C_STREAM_START |
    		ADI_I2C_STREAM_STOP;

    /* Event flags */
    uint32_t eventFlag;

    /* Initialize UART debugging */
    AdiDebugInit();

    /* Initialize the ADI application */
    AdiAppInit();

    for (;;)
    {
    	/* Wait for event handler flags to occur and handle them */
    	if (CyU3PEventGet(&EventHandler, eventMask, CYU3P_EVENT_OR_CLEAR, &eventFlag, CYU3P_WAIT_FOREVER) == CY_U3P_SUCCESS)
    	{
    		/*Handle transfer stream commands */
			if (eventFlag & ADI_TRANSFER_STREAM_START)
			{
				AdiTransferStreamStart();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Transfer stream start finished.\r\n");
#endif
			}
			if (eventFlag & ADI_TRANSFER_STREAM_STOP)
			{
				AdiStopAnyDataStream();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Transfer stream stop finished.\r\n");
#endif
			}
			if (eventFlag & ADI_TRANSFER_STREAM_DONE)
			{
				AdiTransferStreamFinished();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Transfer stream cleanup finished.\r\n");
#endif
			}

			/* Handle real-time stream commands */
			if (eventFlag & ADI_RT_STREAM_START)
			{
				AdiRealTimeStreamStart();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Real time stream start finished.\r\n");
#endif
			}
			if (eventFlag & ADI_RT_STREAM_STOP)
			{
				AdiStopAnyDataStream();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Real time stream stop finished.\r\n");
#endif
			}
			if (eventFlag & ADI_RT_STREAM_DONE)
			{
				AdiRealTimeStreamFinished();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Real time stream cleanup finished.\r\n");
#endif
			}

			/* Handle generic data stream commands */
			if (eventFlag & ADI_GENERIC_STREAM_START)
			{
				AdiGenericStreamStart();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Generic stream start command received.\r\n");
#endif
			}
			if (eventFlag & ADI_GENERIC_STREAM_STOP)
			{
				AdiStopAnyDataStream();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Generic stream stop finished.\r\n");
#endif
			}
			if (eventFlag & ADI_GENERIC_STREAM_DONE)
			{
				AdiGenericStreamFinished();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Generic stream cleanup finished.\r\n");
#endif
			}

			/* Handle burst data stream commands */
			if (eventFlag & ADI_BURST_STREAM_START)
			{
				AdiBurstStreamStart();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Burst stream start finished.\r\n");
#endif
			}
			if (eventFlag & ADI_BURST_STREAM_STOP)
			{
				AdiStopAnyDataStream();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Burst stream stop finished.\r\n");
#endif
			}
			if (eventFlag & ADI_BURST_STREAM_DONE)
			{
				AdiBurstStreamFinished();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "Burst stream cleanup finished.\r\n");
#endif
			}

			/* Handle i2c read stream commands */
			if (eventFlag & ADI_I2C_STREAM_START)
			{
				AdiI2CStreamStart();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "I2C stream start command finished.\r\n");
#endif
			}
			if (eventFlag & ADI_I2C_STREAM_STOP)
			{
				AdiStopAnyDataStream();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "I2C stream stop command finished.\r\n");
#endif
			}
			if (eventFlag & ADI_I2C_STREAM_DONE)
			{
				AdiI2CStreamFinished();
#ifdef VERBOSE_MODE
				CyU3PDebugPrint (4, "I2C stream cleanup finished.\r\n");
#endif
			}

    	}
        /* Allow other ready threads to run. */
        CyU3PThreadRelinquish();
    }
}
