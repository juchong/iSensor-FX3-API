# iSensor FX3 Firmware

## Overview

The iSensor FX3 firmware is designed to provide users with a means of reliably acquiring sensor data over a high-speed USB connection in any .NET compatible application. This firmware was designed for use on the Cypress FX3 SuperSpeed Explorer Kit and relies on the open source libraries provided by Cypress to operate. The freely-available, Eclipse-based, Cypress EZ USB Suite was used for all firmware development. 

Doxygen generated documentation for the firmware can be found here: https://analogdevicesinc.github.io/iSensor-FX3-API//firmware/docs/index.html.

## Setting Up The Development Environment

#### Firmware Environment

This repository includes an eclipse `.project` file for each firmware which enables easily importing the necessary code, resources, and build configuration into the Cypress EZ USB Suite environment. The Cypress EZ USB Suite IDE can be found on Cypress' website [here](https://www.cypress.com/documentation/software-and-drivers/ez-usb-fx3-software-development-kit). Once downloaded and installed, open the `Cypress EZ USB Suite`, Select `File -> Import -> Existing Project Into Workplace` and select the `.project` file in this repository

#### .NET Environment

This firmware relies heavily on the accompanying FX3 API to implement many timing-sensitive vendor commands, data transfers, etc. As of v1.0.4, the firmware version number must match the FX3 API version number for any application to function. Additional details on setting up the API development and example application environments can be found in their respective folders. 

## Drivers

As of v1.0.6, custom, signed, Analog Devices drivers must be used to communicate with the iSensor FX3 Firmware. The driver installation package can be found in the [drivers](https://github.com/analogdevicesinc/iSensor-FX3-API/tree/master/drivers) folder in this repository or downloaded directly from [here](https://github.com/analogdevicesinc/iSensor-FX3-API/raw/master/drivers/FX3DriverSetup.exe). 

## Debugging

Debugging on the Explorer Kit is done primarily through the UART port. Unfortunately, the onboard USB debugging connector utilizes the same FX3 GPIO pins as the SPI peripheral and will not properly function. To enable printing debugging messages, you'll need to use a USB->UART adapter [like this one](https://www.amazon.com/ADAFRUIT-Industries-954-Serial-Raspberry/dp/B00DJUHGHI/ref=sr_1_6?keywords=usb+uart&qid=1564080408&s=gateway&sr=8-6) to monitor GPIO 48 and 49 (labeled DQ30(RX) and DQ31(TX) on the Explorer Kit).  
