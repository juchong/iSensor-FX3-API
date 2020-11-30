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
  * @file		SpiFunctions.h
  * @date		8/1/2019
  * @author		A. Nolan (alex.nolan@analog.com)
  * @author 	J. Chong (juan.chong@analog.com)
  * @brief		Header file for all SPI related functions.
 **/

#ifndef SPI_FUNCTIONS_H
#define SPI_FUNCTIONS_H

/* Include the main header file */
#include "main.h"

/** Structure to store configuration parameters for a bitbang SPI. */
typedef struct BitBangSpiConf
{
	/** The master out, slave in data pin number */
	uint8_t MOSI;

	/** The master in, slave out data pin number */
	uint8_t MISO;

	/** The chip select pin number */
	uint8_t CS;

	/** The SPI clock pin number */
	uint8_t SCLK;

	/** The delay per half-period of the SPI clock. Approx. 62ns per. */
	uint32_t HalfClockDelay;

	/** The delay after dropping CS before toggling SCLK */
	uint16_t CSLeadDelay;

	/** The delay after finishing SCLKs before raising CS */
	uint16_t CSLagDelay;

	/** SPI clock phase setting */
	CyBool_t CPHA;

	/** SPI clock polarity setting */
	CyBool_t CPOL;
}BitBangSpiConf;

/* SPI configuration functions */
CyU3PReturnStatus_t AdiGetSpiSettings();
CyBool_t AdiSpiUpdate(uint16_t index, uint16_t value, uint16_t length);
CyU3PReturnStatus_t AdiSpiResetFifo(CyBool_t isTx, CyBool_t isRx);
CyU3PSpiConfig_t AdiGetSpiConfig();
void AdiSetSpiWordLength(uint8_t wordLength);
void AdiPrintSpiConfig(CyU3PSpiConfig_t config);
CyU3PReturnStatus_t AdiRestartSpi();

/* SPI data transfer functions */
void AdiSpiTransferWord(uint8_t *txBuf, uint8_t *rxBuf);
CyU3PReturnStatus_t AdiTransferBytes(uint32_t writeData);
CyU3PReturnStatus_t AdiWriteRegByte(uint16_t addr, uint8_t data);
CyU3PReturnStatus_t AdiReadRegBytes(uint16_t addr);

/* Bitbang SPI functions */
CyU3PReturnStatus_t AdiBitBangSpiHandler();

/** Offset to make the short side of the bitbang SPI match long side. Approx. 62ns per tick */
#define BITBANG_HALFCLOCK_OFFSET 5

/** Offset for bit bang stall time calc */
#define STALL_COUNT_OFFSET 10

/** Register setting to drive GPIO high (enable (31), drive high (5), drive low (4), out enabled (0)) */
#define GPIO_HIGH	(1 << 31)|(1 << 5)|(1 << 4)|(1 << 0)

/** Register setting to drive GPIO low (enable (31), drive high (5), drive low (4))*/
#define GPIO_LOW	(1 << 31)|(1 << 5)|(1 << 4)

#endif
