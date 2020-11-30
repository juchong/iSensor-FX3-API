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
  * @file		I2cFunctions.h
  * @date		5/13/2020
  * @author		A. Nolan (alex.nolan@analog.com)
  * @brief 		Header file for USB-I2C interfacing module
 **/

#ifndef I2CFUNCTIONS_H_
#define I2CFUNCTIONS_H_

#include "main.h"

/* Public functions */
CyU3PReturnStatus_t AdiI2CReadHandler(uint16_t RequestLength);
CyU3PReturnStatus_t AdiI2CWriteHandler(uint16_t RequestLength);
CyU3PReturnStatus_t AdiI2CInit(uint32_t BitRate, CyBool_t isDMA);
uint32_t I2CParseUSBBuffer(uint32_t * timeout, uint32_t * numBytes, CyU3PI2cPreamble_t * preamble);

#endif /* I2CFUNCTIONS_H_ */
