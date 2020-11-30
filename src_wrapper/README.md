# Overview

The FX3ApiWrapper library presents a simplified interface to the FX3 API and corresponding DUT interfacing libraries

## Use Case

This library is intended to allow easy use of the FX3 API and adisInterface in non .NET languages which can consume .NET DLL's. 

All DUT interfacing functions work on numeric or string primitives, instead of .NET class objects. This significantly simplifies the interface for the caller (Labview, python, etc)

## Labview

Example VI utilizing the FX3ApiWrapper coming soon

## Matlab

Example script (Matlab\fx3_api_example.m) connects to an FX3 board, blinks user LED, and reads output registers from an ADIS1650x DUT
* Tested using Matlab R2017B (64-bit) running on Windows 10

## Python

Example script (Python\fx3_api_example.py) connects to an FX3 board, blinks user LED, and reads output registers from an ADIS1650x DUT
* Tested using Python 3.7 (32-bit) running on Windows 10

## Debugging

The FX3 libraries can be debugged while running under MatLab/Python using the Visual Studio "Attach to Process" functionality. This allows you to step through the .NET source code as it is invoked by the calling language.