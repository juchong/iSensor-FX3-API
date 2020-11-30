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
  * @file		StreamThread.h
  * @date		8/1/2019
  * @author		A. Nolan (alex.nolan@analog.com)
  * @author 	J. Chong (juan.chong@analog.com)
  * @brief		Header file for the streaming thread
 **/

#ifndef STREAM_THREAD_H
#define STREAM_THREAD_H

/* Include the main header file */
#include "main.h"

/* Function definitions (thread entry) */
void AdiStreamThreadEntry(uint32_t input);

/** StreamThread allocated stack size (2KB) */
#define STREAMTHREAD_STACK					(0x0800)

/** StreamThread execution priority for the thread scheduler */
#define STREAMTHREAD_PRIORITY					(8)

#endif
