'Copyright (c) 2018-2020 Analog Devices, Inc. All Rights Reserved.
'This software is proprietary to Analog Devices, Inc. and its licensors.
'
'File:          FX3Api.vb
'Author:        Alex Nolan (alex.nolan@analog.com), Juan Chong (juan.chong@analog.com)
'Date:          05/16/2019
'Description:   Main interfacing library for the Cypress FX3 based eval platform. FX3Connection implements
'               IRegInterface and IPinFcns, and can be used in place of iSensorSpi in most applications.

Imports CyUSB
Imports AdisApi
Imports System.Threading
Imports System.Collections.Concurrent
Imports System.Reflection
Imports Microsoft.VisualBasic.ApplicationServices
Imports StreamDataLogger

''' <summary>
''' This is the primary class for interfacing with the FX3 based eval platform. Implements IRegInterface, ISpi32Interface, and IPinFcns,
''' in addition to a superset of extra interfacing functions specific to the FX3 platform.
''' </summary>
Public Class FX3Connection
    Implements IRegInterface, IPinFcns, ISpi32Interface, IStreamEventProducer

#Region "Interface Variable Declaration"

    'Constant definitions

    'Friendly Name for the Cypress bootloader
    Private Const CYPRESS_BOOTLOADER_NAME As String = "Cypress FX3 USB BootLoader Device"

    'Friendly name for the ADI bootloader
    Private Const ADI_BOOTLOADER_NAME As String = "Analog Devices iSensor FX3 Bootloader"

    'Friendly name for the ADI Application Firmware
    Private Const APPLICATION_NAME As String = "Analog Devices iSensor FX3 Demonstration Platform"

    'Friendly name for flash programmer
    Private Const FLASH_PROGRAMMER_NAME As String = "Cypress FX3 USB BootProgrammer Device"

    'Timeout (in ms) for programming a board with the application firmware
    Private Const PROGRAMMING_TIMEOUT As Integer = 15000

    'Delay (in ms) in polling the cypress USB driver for new devices connected
    Private Const DEVICE_LIST_DELAY As Integer = 200

    'Maximum register list size supported (bytes)
    Private Const MAX_REGLIST_SIZE As Integer = 1000

    'Cypress driver objects

    'CyUSB Control Endpoint
    Private FX3ControlEndPt As CyControlEndPoint

    'CyUSB Bulk Endpoint for streaming real time data from FX3 to PC
    Private StreamingEndPt As CyUSBEndPoint

    'CyUSB bulk endpoint for streaming register data from FX3 to PC
    Private DataInEndPt As CyUSBEndPoint

    'CyUSB bulk endpoint for streaming register data from the PC to the FX3
    Private DataOutEndPt As CyUSBEndPoint

    'Private member variables

    'Event wait handle for when a board is reconnected running the application firmware.
    Private m_AppBoardHandle As EventWaitHandle

    'Event wait handle for when a board is reconnected running the ADI bootloader.
    Private m_BootloaderBoardHandle As EventWaitHandle

    'Track if a board is in the process of being connected
    Private m_BoardConnecting As Boolean

    'Data about the active FX3 board
    Private m_ActiveFX3Info As FX3Board

    'Thread to program the FX3 with the bootloader as needed
    Private BootloaderThread As Thread

    'Blocking queue to tell the bootloader thread a new board needs to be programmed
    Private BootloaderQueue As BlockingCollection(Of CyFX3Device)

    'Bool to track if the FX3 is currently connected
    Private m_FX3Connected As Boolean

    'SPI config struct for tracking the current FX3 configuration
    Private m_FX3SPIConfig As FX3SPIConfig

    'CyUSB object for the active FX3 board
    Private m_ActiveFX3 As CyFX3Device = Nothing

    'Serial number of the active FX3 board
    Private m_ActiveFX3SN As String = Nothing

    'Member variable to track the stream timeout time
    Private m_StreamTimeout As Integer

    'Member variable to track firmware path
    Private m_FirmwarePath As String = "PathNotSet"

    'Member variable to track bootloader path
    Private m_BlinkFirmwarePath As String = "PathNotSet"

    'Track flash programmer path
    Private m_FlashProgrammerPath As String = "PathNotSet"

    'Bootloader version
    Private m_BootloaderVersion As String = "1.0.0"

    'Data ready polarity
    Private m_DrPolarity As Boolean = True

    'Thread safe queue to store real time data frames as UShort arrays
    Private m_StreamData As ConcurrentQueue(Of UShort())

    'Thread safe queue to store transfer data for the ISpi32Interface
    Private m_TransferStreamData As ConcurrentQueue(Of UInteger())

    'Thread safe queue to store byte data received from I2C stream
    Private m_I2CStreamData As ConcurrentQueue(Of Byte())

    'Tracks the number of frames read in from DUT in real time mode
    Private m_FramesRead As Long = 0

    'Thread for pulling real time data off the DUT
    Private m_StreamThread As Thread

    'Boolean to track if the streaming thread is currently running
    Private m_StreamThreadRunning As Boolean

    'Mutex lock for the streaming endpoint
    Private m_StreamMutex As Mutex

    'Mutex for locking the control endpoint
    Private m_ControlMutex As Mutex

    'The total number of buffers to read in real time stream (AdcmXLx021 or IMU)
    Private m_TotalBuffersToRead As UInteger = 0

    'Total number of bytes to read between data ready's (when DrActive = True)
    Private m_BytesPerBulkRead As UShort

    'USB device list generated by the driver libraries
    Private m_usbList As USBDeviceList

    'Tracks the number of bad frames in a given read from the ADcmXLx021
    Private m_numBadFrames As Long

    'track the number of frame skips in a given read from the ADcmXLx021
    Private m_numFrameSkips As Long

    'Timer for tracking stream timeouts
    Private m_streamTimeoutTimer As Stopwatch

    'Tracks enable/disable pin exit post-RT capture
    Private m_pinExit As Boolean = False

    'Tracks enable/disable pin start configuration for RT capture
    Private m_pinStart As Boolean = False

    'Tracks the selected sensor type
    Private m_sensorType As DeviceType

    'Tracks how many bytes should be captured in burst mode
    Private m_burstMode As UInteger = 0

    'Global Timer for measuring disconnect event time
    Private m_disconnectTimer As Stopwatch

    'String to track the serial number of the last board which was disconnected
    Private m_disconnectedFX3SN As String

    'Track the number of boards connected after a disconnect event
    Private m_disconnectEvents As Integer

    'Store PWM config info for all PWM pins
    Private m_PinPwmInfoList As PinList

    'Store bitbang SPI config
    Private m_BitBangSpi As BitBangSpiConfig

    'Track if the trigger word read back should be included in a burst stream transaction
    Private m_StripBurstTriggerWord As Boolean

    'Track which stream type is running
    Private m_StreamType As StreamType

    'watchdog timeout period
    Private m_WatchdogTime As UShort

    'watchdog enable
    Private m_WatchdogEnable As Boolean

    'Track the supply mode
    Private m_DutSupplyMode As DutVoltage

    'track i2c bit rate setting
    Private m_i2cbitrate As UInteger

    'i2c retry count after NAK
    Private m_i2cRetryCount As UShort

    'FX3 Pin GPIO mapping
    Private RESET_PIN As UShort = 10
    Private DIO1_PIN As UShort = 3
    Private DIO2_PIN As UShort = 2
    Private DIO3_PIN As UShort = 1
    Private DIO4_PIN As UShort = 0
    Private FX3_GPIO1_PIN As UShort = 4
    Private FX3_GPIO2_PIN As UShort = 5
    Private FX3_GPIO3_PIN As UShort = 6
    Private FX3_GPIO4_PIN As UShort = 12
    Private FX3_LOOP1_PIN As UShort = 25
    Private FX3_LOOP2_PIN As UShort = 26

    'Events

    ''' <summary>
    ''' This event is raised when the active board is disconnected unexpectedly (IE unplugged)
    ''' </summary>
    ''' <param name="FX3SerialNum">Serial number of the board which was disconnected</param>
    Public Event UnexpectedDisconnect(FX3SerialNum As String)

    ''' <summary>
    ''' This event is raised when the disconnect event for a board has finished, and it is reprogrammed with the ADI bootloader. This event only is triggered for boards
    ''' which were explicitly disconnected (boards which were physically reset will not trigger this event).
    ''' </summary>
    ''' <param name="FX3SerialNum">Serial number of the board</param>
    ''' <param name="DisconnectTime">Time (in ms) elapsed between the disconnect call and board re-enumeration</param>
    Public Event DisconnectFinished(FX3SerialNum As String, DisconnectTime As Integer)

    ''' <summary>
    ''' This event is raised when there is a new buffer available from a buffered stream
    ''' </summary>
    Public Event NewBufferAvailable As IStreamEventProducer.NewBufferAvailableEventHandler Implements IStreamEventProducer.NewBufferAvailable

    ''' <summary>
    ''' This event is raised when a stream is finished
    ''' </summary>
    Public Event StreamFinished As IStreamEventProducer.StreamFinishedEventHandler Implements IStreamEventProducer.StreamFinished

#End Region

#Region "Class Constructors"

    ''' <summary>
    ''' Class Constructor. Loads SPI settings and default values for the interface, and starts a background thread to manage programming newly
    ''' connected boards with the ADI bootloader.
    ''' </summary>
    ''' <param name="FX3FirmwarePath">The path to the FX3 application firmware image file.</param>
    ''' <param name="FX3BootloaderPath">The path to the ADI FX3 bootloader image file.</param>
    ''' <param name="FX3ProgrammerPath">The path to the flash programmer application image file.</param>
    ''' <param name="SensorType">The sensor type. Valid inputs are IMU and ADcmXL. Default is IMU.</param>
    Public Sub New(FX3FirmwarePath As String, FX3BootloaderPath As String, FX3ProgrammerPath As String, Optional SensorType As DeviceType = DeviceType.IMU)

        'Store sensor type in a local variable
        m_sensorType = SensorType

        'Set the application firmware path
        FirmwarePath = FX3FirmwarePath

        'Set the ADI bootloader firmware path
        BootloaderPath = FX3BootloaderPath

        'Set the bootloader programmer path
        FlashProgrammerPath = FX3ProgrammerPath

        'Initialize default values for the interface and look for connected boards
        SetDefaultValues(m_sensorType)

        'Set the event handlers
        m_AppBoardHandle = New EventWaitHandle(False, EventResetMode.AutoReset)
        m_BootloaderBoardHandle = New EventWaitHandle(False, EventResetMode.AutoReset)

        'Start the bootloader programmer thread
        BootloaderQueue = New BlockingCollection(Of CyFX3Device)
        BootloaderThread = New Thread(AddressOf ProgramBootloaderThread)
        BootloaderThread.IsBackground = True
        BootloaderThread.Start()

        'Initialize the board list
        InitBoardList()

    End Sub

    ''' <summary>
    ''' Sets the default values for the interface. Used in constructor and after FX3 reset.
    ''' </summary>
    ''' <param name="SensorType">Parameter to specify default device SPI settings. Valid options are IMU and ADcmXL</param>
    Private Sub SetDefaultValues(SensorType As DeviceType)

        'Set the default SPI config
        m_FX3SPIConfig = New FX3SPIConfig(SensorType)

        'Set the board connection
        m_FX3Connected = False
        m_ActiveFX3 = Nothing
        m_ActiveFX3SN = Nothing
        m_ActiveFX3Info = Nothing

        'Reinitialize the thread safe queue 
        m_StreamData = New ConcurrentQueue(Of UShort())

        'Set streaming variables
        m_StreamThreadRunning = False
        m_StreamMutex = New Mutex()
        m_TotalBuffersToRead = 0
        m_numBadFrames = 0
        m_StreamTimeout = 10
        m_StreamType = StreamType.None

        'Initialize control endpoint mutex
        m_ControlMutex = New Mutex()

        'Set timer
        m_streamTimeoutTimer = New Stopwatch()

        'Set the board connecting flag
        m_BoardConnecting = False

        'Set the PWM pin list
        m_PinPwmInfoList = New PinList

        'set bit bang SPI config
        m_BitBangSpi = New BitBangSpiConfig(False)

        'strip trigger word read back by default
        m_StripBurstTriggerWord = True

        'set watchdog parameters
        m_WatchdogEnable = True
        m_WatchdogTime = 20

        'set i2c parameters (100KHz default)
        m_i2cbitrate = 100000
        m_i2cRetryCount = 1

        'Dut supply at 3.3V (should not be risky)
        m_DutSupplyMode = DutVoltage.On3_3Volts

    End Sub

#End Region

#Region "SPI Configuration"

    ''' <summary>
    ''' Property to get or set the FX3 SPI clock frequency setting.
    ''' Reqcode:   B2
    ''' Value:     Don't Care
    ''' Index:     0
    ''' Length:    4
    ''' Data:      Clock Frequency to be set
    ''' </summary>
    ''' <returns>The current SPI clock frequency, in MHZ. Valid values are in the range 1 to 40,000,000</returns>
    Public Property SclkFrequency As Int32
        Get
            Return m_FX3SPIConfig.SCLKFrequency
        End Get
        Set(value As Int32)
            'Throw an exception if the value is out of the range of frequencies supported by the board
            If IsNothing(value) Or value > 40000000 Or value < 1 Then
                Throw New FX3ConfigurationException("ERROR: Invalid Sclk Frequency entered. Must be in the range (1-40000000)")
            End If
            m_FX3SPIConfig.SCLKFrequency = value
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 0
                ConfigureSPI(m_FX3SPIConfig.SCLKFrequency)
            End If
        End Set
    End Property


    ''' <summary>
    ''' Property to get or set the FX3 SPI controller clock polarity setting (True - Idles High, False - Idles Low)
    ''' Reqcode:   B2
    ''' Value:     Polarity (0 active low, 1 active high)
    ''' 'Index:    1
    ''' Length:    4
    ''' Data:      None
    ''' </summary>
    ''' <returns>The current polarity setting</returns>
    Public Property Cpol As Boolean
        Get
            Return m_FX3SPIConfig.Cpol
        End Get
        Set(value As Boolean)
            m_FX3SPIConfig.Cpol = value
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 1
                m_ActiveFX3.ControlEndPt.Value = CUShort(m_FX3SPIConfig.Cpol)
                ConfigureSPI()
            End If
        End Set
    End Property

    ''' <summary>
    ''' Property to get or set the FX3 SPI controller chip select phase
    ''' Reqcode:   B2
    ''' Value:     Polarity (0 active low, 1 active high)
    ''' Index:     2
    ''' Length:    4
    ''' Data:      None
    ''' </summary>
    ''' <returns>The current chip select phase setting</returns>
    Public Property Cpha As Boolean
        Get
            Return m_FX3SPIConfig.Cpha
        End Get
        Set(value As Boolean)
            m_FX3SPIConfig.Cpha = value
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 2
                m_ActiveFX3.ControlEndPt.Value = CUShort(m_FX3SPIConfig.Cpha)
                ConfigureSPI()
            End If
        End Set
    End Property

    ''' <summary>
    ''' Property to get or set the FX3 SPI chip select polarity (True - Active High, False - Active Low)
    ''' Reqcode:   B2
    ''' Value:     Polarity (0 active low, 1 active high)
    ''' Index:     3
    ''' Length:    4
    ''' Data:      None
    ''' </summary>
    ''' <returns>The current chip select polarity</returns>
    Public Property ChipSelectPolarity As Boolean
        Get
            Return m_FX3SPIConfig.ChipSelectPolarity
        End Get
        Set(value As Boolean)
            m_FX3SPIConfig.ChipSelectPolarity = value
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 3
                m_ActiveFX3.ControlEndPt.Value = CUShort(m_FX3SPIConfig.ChipSelectPolarity)
                ConfigureSPI()
            End If
        End Set
    End Property

    ''' <summary>
    ''' Property to get or set the FX3 SPI controller chip select setting. Should be left on hardware control, changing modes will likely cause unexpected behavior.
    ''' Reqcode:   B2
    ''' Value:     Desired setting (as SpiChipselectControl )
    ''' Index:     4  
    ''' Length:    4
    ''' Data:      None
    ''' </summary>
    ''' <returns>The current chip select control mode</returns>
    Public Property ChipSelectControl As SpiChipselectControl
        Get
            Return m_FX3SPIConfig.ChipSelectControl
        End Get
        Set(value As SpiChipselectControl)
            m_FX3SPIConfig.ChipSelectControl = value
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 4
                m_ActiveFX3.ControlEndPt.Value = CUShort(m_FX3SPIConfig.ChipSelectControl)
                ConfigureSPI()
            End If
        End Set
    End Property

    ''' <summary>
    ''' The number of SPI clock cycles before the SPI transaction that chip select is toggled to active.
    ''' Reqcode:   B2
    ''' Value:     Desired Setting (as SpiLagLeadTime )
    ''' Index:     5 
    ''' Length:    4
    ''' Data:      None
    ''' </summary>
    ''' <returns>The current chip select lead time setting</returns>
    Public Property ChipSelectLeadTime As SpiLagLeadTime
        Get
            Return m_FX3SPIConfig.ChipSelectLeadTime
        End Get
        Set(value As SpiLagLeadTime)
            If value = SpiLagLeadTime.SPI_SSN_LAG_LEAD_ZERO_CLK Then
                Throw New FX3ConfigurationException("ERROR: Chip select lead time of 0 clocks not supported!")
            End If
            m_FX3SPIConfig.ChipSelectLeadTime = value
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 5
                m_ActiveFX3.ControlEndPt.Value = CUShort(m_FX3SPIConfig.ChipSelectLeadTime)
                ConfigureSPI()
            End If
        End Set
    End Property

    ''' <summary>
    ''' The number of SPI clock cycles after the transaction ends that chip select is toggled to idle.
    ''' Reqcode:   B2
    ''' Value:     Desired Setting (as SpiLagLeadTime )
    ''' Index:     6 
    ''' Length:    4
    ''' Data:      None
    ''' </summary>
    ''' <returns>The current chip select lag time setting</returns>
    Public Property ChipSelectLagTime As SpiLagLeadTime
        Get
            Return m_FX3SPIConfig.ChipSelectLagTime
        End Get
        Set(value As SpiLagLeadTime)
            m_FX3SPIConfig.ChipSelectLagTime = value
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 6
                m_ActiveFX3.ControlEndPt.Value = CUShort(m_FX3SPIConfig.ChipSelectLagTime)
                ConfigureSPI()
            End If
        End Set
    End Property

    ''' <summary>
    ''' The FX3 SPI Controller LSB setting. The controller flips the bits depending on this setting.
    ''' Reqcode:   B2
    ''' Value:     Polarity (0 MSB first, 1 LSB first)
    ''' Index:     7 
    ''' Length:    4
    ''' Data:      None
    ''' </summary>
    ''' <returns>The current LSB First setting, as a boolean</returns>
    Public Property IsLSBFirst As Boolean
        Get
            Return m_FX3SPIConfig.IsLSBFirst
        End Get
        Set(value As Boolean)
            m_FX3SPIConfig.IsLSBFirst = value
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 7
                m_ActiveFX3.ControlEndPt.Value = CUShort(m_FX3SPIConfig.IsLSBFirst)
                ConfigureSPI()
            End If
        End Set
    End Property

    ''' <summary>
    ''' The FX3 SPI controller word length. Default is 8 (1 byte per word)
    ''' Reqcode:   B2
    ''' Value:     Word length (as int8)
    ''' Index:     8
    ''' Length:    4
    ''' Data:      None
    ''' </summary>
    ''' <returns>The current word length</returns>
    Public Property WordLength As Byte
        Get
            Return m_FX3SPIConfig.WordLength
        End Get
        Set(value As Byte)
            If Not (value Mod 8 = 0) Then
                Throw New FX3ConfigurationException("ERROR: Word length must by a multiple of 8 bits")
            End If
            m_FX3SPIConfig.WordLength = value
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 8
                m_ActiveFX3.ControlEndPt.Value = m_FX3SPIConfig.WordLength
                ConfigureSPI()
            End If
        End Set
    End Property

    ''' <summary>
    ''' Property to get/set the number of microseconds between words
    ''' Reqcode:   B2
    ''' Value:     Stall time in microseconds (as int16)
    ''' Index:     9  
    ''' Length:    4
    ''' Data:      None
    ''' </summary>
    ''' <returns>The current stall time, in microseconds</returns>
    Public Property StallTime As UInt16
        Get
            Return m_FX3SPIConfig.StallTime
        End Get
        Set(value As UInt16)
            m_FX3SPIConfig.StallTime = value
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 9
                'Send the stall time in microseconds to the FX3 board
                m_ActiveFX3.ControlEndPt.Value = m_FX3SPIConfig.StallTime
                ConfigureSPI()
            End If
            'apply stall time to the bit bang SPI
            If Not IsNothing(m_BitBangSpi) Then
                SetBitBangStallTime(value)
            End If
        End Set
    End Property

    ''' <summary>
    ''' The DUT type connected to the board.
    ''' Reqcode:   B2
    ''' Value:     Part type to set
    ''' Index:     10 
    ''' Length:    4
    ''' Data:      None
    ''' </summary>
    ''' <returns>Returns the DUTType. Defaults to 3 axis</returns>
    Public Property PartType As DUTType
        Get
            Return m_FX3SPIConfig.DUTType
        End Get
        Set(value As DUTType)
            m_FX3SPIConfig.DUTType = value
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 10
                m_ActiveFX3.ControlEndPt.Value = CUShort(m_FX3SPIConfig.DUTType)
                ConfigureSPI()
            End If
        End Set
    End Property

    ''' <summary>
    ''' The Data Ready polarity for streaming mode (index 11)
    ''' </summary>
    ''' <returns>The data ready polarity, as a boolean (True - low to high, False - high to low)</returns>
    Public Property DrPolarity As Boolean
        Get
            Return m_FX3SPIConfig.DrPolarity
        End Get
        Set(value As Boolean)
            m_FX3SPIConfig.DrPolarity = value
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 11
                m_ActiveFX3.ControlEndPt.Value = CUShort(m_FX3SPIConfig.DrPolarity)
                ConfigureSPI()
            End If
        End Set
    End Property

    ''' <summary>
    ''' Property to get or set the DUT data ready pin.
    ''' </summary>
    ''' <returns>The IPinObject of the pin currently configured as the data ready</returns>
    Public Property ReadyPin As IPinObject
        Get
            Return m_FX3SPIConfig.DataReadyPin
        End Get
        Set(value As IPinObject)
            'throw an exception if the pin object is not an FX3PinObject
            If Not IsFX3Pin(value) Then
                Throw New FX3ConfigurationException("ERROR: FX3 Connection must take an FX3 pin object")
            End If
            m_FX3SPIConfig.DataReadyPin = CType(value, FX3PinObject)
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 13
                m_ActiveFX3.ControlEndPt.Value = CUShort(m_FX3SPIConfig.DataReadyPinFX3GPIO And &HFFFFUI)
                ConfigureSPI()
            End If
        End Set
    End Property

    ''' <summary>
    ''' Read only property to get the timer tick scale factor used for converting ticks to ms.
    ''' </summary>
    ''' <returns></returns>
    Public ReadOnly Property TimerTickScaleFactor As UInteger
        Get
            Return m_FX3SPIConfig.SecondsToTimerTicks
        End Get
    End Property

    ''' <summary>
    ''' Function to read the current SPI parameters from the FX3 board
    ''' </summary>
    ''' <returns>Returns a FX3SPIConfig struct representing the current board configuration</returns>
    Private Function GetBoardSpiParameters() As FX3SPIConfig

        'Output buffer
        Dim buf(22) As Byte

        'Variables to store output config
        Dim returnConfig As New FX3SPIConfig

        'Configure control end point for vendor command to read SPI settings
        m_ActiveFX3.ControlEndPt.Target = CyConst.TGT_DEVICE
        m_ActiveFX3.ControlEndPt.Direction = CyConst.DIR_FROM_DEVICE
        m_ActiveFX3.ControlEndPt.ReqType = CyConst.REQ_VENDOR
        m_ActiveFX3.ControlEndPt.ReqCode = CByte(USBCommands.ADI_READ_SPI_CONFIG)
        m_ActiveFX3.ControlEndPt.Value = 0
        m_ActiveFX3.ControlEndPt.Index = 0

        'Transfer data from the part
        If Not XferControlData(buf, 23, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control endpoint transfer for SPI configuration failed.")
        End If

        'Get the SPI Clock from buf 0 - 3
        returnConfig.SCLKFrequency = BitConverter.ToInt32(buf, 0)

        'Get Cpha from buf 4
        returnConfig.Cpha = CBool(buf(4))

        'Get Cpol from buf 5
        returnConfig.Cpol = CBool(buf(5))

        'Get LSB first setting from buf 6
        returnConfig.IsLSBFirst = CBool(buf(6))

        'Get CS lag time from buf 7
        returnConfig.ChipSelectLagTime = CType(buf(7), SpiLagLeadTime)

        'Get CS lead time from buf 8
        returnConfig.ChipSelectLeadTime = CType(buf(8), SpiLagLeadTime)

        'Get CS control type from buf 9
        returnConfig.ChipSelectControl = CType(buf(9), SpiChipselectControl)

        'Get CS polarity from buf 10
        returnConfig.ChipSelectPolarity = CBool(buf(10))

        'Get word length from buf 11
        returnConfig.WordLength = buf(11)

        'Get stall time from buf 12 - 13
        returnConfig.StallTime = BitConverter.ToUInt16(buf, 12)

        'Get DUT Type from buf 14
        returnConfig.DUTType = CType(buf(14), DUTType)

        'GEt data ready active setting from buf 15
        returnConfig.DrActive = CBool(buf(15))

        'Get data ready polarity setting from buf 16
        returnConfig.DrPolarity = CBool(buf(16))

        'Get data ready GPIO number from buf 17 - 18
        returnConfig.DataReadyPinFX3GPIO = BitConverter.ToUInt16(buf, 17)

        'Get timer tick scale factor from buf 19 - 22
        'Note: the timer tick setting in read-only, so there is no accompanying write function
        returnConfig.SecondsToTimerTicks = BitConverter.ToUInt32(buf, 19)

        Return returnConfig

    End Function

    ''' <summary>
    ''' Function which writes the current SPI config to the FX3
    ''' </summary>
    Private Sub WriteBoardSpiParameters()

        'Get the current FX3 config
        Dim boardConfig As FX3SPIConfig = GetBoardSpiParameters()

        'Updating each of the properties invokes their setter, which writes the values to the FX3
        If Not boardConfig.SCLKFrequency = m_FX3SPIConfig.SCLKFrequency Then
            SclkFrequency = m_FX3SPIConfig.SCLKFrequency
        End If

        If Not boardConfig.Cpha = m_FX3SPIConfig.Cpha Then
            Cpha = m_FX3SPIConfig.Cpha
        End If

        If Not boardConfig.Cpol = m_FX3SPIConfig.Cpol Then
            Cpol = m_FX3SPIConfig.Cpol
        End If

        If Not boardConfig.StallTime = m_FX3SPIConfig.StallTime Then
            StallTime = m_FX3SPIConfig.StallTime
        End If

        If Not boardConfig.ChipSelectLagTime = m_FX3SPIConfig.ChipSelectLagTime Then
            ChipSelectLagTime = m_FX3SPIConfig.ChipSelectLagTime
        End If

        If Not boardConfig.ChipSelectLeadTime = m_FX3SPIConfig.ChipSelectLeadTime Then
            ChipSelectLeadTime = m_FX3SPIConfig.ChipSelectLeadTime
        End If

        If Not boardConfig.ChipSelectControl = m_FX3SPIConfig.ChipSelectControl Then
            ChipSelectControl = m_FX3SPIConfig.ChipSelectControl
        End If

        If Not boardConfig.ChipSelectPolarity = m_FX3SPIConfig.ChipSelectPolarity Then
            ChipSelectPolarity = m_FX3SPIConfig.ChipSelectPolarity
        End If

        If Not boardConfig.DUTType = m_FX3SPIConfig.DUTType Then
            PartType = m_FX3SPIConfig.DUTType
        End If

        If Not boardConfig.IsLSBFirst = m_FX3SPIConfig.IsLSBFirst Then
            IsLSBFirst = m_FX3SPIConfig.IsLSBFirst
        End If

        If Not boardConfig.WordLength = m_FX3SPIConfig.WordLength Then
            WordLength = m_FX3SPIConfig.WordLength
        End If

        If Not boardConfig.DataReadyPinFX3GPIO = m_FX3SPIConfig.DataReadyPinFX3GPIO Then
            ReadyPin = m_FX3SPIConfig.DataReadyPin
        End If

        If Not boardConfig.DrActive = m_FX3SPIConfig.DrActive Then
            DrActive = m_FX3SPIConfig.DrActive
        End If

        If Not boardConfig.DrPolarity = m_FX3SPIConfig.DrPolarity Then
            DrPolarity = m_FX3SPIConfig.DrPolarity
        End If

        'Load the ticks to seconds scale factor from the firmware
        m_FX3SPIConfig.SecondsToTimerTicks = boardConfig.SecondsToTimerTicks

    End Sub

    ''' <summary>
    ''' Function which performs the SPI configuration option based on the current control endpoint setting
    ''' </summary>
    ''' <param name="clockFrequency">The SPI clock frequency, if it needs to be set</param>
    Private Sub ConfigureSPI(Optional clockFrequency As Integer = 0)

        'Create buffer for transfer
        Dim buf(3) As Byte

        'Exit if the board is not yet set
        If Not m_FX3Connected Then
            Exit Sub
        End If

        'Configure the control end point
        m_ActiveFX3.ControlEndPt.Target = CyConst.TGT_DEVICE
        m_ActiveFX3.ControlEndPt.Direction = CyConst.DIR_TO_DEVICE
        m_ActiveFX3.ControlEndPt.ReqType = CyConst.REQ_VENDOR
        m_ActiveFX3.ControlEndPt.ReqCode = CByte(USBCommands.ADI_SET_SPI_CONFIG)

        'Store the clock frequency in the buffer
        If Not clockFrequency = 0 Then
            buf(0) = CByte((clockFrequency >> 24) And &HFFUI)
            buf(1) = CByte((clockFrequency >> 16) And &HFFUI)
            buf(2) = CByte((clockFrequency >> 8) And &HFFUI)
            buf(3) = CByte(clockFrequency And &HFFUI)
        End If

        'Transfer data from the FX3
        If Not XferControlData(buf, 4, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control endpoint transfer for SPI configuration timed out.")
        End If

    End Sub


#End Region

#Region "FX3 Other Functions"

    'The functions in this region are not a part of the IDutInterface, and are specific to the FX3 board

    ''' <summary>
    ''' This function reads the current value from the 10MHz timer running on the FX3
    ''' </summary>
    ''' <returns>The 32-bit timer value</returns>
    Public Function GetTimerValue() As UInteger

        'status code from FX3
        Dim status As UInteger

        'Create buffer for transfer
        Dim buf(7) As Byte

        ConfigureControlEndpoint(USBCommands.ADI_READ_TIMER_VALUE, False)

        'Transfer data from the FX3
        If Not XferControlData(buf, 8, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control endpoint transfer to read timer value timed out!")
        End If

        'parse status
        status = BitConverter.ToUInt32(buf, 0)
        If status <> 0 Then
            Throw New FX3BadStatusException("ERROR: Bad status code after reading timer. Error code: 0x" + status.ToString("X4"))
        End If

        'return timer value (stored in bytes 4 - 7)
        Return BitConverter.ToUInt32(buf, 4)

    End Function

    ''' <summary>
    ''' Set the FX3 GPIO input stage pull up or pull down resistor setting. All FX3 GPIOs have a software configurable
    ''' pull up / pull down resistor (10KOhm).
    ''' </summary>
    ''' <param name="Pin">The pin to set (FX3PinObject)</param>
    ''' <param name="Setting">The pin resistor setting to apply</param>
    Public Sub SetPinResistorSetting(Pin As IPinObject, Setting As FX3PinResistorSetting)

        'status code from FX3
        Dim status As UInteger

        'Create buffer for transfer
        Dim buf(3) As Byte

        'validate pin
        If Not IsFX3Pin(Pin) Then
            Throw New FX3ConfigurationException("ERROR: FX3 pin functions must operate with FX3PinObjects")
        End If

        'configure the control endpoint
        ConfigureControlEndpoint(USBCommands.ADI_SET_PIN_RESISTOR, False)
        FX3ControlEndPt.Value = CUShort(Setting)
        FX3ControlEndPt.Index = CUShort(Pin.pinConfig And &HFFFFUI)

        'Transfer data from the FX3
        If Not XferControlData(buf, 4, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control endpoint transfer to set pin resistor configuration failed!")
        End If

        'parse status
        status = BitConverter.ToUInt32(buf, 0)
        If status <> 0 Then
            Throw New FX3BadStatusException("ERROR: Bad status code after setting pin resistor configuration. Error code: 0x" + status.ToString("X4"))
        End If

    End Sub

    ''' <summary>
    ''' Set the FX3 firmware watchdog timeout period (in seconds). If the watchdog is triggered the FX3 will reset.
    ''' </summary>
    ''' <returns></returns>
    Public Property WatchdogTimeoutSeconds As UShort
        Get
            Return m_WatchdogTime
        End Get
        Set(value As UShort)
            If value < 10 Then
                Throw New FX3ConfigurationException("ERROR: Invalid watchdog timeout period of " + value.ToString() + "s. Must be at least 10 seconds")
            End If
            If value > UShort.MaxValue Then
                Throw New FX3ConfigurationException("ERROR: Invalid watchdog timeout period " + value.ToString() + "s")
            End If
            m_WatchdogTime = value
            UpdateWatchdog()
        End Set
    End Property

    ''' <summary>
    ''' Enable or disable the FX3 firmware watchdog.
    ''' </summary>
    ''' <returns></returns>
    Public Property WatchdogEnable As Boolean
        Get
            Return m_WatchdogEnable
        End Get
        Set(value As Boolean)
            m_WatchdogEnable = value
            UpdateWatchdog()
        End Set
    End Property

    ''' <summary>
    ''' Get or set the DUT supply mode on the FX3. Available options are regulated 3.3V, USB 5V, and off. This feature is only available on the 
    ''' ADI in-house iSensor FX3 eval platform, not the platform based on the Cypress Explorer kit. If a Cypress Explorer kit is connected, the 
    ''' setter for this property will be disabled.
    ''' </summary>
    ''' <returns>The DUT supply voltage setting</returns>
    Public Property DutSupplyMode As DutVoltage
        Get
            Return m_DutSupplyMode
        End Get
        Set(value As DutVoltage)

            'Disable setting if not iSensor board
            If m_ActiveFX3Info.BoardType = FX3BoardType.CypressFX3Board Then
                Exit Property
            End If

            'set up control endpoint
            ConfigureControlEndpoint(USBCommands.ADI_SET_DUT_SUPPLY, False)
            FX3ControlEndPt.Index = 0
            FX3ControlEndPt.Value = CUShort(value)

            'Create buffer for transfer
            Dim buf(3) As Byte

            'Transfer data from the FX3
            If Not XferControlData(buf, 4, 2000) Then
                Throw New FX3CommunicationException("ERROR: Setting DUT supply mode timed out!")
            End If

            'parse status
            Dim status As UInteger = BitConverter.ToUInt32(buf, 0)

            'check that status is a success
            If status <> 0 Then
                Throw New FX3BadStatusException("ERROR: Bad status code after power supply set. Error code: 0x" + status.ToString("X4"))
            End If
            m_DutSupplyMode = value
        End Set
    End Property

    Private Sub UpdateWatchdog()

        'configure the control endpoint
        ConfigureControlEndpoint(USBCommands.ADI_SET_SPI_CONFIG, True)

        If m_WatchdogEnable Then
            'enable watchdog (index = 14)
            FX3ControlEndPt.Index = 14
            FX3ControlEndPt.Value = m_WatchdogTime
        Else
            'disable watchdog (index = 15)
            FX3ControlEndPt.Index = 15
            FX3ControlEndPt.Value = m_WatchdogTime
        End If

        'Create buffer for transfer
        Dim buf(3) As Byte

        'Transfer data from the FX3
        If Not XferControlData(buf, 4, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control endpoint transfer for watchdog configuration failed.")
        End If

    End Sub

    ''' <summary>
    ''' Get the firmware build date and time
    ''' </summary>
    ''' <returns></returns>
    Private Function GetFirmwareBuildDate() As String
        ConfigureControlEndpoint(USBCommands.ADI_GET_BUILD_DATE, False)
        Dim buf(19) As Byte

        'Transfer data from the FX3
        If Not XferControlData(buf, 20, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control endpoint transfer to get firmware build date/time failed.")
        End If

        'parse result
        Dim buildDate As String = System.Text.Encoding.UTF8.GetString(buf)

        Return buildDate

    End Function

    ''' <summary>
    ''' Set the boot unix timestamp in the FX3 application firmware
    ''' </summary>
    Private Sub SetBootTimeStamp()
        Dim time As UInteger = CUInt((Date.UtcNow - #1/1/1970#).TotalSeconds)
        Dim buf(3) As Byte
        buf(0) = CByte(time And &HFFUI)
        buf(1) = CByte((time And &HFF00UI) >> 8)
        buf(2) = CByte((time And &HFF0000UI) >> 16)
        buf(3) = CByte((time And &HFF000000UI) >> 24)

        'set up control endpoint
        ConfigureControlEndpoint(USBCommands.ADI_SET_BOOT_TIME, True)

        'Transfer data
        If Not XferControlData(buf, 4, 2000) Then
            Throw New FX3CommunicationException("ERROR: Setting FX3 boot time failed!")
        End If

    End Sub

    ''' <summary>
    ''' Gets the current status code from the FX3.
    ''' </summary>
    ''' <param name="VerboseMode">Return by reference of the verbose mode of the FX3</param>
    Private Function GetBoardStatus(ByRef VerboseMode As Boolean) As UInteger

        Dim buf(4) As Byte

        'Setup a set pin command
        ConfigureControlEndpoint(USBCommands.ADI_GET_STATUS, False)

        'Transfer data
        If Not XferControlData(buf, 5, 2000) Then
            Throw New FX3CommunicationException("ERROR: Pin set operation timed out")
        End If

        'read verbose mode
        VerboseMode = CBool(buf(4))

        'Return status code
        Return BitConverter.ToUInt32(buf, 0)

    End Function

    ''' <summary>
    ''' This property returns a class containing some useful information about the current FX3 Dll. Some of the
    ''' information is available as a attribute of the DLL, while others (build date/time and git revision) are
    ''' generated at compile time using a pre-build batch file script.
    ''' </summary>
    ''' <returns></returns>
    Public ReadOnly Property GetFX3ApiInfo As FX3ApiInfo
        Get
            Dim ApiInfo As New FX3ApiInfo
            Dim buildVersion As String
            'Get the assembly info for where the fx3 connection lives
            Dim FX3ApiDLL As Assembly = Assembly.GetAssembly(GetType(FX3Api.FX3Connection))
            Dim DllInfo As New AssemblyInfo(FX3ApiDLL)
            'Project name
            ApiInfo.Name = DllInfo.AssemblyName
            'Build version
            buildVersion = DllInfo.Version.ToString()
            'Remove last number
            buildVersion = buildVersion.Remove(buildVersion.Length - 2)
            ApiInfo.VersionNumber = buildVersion + "-PUB"
            'Project description
            ApiInfo.Description = DllInfo.Description
            'Add compile time
            ApiInfo.BuildDateTime = My.Resources.BuildDate
            'Add URL
            ApiInfo.GitURL = My.Resources.CurrentURL
            'Add git branch
            ApiInfo.GitBranch = My.Resources.CurrentBranch
            'commit sha1
            ApiInfo.GitCommitSHA1 = My.Resources.CurrentCommit
            Return ApiInfo
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get the number of bad frames purged with a call to PurgeBadFrameData. Frames are purged when the CRC appended to the end of
    ''' the frame does not match the expected CRC.
    ''' </summary>
    ''' <returns>Number of frames purged from data array</returns>
    Public ReadOnly Property NumFramesPurged As Long
        Get
            Return m_numBadFrames
        End Get
    End Property

    ''' <summary>
    ''' Property to get the number of frame skips in an ADcmXL real time stream
    ''' </summary>
    ''' <returns></returns>
    Public ReadOnly Property NumFramesSkipped As Long
        Get
            Return m_numFrameSkips
        End Get
    End Property

    ''' <summary>
    ''' Property to get the device family type the FX3 was initialized for. Setting this property restores all SPI settings to the
    ''' default for the selected device family.
    ''' </summary>
    ''' <returns>The current device mode, as an FX3Interface.DeviceType</returns>
    Public Property SensorType As DeviceType
        Get
            Return m_sensorType
        End Get
        Set(value As DeviceType)
            If m_sensorType <> value Then
                m_sensorType = value
                m_FX3SPIConfig = New FX3SPIConfig(m_sensorType, m_ActiveFX3Info.BoardType)
                WriteBoardSpiParameters()
            End If
        End Set
    End Property

    ''' <summary>
    ''' Gets and sets the sync pin exit configuration for exiting real-time stream mode on ADcmXL DUT's.
    ''' </summary>
    ''' <returns>RTS pin exit configuration (false = Pin Exit Disabled, true = Pin Exit Enabled)</returns>
    Public Property PinExit As Boolean
        Get
            Return m_pinExit
        End Get
        Set
            m_pinExit = Value
        End Set
    End Property

    ''' <summary>
    ''' Gets and sets the sync pin start configuration for starting real-time stream mode on ADcmXL DUT's.
    ''' </summary>
    ''' <returns>RTS pin start configuration (false = Pin Start Disabled, true = Pin Start Enabled</returns>
    Public Property PinStart As Boolean
        Get
            Return m_pinStart
        End Get
        Set
            m_pinStart = Value
        End Set
    End Property

    ''' <summary>
    ''' Checks if a streaming frame is available, or will be available soon in thread safe queue. If there is no data in the queue
    ''' and the streaming thread is not currently running, it will return false.
    ''' </summary>
    ''' <returns>The frame availability</returns>
    Public ReadOnly Property BufferAvailable As Boolean
        Get
            'Make sure buffer is initialized
            If IsNothing(m_StreamData) Then
                Return False
            End If
            'return if there is a value which can be read from the queue
            Return ((m_StreamData.Count > 0) Or m_StreamThreadRunning)
        End Get
    End Property

    ''' <summary>
    ''' Gets one frame from the thread safe queue. Waits to return until a frame is available if there is a stream running. If
    ''' there is not a stream running, and there is no data in the queue this call returns "Nothing".
    ''' </summary>
    ''' <returns>The frame, as a byte array</returns>
    Public Function GetBuffer() As UShort()

        'Ensure that the queue has been initialized
        If IsNothing(m_StreamData) Then
            Return Nothing
        End If

        'Return nothing if there is no data in the queue and the producer thread is idle
        If (m_StreamData.Count = 0) And (Not m_StreamThreadRunning) Then
            Return Nothing
        End If

        'Set up variables for return buffer
        Dim buffer() As UShort = Nothing
        Dim validData As Boolean = False
        m_streamTimeoutTimer.Restart()

        'Wait for a buffer to be available and dequeue
        'While (Not validData) And (m_streamTimeoutTimer.ElapsedMilliseconds < m_StreamTimeout * 1000)
        '    validData = m_StreamData.TryDequeue(buffer)
        'End While
        m_StreamData.TryDequeue(buffer)
        Return buffer
    End Function

    ''' <summary>
    ''' Read-only property to get the number of buffers read in from the DUT in buffered streaming mode
    ''' </summary>
    ''' <returns>The current buffer read count</returns>
    Public ReadOnly Property GetNumBuffersRead As Long
        Get
            'Interlocked is used to ensure atomic integer read operation
            Return Interlocked.Read(m_FramesRead)
        End Get
    End Property

#End Region

#Region "Checksum Calculations"
    ''' <summary>
    '''Expects bytes in the order they are clocked out of ADcmXLx021
    '''CRC-16-CCITT, initialized with CRC = 0xFFFF, No final XOR.
    '''Limit crc accumulation to 16 bits to prevent U32 overflow.
    ''' </summary>
    ''' <param name="ByteData">The input data set to calculate the CRC of</param>
    ''' <returns>The CRC value for the input array</returns>
    Private Function calcCCITT16(ByteData() As Byte) As UInteger
        Dim crc As UInteger = &HFFFF
        Dim poly As UInteger = &H1021
        Dim dat As UInteger
        Dim i As Integer = 0
        Dim j As Integer = 0
        Dim offset As Integer

        For i = 0 To ByteData.Count - 1 Step 2
            For offset = 1 To 0 Step -1
                dat = ByteData(i + offset)
                crc = crc Xor (dat << 8)
                For j = 1 To 8
                    If ((crc And &H8000) = &H8000) Then
                        crc = crc * 2UI
                        crc = crc Xor poly
                    Else
                        crc = crc * 2UI
                    End If
                    crc = crc And &HFFFFUI
                Next
            Next
        Next

        Return crc
    End Function

    ''' <summary>
    ''' Overload for CRC calculation which takes UShort array
    ''' </summary>
    ''' <param name="UShortData">The data to calculate CRC for</param>
    ''' <returns>The CRC value</returns>
    Private Function calcCCITT16(UShortData() As UShort) As UInteger
        'Variable initialization
        Dim crc As UInteger = &HFFFF
        Dim poly As UInteger = &H1021
        Dim dat As UInteger
        Dim i As Integer = 0
        Dim j As Integer = 0
        Dim offset As Integer

        For i = 0 To UShortData.Count - 1
            For offset = 0 To 1
                'get the data
                If offset = 1 Then
                    dat = (UShortData(i) And &HFF00UI) >> 8
                Else
                    dat = (UShortData(i) And &HFFUI)
                End If
                crc = crc Xor (dat << 8)
                For j = 1 To 8
                    If ((crc And &H8000) = &H8000) Then
                        crc = crc * 2UI
                        crc = crc Xor poly
                    Else
                        crc = crc * 2UI
                    End If
                    crc = crc And &HFFFFUI
                Next
            Next
        Next

        Return crc

    End Function

    ''' <summary>
    ''' Checks the CRC for a real time frame
    ''' </summary>
    ''' <param name="frame">The frame to check</param>
    ''' <returns>A boolean indicating if the accelerometer data CRC matches the frame CRC</returns>
    Private Function CheckDUTCRC(ByRef frame() As UShort) As Boolean
        'Read CRC from frame
        Dim DUTCRC As UShort = frame(frame.Count - 1)
        Dim temp As UShort = DUTCRC >> 8
        DUTCRC = DUTCRC << 8
        DUTCRC = DUTCRC + temp
        Dim CRCData As New List(Of UShort)
        'Calculate the CRC
        CRCData.Clear()
        If m_FX3SPIConfig.DUTType = DUTType.ADcmXL3021 Then
            For Index = 1 To frame.Count - 4
                CRCData.Add(frame(Index))
            Next
        ElseIf m_FX3SPIConfig.DUTType = DUTType.ADcmXL1021 Then
            For Index = 9 To frame.Count - 4
                CRCData.Add(frame(Index))
            Next
        Else
            Throw New FX3Exception("ERROR: Validating DUT CRC only supported for ADcmXL1021 and ADcmXL3021")
        End If

        Dim expectedCRC = calcCCITT16(CRCData.ToArray)
        Return (expectedCRC = DUTCRC)
    End Function

#End Region

End Class