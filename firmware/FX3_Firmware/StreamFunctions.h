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
  * @file		StreamFunctions.h
  * @date		8/1/2019
  * @author		A. Nolan (alex.nolan@analog.com)
  * @author 	J. Chong (juan.chong@analog.com)
  * @brief		Header file for all data streaming functions (generic, burst, ADcm real time, transfer).
 **/

#ifndef STREAM_FUNCTIONS_H
#define STREAM_FUNCTIONS_H

/* Include the main header file */
#include "main.h"

/* Real-time data stream functions. */
CyU3PReturnStatus_t AdiRealTimeStreamStart();
CyU3PReturnStatus_t AdiRealTimeStreamFinished();

/* Generic data stream functions. */
CyU3PReturnStatus_t AdiGenericStreamStart();
CyU3PReturnStatus_t AdiGenericStreamFinished();

/* Transfer stream functions */
CyU3PReturnStatus_t AdiTransferStreamStart();
CyU3PReturnStatus_t AdiTransferStreamFinished();

/* Burst stream functions. */
CyU3PReturnStatus_t AdiBurstStreamStart();
CyU3PReturnStatus_t AdiBurstStreamFinished();

/* I2C stream functions */
CyU3PReturnStatus_t AdiI2CStreamStart();
CyU3PReturnStatus_t AdiI2CStreamFinished();

/* General stream functions. */
CyU3PReturnStatus_t AdiStopAnyDataStream();
CyBool_t AdiPrintStreamState();
CyU3PReturnStatus_t AdiConfigureDrPin();

/* Config functions */
void AdiConfigStreamStallTimer();

/*
 * Stream action commands
 */

/** Control endpoint index value to indicate a stream is done (all data read). */
#define ADI_STREAM_DONE_CMD						0

/** Control endpoint index value to indicate a stream needs to be started. */
#define ADI_STREAM_START_CMD					1

/** Control endpoint index value to asynchronously stop a stream. */
#define ADI_STREAM_STOP_CMD						2

#endif
