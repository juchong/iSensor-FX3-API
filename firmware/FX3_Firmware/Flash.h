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
  * @file		Flash.h
  * @date		04/30/2020
  * @author		A. Nolan (alex.nolan@analog.com)
  * @brief		Header file for FX3 flash interfacing module.
 **/

#ifndef FLASH_H_
#define FLASH_H_

/* Includes */
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyu3i2c.h"
#include "main.h"

/* Public function prototypes */
CyU3PReturnStatus_t AdiFlashInit();
void AdiFlashDeInit();
void AdiFlashWrite(uint32_t Address, uint16_t NumBytes, uint8_t* WriteBuf);
void AdiFlashRead(uint32_t Address, uint16_t NumBytes, uint8_t* ReadBuf);
void AdiFlashReadHandler(uint32_t Address, uint16_t NumBytes);

/** Page size for attached i2c flash memory (64 bytes)  */
#define FLASH_PAGE_SIZE		0x40

/** Flash operation timeout  */
#define FLASH_TIMEOUT_MS	5000

#endif /* FLASH_H_ */
