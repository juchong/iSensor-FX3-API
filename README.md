# iSensor FX3 API - A .NET API for the iSensor FX3 Firmware

## Test Status

![Most Recent Test Results](https://raw.githubusercontent.com/ajn96/iSensor-FX3-Test/master/Results/test_status.png)

The test repository for the iSensor FX3 API and FX3 Firmware is hosted [here](https://github.com/ajn96/iSensor-FX3-Test). This repository includes source for all test cases (written in C# running on [nUnit](https://github.com/nunit) 2.6.4) and a simple nightly build CI script for a Windows host. All test cases are system level tests running on target hardware.

## Overview

The FX3 API manages all the complex USB transactions and implements all the necessary tools to begin capturing high-speed, high-performance data in custom applications. This .NET-compatible API, written in VB.NET and C#, includes data streaming features tailored to reliably capturing inertial sensor data at the maximum data rate.

The FX3 Firmware was designed with compatibility and flexibility in mind. The firmware attempts to follow the Cypress program workflow and relies on FX3 system threading, execution priority, and event flags to execute firmware subroutines and transmit sensor data. Custom vendor commands trigger subroutines embedded in the firmware that read and write SPI data, measure external pulses, generate clock signals, and manage board configuration. Several SPI streaming modes are implemented, which allow applications to communicate with nearly every device in the iSensor portfolio. The freely-available, Eclipse-based, Cypress EZ USB Suite was used for all firmware development. 

## API and Firmware Documentation

Static-generated documentation for the FX3 API .NET library and firmware can be found here: [https://analogdevicesinc.github.io/iSensor-FX3-API/](https://analogdevicesinc.github.io/iSensor-FX3-API/)

## Windows Drivers

As of v1.0.6, custom, signed, Analog Devices drivers must be used to communicate with the iSensor FX3 Firmware. The driver installation package can be found in the [drivers](drivers) folder in this repository or downloaded directly from [here](drivers/FX3DriverSetup.exe). 

## iSensor FX3 Hardware

Design files, schematics, and other reference documents for the EVAL-ADIS-FX3 and the Cypress FX3 Explorer Breakout Board can be found [here](hardware).

## Getting Started

#### Bootloader Firmware Stage

The API is designed such that custom bootloader firmware is loaded into FX3 RAM prior to executing the  `Connect()`function. This custom bootloader exposes LED blinking commands, a unique FX3 serial number in the USB vendor descriptor, and more importantly allows the main application firmware to be loaded over the bootloader firmware without a reboot. These features allow multiple FX3 boards to communicate with multiple application instances on a single machine. They also provide a means of visually identifying multiple FX3 boards when preparing to initiate a connection. All connected boards should be running the bootloader firmware to be considered as a "valid" board by the API. If an FX3 board is identified by the Cypress driver, but is not running the custom bootloader, then it will be ignored by the API. *FX3 boards must be running the custom bootloader firmware prior to loading application firmware~*

#### Application Firmware Loading Stage

The API provides functions to detect, identify, and program FX3 boards in user applications. Once a valid board has been identified, the `Connect()` function will push the application firmware into FX3 RAM, overwriting the bootloader firmware. The function also verifies whether communication with the FX3 board is as expected.

#### API Features Overview

The FX3 API translates high-level user commands into the necessary low-level Cypress API calls required to communicate with the FX3 firmware. The API also simplifies SPI configuration, data ready behavior, and device management. Using the vendor command structure outlined by Cypress, different SPI capture and streaming modes can be called based upon the user's requirements. Additional functionality such as generating clocks and pulses, measuring the time between pin pulses, waiting for external triggers, and a few other features have also been baked into the firmware and API.

#### Example Application

An example application was developed to provide users with a simple starting point. The application repository can be found here.

## iSensor-Specific Library Information

The FX3 API implements in-house closed-source interface libraries (IRegInterface and IPinFcns) included in the `resources` folder in this repository. These libraries are required to maintain control and consistency with other iSensor devices and are defined in the AdisApi. 

This API (and the FX3-specific classes) should be used in place of the AdisBase and iSensorSpi classes for performing read/write operations. Unlike iSensorSpi, this class includes all the connection and SPI setup functions defined internally. This allows the FX3 API to perform the device connection and enumeration operations without having to pass the class an instance of AdisBase. 
