'Copyright (c) 2018-2020 Analog Devices, Inc. All Rights Reserved.
'This software is proprietary to Analog Devices, Inc. and its licensors.
'
'File:         FX3DataStructures.vb
'Author:       Alex Nolan (alex.nolan@analog.com), Juan Chong (juan.chong@analog.com)
'Date:         7/31/2018     
'Description:  Collection of helper data structures used in the FX3Connection 

Imports AdisApi

#Region "Enums"

''' <summary>
''' Enum of all possible stream types which can be running
''' </summary>
Public Enum StreamType
    None = 0
    BurstStream = 1
    RealTimeStream = 2
    GenericStream = 3
    TransferStream = 4
    I2CReadStream = 5
End Enum

''' <summary>
''' This enum lists all USB endpoints generated and used by the application firmware.
''' </summary>
Public Enum EndpointAddresses
    ADI_STREAMING_ENDPOINT = &H81
    ADI_FROM_PC_ENDPOINT = &H1
    ADI_TO_PC_ENDPOINT = &H82
End Enum

''' <summary>
''' This enum lists all the available streaming commands which can be sent to the FX3 (in the endpoint index)
''' </summary>
Public Enum StreamCommands
    ADI_STREAM_DONE_CMD = 0
    ADI_STREAM_START_CMD = 1
    ADI_STREAM_STOP_CMD = 2
End Enum

''' <summary>
''' Possible FX3 board types. Used to differentiate iSensors FX3 board from Cypress Explorer kit. This list enumerates 
''' all possible hardware ID's provided by the FX3 ID pins. Most are not used.
''' </summary>
Public Enum FX3BoardType
    CypressFX3Board = 0
    iSensorFX3Board_A = 1
    iSensorFX3Board_B = 2
    iSensorFX3Board_C = 3
    iSensorFX3Board_D = 4
    iSensorFX3Board_E = 5
    iSensorFX3Board_F = 6
    iSensorFX3Board_G = 7
    iSensorFX3Board_H = 8
End Enum

''' <summary>
''' Setting for pull up / pull down resistors on FX3 GPIO input stage.
''' </summary>
Public Enum FX3PinResistorSetting
    'Disable internal resistor on FX3 GPIO input stage
    None = 0
    'Enable pull down resistor
    PullDown = 1
    'Enable pull up resistor
    PullUp = 2
End Enum

''' <summary>
''' This enum lists all supported vendor commands for the FX3 firmware. The LED commands can only be used with the ADI bootloader firmware.
''' </summary>
Public Enum USBCommands

    'Request codes 0 - 12 are reserved by USB protocol and handled by RTOS

    'I2C set bit rate command
    ADI_I2C_SET_BIT_RATE = &H10

    'I2C read command
    ADI_I2C_READ_BYTES = &H11

    'I2C write command
    ADI_I2C_WRITE_BYTES = &H12

    'I2C continuous stream read command
    ADI_I2C_READ_STREAM = &H13

    'I2C set rety count after slave sends NAK
    ADI_I2C_RETRY_COUNT = &H14

    'Return FX3 firmware ID
    ADI_FIRMWARE_ID_CHECK = &HB0

    'Hard-reset the FX3 firmware (return to bootloader mode)
    ADI_HARD_RESET = &HB1

    'Set FX3 SPI configuration
    ADI_SET_SPI_CONFIG = &HB2

    'Return FX3 SPI configuration
    ADI_READ_SPI_CONFIG = &HB3

    'Return the current status of the FX3 firmware
    ADI_GET_STATUS = &HB4

    'Return the FX3 unique serial number
    ADI_SERIAL_NUMBER_CHECK = &HB5

    ' Soft-reset the FX3 firmware (don't return to bootloader mode)
    ADI_WARM_RESET = &HB6

    'Set the DUT supply voltage (only works on ADI FX3 boards)
    ADI_SET_DUT_SUPPLY = &HB7

    'Get build date / time
    ADI_GET_BUILD_DATE = &HB8

    'Set the boot time code
    ADI_SET_BOOT_TIME = &HB9

    ' Get the type of the programmed board
    ADI_GET_BOARD_TYPE = &HBA

    'Start/stop a generic data stream
    ADI_STREAM_GENERIC_DATA = &HC0

    'Start/stop a burst data stream
    ADI_STREAM_BURST_DATA = &HC1

    'Read the value of a user-specified GPIO
    ADI_READ_PIN = &HC3

    'Read the current FX3 timer register value
    ADI_READ_TIMER_VALUE = &HC4

    'Drive a user-specified GPIO for a user-specified time
    ADI_PULSE_DRIVE = &HC5

    'Wait for a user-specified pin to reach a user-specified level (with timeout)
    ADI_PULSE_WAIT = &HC6

    'Drive a user-specified GPIO
    ADI_SET_PIN = &HC7

    'Return the pulse frequency (data ready on a user-specified pin
    ADI_MEASURE_DR = &HC8

    'Command to enable or disable a PWM signal
    ADI_PWM_CMD = &HC9

    'Used to transfer bytes without any intervention/protocol management
    ADI_TRANSFER_BYTES = &HCA

    'Command to trigger an event on the DUT and measure a subsequent pulse
    ADI_BUSY_MEASURE = &HCB

    'Starts a transfer stream for the ISpi32Interface
    ADI_TRANSFER_STREAM = &HCC

    'Bitbang SPI command
    ADI_BITBANG_SPI = &HCD

    'Reset the SPI controller
    ADI_RESET_SPI = &HCE

    'pin delay measure
    ADI_PIN_DELAY_MEASURE = &HCF

    'Start/stop a real-time stream
    ADI_STREAM_REALTIME = &HD0

    'Do nothing (default case)
    ADI_NULL_COMMAND = &HD1

    'Set GPIO resistor value
    ADI_SET_PIN_RESISTOR = &HD2

    'Read a word at a specified address and return the data over the control endpoint
    ADI_READ_BYTES = &HF0

    'Write one byte of data to a user-specified address
    ADI_WRITE_BYTE = &HF1

    'Clear error log stored in flash memory
    ADI_CLEAR_FLASH_LOG = &HF2

    ' Read flash memory
    ADI_READ_FLASH = &HF3

    'The following commands are for the ADI bootloader only

    'Turn on APP_LED_GPIO solid 
    ADI_LED_ON = &HEC

    'Turn off APP_LED_GPIO 
    ADI_LED_OFF = &HED

    'Turn off APP_LED_GPIO blinking 
    ADI_LED_BLINKING_OFF = &HEE

    'Turn on APP_LED_GPIO blinking 
    ADI_LED_BLINKING_ON = &HEF

End Enum

''' <summary>
''' Enum for the possible chip select modes
''' </summary>
Public Enum SpiChipselectControl
    'SSN Is controlled by API And Is Not at clock boundaries. 
    SPI_SSN_CTRL_FW = 0
    'SSN Is controlled by hardware And Is done In sync With clock.
    'The SSN Is asserted at the beginning Of a transfer, and de-asserted
    'at the End Of a transfer Or When no data Is available To transmit. 
    SPI_SSN_CTRL_HW_END_OF_XFER
    'SSN Is controlled by the hardware And Is done In sync With clock.
    'The SSN Is asserted at the beginning Of transfer Of every word, And
    'de-asserted at the End Of the transfer Of that word. 
    SPI_SSN_CTRL_HW_EACH_WORD
    'If CPHA Is 0, the SSN control Is per word; And If CPHA Is 1, Then the
    'SSN control Is per transfer. 
    SPI_SSN_CTRL_HW_CPHA_BASED
    'SSN control Is done externally by the firmware application.
    SPI_SSN_CTRL_NONE
End Enum

''' <summary>
''' Enum for determining the default device settings to be initialized
''' </summary>
Public Enum DeviceType
    'IMU device
    IMU = 0
    'ADcmXL device
    ADcmXL
    'Automotive grade part (32 bit SPI with CRC)
    AutomotiveSpi
    'Inertial component sensor
    ComponentSensor
End Enum

''' <summary>
''' Enum for the possible chip select lag/lead times, in SPI clock cycles
''' </summary>
Public Enum SpiLagLeadTime
    SPI_SSN_LAG_LEAD_ZERO_CLK = 0       ' SSN Is in sync with SCK.
    SPI_SSN_LAG_LEAD_HALF_CLK           ' SSN leads / lags SCK by a half clock cycle.
    SPI_SSN_LAG_LEAD_ONE_CLK            ' SSN leads / lags SCK by one clock cycle.
    SPI_SSN_LAG_LEAD_ONE_HALF_CLK       ' SSN leads / lags SCK by one And half clock cycles.
End Enum

''' <summary>
''' Enum of the possible iSensors DUT types
''' </summary>
Public Enum DUTType
    ADcmXL1021 = 0
    ADcmXL2021
    ADcmXL3021
    IMU
    LegacyIMU
End Enum

''' <summary>
''' Enum for DUT power supply modes. These power supply modes are only available on the ADI in-house
''' FX3 Demonstration platform.
''' </summary>
Public Enum DutVoltage
    Off = 0
    On3_3Volts = 1
    On5_0Volts = 2
End Enum

#End Region

#Region "BitBang SPI Config Class"

''' <summary>
''' This class stores all the relevant information about a given bit bang SPI connection.
''' </summary>
Public Class BitBangSpiConfig

    ''' <summary>
    ''' Chip select pin for bit bang SPI
    ''' </summary>
    Public CS As FX3PinObject

    ''' <summary>
    ''' SCLK pin for bit bang SPI
    ''' </summary>
    Public SCLK As FX3PinObject

    ''' <summary>
    ''' MOSI (master out, slave in) pin for bit bang SPI
    ''' </summary>
    Public MOSI As FX3PinObject

    ''' <summary>
    ''' MISO (master is, slave out) pin for bit bang SPI
    ''' </summary>
    Public MISO As FX3PinObject

    ''' <summary>
    ''' Number of timer ticks from CS falling edge to first sclk edge
    ''' </summary>
    Public CSLeadTicks As UShort

    ''' <summary>
    ''' Number of timer ticks from last sclk edge to CS rising edge
    ''' </summary>
    Public CSLagTicks As UShort

    ''' <summary>
    ''' Half SCLK period timer ticks
    ''' </summary>
    Public SCLKHalfPeriodTicks As UInteger

    ''' <summary>
    ''' Stall time timer ticks
    ''' </summary>
    Public StallTicks As UInteger

    ''' <summary>
    ''' Bit bang SPI clock phase. If set to false (0) data is sampled
    ''' on the clock active to inactive edge, and updated on the inactive to active
    ''' edge. If set to true (1), data is updated on the clock 
    ''' active to inactive transition, and data is sampled on the inactive to 
    ''' active edge. The CPHA/CPOL settings used are based on this diagram:
    ''' https://en.wikipedia.org/wiki/Serial_Peripheral_Interface#/media/File:SPI_timing_diagram2.svg
    ''' </summary>
    Public CPHA As Boolean

    ''' <summary>
    ''' Bit bang SPI clock polarity. True (1) is idle high, False (0) is
    ''' idle low. 
    ''' </summary>
    Public CPOL As Boolean

    ''' <summary>
    ''' Constructor which lets you specify set of default pins to use as bit bang SPI pins
    ''' </summary>
    ''' <param name="OverrideHardwareSpi">If the constructed BitBangSpiConfig should use hardware SPI pins, or FX3GPIO</param>
    Public Sub New(OverrideHardwareSpi As Boolean)

        If OverrideHardwareSpi Then
            'Override the hardware SPI pins
            SCLK = New FX3PinObject(53)
            CS = New FX3PinObject(54)
            MOSI = New FX3PinObject(56)
            MISO = New FX3PinObject(55)
        Else
            'Provide defaults onto the FX3 GPIO pins
            SCLK = New FX3PinObject(5) 'FX3_GPIO1
            CS = New FX3PinObject(6) 'FX3_GPIO2
            MOSI = New FX3PinObject(7) 'FX3_GPIO3
            MISO = New FX3PinObject(12) 'FX3_GPIO4
            m_UpdatePins = True
        End If

        'set defaults for iSensor SPI protocol (mode 3)
        CPHA = True
        CPOL = True

        CSLeadTicks = 5 'Lead one SCLK period
        CSLagTicks = 5 'Lag one SCLK period
        SCLKHalfPeriodTicks = 5 'Should give approx. 1MHz
        StallTicks = 82 '5us
    End Sub

    ''' <summary>
    ''' Get a parameters array for the current bit bang SPI configuration
    ''' </summary>
    ''' <returns>The parameter array to send to the FX3 for a bit bang vendor command</returns>
    Public Function GetParameterArray() As Byte()
        Dim params As New List(Of Byte)
        params.Add(CByte(SCLK.pinConfig() And &HFFUI)) '0
        params.Add(CByte(CS.pinConfig() And &HFFUI)) '1
        params.Add(CByte(MOSI.pinConfig() And &HFFUI)) '2
        params.Add(CByte(MISO.pinConfig() And &HFFUI)) '3
        params.Add(CByte(SCLKHalfPeriodTicks And &HFFUI)) '4
        params.Add(CByte((SCLKHalfPeriodTicks And &HFF00UI) >> 8)) '5
        params.Add(CByte((SCLKHalfPeriodTicks And &HFF0000UI) >> 16)) '6
        params.Add(CByte((SCLKHalfPeriodTicks And &HFF000000UI) >> 24)) '7
        params.Add(CByte(CSLeadTicks And &HFFUI)) '8
        params.Add(CByte((CSLeadTicks And &HFF00UI) >> 8)) '9
        params.Add(CByte(CSLagTicks And &HFFUI)) '10
        params.Add(CByte((CSLagTicks And &HFF00UI) >> 8)) '11
        params.Add(CByte(StallTicks And &HFFUI)) '12
        params.Add(CByte((StallTicks And &HFF00UI) >> 8)) '13
        params.Add(CByte((StallTicks And &HFF0000UI) >> 16)) '14
        params.Add(CByte((StallTicks And &HFF000000UI) >> 24)) '15
        params.Add(CByte(CPHA)) '16
        params.Add(CByte(CPOL)) '17
        Return params.ToArray()
    End Function

    ''' <summary>
    ''' Kinda jank. Flag pin update required at construction so FX3 Connection can fix pin assignments.
    ''' Will only return true one time (if pin update is required)
    ''' </summary>
    ''' <returns>If GPIO re-assignment is required</returns>
    Friend ReadOnly Property UpdatePinsRequired As Boolean
        Get
            Dim retVal As Boolean = m_UpdatePins
            m_UpdatePins = False
            Return retVal
        End Get
    End Property
    Private m_UpdatePins As Boolean = False

End Class

#End Region

#Region "FX3SPIConfig Class"

''' <summary>
''' Class for all the programmable SPI configuration options on the FX3.
''' </summary>
Public Class FX3SPIConfig

    'Public Variables

    ''' <summary>
    ''' SPI word length (in bits)
    ''' </summary>
    Public WordLength As Byte

    ''' <summary>
    ''' SCLK polarity
    ''' </summary>
    Public Cpol As Boolean

    ''' <summary>
    ''' CS polarity
    ''' </summary>
    Public ChipSelectPolarity As Boolean

    ''' <summary>
    ''' Clock phase
    ''' </summary>
    Public Cpha As Boolean

    ''' <summary>
    ''' Select if SPI controller works LSB first or MSB first
    ''' </summary>
    Public IsLSBFirst As Boolean

    ''' <summary>
    ''' Chip select control mode
    ''' </summary>
    Public ChipSelectControl As SpiChipselectControl

    ''' <summary>
    ''' Chip select lead delay mode
    ''' </summary>
    Public ChipSelectLeadTime As SpiLagLeadTime

    ''' <summary>
    ''' Chip select lag delay mode
    ''' </summary>
    Public ChipSelectLagTime As SpiLagLeadTime

    ''' <summary>
    ''' Connected DUT type
    ''' </summary>
    Public DUTType As DUTType

    ''' <summary>
    ''' Enable/Disable data ready interrupt triggering for SPI
    ''' </summary>
    Public DrActive As Boolean

    ''' <summary>
    ''' Data ready polarity for interrupt triggering (posedge or negedge)
    ''' </summary>
    Public DrPolarity As Boolean

    ''' <summary>
    ''' Scale factor to convert seconds to timer ticks)
    ''' </summary>
    Public SecondsToTimerTicks As UInteger

    'Private variables for general use
    Private SclkPeriod As Double
    Private StallSeconds As Double
    Private ClockFrequency As Integer

    'Private member variable to store the current data ready pin GPIO number
    Private m_ReadyPinGPIO As UInteger

    'Private member variable to store the stall time
    Private m_StallTime As UShort = 50

    'Private member variable to store the stall cycles
    Private m_StallCycles As UShort = 100

    'Private member variable to store the current data ready pin
    Private m_ReadyPin As FX3PinObject

    ''' <summary>
    ''' Property to store the current SPI clock. Updates the StallTime when set.
    ''' </summary>
    ''' <returns>The current SPI clock frequency</returns>
    Public Property SCLKFrequency As Integer
        Get
            Return ClockFrequency
        End Get
        Set(value As Integer)
            ClockFrequency = value
            SclkPeriod = (1 / ClockFrequency)
            'Update the stall cycles
            m_StallCycles = CUShort(m_StallTime / (SclkPeriod * 1000000))
        End Set
    End Property

    ''' <summary>
    ''' Property to get/set the stall time (in microseconds)
    ''' </summary>
    ''' <returns>The current stall time setting, in microseconds</returns>
    Public Property StallTime As UInt16
        Get
            Return m_StallTime
        End Get
        Set(value As UInt16)
            If (value > (UInteger.MaxValue / 10078)) Then
                Throw New FX3ConfigurationException("ERROR: Stall time of " + value.ToString() + " not supported")
            End If
            m_StallTime = value
            'Calculate the new stall cycles value based on SPI clock and update private variable
            SclkPeriod = 1 / ClockFrequency
            StallSeconds = m_StallTime / 1000000
            m_StallCycles = Convert.ToUInt16(StallSeconds / SclkPeriod)
        End Set
    End Property

    ''' <summary>
    ''' Property to set the stall time, in terms of SPI clock cycles
    ''' </summary>
    ''' <returns>The current stall cycles</returns>
    Public ReadOnly Property StallCycles As UInt16
        Get
            Return m_StallCycles
        End Get
    End Property

    ''' <summary>
    ''' Property to get/set the data ready pin
    ''' </summary>
    ''' <returns>The ready pin, as an FX3PinObject</returns>
    Public Property DataReadyPin As FX3PinObject
        Get
            Return m_ReadyPin
        End Get
        Set(value As FX3PinObject)
            m_ReadyPin = value
            m_ReadyPinGPIO = m_ReadyPin.PinNumber
        End Set
    End Property

    ''' <summary>
    ''' Property to get/set the data ready FX3 GPIO number
    ''' </summary>
    ''' <returns>The GPIO number, as a UInteger</returns>
    Public Property DataReadyPinFX3GPIO As UInteger
        Get
            Return m_ReadyPinGPIO
        End Get
        Set(value As UInteger)
            m_ReadyPinGPIO = value
            m_ReadyPin = New FX3PinObject(m_ReadyPinGPIO)
        End Set
    End Property

    ''' <summary>
    ''' Class Constructor, sets reasonable default values for IMU and ADcmXL devices
    ''' </summary>
    ''' <param name="SensorType">Optional parameter to specify default device SPI settings. Valid options are IMU and ADcmXL</param>
    Public Sub New(Optional SensorType As DeviceType = DeviceType.IMU, Optional BoardType As FX3BoardType = FX3BoardType.CypressFX3Board)
        'Set the properties true for all devices
        Cpol = True
        Cpha = True
        ChipSelectPolarity = False
        ChipSelectControl = SpiChipselectControl.SPI_SSN_CTRL_HW_END_OF_XFER
        ChipSelectLagTime = SpiLagLeadTime.SPI_SSN_LAG_LEAD_ONE_CLK
        ChipSelectLeadTime = SpiLagLeadTime.SPI_SSN_LAG_LEAD_ONE_CLK
        IsLSBFirst = False
        DrPolarity = True
        DrActive = False
        DataReadyPinFX3GPIO = 4
        If BoardType = FX3BoardType.CypressFX3Board Then
            DataReadyPinFX3GPIO = 4
        Else
            DataReadyPinFX3GPIO = 5
        End If

        If SensorType = DeviceType.ADcmXL Then
            'ADcmXL (machine health)
            m_StallTime = 15
            ClockFrequency = 14000000
            WordLength = 16
            DUTType = DUTType.ADcmXL3021
            If BoardType = FX3BoardType.CypressFX3Board Then
                DataReadyPinFX3GPIO = 3
            Else
                DataReadyPinFX3GPIO = 4
            End If
        ElseIf SensorType = DeviceType.IMU Then
            'General IMU
            ClockFrequency = 2000000
            WordLength = 16
            m_StallTime = 15
            DUTType = DUTType.IMU
        ElseIf SensorType = DeviceType.ComponentSensor Then
            'component sensor. SPI protocol must be set up later. 5MHz sclk / 5us stall should work for all
            ClockFrequency = 5000000
            m_StallTime = 5
            WordLength = 16
            DUTType = DUTType.IMU
        Else
            'Automotive IMU with iSensorAutomotiveSpi protocol
            ClockFrequency = 4000000
            WordLength = 32
            m_StallTime = 5
            DUTType = DUTType.IMU
        End If

    End Sub

End Class

#End Region

#Region "FX3APIInfo Class"

''' <summary>
''' This class provides a collection of information about the FX3 API. All the fields are hard-coded into the DLL at compile time.
''' To retrieve the FX3ApiInfo set during compile time, use the GetFX3ApiInfo call within FX3 connection.
''' </summary>
Public Class FX3ApiInfo
    ''' <summary>
    ''' The project name (should be FX3Api)
    ''' </summary>
    Public Name As String

    ''' <summary>
    ''' The project description
    ''' </summary>
    Public Description As String

    ''' <summary>
    ''' The date and time of the current FX3Api build in use.
    ''' </summary>
    Public BuildDateTime As String

    ''' <summary>
    ''' The build version of this FX3Api instance. Should match application firmware.
    ''' </summary>
    Public VersionNumber As String

    'Remote URL for the .git folder in the source
    Private m_GitURL As String

    'Link to the last commit when the project was built
    Private m_GitCommitURL As String

    'Current branch
    Private m_GitBranch As String

    'Current commit sha1 hash
    Private m_GitCommitSHA1 As String

    ''' <summary>
    ''' Constructor which initializes values to "Error: Not Set"
    ''' </summary>
    Public Sub New()
        Name = "Error: Not Set"
        Description = "Error: Not Set"
        BuildDateTime = "Error: Not Set"
        VersionNumber = "Error: Not Set"
        m_GitBranch = "Error: Not Set"
        m_GitCommitSHA1 = "Error: Not Set"
        m_GitCommitURL = "Error: Not Set"
        m_GitURL = "Error: Not Set"
    End Sub

    ''' <summary>
    ''' The base git remote URL which this version of the FX3Api was build on.
    ''' </summary>
    ''' <returns></returns>
    Public Property GitURL As String
        Get
            Return m_GitURL
        End Get
        Set(value As String)
            'Strip newline generate by piping the git output to a file
            value.Replace(Environment.NewLine, "")
            m_GitURL = value
        End Set
    End Property

    ''' <summary>
    ''' The branch which this version of the FX3Api was built on.
    ''' </summary>
    ''' <returns></returns>
    Public Property GitBranch As String
        Get
            Return m_GitBranch
        End Get
        Set(value As String)
            'Strip newline generate by piping the git output to a file
            value.Replace(Environment.NewLine, "")
            m_GitBranch = value
        End Set
    End Property

    ''' <summary>
    ''' The hast for the git commit which this version of the FX3Api was built on.
    ''' </summary>
    ''' <returns></returns>
    Public Property GitCommitSHA1 As String
        Get
            Return m_GitCommitSHA1
        End Get
        Set(value As String)
            'Strip newline generate by piping the git output to a file
            value.Replace(Environment.NewLine, "")
            m_GitCommitSHA1 = value
        End Set
    End Property

    ''' <summary>
    ''' The URL of the git commit which this version of the FX3Api was built on.
    ''' </summary>
    ''' <returns></returns>
    Public ReadOnly Property GitCommitURL As String
        Get
            Dim strippedURL As String
            Try
                strippedURL = m_GitURL
                strippedURL = strippedURL.Replace(".git", "")
                m_GitCommitURL = strippedURL + "/tree/" + m_GitCommitSHA1
            Catch ex As Exception
                m_GitCommitURL = "Error building link"
            End Try
            Return m_GitCommitURL
        End Get
    End Property

    ''' <summary>
    ''' Overload of the toString function to allow for better formatting.
    ''' </summary>
    ''' <returns>A string representing all available FX3 API information.</returns>
    Public Overrides Function ToString() As String
        Dim info As String
        info = "Project Name: " + Name + Environment.NewLine
        info = info + "Description: " + Description + Environment.NewLine
        info = info + "Version Number: " + VersionNumber + Environment.NewLine
        info = info + "Build Date and Time: " + BuildDateTime + Environment.NewLine
        info = info + "Base Git URL: " + m_GitURL
        info = info + "Current Branch: " + m_GitBranch + Environment.NewLine
        info = info + "Most Recent Commit Hash: " + m_GitCommitSHA1 + Environment.NewLine
        info = info + "Link to the commit: " + GitCommitURL
        Return info
    End Function

End Class

#End Region

#Region "PinPWMInfo Class"

''' <summary>
''' Structure which contains all the info about the PWM status of a given pin
''' </summary>
Public Class PinPWMInfo

    'Private members
    Private m_FX3GPIONumber As Integer
    Private m_FX3TimerBlock As Integer
    Private m_IdealFrequency As Double
    Private m_RealFrequency As Double
    Private m_IdealDutyCycle As Double
    Private m_RealDutyCycle As Double

    ''' <summary>
    ''' Constructor sets defaults
    ''' </summary>
    Public Sub New()
        'Initial values
        m_FX3GPIONumber = -1
        m_FX3TimerBlock = -1
        m_IdealDutyCycle = -1
        m_RealFrequency = -1
        m_IdealDutyCycle = -1
        m_RealDutyCycle = -1
    End Sub

    ''' <summary>
    ''' Overload of toString for a PinPWMInfo
    ''' </summary>
    ''' <returns>String with all pertinent data about the pin PWM</returns>
    Public Overrides Function toString() As String
        Return "Pin: " + FX3GPIONumber.ToString() + " Timer Block: " + FX3TimerBlock.ToString() + " Freq: " + IdealFrequency.ToString() + " Duty Cycle: " + IdealDutyCycle.ToString()
    End Function

    Friend Sub SetValues(Pin As IPinObject, SelectedFreq As Double, RealFreq As Double, SelectedDutyCycle As Double, RealDutyCycle As Double)
        m_FX3GPIONumber = CInt(Pin.pinConfig And &HFFUI)
        m_FX3TimerBlock = m_FX3GPIONumber Mod 8

        m_IdealFrequency = SelectedFreq
        m_RealFrequency = RealFreq
        m_IdealDutyCycle = SelectedDutyCycle
        m_RealDutyCycle = RealDutyCycle

    End Sub

    ''' <summary>
    ''' The FX3 GPIO number for the pin (0-63)
    ''' </summary>
    Public ReadOnly Property FX3GPIONumber As Integer
        Get
            Return m_FX3GPIONumber
        End Get
    End Property

    ''' <summary>
    ''' The associated complex timer block used to drive the PWM signal (0-7)
    ''' </summary>
    Public ReadOnly Property FX3TimerBlock As Integer
        Get
            Return m_FX3TimerBlock
        End Get
    End Property

    ''' <summary>
    ''' The selected frequency (in Hz)
    ''' </summary>
    Public ReadOnly Property IdealFrequency As Double
        Get
            Return m_IdealFrequency
        End Get
    End Property

    ''' <summary>
    ''' The actual frequency the PWM signal should be (in Hz).
    ''' </summary>
    Public ReadOnly Property RealFrequency As Double
        Get
            Return m_RealFrequency
        End Get
    End Property

    ''' <summary>
    ''' The selected duty cycle
    ''' </summary>
    Public ReadOnly Property IdealDutyCycle As Double
        Get
            Return m_IdealDutyCycle
        End Get
    End Property

    ''' <summary>
    ''' The actual duty cycle of the PWM pin.
    ''' </summary>
    Public ReadOnly Property RealDutyCycle As Double
        Get
            Return m_RealDutyCycle
        End Get
    End Property

End Class

#End Region

#Region "PinList Class"

''' <summary>
''' Custom list class with some extra ease of use functions added.
''' </summary>
Public Class PinList
    Inherits List(Of PinPWMInfo)

    ''' <summary>
    ''' Adds a pin to the list. Replaces any existing pin with the same FX3 GPIO number.
    ''' </summary>
    ''' <param name="Pin">The PinPWMInfo to add</param>
    Public Sub AddReplace(Pin As PinPWMInfo)
        For Each value In Me
            If value.FX3GPIONumber = Pin.FX3GPIONumber Then
                'If the pin is already in the list replace it
                Remove(value)
                Add(Pin)
                Exit Sub
            End If
        Next
        'If the pin doesn't already exist in the list add it
        Add(Pin)
    End Sub

    ''' <summary>
    ''' Gets the info for the selected pin
    ''' </summary>
    ''' <param name="Pin">The pin to get the info for, as an IPinObject</param>
    ''' <returns>The pin info, as PinPWMInfo. Will have -1 for all fields if not found</returns>
    Public Function GetPinPWMInfo(Pin As IPinObject) As PinPWMInfo
        Return GetPinPWMInfo(Pin.pinConfig)
    End Function

    ''' <summary>
    ''' Gets the info for the selected pin
    ''' </summary>
    ''' <param name="Pin">The pin to get the info for, as a UInteger (FX3 GPIO number)</param>
    ''' <returns>The pin info, as PinPWMInfo. Will have -1 for all fields if not found</returns>
    Public Function GetPinPWMInfo(Pin As UInteger) As PinPWMInfo
        For Each value In Me
            If value.FX3GPIONumber = Pin Then
                Return value
            End If
        Next
        'Return new if not in the list
        Return New PinPWMInfo()
    End Function

    ''' <summary>
    ''' Overload of contains which checks if the list contains the given Pin
    ''' </summary>
    ''' <param name="Pin">The pin to look for (As IPinObject)</param>
    ''' <returns>If the pin is contained in the list</returns>
    Public Overloads Function Contains(Pin As IPinObject) As Boolean
        Return Me.Contains(Pin.pinConfig)
    End Function

    ''' <summary>
    ''' Overload of contains which checks if the list contains the given Pin
    ''' </summary>
    ''' <param name="Pin">The pin to look for (As Integer)</param>
    ''' <returns>If the pin is contained in the list</returns>
    Public Overloads Function Contains(Pin As UInteger) As Boolean
        For Each value In Me
            If value.FX3GPIONumber = Pin Then
                Return True
            End If
        Next
        Return False
    End Function

End Class


#End Region

#Region "FX3Board Class"
''' <summary>
''' This class contains information about the connected FX3 board
''' </summary>
Public Class FX3Board

    'Private member variables
    Private m_SerialNumber As String
    Private m_bootTime As DateTime
    Private m_firmwareVersion As String
    Private m_versionNumber As String
    Private m_verboseMode As Boolean
    Private m_bootloaderVersion As String
    Private m_buildDateTime As String
    Private m_boardType As FX3BoardType

    ''' <summary>
    ''' Constructor, should be only called by FX3Connection instance
    ''' </summary>
    ''' <param name="SerialNumber">Board SN</param>
    ''' <param name="BootTime">Board boot time</param>
    Public Sub New(SerialNumber As String, BootTime As DateTime)

        'set the serial number string
        m_SerialNumber = SerialNumber

        'Set the boot time
        m_bootTime = BootTime

        'Set the verbose mode
        m_verboseMode = False

    End Sub

    'Public interfaces

    ''' <summary>
    ''' The FX3 board type (iSensor FX3 board or Cypress eval FX3 board)
    ''' </summary>
    ''' <returns></returns>
    Public ReadOnly Property BoardType As FX3BoardType
        Get
            Return m_boardType
        End Get
    End Property

    ''' <summary>
    ''' Override of the ToString function
    ''' </summary>
    ''' <returns>String with board information</returns>
    Public Overrides Function ToString() As String
        Return "Firmware Version: " + FirmwareVersion + Environment.NewLine +
            "Board Type: " + [Enum].GetName(GetType(FX3BoardType), BoardType) + Environment.NewLine +
            "Build Date and Time: " + BuildDateTime + Environment.NewLine +
            "Serial Number: " + SerialNumber + Environment.NewLine +
            "Debug Mode: " + VerboseMode.ToString() + Environment.NewLine +
            "Uptime: " + Uptime.ToString() + "ms" + Environment.NewLine +
            "Bootloader Version: " + BootloaderVersion
    End Function

    ''' <summary>
    ''' Read-only property to get the current board uptime
    ''' </summary>
    ''' <returns>The board uptime, in ms, as a long</returns>
    Public ReadOnly Property Uptime As Long
        Get
            Return CLng(DateTime.Now.Subtract(m_bootTime).TotalMilliseconds)
        End Get
    End Property

    ''' <summary>
    ''' Gets the date and time the FX3 application firmware was compiled.
    ''' </summary>
    ''' <returns>The date time string</returns>
    Public ReadOnly Property BuildDateTime As String
        Get
            Return m_buildDateTime
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get the active FX3 serial number
    ''' </summary>
    ''' <returns>The board serial number, as a string</returns>
    Public ReadOnly Property SerialNumber As String
        Get
            Return m_SerialNumber
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get the current application firmware version on the FX3
    ''' </summary>
    ''' <returns>The firmware version, as a string</returns>
    Public ReadOnly Property FirmwareVersion As String
        Get
            Return m_firmwareVersion
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get the firmware version number
    ''' </summary>
    ''' <returns></returns>
    Public ReadOnly Property FirmwareVersionNumber As String
        Get
            Return m_versionNumber
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to check if the firmware version was compiled with verbose mode enabled. When verbose mode
    ''' is enabled, much more data will be logged to the UART output. This is useful for debugging, but causes significant
    ''' performance loss for high throughput applications.
    ''' </summary>
    ''' <returns></returns>
    Public ReadOnly Property VerboseMode As Boolean
        Get
            Return m_verboseMode
        End Get
    End Property

    ''' <summary>
    ''' Get the FX3 bootloader version. This is a second stage bootloader which is stored on the I2C EEPROM
    ''' </summary>
    ''' <returns>The bootloader version, as a string</returns>
    Public ReadOnly Property BootloaderVersion As String
        Get
            Return m_bootloaderVersion
        End Get
    End Property

    ''' <summary>
    ''' Set the firmware version. Is friend so as to not be accessible to outside classes.
    ''' </summary>
    ''' <param name="FirmwareVersion">The firmware version to set, as a string</param>
    Friend Sub SetFirmwareVersion(FirmwareVersion As String)
        If IsNothing(FirmwareVersion) Or FirmwareVersion = "" Then
            Throw New FX3ConfigurationException("Error: Bad firmware version number")
        End If
        m_firmwareVersion = FirmwareVersion
        m_versionNumber = m_firmwareVersion.Substring(m_firmwareVersion.IndexOf("REV") + 4)
        m_versionNumber = m_versionNumber.Replace(" ", "")
    End Sub

    ''' <summary>
    ''' Sets if the firmware is currently running in verbose mode. Should NOT be used in verbose mode in normal operating conditions.
    ''' </summary>
    ''' <param name="isVerbose">If the board is in verbose mode or not</param>
    Friend Sub SetVerboseMode(isVerbose As Boolean)
        m_verboseMode = isVerbose
    End Sub

    ''' <summary>
    ''' Sets the bootloader version
    ''' </summary>
    ''' <param name="BootloaderVersion">The current bootloader version</param>
    Friend Sub SetBootloaderVersion(BootloaderVersion As String)
        m_bootloaderVersion = BootloaderVersion
    End Sub

    ''' <summary>
    ''' Set the Date/time string after connecting
    ''' </summary>
    ''' <param name="DateTime"></param>
    Friend Sub SetDateTime(DateTime As String)
        m_buildDateTime = DateTime
    End Sub

    Friend Sub SetBoardType(BoardType As FX3BoardType)
        m_boardType = BoardType
    End Sub


End Class

#End Region