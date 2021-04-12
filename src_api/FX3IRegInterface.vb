'Copyright (c) 2018-2020 Analog Devices, Inc. All Rights Reserved.
'This software is proprietary to Analog Devices, Inc. and its licensors.
'
'File:          FX3IRegInterface.vb
'Author:        Alex Nolan (alex.nolan@analog.com), Juan Chong (juan.chong@analog.com)
'Date:          8/1/2018
'Description:   Extension of the FX3Connection class. Has all the functions needed to 
'               implement the IRegInterface interface defined in the AdisApi.

Imports System.ComponentModel
Imports AdisApi
Imports FX3USB

Partial Class FX3Connection

#Region "Properties"

    ''' <summary>
    ''' If the data ready is used for register reads
    ''' </summary>
    ''' <returns>The current data ready usage setting</returns>
    Public Property DrActive As Boolean Implements AdisApi.IRegInterface.DrActive
        Get
            Return m_FX3SPIConfig.DrActive
        End Get
        Set(value As Boolean)
            m_FX3SPIConfig.DrActive = value
            If m_FX3Connected Then
                m_ActiveFX3.ControlEndPt.Index = 12
                m_ActiveFX3.ControlEndPt.Value = CUShort(m_FX3SPIConfig.DrActive)
                ConfigureSPI()
            End If
        End Set
    End Property

    ''' <summary>
    ''' Switches burstMode on and off. Set burstMode to the number of burst read registers. 
    ''' </summary>
    ''' <returns>The number of burst read registers.</returns>
    Public Property BurstMode As UShort Implements IRegInterface.BurstMode
        Get
            Return CUShort(m_burstMode)
        End Get
        Set(value As UShort)
            m_burstMode = value
        End Set
    End Property

    ''' <summary>
    ''' Sets the timeout for the Bulk Endpoint used in real time streaming modes.
    ''' </summary>
    ''' <returns>The timeout time, in seconds</returns>
    Public Property StreamTimeoutSeconds As Integer Implements IRegInterface.StreamTimeoutSeconds
        Get
            'Initialize if not set
            If IsNothing(m_StreamTimeout) Then
                m_StreamTimeout = 3
            End If
            Return m_StreamTimeout
        End Get
        Set(value As Integer)
            If value < 1 Then
                Throw New FX3ConfigurationException("ERROR: Stream timeout " + value.ToString() + "s invalid!")
            End If
            m_StreamTimeout = value
        End Set
    End Property

#End Region

#Region "Stream Functions"

    ''' <summary>
    ''' Starts a buffered stream for only a single buffer. 
    ''' This is equivalent to StartBufferedStream(addr, numCaptures, 1, CurrentTimeout, Nothing)
    ''' </summary>
    ''' <param name="addr">The address list to read from</param>
    ''' <param name="numCaptures">The number of times to capture that address list</param>
    Public Sub StartStream(addr As IEnumerable(Of UInteger), numCaptures As UInteger) Implements IRegInterface.StartStream
        StartBufferedStream(addr, numCaptures, 1, StreamTimeoutSeconds, Nothing)
    End Sub

    ''' <summary>
    ''' Starts a buffered stream operation. The registers listed in addr are read numCaptures times per register buffer. This process is repeated numBuffers times. 
    ''' </summary>
    ''' <param name="addr">List of register addresses to read</param>
    ''' <param name="numCaptures">Number of times to read the register list per buffer.</param>
    ''' <param name="numBuffers">Number of total register buffers to read.</param>
    ''' <param name="timeoutSeconds">Stream timeout, in seconds</param>
    ''' <param name="worker">Background worker to handle progress updates</param>
    Public Sub StartBufferedStream(addr As IEnumerable(Of UInteger), numCaptures As UInteger, numBuffers As UInteger, timeoutSeconds As Integer, worker As BackgroundWorker) Implements IRegInterface.StartBufferedStream

        'Insert "Nothing" to make StartBufferedStream() happy
        Dim addrDataList As New List(Of AddrDataPair)
        For Each item In addr
            addrDataList.Add(New AddrDataPair(item, Nothing))
        Next

        StartBufferedStream(addrDataList, numCaptures, numBuffers, timeoutSeconds, worker)
    End Sub

    ''' <summary>
    ''' Starts a buffered stream operation. This is usually called from the TextFileStreamManager. DUTType must be set before executing. 
    ''' </summary>
    ''' <param name="addrData">The list of register addresses to read from, when PartType is not ADcmXLx021</param>
    ''' <param name="numCaptures">The number of reads to perform on each register listed in addr</param>
    ''' <param name="numBuffers">The total number of buffers to read. One buffer is either a frame or a set of register reads</param>
    ''' <param name="timeoutSeconds">The bulk endpoint timeout time</param>
    ''' <param name="worker">A Background worker object which can be used by a GUI to track the current stream status and send cancellation requests</param>
    Public Sub StartBufferedStream(addrData As IEnumerable(Of AddrDataPair), numCaptures As UInteger, numBuffers As UInteger, timeoutSeconds As Integer, worker As BackgroundWorker) Implements IRegInterface.StartBufferedStream

        'Check the worker status
        Dim reportProgress As Boolean = Not IsNothing(worker)
        If reportProgress Then
            reportProgress = reportProgress And worker.WorkerReportsProgress
        End If
        Dim supportsCancellation As Boolean = (Not IsNothing(worker))
        If supportsCancellation Then
            supportsCancellation = supportsCancellation And worker.WorkerSupportsCancellation
        End If

        'Set the write bit for each address that has data (likely a page write)
        For Each item In addrData
            If Not item.data Is Nothing Then
                item.addr = item.addr Or &H80UI
            End If
        Next

        'Track the progress in the current buffered stream
        Dim progress, oldProgress As Integer

        'Update the timeout property of the interface
        StreamTimeoutSeconds = timeoutSeconds

        'Throw exception if the number of buffers (reads) is invalid
        If numBuffers < 1 Then
            Throw New FX3ConfigurationException("ERROR: numBuffers must be at least one")
        End If

        'Initialize progress counters
        progress = 0
        oldProgress = 0

        'Spin up the streaming threads

        'If the DUT is set as a ADcmXLx021
        If PartType = DUTType.ADcmXL1021 Or PartType = DUTType.ADcmXL2021 Or PartType = DUTType.ADcmXL3021 Then
            'Start the streaming threads
            StartRealTimeStreaming(numBuffers)
        Else
            If BurstMode = 0 Then
                'Generic stream manager implementation for IMU, etc
                StartGenericStream(addrData, numCaptures, numBuffers)
            Else
                'Burst stream manager implementation
                StartBurstStream(numBuffers, BurstMOSIData)
            End If
        End If

        'While loop for worker events
        While (GetNumBuffersRead < numBuffers) And m_StreamThreadRunning
            'Check for cancellations
            If supportsCancellation Then
                If worker.CancellationPending Then
                    'When a cancel is received stop stream and exit
                    StopStream()
                    Exit Sub
                End If
            End If
            'Update progress every percent
            If reportProgress Then
                progress = CInt((GetNumBuffersRead * 100) / numBuffers)
                If progress > oldProgress Then
                    worker.ReportProgress(progress)
                    oldProgress = progress
                End If
            End If
            'Sleep so as to not rail the processor
            Threading.Thread.Sleep(25)
        End While

    End Sub

    ''' <summary>
    ''' Stops the currently running data stream, if any.
    ''' </summary>
    Public Sub StopStream() Implements IRegInterface.StopStream
        'depending on the stream mode, cancel as needed
        Select Case m_StreamType
            Case StreamType.BurstStream
                CancelStreamImplementation(USBCommands.ADI_STREAM_BURST_DATA)
            Case StreamType.GenericStream
                CancelStreamImplementation(USBCommands.ADI_STREAM_GENERIC_DATA)
            Case StreamType.RealTimeStream
                'don't use general cancel implementation because of pin exit condition
                StopRealTimeStreaming()
            Case StreamType.TransferStream
                CancelStreamImplementation(USBCommands.ADI_TRANSFER_STREAM)
            Case StreamType.I2CReadStream
                CancelStreamImplementation(USBCommands.ADI_I2C_READ_STREAM)
            Case Else
                'no stream running
        End Select
        'sleep for brief period to give the firmware time to cancel. This is kind of hacky (and should not be needed),
        'but want to avoid potential for some sort of conflict if a cancel is issued then immediately after a stream start
        System.Threading.Thread.Sleep(40)
    End Sub

    ''' <summary>
    ''' This function returns a single buffered stream packet. Needed for IBufferedStreamProducer
    ''' </summary>
    ''' <returns>The stream data packet, as a short</returns>
    Public Function GetBufferedStreamDataPacket() As UShort() Implements IRegInterface.GetBufferedStreamDataPacket
        'Wait for the frame (or buffer in case of IMU) and then return it
        Return GetBuffer()
    End Function

    ''' <summary>
    ''' This function does the same thing as GetBufferedStreamDataPacket()
    ''' </summary>
    ''' <returns>The last buffer read from the DUT</returns>
    Public Function GetStreamDataPacketU16() As UShort() Implements IRegInterface.GetStreamDataPacketU16
        Return GetBuffer()
    End Function

#End Region

#Region "Single Register Read/Writes"

    ''' <summary>
    ''' This is the most general ReadRegByte. Other implementations are based on this.
    ''' </summary>
    ''' <param name="addr">The address to read</param>
    ''' <returns>Returns the value read in over SPI as a short</returns>
    Public Function ReadRegByte(addr As UInteger) As UShort Implements IRegInterface.ReadRegByte

        Dim value As UShort = ReadRegWord(addr)

        'Return upper byte if even address, lower if odd
        If addr Mod 2 = 0 Then
            'Even case
            Return CUShort(value And &HFFUI)
        Else
            'Odd case
            Return (value << 8)
        End If

    End Function

    ''' <summary>
    ''' Reads a single 16 bit register on the DUT
    ''' </summary>
    ''' <param name="addr">The address of the register to read</param>
    ''' <returns>The 16 bit register value, as a UShort</returns>
    Public Function ReadRegWord(addr As UInteger) As UShort Implements IRegInterface.ReadRegWord

        'Transfer buffer
        Dim buf(5) As Byte

        'Status word
        Dim status As UInteger

        'Variables to parse value from the buffer
        Dim returnValue As UShort

        'Configure the control endpoint for a single register read
        ConfigureControlEndpoint(USBCommands.ADI_READ_BYTES, False)

        'Set the value to 0
        FX3ControlEndPt.Value = 0

        'Set the index to the register address (lower byte)
        FX3ControlEndPt.Index = CUShort(addr)

        'Transfer the data
        If Not XferControlData(buf, 6, 2000) Then
            Throw New FX3CommunicationException("ERROR: FX3 is not responding to transfer request")
        End If

        'Calculate reg value
        returnValue = BitConverter.ToUInt16(buf, 4)

        'Read back the operation status from the return buffer
        status = BitConverter.ToUInt32(buf, 0)

        If Not status = 0 Then
            Throw New FX3BadStatusException("ERROR: Bad read command - " + status.ToString("X4"))
        End If

        Return returnValue

    End Function

    ''' <summary>
    ''' This is the most general WriteRegByte, which the others are based on
    ''' </summary>
    ''' <param name="addr">The address to write to</param>
    ''' <param name="data">The byte of data to write</param>
    Public Sub WriteRegByte(addr As UInteger, data As UInteger) Implements IRegInterface.WriteRegByte

        'Transfer buffer
        Dim buf(3) As Byte

        'status message
        Dim status As UInteger

        'Configure control endpoint for a single byte register write
        ConfigureControlEndpoint(USBCommands.ADI_WRITE_BYTE, False)
        FX3ControlEndPt.Value = CUShort(data And &HFFFFUI)
        FX3ControlEndPt.Index = CUShort(addr And &HFFFFUI)

        'Transfer data
        If Not XferControlData(buf, 4, 2000) Then
            Throw New FX3CommunicationException("ERROR: WriteRegByte timed out - Check board connection")
        End If

        'Read back the operation status from the return buffer
        status = BitConverter.ToUInt32(buf, 0)
        If Not status = 0 Then
            Throw New FX3BadStatusException("ERROR: Bad write command - " + status.ToString("X4"))
        End If

    End Sub

    ''' <summary>
    ''' This function writes a single register byte, given as an Address / Data pair
    ''' </summary>
    ''' <param name="addrData">The AddrDataPair to be written</param>
    Public Sub WriteRegByte(addrData As AddrDataPair) Implements IRegInterface.WriteRegByte

        'Make a call to the hardware level WriteRegByte function with the given data
        WriteRegByte(addrData.addr, CUInt(addrData.data))

    End Sub

    ''' <summary>
    ''' This function is not currently implemented. Calling it will throw a NotImplementedException.
    ''' </summary>
    ''' <param name="addr"></param>
    ''' <param name="data"></param>
    Public Sub WriteRegWord(addr As UInteger, data As UInteger) Implements IRegInterface.WriteRegWord
        Throw New NotImplementedException()
    End Sub

#End Region

#Region "Array Register Read/Writes"

    ''' <summary>
    ''' This is the most generic array register function. All other array read/write functions call down to this one.
    ''' </summary>
    ''' <param name="addrData">The list of register addresses and optional write data for each capture</param>
    ''' <param name="numCaptures">The number of times to iterate through addrData per DUT data ready (if DrActive is set)</param>
    ''' <param name="numBuffers">The total number of buffers to read, where one buffer is considered numCaptures iterations through addrData</param>
    ''' <returns>An array of 16 bit values read back from the DUT. The size will be addrData.Count() * numCaptures * numBuffers</returns>
    Public Function ReadRegArrayStream(addrData As IEnumerable(Of AddrDataPair), numCaptures As UInteger, numBuffers As UInteger) As UShort() Implements IRegInterface.ReadRegArrayStream

        'Track endpoint transfer status
        Dim validTransfer As Boolean
        'Track number of 16 bit words to read
        Dim WordsToRead As Integer
        'List to build output buffer in USHORT format
        Dim resultBuffer As New List(Of UShort)
        'Bytes per USB buffer
        Dim bytesPerUSBBuffer As Integer
        'transfer size
        Dim transferSize As Integer
        'bytes per data ready
        Dim bytesPerDrTransfer As Integer

        'Find transfer size and create data buffer
        If m_ActiveFX3.bSuperSpeed Then
            transferSize = 1024
        ElseIf m_ActiveFX3.bHighSpeed Then
            transferSize = 512
        Else
            Throw New FX3Exception("ERROR: Streaming application requires USB 2.0 or 3.0 connection to function")
        End If

        'Buffer to hold data from the FX3
        Dim buf(transferSize - 1) As Byte

        'Calculate the number of words to read
        WordsToRead = CInt(addrData.Count() * numCaptures * numBuffers)

        'Calculate the bytes per USB buffer
        bytesPerDrTransfer = CInt(addrData.Count() * numCaptures * 2)
        If bytesPerDrTransfer > transferSize Then
            bytesPerUSBBuffer = transferSize
        Else
            bytesPerUSBBuffer = CInt(Math.Floor(transferSize / bytesPerDrTransfer) * bytesPerDrTransfer)
        End If

        'Stop any previously running stream thread
        m_StreamThreadRunning = False

        'Take the streaming endpoint mutex
        m_StreamMutex.WaitOne()

        'Setup generic stream and send start command
        GenericStreamSetup(addrData, numCaptures, numBuffers)

        'Read data back from the streaming endpoint and build the output array
        While resultBuffer.Count() < WordsToRead
            'Read data from FX3
            validTransfer = USB.XferData(buf, transferSize, StreamingEndPt)
            'Check that the data was read correctly
            If validTransfer Then
                For bufIndex As Integer = 0 To bytesPerUSBBuffer - 2 Step 2
                    'Add the 16 bit value at the current index
                    resultBuffer.Add(BitConverter.ToUInt16(buf, bufIndex))
                    'Check if we've read all the data
                    If resultBuffer.Count() = WordsToRead Then
                        Exit For
                    End If
                Next
            Else
                'Release the streaming endpoint mutex (in case exceptions are being caught, don't want to keep locking things up forever)
                m_StreamMutex.ReleaseMutex()
                'Send generic stream stop command
                CancelStreamImplementation(USBCommands.ADI_STREAM_GENERIC_DATA)
                'Throw exception
                Throw New FX3CommunicationException("ERROR: Transfer failed during register array read/write. Error code: " + StreamingEndPt.LastError.ToString() + " (0x" + StreamingEndPt.LastError.ToString("X4") + ")")
                Exit While
            End If
        End While

        'Send generic stream done command
        GenericStreamDone()

        'Release the mutex
        m_StreamMutex.ReleaseMutex()

        'Return the data read from the FX3
        Return resultBuffer.ToArray()

    End Function

    ''' <summary>
    ''' 
    ''' </summary>
    ''' <param name="addr"></param>
    ''' <param name="numCaptures"></param>
    ''' <param name="numBuffers"></param>
    ''' <returns></returns>
    Public Function ReadRegArrayStream(addr As IEnumerable(Of UInteger), numCaptures As UInteger, numBuffers As UInteger) As UShort() Implements IRegInterface.ReadRegArrayStream

        Dim addrData As New List(Of AddrDataPair)

        'Build a list of address data pairs from addr
        For Each address In addr
            addrData.Add(New AddrDataPair(address, Nothing))
        Next

        'Call the overload which takes an addrDataPair
        Return ReadRegArrayStream(addrData, numCaptures, numBuffers)

    End Function

    ''' <summary>
    ''' Overload of ReadRegArray which builds a new IEnumerable of addr and call the overload which takes an enumerable of addr
    ''' </summary>
    ''' <param name="addr">List of register address's to read</param>
    ''' <param name="numCaptures">Number of captures to perform on the register list</param>
    ''' <returns>The register values, as a short array</returns>
    Public Function ReadRegArray(addr As IEnumerable(Of UInteger), numCaptures As UInteger) As UShort() Implements IRegInterface.ReadRegArray

        'Call implementation version with numBuffers = 1
        Return ReadRegArrayStream(addr, numCaptures, 1)

    End Function

    ''' <summary>
    ''' This function writes an enumerable list of data to the DUT as AddrDataPairs
    ''' </summary>
    ''' <param name="addrData">The list of AddrDataPair to be written to DUT</param>
    Public Sub WriteRegByte(addrData As IEnumerable(Of AddrDataPair)) Implements IRegInterface.WriteRegByte

        'Calls the array overload with numCaptures = 1, numBuffers = 1. Don't read back value because it is write only
        ReadRegArrayStream(addrData, 1, 1)

    End Sub

    ''' <summary>
    ''' Overload of WriteRegByte which allows for multiple registers to be specified to write to, as an IEnumerable list of register addresses.
    ''' </summary>
    ''' <param name="addr">The list of register addresses to write to.</param>
    ''' <param name="data">The data to write to each register in the address list.</param>
    Public Sub WriteRegByte(addr As IEnumerable(Of UInteger), data As IEnumerable(Of UInteger)) Implements IRegInterface.WriteRegByte

        'Check input parameters
        If addr.Count <> data.Count() Then
            Throw New FX3ConfigurationException("ERROR: WriteRegByte must take the same number of addresses and data values")
        End If

        Dim addrData As New List(Of AddrDataPair)
        For i As Integer = 0 To addr.Count() - 1
            addrData.Add(New AddrDataPair With {.addr = addr(i), .data = data(i)})
        Next

        'Call the array overload with the address data pair list, numCaptures = 1, numBuffers = 1
        ReadRegArrayStream(addrData, 1UI, 1UI)

    End Sub

    ''' <summary>
    ''' Reads an array of 16 bit register values.
    ''' </summary>
    ''' <param name="addr">The list of registers to read</param>
    ''' <returns>The register values, as a UShort array</returns>
    Public Function ReadRegArray(addr As IEnumerable(Of UInteger)) As UShort() Implements IRegInterface.ReadRegArray

        'Call the general overload with numCaptures = 1, numBuffers = 1
        Return ReadRegArrayStream(addr, 1, 1)

    End Function

    ''' <summary>
    ''' ReadRegArray overload which includes register writes. Breaks the call into multiple calls of readRegByte and writeRegByte
    ''' </summary>
    ''' <param name="addrData">The data to read/write</param>
    ''' <param name="numCaptures">The number of times to perform the read/write operation</param>
    ''' <returns>The output data, as a UShort array</returns>
    Public Function ReadRegArray(addrData As IEnumerable(Of AddrDataPair), numCaptures As UInteger) As UShort() Implements IRegInterface.ReadRegArray

        'Call the general overload with numBuffers = 1
        Return ReadRegArrayStream(addrData, numCaptures, 1)

    End Function

#End Region

#Region "Other Functions"

    ''' <summary>
    ''' Drives the Reset pin low for 10ms, sleeps for 100ms, and then blocks until the ReadyPin is high (500ms timeout)
    ''' </summary>
    Public Sub Reset() Implements IRegInterface.Reset

        'Drives a low pulse on the reset pin for 10ms
        PulseDrive(ResetPin, 0, 10, 1)
        'Sleep for 100 ms
        System.Threading.Thread.Sleep(100)
        'Wait for ready pin to be high
        PulseWait(ReadyPin, 1, 0, 500)

    End Sub

    ''' <summary>
    ''' This function is not currently implemented. Calling it will throw a NotImplementedException.
    ''' </summary>
    Public Sub Start() Implements IRegInterface.Start
        Throw New NotImplementedException()
    End Sub

#End Region

End Class