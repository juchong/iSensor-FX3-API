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
  * @file		HelperFunctions.h
  * @date		6/17/2020
  * @author		A. Nolan (alex.nolan@analog.com)
  * @brief 		Header file for a set of general purpose iSensor FX3 helper functions
 **/

#ifndef HELPERFUNCTIONS_H_
#define HELPERFUNCTIONS_H_

/* Include main */
#include "main.h"

/** Enum of possible DUT supply voltages */
typedef enum DutVoltage
{
	/** DUT supply off */
	Off = 0,

	/** DUT supply on, at 3.3V regulated output */
	On3_3Volts = 1,

	/** DUT supply on, at 5V USB output */
	On5_0Volts = 2
}DutVoltage;

/* Public function prototypes */
void AdiConfigureWatchdog();
void AdiGetBuildDate(uint8_t * outBuf);
void AdiSendStatus(uint32_t status, uint16_t count, CyBool_t isControlEndpoint);
CyU3PReturnStatus_t AdiSetDutSupply(DutVoltage SupplyMode);
CyU3PReturnStatus_t AdiSleepForMicroSeconds(uint32_t numMicroSeconds);
void AdiReturnBulkEndpointData(CyU3PReturnStatus_t status, uint16_t length);

#endif /* HELPERFUNCTIONS_H_ */
