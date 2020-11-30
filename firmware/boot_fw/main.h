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
  * @file		main.h
  * @date		8/1/2019
  * @author		A. Nolan (alex.nolan@analog.com)
  * @author 	J. Chong (juan.chong@analog.com)
  * @brief 		Main header file for the Analog Devices iSensor FX3 Demonstration Platform USB bootloader.
 **/

#ifndef MAIN_H_
#define MAIN_H_

/* Keep track of LED mode */
extern uint16_t mode;

/* Define LED GPIO */
#define APP_LED_GPIO   			(54)

/* Define SCK GPIO */
#define	APP_SCLK_GPIO			(53)

/*
 * Bootloader Vendor Commands
 */
/* Hard-reset the FX3 firmware (return to bootloader mode) */
#define ADI_HARD_RESET			(0xB1)

/* Turn on APP_LED_GPIO solid */
#define ADI_LED_ON				(0xEC)

/* Turn off APP_LED_GPIO */
#define ADI_LED_OFF				(0xED)

/* Turn off APP_LED_GPIO blinking */
#define ADI_LED_BLINKING_OFF	(0xEE)

/* Turn on APP_LED_GPIO blinking */
#define ADI_LED_BLINKING_ON		(0xEF)

#endif /* MAIN_H_ */
