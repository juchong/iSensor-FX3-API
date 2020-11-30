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
  * @file		PinFunctions.h
  * @date		8/1/2019
  * @author		A. Nolan (alex.nolan@analog.com)
  * @author 	J. Chong (juan.chong@analog.com)
  * @brief		Header file for all the pin I/O and timing related functions
 **/

#ifndef PIN_FUNCTIONS_H
#define PIN_FUNCTIONS_H

/* Include the main header file */
#include "main.h"

/** Enum of pin resistor settings for GPIO pull up/down*/
typedef enum PinResistorSetting
{
	/** No resistor on pin input stage */
	None = 0,

	/** Weak pull down (to ground). Approx. 50KOhm (per FX3 TRM) */
	PullDown = 1,

	/** Weak pull up (to Vdd) Approx. 50KOhm (per FX3 TRM) */
	PullUp = 2

}PinResistorSetting;

/** Enum of possible states for GPIO input pin */
typedef enum PinState
{
	/** Pin is logic low */
	Low = 0,

	/** Pin is logic high */
	High = 1,

	/** Pin is not being driven. This state is determined when the pin logic level follows the internal pull up/down setting */
	HighZ = 2
}PinState;

/* Function definitions */
CyU3PReturnStatus_t AdiPulseDrive();
CyU3PReturnStatus_t AdiPulseWait(uint16_t transferLength);
CyU3PReturnStatus_t AdiSetPin(uint16_t pinNumber, CyBool_t polarity);
CyU3PReturnStatus_t AdiMeasurePinFreq();
CyU3PReturnStatus_t AdiWaitForPin(uint32_t pinNumber, CyU3PGpioIntrMode_t interruptSetting, uint32_t timeoutTicks);
CyU3PReturnStatus_t AdiPinRead(uint16_t pin);
CyU3PReturnStatus_t AdiReadTimerValue();
CyU3PReturnStatus_t AdiConfigurePWM(CyBool_t EnablePWM);
CyU3PReturnStatus_t AdiMeasureBusyPulse(uint16_t transferLength);
CyU3PReturnStatus_t AdiConfigurePinInterrupt(uint16_t pin, CyBool_t polarity);
CyU3PReturnStatus_t AdiMeasurePinDelay(uint16_t transferLength);
CyU3PReturnStatus_t AdiSetPinResistor(uint16_t pin, PinResistorSetting setting);
uint32_t AdiMStoTicks(uint32_t desiredStallTime);
uint32_t AdiReadTimerRegValue();
CyBool_t AdiIsValidGPIO(uint16_t GpioId);
PinState AdiGetPinState(uint16_t pin);
void AdiGetBoardPinInfo(uint8_t * outBuf);

/*
 * GPIO Pin mapping definitions
 */

/** Control pins for power management circuit. 3.3V enable pin */
#define ADI_3_3V_EN								(33)

/** Control pins for power management circuit. 5V enable pin */
#define ADI_5V_EN 								(34)

/** FX3 hardware ID pin 0. Two ID pins allows for 9 possible board IDs */
#define ADI_ID_PIN_0							(17)

/** FX3 hardware ID pin 1. Two ID pins allows for 9 possible board IDs  */
#define ADI_ID_PIN_1							(15)

/** Flash write enable pin. Set to 0 to enable flash write, 1 to disable flash write */
#define ADI_FLASH_WRITE_ENABLE_PIN				(35)

/** Uart Tx Pin (for debug console) */
#define ADI_DEBUG_TX_PIN						(48)

/** I2C clock pin */
#define ADI_I2C_SCL_PIN							(58)

/** I2C data pin */
#define ADI_I2C_SDA_PIN							(59)

/** GPIO for user LED (Turn on/off or blink via firmware) */
#define ADI_USER_LED_PIN						(13)

/** Complex GPIO assigned as a timer input */
#define ADI_TIMER_PIN							(24)

/*
 * ADI GPIO Event Handler Definitions
 */

/** Event flag indicating a GPIO interrupt has triggered on DIO1 */
#define ADI_DIO1_INTERRUPT_FLAG					(1 << 0)

/** Event flag indicating a GPIO interrupt has triggered on DIO2 */
#define ADI_DIO2_INTERRUPT_FLAG					(1 << 1)

/** Event flag indicating a GPIO interrupt has triggered on DIO3 */
#define ADI_DIO3_INTERRUPT_FLAG					(1 << 2)

/** Event flag indicating a GPIO interrupt has triggered on DIO4 */
#define ADI_DIO4_INTERRUPT_FLAG					(1 << 3)

/** Event flag indicating a GPIO interrupt has triggered on FX3_GPIO1 */
#define FX3_GPIO1_INTERRUPT_FLAG					(1 << 4)

/** Event flag indicating a GPIO interrupt has triggered on FX3_GPIO2 */
#define FX3_GPIO2_INTERRUPT_FLAG					(1 << 5)

/** Event flag indicating a GPIO interrupt has triggered on FX3_GPIO3 */
#define FX3_GPIO3_INTERRUPT_FLAG					(1 << 6)

/** Event flag indicating a GPIO interrupt has triggered on FX3_GPIO4 */
#define FX3_GPIO4_INTERRUPT_FLAG					(1 << 7)

#endif
