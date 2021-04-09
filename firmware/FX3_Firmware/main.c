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
  * @file		main.c
  * @date		8/1/2019
  * @author		A. Nolan (alex.nolan@analog.com)
  * @author 	J. Chong (juan.chong@analog.com)
  * @brief		Entry point and setup functions for the Analog Devices iSensor FX3 Demonstration Platform firmware.
 **/

/**
  * @mainpage Analog Devices iSensor FX3 Demonstration Platform Firmware
  *
  * @section overview Firmware Overview
  *
  * The iSensor FX3 firmware is an RTOS based firmware for the Cypress FX3 platform. It is designed to provide users with a means of reliably
  * acquiring data from iSensor IMUs and condition monitoring modules over a high-speed USB connection, using any .NET framework compatible application.
  * This firmware was designed for use on the Cypress FX3 SuperSpeed Explorer Kit and relies on the open source libraries provided by Cypress to
  * operate. The freely-available, Eclipse-based, Cypress EZ USB Suite was used for all firmware development. This firmware can be run on a
  * Cypress SuperSpeed Explorer FX3 board with a break out connector, or the Analog Devices iSensor FX3 Demonstration Platform.
  *
  * The Cypress EZ USB Suite can be found here:
  * https://www.cypress.com/documentation/software-and-drivers/ez-usb-fx3-software-development-kit
  *
  * @section design Firmware Design and Software Interface
  *
  * The iSensor FX3 firmware attempts to follow the Cypress program work flow and relies on FX3 system threading, execution priority, and event
  * flags to execute firmware subroutines and transmit sensor data. Unique vendor commands trigger subroutines embedded in the iSensor FX3
  * firmware that read and write SPI data, measure external pulses, generate clock signals, and manage board configuration. Different SPI streaming
  * modes are implemented which allow applications to easily communicate to most products in the iSensor portfolio.
  *
  * A .NET-compatible API (FX3Api) has been developed in parallel to simplify interfacing with the iSensor FX3 firmware. This API provides
  * simple and easy to use access to all the functionality built into the FX3 firmware.
  *
  * The FX3Api and associated documentation can be found here:
  * https://github.com/juchong/iSensor-FX3-API
 **/

#include "main.h"

/*
 * Thread and Event Management Definitions
 */

/** RTOS thread handle for continuous data streaming functionality */
CyU3PThread StreamThread = {0};

/** RTOS thread handle for the main application */
CyU3PThread AppThread = {0};

/** ADI event structure */
CyU3PEvent EventHandler = {0};

/** ADI GPIO event structure (RTOS handles GPIO ISR) */
CyU3PEvent GpioHandler = {0};

/*
 * DMA Channel Definitions
 */

/** DMA channel for real time streaming (SPI to USB BULK-IN 0x81) */
CyU3PDmaChannel StreamingChannel = {0};

/** DMA channel for BULK-OUT endpoint 0x1 (PC to FX3) */
CyU3PDmaChannel ChannelFromPC = {0};

/** DMA channel for BULK-IN endpoint 0x82 (FX3 to PC) */
CyU3PDmaChannel ChannelToPC = {0};

/** DMA channel for reading a memory location into a DMA consumer */
CyU3PDmaChannel MemoryToSPI = {0};

/*
 * Buffer Definitions
 */

/** 4KB USB Data buffer. Used to receive data from the control endpoint */
uint8_t USBBuffer[4096] __attribute__((aligned(32))) = {0};

/** 12KB Generic bulk buffer. Used for when data is manually sent to or received from the PC via bulk endpoints. */
uint8_t BulkBuffer[12288] __attribute__((aligned(32))) = {0};

/** DMA buffer structure for output buffer */
CyU3PDmaBuffer_t ManualDMABuffer = {0};

/** DMA buffer structure for SPI transmit */
CyU3PDmaBuffer_t SpiDmaBuffer = {0};

/*
 * Application constants
 */

/** Constant firmware ID string. Manually updated when releasing new version of the FX3 firmware. Must match FX3 API version number. */
const uint8_t FirmwareID[32] __attribute__((aligned(32))) = "ADI FX3 REV 2.9.4-PUB\0";

/** FX3 unique serial number. Set at runtime during the initialization process. */
char serial_number[] __attribute__((aligned(32))) = {'0',0x00,'0',0x00,'0',0x00,'0',0x00, '0',0x00,'0',0x00,'0',0x00,'0',0x00, '0',0x00,'0',0x00,'0',0x00,'0',0x00, '0',0x00,'0',0x00,'0',0x00,'0',0x00};

/*
 * Application configuration information
 */

/** Struct. which stores all run time configurable FX3 settings */
BoardState FX3State = {.SpiConfig = {0}, .PinMap = {0}};

/*
 * Thread synchronization data
 */

/** Signal data stream thread to kill data capture early (True = kill thread signaled, False = allow execution) */
volatile CyBool_t KillStreamEarly = CyFalse;

/** Struct of data used to synchronize the data streaming / app threads */
StreamState StreamThreadState = {0};

/**
  * @brief This is the main entry point function for the iSensor FX3 application firmware.
  *
  * @return A status code indicating the success of the function. Should never return.
  *
  * This firmware image is loaded into RAM over USB by the second-stange iSensor FX3 Bootloader when the Connect() function is called
  * in the FX3 API. Once the full image has been loaded into SRAM, and the CRC verified, the iSensor FX3 bootloader jumps to this main
  * function. Main initializes the device, memory, and IO matrix, and then boots the RTOS kernel.
 **/
int main (void)
{
    CyU3PIoMatrixConfig_t io_cfg = {0};
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
    CyU3PSysClockConfig_t sysclk_cfg = {0};

    /* Configure system clocks */
    sysclk_cfg.setSysClk400 = CyTrue;
    sysclk_cfg.useStandbyClk = CyFalse;
    sysclk_cfg.clkSrc = CY_U3P_SYS_CLK;
    sysclk_cfg.cpuClkDiv = 2;
    sysclk_cfg.dmaClkDiv = 2;
    sysclk_cfg.mmioClkDiv = 2;

    /* Initialize the device */
    status = CyU3PDeviceInit (&sysclk_cfg);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }
    /* Initialize the caches. Enable both Instruction and Data Caches. */
    status = CyU3PDeviceCacheControl (CyTrue, CyTrue, CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* Configure the io matrix to implement SPI and enable UART
     * debugging on DQ30 and DQ31 (GPIO48 and GPIO49).
     */
    CyU3PMemSet ((uint8_t *)&io_cfg, 0, sizeof(io_cfg));
    io_cfg.isDQ32Bit = CyFalse;
    io_cfg.s0Mode = CY_U3P_SPORT_INACTIVE;
    io_cfg.s1Mode = CY_U3P_SPORT_INACTIVE;
    io_cfg.useUart   = CyTrue;
    io_cfg.useI2C    = CyTrue;
    io_cfg.useI2S    = CyFalse;
    io_cfg.useSpi    = CyTrue;
    io_cfg.lppMode   = CY_U3P_IO_MATRIX_LPP_DEFAULT;
    io_cfg.gpioSimpleEn[0]  = 0;
    io_cfg.gpioSimpleEn[1]  = 0;
    io_cfg.gpioComplexEn[0] = 0;
    io_cfg.gpioComplexEn[1] = 0;
    status = CyU3PDeviceConfigureIOMatrix (&io_cfg);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* This is a non returnable call for initializing the RTOS kernel */
    CyU3PKernelEntry();

    /* Dummy return to make the compiler happy */
    return 0;

	/* Cannot recover from device initialization error. */
handle_fatal_error:

	/* Force CPU to perform hard reset */
	GCTLAON->control &= ~CY_U3P_GCTL_HARD_RESET_N;

	/* Kick into infinite loop (shouldn't get here) */
	while(1);
}

/**
  * @brief This function handles events generated by the control endpoint. All vendor requests are handled in this function.
  *
  * @param setupdat0 The first set of setup data. Contains of the request and value fields.
  *
  * @param setupdat1 The second set of setup data. Contains the index and length fields.
  *
  * @returns A boolean indicating if the control endpoint event was handled properly.
  *
  * This function handles all USB events generated by the control endpoint. For the iSensor FX3 firmware, these events are a set
  * of custom vendor commands. These vendor commands must be issued by the host PC. To ensure consistent behavior, all vendor commands
  * should be issued using a function call in the FX3API. The FX3 API manages the control endpoint parameters to ensure valid behavior
  * in all cases.
  *
  * Fast enumeration is used. Only requests addressed to the interface, class,
  * vendor and unknown control requests are received by this function.
 **/
CyBool_t AdiControlEndpointHandler (uint32_t setupdat0, uint32_t setupdat1)
{
	uint8_t  bRequest, bReqType;
    uint8_t  bType, bTarget;
    uint16_t wValue, wIndex, wLength;
    CyBool_t isHandled = CyFalse;
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
    uint16_t *bytesRead = 0;

    /* Decode the fields from the setup request. */
    bReqType = (setupdat0 & CY_U3P_USB_REQUEST_TYPE_MASK);
    bType    = (bReqType & CY_U3P_USB_TYPE_MASK);
    bTarget  = (bReqType & CY_U3P_USB_TARGET_MASK);
    bRequest = ((setupdat0 & CY_U3P_USB_REQUEST_MASK) >> CY_U3P_USB_REQUEST_POS);
    wValue   = ((setupdat0 & CY_U3P_USB_VALUE_MASK)   >> CY_U3P_USB_VALUE_POS);
    wIndex   = ((setupdat1 & CY_U3P_USB_INDEX_MASK)   >> CY_U3P_USB_INDEX_POS);
    wLength   = ((setupdat1 & CY_U3P_USB_LENGTH_MASK)   >> CY_U3P_USB_LENGTH_POS);

    /* Handle vendor requests */
    if (bType == CY_U3P_USB_VENDOR_RQT)
    {
        isHandled = CyTrue;

#ifdef VERBOSE_MODE
        CyU3PDebugPrint (4, "Vendor request = 0x%x\r\n", bRequest);
#endif

        switch (bRequest)
        {
        	/* Special command to trigger a data capture and measure the corresponding busy pulse. This
        	 * feature is most useful for ADcmXL products, but can be used for any product */
        	case ADI_BUSY_MEASURE:
        		status = AdiMeasureBusyPulse(wLength);
        		break;

        	/* Read single word for IRegInterface */
        	case ADI_READ_BYTES:
        		status = AdiReadRegBytes(wIndex);
        		break;

        	/* Write single byte for IRegInterface */
        	case ADI_WRITE_BYTE:
        		status = AdiWriteRegByte(wIndex, wValue & 0xFF);
        		break;

        	/* Set the application boot time */
        	case ADI_SET_BOOT_TIME:
        		status = CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
        		FX3State.BootTime = USBBuffer[0];
        		FX3State.BootTime |= (USBBuffer[1] << 8);
        		FX3State.BootTime |= (USBBuffer[2] << 16);
        		FX3State.BootTime |= (USBBuffer[3] << 24);
#ifdef VERBOSE_MODE
            	CyU3PDebugPrint (4, "Boot Time Stamp: %d\r\n", FX3State.BootTime);
#endif
        		break;

        	/* Pulse drive for a specified amount of time */
        	case ADI_PULSE_DRIVE:
        		/* Read config data into USBBuffer */
        		status = CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
        		/* Run pulse drive function */
        		status |= AdiPulseDrive();
        		/* Send back status over the BULK-In endpoint */
        		AdiSendStatus(status, 4, CyFalse);
        		break;

        	/* Wait on an edge, with timeout */
        	case ADI_PULSE_WAIT:
        		/* Run pulse wait function */
        		status = AdiPulseWait(wLength);
        		break;

        	/* Set a pin value */
        	case ADI_SET_PIN:
        		status = AdiSetPin(wIndex, (CyBool_t) wValue);
        		AdiSendStatus(status, wLength, CyTrue);
        		break;

        	/* ID Check */
            case ADI_FIRMWARE_ID_CHECK:
                status = CyU3PUsbSendEP0Data (32, (uint8_t *)FirmwareID);
#ifdef VERBOSE_MODE
            	CyU3PDebugPrint (4, "Firmware ID: %s\r\n", FirmwareID);
#endif
                break;

            /* Serial Number Check */
            case ADI_SERIAL_NUMBER_CHECK:
            	status = CyU3PUsbSendEP0Data(32, (uint8_t *)serial_number);
                break;

            /* Get firmware build date */
            case ADI_GET_BUILD_DATE:
            	AdiGetBuildDate(USBBuffer);
            	CyU3PUsbSendEP0Data(wLength, USBBuffer);
            	break;

            /* Hard-reset the FX3 firmware (return to bootloader mode) */
            case ADI_HARD_RESET:
            	CyU3PUsbAckSetup();
            	CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
            	CyU3PDebugPrint (4, "Rebooting FX3!\r\n");
            	CyU3PThreadSleep(500);
            	CyU3PConnectState(CyFalse, CyTrue);
            	AdiAppStop();
            	CyU3PPibDeInit();
            	CyU3PThreadSleep(500);
				CyU3PDeviceReset(CyFalse);
            	break;

            /* Soft-reset the FX3 firmware (restart the ADI application firmware) */
            case ADI_WARM_RESET:
            	CyU3PUsbAckSetup();
            	CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
            	CyU3PThreadSleep(500);
            	CyU3PConnectState(CyFalse, CyTrue);
            	AdiAppStop ();
            	CyU3PPibDeInit ();
            	CyU3PThreadSleep(500);
            	CyU3PDeviceReset(CyTrue);
            	break;

            /* Set the SPI config */
            case ADI_SET_SPI_CONFIG:
            	isHandled = AdiSpiUpdate(wIndex, wValue, wLength);
            	break;

            /* Read a GPIO pin specified by index */
            case ADI_READ_PIN:
            	status = AdiPinRead(wIndex);
            	break;

            /* Measure pin delay */
            case ADI_PIN_DELAY_MEASURE:
            	status = AdiMeasurePinDelay(wLength);
            	break;

            /* Read the current SPI config */
            case ADI_READ_SPI_CONFIG:
            	status = AdiGetSpiSettings();
            	break;

            /* Read the value from the complex GPIO timer */
            case ADI_READ_TIMER_VALUE:
            	status = AdiReadTimerValue();
            	/* Send back status + timer value */
            	AdiSendStatus(status, 8, CyTrue);
            	break;

            /* Vendor command to set the DUT supply voltage */
            case ADI_SET_DUT_SUPPLY:
            	/* Set the voltage (setting is supplied in value field) */
            	status = AdiSetDutSupply((DutVoltage) wValue);
            	/* Return the status code */
            	AdiSendStatus(status, wLength, CyTrue);
            	break;

            /* Get the current status of the FX3 */
            case ADI_GET_STATUS:
            	/* Return the verbose mode state in USBBuffer byte 4 */
            	USBBuffer[4] = 0;
#ifdef VERBOSE_MODE
            	USBBuffer[4] = 1;
#endif
            	/* Return the status in USBBuffer bytes 0-3. Total return data 5 bytes */
            	AdiSendStatus(status, wLength, CyTrue);
            	break;

            /* Get the board type and pin mapping info */
            case ADI_GET_BOARD_TYPE:
            	AdiGetBoardPinInfo(USBBuffer);
            	status = CyU3PUsbSendEP0Data(wLength, USBBuffer);
            	break;

            /* Generic stream is a register stream triggered on data ready */
            case ADI_STREAM_GENERIC_DATA:
            	/* Start, stop, async stop depending on index */
            	switch(wIndex)
            	{
            	case ADI_STREAM_START_CMD:
            		/* Get the data from the control endpoint */
            		status = CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
            		/* Set the generic stream start event */
            		status |= CyU3PEventSet(&EventHandler, ADI_GENERIC_STREAM_START, CYU3P_EVENT_OR);
            		StreamThreadState.TransferByteLength = wLength;
            		break;
            	case ADI_STREAM_DONE_CMD:
            		/* Get the data from the control endpoint */
            		status = CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
            		/* Set the stream done event */
            		status |= CyU3PEventSet(&EventHandler, ADI_GENERIC_STREAM_DONE, CYU3P_EVENT_OR);
            		break;
            	case ADI_STREAM_STOP_CMD:
            		status = CyU3PEventSet(&EventHandler, ADI_GENERIC_STREAM_STOP, CYU3P_EVENT_OR);
            		break;
            	default:
            		/* Shouldn't get here */
            		isHandled = CyFalse;
            		break;
            	}
            	if (status != CY_U3P_SUCCESS)
            	{
					AdiLogError(Main_c, __LINE__, status);
            	}
            	break;

			/* Burst stream control for IMUs */
			case ADI_STREAM_BURST_DATA:
            	/* Start, stop, async stop depending on index */
            	switch(wIndex)
            	{
            	case ADI_STREAM_START_CMD:
            		/* Set USB transfer length */
            		StreamThreadState.TransferWordLength = wLength;
            		/* Set event handler */
            		status = CyU3PEventSet(&EventHandler, ADI_BURST_STREAM_START, CYU3P_EVENT_OR);
            		break;
            	case ADI_STREAM_DONE_CMD:
            		/* Get the data from the control endpoint */
            		status = CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
            		/* Set event handler*/
            		status |= CyU3PEventSet(&EventHandler, ADI_BURST_STREAM_DONE, CYU3P_EVENT_OR);
            		break;
            	case ADI_STREAM_STOP_CMD:
            		status = CyU3PEventSet(&EventHandler, ADI_BURST_STREAM_STOP, CYU3P_EVENT_OR);
            		break;
            	default:
            		/* Shouldn't get here */
            		isHandled = CyFalse;
            		break;
            	}
				if (status != CY_U3P_SUCCESS)
				{
					AdiLogError(Main_c, __LINE__, status);
				}
				break;

			/* Real time stream control. Index will determine the event to set, and value will enable(1)/disable(0) pin exit */
			case ADI_STREAM_REALTIME:
				/* Start, stop, async stop depending on index */
				switch(wIndex)
				{
				case ADI_STREAM_START_CMD:
					StreamThreadState.PinExitEnable = (CyBool_t) wValue;
					status = CyU3PEventSet(&EventHandler, ADI_RT_STREAM_START, CYU3P_EVENT_OR);
					break;
				case ADI_STREAM_DONE_CMD:
            		/* Get the data from the control endpoint */
            		status = CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
            		/* Set stream done event */
					status |= CyU3PEventSet(&EventHandler, ADI_RT_STREAM_DONE, CYU3P_EVENT_OR);
					break;
				case ADI_STREAM_STOP_CMD:
					status = CyU3PEventSet(&EventHandler, ADI_RT_STREAM_STOP, CYU3P_EVENT_OR);
					break;
				default:
            		/* Shouldn't get here */
            		isHandled = CyFalse;
            		break;
				}
				if (status != CY_U3P_SUCCESS)
				{
					AdiLogError(Main_c, __LINE__, status);
				}
				break;

			/* Transfer stream control */
			case ADI_TRANSFER_STREAM:
				switch(wIndex)
				{
				case ADI_STREAM_START_CMD:
					status = CyU3PEventSet(&EventHandler, ADI_TRANSFER_STREAM_START, CYU3P_EVENT_OR);
					StreamThreadState.TransferByteLength = wLength;
					break;
				case ADI_STREAM_DONE_CMD:
            		/* Get the data from the control endpoint */
            		status = CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
            		/* Set stream done event */
					status |= CyU3PEventSet(&EventHandler, ADI_TRANSFER_STREAM_DONE, CYU3P_EVENT_OR);
					break;
				case ADI_STREAM_STOP_CMD:
					status = CyU3PEventSet(&EventHandler, ADI_TRANSFER_STREAM_STOP, CYU3P_EVENT_OR);
					break;
				default:
            		/* Shouldn't get here */
            		isHandled = CyFalse;
            		break;
				}
				if (status != CY_U3P_SUCCESS)
				{
					AdiLogError(Main_c, __LINE__, status);
				}
				break;

			/* Get the measured DR frequency */
            case ADI_MEASURE_DR:
            	/* Read config data into USBBuffer */
				status = CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
				/* Run pulse drive function */
				status |= AdiMeasurePinFreq();
				break;

			/* PWM configuration */
            case ADI_PWM_CMD:
            	/* Read config data into USBBuffer */
				status = CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
				/* Run pulse drive function (index = 1 to enable, 0 to disable) */
				status |= AdiConfigurePWM((CyBool_t) wIndex);
            	break;

            /* 1-4 byte single transfer */
            case ADI_TRANSFER_BYTES:
            	/* Call the transfer bytes function
            	 * upper 2 write bytes are passed in wIndex, lower are passed in wValue */
            	status = AdiTransferBytes(wIndex << 16 | wValue);
            	/* Send status and 4 bytes data back */
            	AdiSendStatus(status, 8, CyTrue);
            	break;

            /* Bit bang SPI transfer handler */
            case ADI_BITBANG_SPI:
            	/* Call the handler function for the SPI bit bang. Returns data to PC over bulk endpoint */
            	status = CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
            	status |= AdiBitBangSpiHandler();
            	break;

            /* Reset SPI peripheral (to recover from using bit bang SPI) */
            case ADI_RESET_SPI:
            	status = AdiRestartSpi();
            	/* Return the status over control endpoint */
            	AdiSendStatus(status, wLength, CyTrue);
            	break;

            /* Enable internal pull up/down resistor on a GPIO */
            case ADI_SET_PIN_RESISTOR:
            	status = AdiSetPinResistor(wIndex, wValue);
            	/* Return the status over control endpoint */
            	AdiSendStatus(status, wLength, CyTrue);
            	break;

			/* Arbitrary flash read command */
			case ADI_READ_FLASH:
				AdiFlashReadHandler((wIndex << 16) | wValue, wLength);
				break;

			/* Clear flash error log command */
			case ADI_CLEAR_FLASH_LOG:
				WriteErrorLogCount(0);
				status = CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
				break;

			/*Set I2C bit rate */
			case ADI_I2C_SET_BIT_RATE:
				status = AdiI2CInit(wIndex << 16 | wValue, CyFalse);
				/* Return the status over control endpoint */
				AdiSendStatus(status, wLength, CyTrue);
				break;

			/* I2C retry count property */
			case ADI_I2C_RETRY_COUNT:
				FX3State.I2CRetryCount = wValue;
				AdiSendStatus(status, wLength, CyTrue);
				break;

			/* I2C single read */
			case ADI_I2C_READ_BYTES:
				status = AdiI2CReadHandler(wLength);
				break;

			/* I2C single write */
			case ADI_I2C_WRITE_BYTES:
				status = AdiI2CWriteHandler(wLength);
				/* Send back status indicating operation is done */
				AdiSendStatus(status, 4, CyFalse);
				break;

			/* I2C read stream start/done/cancel */
			case ADI_I2C_READ_STREAM:
				switch(wIndex)
				{
				case ADI_STREAM_START_CMD:
					status = CyU3PEventSet(&EventHandler, ADI_I2C_STREAM_START, CYU3P_EVENT_OR);
					StreamThreadState.TransferByteLength = wLength;
					break;
				case ADI_STREAM_DONE_CMD:
					/* Get the data from the control endpoint */
					status = CyU3PUsbGetEP0Data(wLength, USBBuffer, bytesRead);
					/* Set stream done event */
					status |= CyU3PEventSet(&EventHandler, ADI_I2C_STREAM_DONE, CYU3P_EVENT_OR);
					break;
				case ADI_STREAM_STOP_CMD:
					status = CyU3PEventSet(&EventHandler, ADI_I2C_STREAM_STOP, CYU3P_EVENT_OR);
					break;
				default:
            		/* Shouldn't get here */
            		isHandled = CyFalse;
            		break;
				}
				if (status != CY_U3P_SUCCESS)
				{
					AdiLogError(Main_c, __LINE__, status);
				}
				break;

			/* Command to do nothing. Might remove, this isn't really used at all */
			case ADI_NULL_COMMAND:
				isHandled = CyTrue;
				break;

            default:
                /* This is an unknown request */
#ifdef VERBOSE_MODE
            	CyU3PDebugPrint (4, "ERROR: Un-handled vendor command 0x%x\r\n", bRequest);
#endif
                isHandled = CyFalse;
                break;
        }

        if (bType == CY_U3P_USB_STANDARD_RQT)
        {
            /* Handle SET_FEATURE(FUNCTION_SUSPEND) and CLEAR_FEATURE(FUNCTION_SUSPEND)
             * requests here. It should be allowed to pass if the device is in configured
             * state and failed otherwise. */
            if ((bTarget == CY_U3P_USB_TARGET_INTF) && ((bRequest == CY_U3P_USB_SC_SET_FEATURE)
                        || (bRequest == CY_U3P_USB_SC_CLEAR_FEATURE)) && (wValue == 0))
            {
                if (FX3State.AppActive)
                    CyU3PUsbAckSetup ();
                else
                    CyU3PUsbStall (0, CyTrue, CyFalse);

                isHandled = CyTrue;
            }

            if(bTarget == CY_U3P_USB_TARGET_ENDPT)
            {
            	isHandled = CyTrue;
            }
        }

        /* If there was an error return false to stall the request */
        if (status != CY_U3P_SUCCESS)
        {
            isHandled = CyFalse;
        }
    }
    /* Return the handled status to the RTOS */
    return isHandled;
}

/**
  * @brief This function handles events generated by the bulk endpoint
  *
  * @param evType The type of the event being handled
  *
  * @param usbSpeed The current connection speed
  *
  * @param epNum The end point number
  *
  * @return A status code indicating the success of the function.
  *
  * This function is currently unused. All event management to the host PC
  * is performed through the control endpoint.
  *
 **/
void AdiBulkEndpointHandler(CyU3PUsbEpEvtType evType, CyU3PUSBSpeed_t usbSpeed, uint8_t epNum)
{
	UNUSED(evType);
	UNUSED(usbSpeed);
	UNUSED(epNum);
}

/**
  * @brief This is a callback function to handle generic USB events.
  *
  * @param evtype The type of the event being handled
  *
  * @param evdata The data from the USB event
  *
  * This function handles USB events by calling start/stop functions to manage the ADI application.
  *
 **/
void AdiUSBEventHandler (CyU3PUsbEventType_t evtype, uint16_t evdata)
{
	UNUSED(evdata);
    switch (evtype)
    {
        case CY_U3P_USB_EVENT_SETCONF:
        	/* Disable the low power entry to optimize USB throughput */
        	CyU3PUsbLPMDisable();
        	/* Stop the application before re-starting. */
        	if (FX3State.AppActive)
        	{
        		AdiAppStop();
        	}
			/* Start the application */
        	AdiAppStart();
            break;

        case CY_U3P_USB_EVENT_RESET:
        case CY_U3P_USB_EVENT_DISCONNECT:
        	/* Stop the application */
        	if (FX3State.AppActive)
        	{
        		AdiAppStop();
        	}
            break;

        default:
            break;
    }
}

/**
  * @brief This is a callback function to handle Link Power Management (LPM) requests.
  *
  * @param link_mode The USB link power state that is being set
  *
  * @return Returns true so that the USB driver always stays in high power state.
 **/
CyBool_t AdiLPMRequestHandler(CyU3PUsbLinkPowerMode link_mode)
{
	/* Dont care about link power mode, always stay high power */
	UNUSED(link_mode);
    return CyTrue;
}

/**
  * @brief This function handles GPIO interrupts and sets the appropriate flag in GpioHandler.
  *
  * @param gpioId The pin number of the pin which generated the interrupt
  *
  * @returns void
  *
  * This function is called by the RTOS whenever the GPIO interrupt vector is enabled and a
  * GPIO interrupt is received. Instead of performing any work in this function, to improve
  * system responsiveness, this function sets an RTOS event flag, to be handled by the
  * application thread.
 **/
void AdiGPIOEventHandler(uint8_t gpioId)
{
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	CyBool_t gpioValue = CyFalse;
	status = CyU3PGpioGetValue(gpioId, &gpioValue);
    if (status == CY_U3P_SUCCESS)
    {
    	/* Read the pin ID that generated the event and set the appropriate flag */
		if(gpioId == FX3State.PinMap.ADI_PIN_DIO1)
			CyU3PEventSet(&GpioHandler, ADI_DIO1_INTERRUPT_FLAG, CYU3P_EVENT_OR);

		if(gpioId == FX3State.PinMap.ADI_PIN_DIO2)
			CyU3PEventSet(&GpioHandler, ADI_DIO2_INTERRUPT_FLAG, CYU3P_EVENT_OR);

		if(gpioId == FX3State.PinMap.ADI_PIN_DIO3)
			CyU3PEventSet(&GpioHandler, ADI_DIO3_INTERRUPT_FLAG, CYU3P_EVENT_OR);

		if(gpioId == FX3State.PinMap.ADI_PIN_DIO4)
			CyU3PEventSet(&GpioHandler, ADI_DIO4_INTERRUPT_FLAG, CYU3P_EVENT_OR);

		if(gpioId == FX3State.PinMap.FX3_PIN_GPIO1)
			CyU3PEventSet(&GpioHandler, FX3_GPIO1_INTERRUPT_FLAG, CYU3P_EVENT_OR);

		if(gpioId == FX3State.PinMap.FX3_PIN_GPIO2)
			CyU3PEventSet(&GpioHandler, FX3_GPIO2_INTERRUPT_FLAG, CYU3P_EVENT_OR);

		if(gpioId == FX3State.PinMap.FX3_PIN_GPIO3)
			CyU3PEventSet(&GpioHandler, FX3_GPIO3_INTERRUPT_FLAG, CYU3P_EVENT_OR);

		if(gpioId == FX3State.PinMap.FX3_PIN_GPIO4)
			CyU3PEventSet(&GpioHandler, FX3_GPIO4_INTERRUPT_FLAG, CYU3P_EVENT_OR);
    }
}

/**
  * @brief This function handles critical errors generated by the ADI application.
  *
  * @param status The error code which corresponds with the fatal error encountered during startup.
  *
  * @returns void
  *
  * This function prints the error message to the debug console, waits five seconds, and performs
  * a hard reset. Performing the hard reset will clear the SRAM and reboot the FX3 into the second
  * stage iSensors FX3 bootloader. Could probably do something more intelligent, but at least this
  * approach will not lock up the FX3 after a failed boot.
 **/
void AdiAppErrorHandler(CyU3PReturnStatus_t status)
{
    /* Application failed with the error code status */
	CyU3PDebugPrint (4, "Application failed with fatal error! Error code: 0x%x\r\n", status);

	for(int i = 5; i > 0; i--)
	{
		CyU3PDebugPrint (4, "Rebooting in %d seconds...\r\n", (i - 1));
		/* Thread sleep : 1000 ms */
		CyU3PThreadSleep(1000);
	}
	 /* Perform hard system reset */
	CyU3PDeviceReset(CyFalse);
}

/**
  * @brief This function is called to shut down the application.
  *
  * @returns void
  *
  * This function cleans up the resources used by the ADI application
  * and prepares them for the next run. Since this function is intended
  * to be called as part of the shut down process, no error handling is
  * performed. Any transient errors should be resolved the next time
  * the system boots (after this function call is finished).
 **/
void AdiAppStop()
{
	CyU3PDebugPrint (4, "Application stopping!\r\n");

	/* Signal that the app thread has been stopped */
	FX3State.AppActive = CyFalse;

    /* De-init flash memory */
    AdiFlashDeInit();

	/* Clean up UART (debug) */
	CyU3PUartDeInit ();

	/* Clean up GPIO */
	CyU3PGpioDeInit();

	/* Clean up SPI */
	CyU3PSpiDeInit();

	/* Clean up event handlers */
	CyU3PEventDestroy(&EventHandler);
	CyU3PEventDestroy(&GpioHandler);

	/* Flush endpoint memory */
	CyU3PUsbFlushEp(ADI_STREAMING_ENDPOINT);
	CyU3PUsbFlushEp(ADI_FROM_PC_ENDPOINT);
	CyU3PUsbFlushEp(ADI_TO_PC_ENDPOINT);

	/* Clean up DMAs */
	CyU3PDmaChannelDestroy(&ChannelFromPC);
	CyU3PDmaChannelDestroy(&ChannelToPC);

	/* Disable endpoints */
	CyU3PEpConfig_t epConfig;
	CyU3PMemSet ((uint8_t *)&epConfig, 0, sizeof (epConfig));
	epConfig.enable = CyFalse;

	/* Write configuration to each endpoint */

	/* Set endpoint config for RTS endpoint */
	CyU3PSetEpConfig(ADI_STREAMING_ENDPOINT, &epConfig);

	/* Set endpoint config for the PC to FX3 endpoint */
	CyU3PSetEpConfig(ADI_FROM_PC_ENDPOINT, &epConfig);

	/* Set endpoint config for the FX3 to PC endpoint */
	CyU3PSetEpConfig(ADI_TO_PC_ENDPOINT, &epConfig);
}

/**
  * @brief This function sets up the necessary resources to start the ADI application.
  *
  * @returns void
  *
  * The application startup process configures all GPIO and timers used by the firmware, as
  * well as the USB endpoints, DMA controller, and SPI hardware. After all configuration is
  * performed, the AppActive flag is set to true.
  *
  * GPIO Clock configuration:
  * SYS_CLK = 403.2MHz
  * GPIO Fast Clock = SYS_CLK / 2 -> 201.6MHz
  * GPIO Slow Clock (Used for 10MHz timer) = Fast Clock / 20 -> 10.08MHz
  * Simple GPIO Sample Clock = Fast Clock / 2 -> 100.8MHz
 **/
void AdiAppStart()
{
	CyU3PUSBSpeed_t usbSpeed = CyU3PUsbGetSpeed();
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
	CyU3PGpioSimpleConfig_t gpioConfig = {0};
	CyU3PGpioClock_t gpioClock = {0};
	CyU3PGpioComplexConfig_t gpioComplexConfig = {0};
	CyU3PEpConfig_t epConfig = {0};
	CyU3PDmaChannelConfig_t dmaConfig = {0};

    /* Based on the Bus Speed configure the endpoint packet size */
    switch (usbSpeed)
    {
        case CY_U3P_FULL_SPEED:
        	FX3State.UsbBufferSize = 64;
            CyU3PDebugPrint (4, "Connected at USB 1.0 speed.\r\n");
            break;

        case CY_U3P_HIGH_SPEED:
        	FX3State.UsbBufferSize = 512;
            CyU3PDebugPrint (4, "Connected at USB 2.0 speed.\r\n");
            break;

        case  CY_U3P_SUPER_SPEED:
        	FX3State.UsbBufferSize = 1024;
            CyU3PDebugPrint (4, "Connected at USB 3.0 speed.\r\n");
            break;

        default:
        	/* Invalid USB speed */
        	AdiLogError(Main_c, __LINE__, status);
            AdiAppErrorHandler (CY_U3P_ERROR_FAILURE);
            break;
    }

    /* Configure GPIO clocks for ADI application */
    CyU3PMemSet ((uint8_t *)&gpioClock, 0, sizeof (gpioClock));
	gpioClock.fastClkDiv = 2;
	gpioClock.slowClkDiv = 20;
	gpioClock.simpleDiv = CY_U3P_GPIO_SIMPLE_DIV_BY_2;
	gpioClock.clkSrc = CY_U3P_SYS_CLK;
	gpioClock.halfDiv = 0;

	/* Set GPIO configuration and attach GPIO event handler */
	status = CyU3PGpioInit(&gpioClock, AdiGPIOEventHandler);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

    /* Get FX3 board type for FX3 state */
    FX3State.BoardType = AdiGetFX3BoardType();

    /* Enable 3.3V power supply by driving 5V pin high, then 3.3V pin low */
    if(FX3State.BoardType != CypressFX3Board)
    {
    	CyU3PDebugPrint (4, "Analog Devices iSensor FX3 Board Detected! Configuring Power Control Circuit...\r\n");
    	/* Configure power control circuit */
    	CyU3PDeviceGpioOverride(ADI_5V_EN, CyTrue);
    	CyU3PDeviceGpioOverride(ADI_3_3V_EN, CyTrue);
    	CyU3PMemSet ((uint8_t *)&gpioConfig, 0, sizeof (gpioConfig));
    	gpioConfig.outValue = CyTrue;
    	gpioConfig.inputEn = CyFalse;
    	gpioConfig.driveLowEn = CyTrue;
    	gpioConfig.driveHighEn = CyTrue;
    	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
    	status = CyU3PGpioSetSimpleConfig(ADI_5V_EN, &gpioConfig);
    	gpioConfig.outValue = CyFalse;
    	status = CyU3PGpioSetSimpleConfig(ADI_3_3V_EN, &gpioConfig);
    	/* Configure flash write enable (pull up, output set high) */
		CyU3PDeviceGpioOverride(ADI_FLASH_WRITE_ENABLE_PIN, CyTrue);
		gpioConfig.outValue = CyTrue;
		status = CyU3PGpioSetSimpleConfig(ADI_FLASH_WRITE_ENABLE_PIN, &gpioConfig);
		GCTL_WPU_CFG_UPPR |= (1 << (ADI_FLASH_WRITE_ENABLE_PIN - 32));
    	/* Map pin assignments */
    	FX3State.PinMap.ADI_PIN_RESET = 1;
    	FX3State.PinMap.ADI_PIN_DIO4 = 2;
    	FX3State.PinMap.ADI_PIN_DIO3 = 3;
    	FX3State.PinMap.ADI_PIN_DIO2 = 4;
    	FX3State.PinMap.ADI_PIN_DIO1 = 5;
    	FX3State.PinMap.FX3_PIN_GPIO1 = 6;
    	FX3State.PinMap.FX3_PIN_GPIO2 = 7;
    	FX3State.PinMap.FX3_PIN_GPIO3 = 8;
    	FX3State.PinMap.FX3_PIN_GPIO4 = 12;
    }
    else
    {
    	CyU3PDebugPrint (4, "Cypress SuperSpeed Explorer FX3 Board Detected!\r\n");
    	/* Map pin assignments */
    	FX3State.PinMap.ADI_PIN_RESET = 0;
    	FX3State.PinMap.ADI_PIN_DIO4 = 1;
    	FX3State.PinMap.ADI_PIN_DIO3 = 2;
    	FX3State.PinMap.ADI_PIN_DIO2 = 3;
    	FX3State.PinMap.ADI_PIN_DIO1 = 4;
    	FX3State.PinMap.FX3_PIN_GPIO1 = 5;
    	FX3State.PinMap.FX3_PIN_GPIO2 = 6;
    	FX3State.PinMap.FX3_PIN_GPIO3 = 7;
    	FX3State.PinMap.FX3_PIN_GPIO4 = 12;
    }

	/* Override all pins used by ADI to act as GPIO.
	 * Configuration relies on io matrix configuration in main(). For failed
	 * GPIO config, firmware logs error then continues (GPIO config not crucial for
	 * system functionality, don't want to reboot */
	status = CyU3PDeviceGpioOverride (FX3State.PinMap.ADI_PIN_DIO1, CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PDeviceGpioOverride (FX3State.PinMap.ADI_PIN_DIO2, CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PDeviceGpioOverride (FX3State.PinMap.ADI_PIN_DIO3, CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PDeviceGpioOverride (FX3State.PinMap.ADI_PIN_DIO4, CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PDeviceGpioOverride (FX3State.PinMap.FX3_PIN_GPIO1, CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PDeviceGpioOverride (FX3State.PinMap.FX3_PIN_GPIO2, CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PDeviceGpioOverride (FX3State.PinMap.FX3_PIN_GPIO3, CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PDeviceGpioOverride (FX3State.PinMap.FX3_PIN_GPIO4, CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PDeviceGpioOverride (FX3State.PinMap.ADI_PIN_RESET, CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PDeviceGpioOverride(ADI_USER_LED_PIN, CyTrue);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PDeviceGpioOverride(ADI_TIMER_PIN, CyFalse);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	/* Set the GPIO configuration for each GPIO that was just overridden */
	CyU3PMemSet ((uint8_t *)&gpioConfig, 0, sizeof (gpioConfig));
	gpioConfig.outValue = CyFalse;
	gpioConfig.inputEn = CyTrue;
	gpioConfig.driveLowEn = CyFalse;
	gpioConfig.driveHighEn = CyFalse;
	gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;

	status = CyU3PGpioSetSimpleConfig(FX3State.PinMap.ADI_PIN_DIO1, &gpioConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PGpioSetSimpleConfig(FX3State.PinMap.ADI_PIN_DIO2, &gpioConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PGpioSetSimpleConfig(FX3State.PinMap.ADI_PIN_DIO3, &gpioConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PGpioSetSimpleConfig(FX3State.PinMap.ADI_PIN_DIO4, &gpioConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PGpioSetSimpleConfig(FX3State.PinMap.FX3_PIN_GPIO1, &gpioConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PGpioSetSimpleConfig(FX3State.PinMap.FX3_PIN_GPIO2, &gpioConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PGpioSetSimpleConfig(FX3State.PinMap.FX3_PIN_GPIO3, &gpioConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PGpioSetSimpleConfig(FX3State.PinMap.FX3_PIN_GPIO4, &gpioConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	status = CyU3PGpioSetSimpleConfig(FX3State.PinMap.ADI_PIN_RESET, &gpioConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

    /* Configure user LED pin as output driven low */
	gpioConfig.outValue = CyFalse;
	gpioConfig.inputEn = CyFalse;
	gpioConfig.driveLowEn = CyTrue;
	gpioConfig.driveHighEn = CyTrue;
	status = CyU3PGpioSetSimpleConfig(ADI_USER_LED_PIN, &gpioConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    }

	/* Configure high-speed, high-resolution timer using a complex GPIO */
	CyU3PMemSet ((uint8_t *)&gpioComplexConfig, 0, sizeof (gpioComplexConfig));
	gpioComplexConfig.outValue = CyFalse;
	gpioComplexConfig.inputEn = CyFalse;
	gpioComplexConfig.driveLowEn = CyTrue;
	gpioComplexConfig.driveHighEn = CyTrue;
	gpioComplexConfig.pinMode = CY_U3P_GPIO_MODE_STATIC;
	gpioComplexConfig.intrMode = CY_U3P_GPIO_NO_INTR;
	gpioComplexConfig.timerMode = CY_U3P_GPIO_TIMER_LOW_FREQ;
	gpioComplexConfig.timer = 0;
	gpioComplexConfig.period = 0xFFFFFFFF;
	gpioComplexConfig.threshold = 0xFFFFFFFF;
	status = CyU3PGpioSetComplexConfig(ADI_TIMER_PIN, &gpioComplexConfig);
	/* Timer config failure is critical error, force system reboot */
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

    /* Save bitmask of the timer pin config */
    FX3State.TimerPinConfig = (GPIO->lpp_gpio_pin[ADI_TIMER_PIN_INDEX].status & ~CY_U3P_LPP_GPIO_INTR);

    /* Configure the SPI controller */

    /* Set the stall time in microseconds */
    FX3State.StallTime = 25;

    /* Set the DUT type */
    FX3State.DutType = ADcmXL3021;

    /* Set the data ready pin */
    FX3State.DrPin = FX3State.PinMap.ADI_PIN_DIO2;

    /* Enable the use of a data ready pin */
    FX3State.DrActive = CyTrue;

    /* Set the data ready polarity */
    FX3State.DrPolarity = CyTrue;

    /* Configure default global SPI parameters */
    CyU3PMemSet ((uint8_t *)&FX3State.SpiConfig, 0, sizeof(FX3State.SpiConfig));
    FX3State.SpiConfig.isLsbFirst = CyFalse;
    FX3State.SpiConfig.cpol       = CyTrue;
    FX3State.SpiConfig.ssnPol     = CyFalse;
    FX3State.SpiConfig.cpha       = CyTrue;
    FX3State.SpiConfig.leadTime   = CY_U3P_SPI_SSN_LAG_LEAD_ONE_CLK;
    FX3State.SpiConfig.lagTime    = CY_U3P_SPI_SSN_LAG_LEAD_ONE_CLK;
    FX3State.SpiConfig.ssnCtrl    = CY_U3P_SPI_SSN_CTRL_HW_END_OF_XFER;
    FX3State.SpiConfig.clock      = 2000000;
    FX3State.SpiConfig.wordLen    = 16;

    /* Start the SPI module and configure the FX3 as a master.
     * As with the GPIO configuration, SPI also relies on the io matrix to be correct. */
    status = CyU3PSpiInit();
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

    status = CyU3PSpiSetConfig (&FX3State.SpiConfig, NULL);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

    /* Configure I2C */
    FX3State.I2CBitRate = 100000;
    AdiI2CInit(100000, CyFalse);

    /* Configure global, user event flags */

	/* Create the stream/general use event handler */
	status = CyU3PEventCreate(&EventHandler);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

	/* Create GPIO event handler */
	status = CyU3PEventCreate(&GpioHandler);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

    /* Configure bulk endpoints */
	CyU3PMemSet((uint8_t *)&epConfig, 0, sizeof (epConfig));

	/* Set bulk endpoint parameters */
	epConfig.enable = CyTrue;
	epConfig.epType = CY_U3P_USB_EP_BULK;
	epConfig.burstLen = 1;
	epConfig.pcktSize = FX3State.UsbBufferSize;
	epConfig.streams = 0;

	/* Set endpoint config for RTS endpoint */
	status = CyU3PSetEpConfig(ADI_STREAMING_ENDPOINT, &epConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

	/* Set endpoint config for the PC to FX3 endpoint */
	status = CyU3PSetEpConfig(ADI_FROM_PC_ENDPOINT, &epConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

	/* Set endpoint config for the FX3 to PC endpoint */
	status = CyU3PSetEpConfig(ADI_TO_PC_ENDPOINT, &epConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

	/* Flush endpoint memory */
	CyU3PUsbFlushEp(ADI_STREAMING_ENDPOINT);
	CyU3PUsbFlushEp(ADI_FROM_PC_ENDPOINT);
	CyU3PUsbFlushEp(ADI_TO_PC_ENDPOINT);

	/* Configure DMAs */
	CyU3PMemSet((uint8_t *)&dmaConfig, 0, sizeof (dmaConfig));
    dmaConfig.size 				= FX3State.UsbBufferSize;
    dmaConfig.count 			= 0;
    dmaConfig.dmaMode 			= CY_U3P_DMA_MODE_BYTE;
    dmaConfig.prodHeader     	= 0;
    dmaConfig.prodFooter     	= 0;
    dmaConfig.consHeader     	= 0;
    dmaConfig.notification   	= 0;
    dmaConfig.cb             	= NULL;
    dmaConfig.prodAvailCount 	= 0;

    /* Configure DMA for ChannelFromPC */
    dmaConfig.prodSckId = CY_U3P_UIB_SOCKET_PROD_1;
    dmaConfig.consSckId = CY_U3P_CPU_SOCKET_CONS;
    status = CyU3PDmaChannelCreate(&ChannelFromPC, CY_U3P_DMA_TYPE_MANUAL_IN, &dmaConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

    /* Configure DMA for ChannelToPC */
    dmaConfig.prodSckId = CY_U3P_CPU_SOCKET_PROD;
    dmaConfig.consSckId = CY_U3P_UIB_SOCKET_CONS_2;
    status = CyU3PDmaChannelCreate(&ChannelToPC, CY_U3P_DMA_TYPE_MANUAL_OUT, &dmaConfig);
    if (status != CY_U3P_SUCCESS)
    {
    	AdiLogError(Main_c, __LINE__, status);
    	AdiAppErrorHandler(status);
    }

    /* Set app active flag */
    FX3State.AppActive = CyTrue;

    /*Print verbose mode message */
#ifdef VERBOSE_MODE
    CyU3PDebugPrint (4, "Verbose mode enabled. Device status will be logged to the serial output.\r\n");
#endif

#ifndef VERBOSE_MODE
    CyU3PDebugPrint (4, "Verbose mode not enabled. Only error messages will be logged to the serial output.\r\n");
#endif

    /*Print boot message */
    CyU3PDebugPrint (4, "Analog Devices iSensor FX3 Demonstration Platform started successfully!\r\n");
}

/**
  * @brief This function determines the type of the connected FX3 board.
  *
  * @returns The connected board type. This can be a Cypress Explorer kit or iSensor FX3 board
  *
  * This function works by measuring two ID pins which are assigned on the iSensor FX3 demonstration
  * platform. These ID pins can be high, low, or tri-stated, allowing for 9 possible hardware ID
  * configurations. The weak pull up / weak pull down functionality of the FX3 GPIO allows reliable
  * differentiation between all three states.
 **/
FX3BoardType AdiGetFX3BoardType()
{
	PinState id0, id1;

	/* Get state for each ID pin */
	id0 = AdiGetPinState(ADI_ID_PIN_0);
	id1 = AdiGetPinState(ADI_ID_PIN_1);

	/* Convert hardware ID to board type */

	/* 2'bZZ: iSensor Rev A */
	if((id1 == HighZ)&&(id0 == HighZ))
	{
		return iSensorFX3Board_A;
	}

	/* 2'bZ0: iSensor Rev B */
	if((id1 == HighZ)&&(id0 == Low))
	{
		return iSensorFX3Board_B;
	}

	/* 2'bZ1: Cypress board*/
	if((id1 == HighZ)&&(id0 == High))
	{
		return CypressFX3Board;
	}

	/* 2'b0Z: iSensor Rev D */
	if((id1 == Low)&&(id0 == HighZ))
	{
		return iSensorFX3Board_D;
	}

	/* 2'b00: iSensor Rev E */
	if((id1 == Low)&&(id0 == Low))
	{
		return iSensorFX3Board_E;
	}

	/* 2'b01: iSensor Rev F */
	if((id1 == Low)&&(id0 == High))
	{
		return iSensorFX3Board_F;
	}

	/* 2'b1Z: iSensor Rev C */
	if((id1 == High)&&(id0 == HighZ))
	{
		return iSensorFX3Board_C;
	}

	/* 2'b10: iSensor Rev G */
	if((id1 == High)&&(id0 == Low))
	{
		return iSensorFX3Board_G;
	}

	/* 2'b11: iSensor Rev H */
	if((id1 == High)&&(id0 == High))
	{
		return iSensorFX3Board_H;
	}

	/* Shouldnt get here. Return bad value to make fail up the line */
	return 0xFF;
}

/**
  * @brief This function is called by the RTOS kernel after booting and creates all the user threads.
  *
  * After the ThreadX kernel is started by a call to CyU3PKernelEntry() in main, this function is called.
  * It creates the AppThread (for general execution / handling vendor requests) and the StreamThread for
  * handling high throughput data streaming from a DUT.
 **/
void CyFxApplicationDefine (void)
{
    void *ptr = NULL;
    uint32_t retThrdCreate = CY_U3P_SUCCESS;

    /* Create application (main) thread */
    ptr = CyU3PMemAlloc(APPTHREAD_STACK);

    /* Create the thread for the application */
    retThrdCreate = CyU3PThreadCreate (&AppThread, /* Thread structure. */
            "21:AppThread",                        /* Thread ID and name. */
            AdiAppThreadEntry,                     /* Thread entry function. */
            0,                                     /* Thread input parameter. */
            ptr,                                   /* Pointer to the allocated thread stack. */
            APPTHREAD_STACK,                       /* Allocated thread stack size. */
            APPTHREAD_PRIORITY,                    /* Thread priority. */
            APPTHREAD_PRIORITY,                    /* Thread pre-emption threshold: No preemption. */
            CYU3P_NO_TIME_SLICE,                   /* No time slice. Thread will run until task is
                                                      completed or until the higher priority
                                                      thread gets active. */
            CYU3P_AUTO_START                       /* Start the thread immediately. */
            );

    /* Check if creating thread succeeded */
    if(retThrdCreate != CY_U3P_SUCCESS)
    {
    	/* Thread creation failed. Fatal error. Cannot continue. */
    	while(1);
    }

    /* Create the thread for streaming data */
    ptr = CyU3PMemAlloc(STREAMTHREAD_STACK);

    /* Create the streaming thread */
    retThrdCreate = CyU3PThreadCreate (&StreamThread, 	/* Thread structure. */
            "22:StreamThread",                 			/* Thread ID and name. */
            AdiStreamThreadEntry,              			/* Thread entry function. */
            0,                                     		/* Thread input parameter. */
            ptr,                                   		/* Pointer to the allocated thread stack. */
            STREAMTHREAD_STACK,                       	/* Allocated thread stack size. */
            STREAMTHREAD_PRIORITY,                    	/* Thread priority. */
            STREAMTHREAD_PRIORITY,                    	/* Thread pre-emption threshold: No preemption. */
            CYU3P_NO_TIME_SLICE,                   		/* No time slice. Thread will run until task is
                                                      	 completed or until the higher priority
                                                      	 thread gets active. */
            CYU3P_AUTO_START                      		/* Start the thread immediately. */
            );

    /* Check if creating thread succeeded */
    if (retThrdCreate != CY_U3P_SUCCESS)
    {
    	/* Thread creation failed. Fatal error. Cannot continue. */
    	while(1);
    }
}
