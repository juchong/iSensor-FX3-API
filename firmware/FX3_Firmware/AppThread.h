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
  * @file		AppThread.h
  * @date		8/1/2019
  * @author		A. Nolan (alex.nolan@analog.com)
  * @author 	J. Chong (juan.chong@analog.com)
  * @brief		Header file for all the primary application thread functions.
 **/

#ifndef APP_THREAD_H
#define APP_THREAD_H

/* Include the main header file */
#include "main.h"

/* Function definitions */
void AdiAppThreadEntry(uint32_t input);

/** AppThread allocated stack size (2KB) */
#define APPTHREAD_STACK        					(0x0800)

/** AppThread execution priority */
#define APPTHREAD_PRIORITY    						(8)

/*
 * ADI Event Handler Flag Definitions
 */

/** Event handler bit to kill any arbitrary thread early */
#define ADI_KILL_THREAD_EARLY					(1 << 0)

/** Event handler bit for real time stream start */
#define ADI_RT_STREAM_START						(1 << 1)

/** Event handler bit for asynchronously stopping a real time stream */
#define ADI_RT_STREAM_STOP						(1 << 2)

/** Event handler bit for cleaning up a real time stream */
#define ADI_RT_STREAM_DONE						(1 << 3)

/** Event handler bit for contiuing a real time stream, within the StreamThread */
#define ADI_RT_STREAM_ENABLE					(1 << 4)

/** Event handler bit for generic stream start */
#define ADI_GENERIC_STREAM_START				(1 << 5)

/** Event handler bit for asynchronously stopping a generic stream */
#define ADI_GENERIC_STREAM_STOP					(1 << 6)

/** Event handler bit for cleaning up a generic stream */
#define ADI_GENERIC_STREAM_DONE					(1 << 7)

/** Event handler bit for continuing a generic stream, within the StreamThread */
#define ADI_GENERIC_STREAM_ENABLE				(1 << 8)

/** Event handler bit for burst stream start */
#define ADI_BURST_STREAM_START					(1 << 9)

/** Event handler bit to asynchronously stop a burst stream */
#define ADI_BURST_STREAM_STOP					(1 << 10)

/** Event handler bit for cleaning up a burst stream */
#define ADI_BURST_STREAM_DONE					(1 << 11)

/** Event handler bit for continuing a burst stream, within the StreamThread */
#define ADI_BURST_STREAM_ENABLE					(1 << 12)

/** Event handler bit for starting a transfer (ISpi32) stream */
#define ADI_TRANSFER_STREAM_START				(1 << 13)

/** Event handler bit to asynchronously stop a transfer stream */
#define ADI_TRANSFER_STREAM_STOP				(1 << 14)

/** Event handler bit for cleaning up a transfer stream */
#define ADI_TRANSFER_STREAM_DONE				(1 << 15)

/** Event handler bit for continuing a transfer stream after one "buffer", within the StreamThread */
#define ADI_TRANSFER_STREAM_ENABLE				(1 << 16)

/** Start I2C read stream */
#define ADI_I2C_STREAM_START					(1 << 17)

/** Stop I2C read stream */
#define ADI_I2C_STREAM_STOP						(1 << 18)

/** I2C read stream done */
#define ADI_I2C_STREAM_DONE						(1 << 19)

/** I2C read stream enable */
#define ADI_I2C_STREAM_ENABLE					(1 << 20)

#endif
