'Copyright (c) 2018-2020 Analog Devices, Inc. All Rights Reserved.
'This software is proprietary to Analog Devices, Inc. and its licensors.
'
'File:          FX3Spi32.vb
'Author:        Alex Nolan (alex.nolan@analog.com), Juan Chong (juan.chong@analog.com)
'Date:          07/15/2019
'Description:   This file contains all the implementation functions for the ISpi32Interface, which allows for a protocol agnostic interface to the SPI bus.

Imports System.Collections.Concurrent
Imports System.ComponentModel
Imports System.Threading
Imports AdisApi
Imports FX3USB

Partial Class FX3Connection

    ''' <summary>
    ''' SPI data to transmit at the start of a transfer stream. No read
    ''' back performed
    ''' </summary>
    Private m_SPI32InitialMOSI As New List(Of UInteger)

    ''' <summary>
    ''' Track if initial MOSI data is transfered with DrActive
    ''' </summary>
    Private m_SPI32InitialDrActive As Boolean

    ''' <summary>
    ''' This function performs a single bi-directional 32 bit SPI transaction. If DrActive is set to false the transfer is performed asynchronously. If DrActive is set to true, 
    ''' the transfer should wait until a data ready condition (determined by DrPin and DrPolarity) is true.
    ''' </summary>
    ''' <param name="WriteData">The 32 bit data to be send to the slave on the MOSI line</param>
    ''' <returns>The 32 bit data sent to the master over the MISO line during the SPI transaction</returns>
    Public Function Transfer(WriteData As UInteger) As UInteger Implements ISpi32Interface.Transfer
        Dim readValue As UInteger
        Dim buf(7) As Byte
        Dim status As UInteger

        'Configure the control endpoint
        ConfigureControlEndpoint(USBCommands.ADI_TRANSFER_BYTES, False)

        'Set the write value
        FX3ControlEndPt.Index = CUShort(((WriteData And &HFFFF0000UI) >> 16))
        FX3ControlEndPt.Value = CUShort(WriteData And &HFFFFUI)

        'Send the vendor command
        If Not XferControlData(buf, 8, 2000) Then
            Throw New FX3CommunicationException("ERROR: Timeout during control endpoint transfer for SPI byte transfer")
        End If

        'Read back data from buffer
        readValue = BitConverter.ToUInt32(buf, 4)

        'Read back the operation status from the return buffer
        status = BitConverter.ToUInt32(buf, 0)

        If Not status = 0 Then
            Throw New FX3BadStatusException("ERROR: Bad read command - " + status.ToString("X4"))
        End If

        Return readValue
    End Function

    ''' <summary>
    ''' This function performs an array bi-directional SPI transfer. WriteData.Count() total SPI transfers are performed. If DrActive is set to true, the transfer should wait
    ''' until a data ready condition (determined by DrPin and DrPolarity) is true, and then perform all SPI transfers. If DrActive is false it is performed asynchronously.
    ''' </summary>
    ''' <param name="WriteData">The data to be written to the slave on the MOSI line in each SPI transaction. The total number of transfers performed is determined by the size of WriteData.</param>
    ''' <returns>The data received from the slave device on the MISO line, as an array</returns>
    Public Function TransferArray(WriteData As IEnumerable(Of UInteger)) As UInteger() Implements ISpi32Interface.TransferArray
        Return TransferArray(WriteData, 1, 1)
    End Function

    ''' <summary>
    ''' This function performs an array bi-directional SPI transfer. This overload transfers all the data in WriteData numCaptures times. The total
    ''' number of SPI words transfered is WriteData.Count() * numCaptures.
    ''' If DrActive is set to true, the transfer should wait until a data ready condition (determined by DrPin and DrPolarity) is true, and
    ''' then perform all SPI transfers. If DrActive is false it is performed asynchronously.
    ''' </summary>
    ''' <param name="WriteData">The data to be written to the slave on the MOSI line in each SPI transaction.</param>
    ''' <param name="numCaptures">The number of transfers of the WriteData array performed.</param>
    ''' <returns>The data received from the slave device on the MISO line, as an array. The total size is WriteData.Count() * numCaptures</returns>
    Public Function TransferArray(WriteData As IEnumerable(Of UInteger), numCaptures As UInteger) As UInteger() Implements ISpi32Interface.TransferArray
        Return TransferArray(WriteData, numCaptures, 1)
    End Function

    ''' <summary>
    ''' This function performs an array bi-directional SPI transfer. If DrActive is set to true, this overload transfers all the data in WriteData
    ''' numCaptures times per data ready condition being met. It captures data from numBuffers data ready signals. If DrActive is set to false, all the
    ''' transfers are performed asynchronously. The total number of SPI transfers is WriteData.Count()*numCaptures*numBuffers.
    ''' 
    ''' The following pseduo-code snippet would perform 400 total SPI transfers, across 100 data ready conditions. 
    ''' 
    ''' MOSI = {0x1234, 0x5678}
    ''' myISpi32.DrActive = True
    ''' MISO = myISpi32.TransferArray(MOSI, 2, 100)
    ''' 
    ''' During the transfers, the SPI bus would look like the following:
    ''' 
    ''' MOSI: ---(0x1234)---(0x5678)---(0x1234)---(0x5678)-----------------(0x1234)---(0x5678)---(0x1234)---(0x5678)--...-----(0x1234)-----(0x5678)-----(0x1234)-----(0x5678)--
    ''' MISO:----MISO(0)----MISO(1)----MISO(2)----MISO(3)------------------MISO(4)----MISO(5)----MISO(6)----MISO(7)---...-----MISO(196)----MISO(197)----MISO(198)----MISO(199)-
    ''' DR:   ___|¯¯¯|_____________________________________________________|¯¯¯|______________________________________..._____|¯¯¯|____________________________________________
    ''' </summary>
    ''' <param name="WriteData">The data to be written to the slave over the MOSI line in each SPI transaction</param>
    ''' <param name="numCaptures">The number of transfers of the WriteData array performed on each data ready (if enabled).</param>
    ''' <param name="numBuffers">The total number of data ready's to capture.</param>
    ''' <returns></returns>
    Public Function TransferArray(WriteData As IEnumerable(Of UInteger), numCaptures As UInteger, numBuffers As UInteger) As UInteger() Implements ISpi32Interface.TransferArray
        'no initial MOSI data
        m_SPI32InitialMOSI.Clear()

        'call lower level implementation
        Return TransferArrayImplementation(WriteData, numCaptures, numBuffers)
    End Function

    ''' <summary>
    ''' Transfer array low level implementation
    ''' </summary>
    ''' <param name="WriteData"></param>
    ''' <param name="numCaptures"></param>
    ''' <param name="numBuffers"></param>
    ''' <returns></returns>
    Private Function TransferArrayImplementation(WriteData As IEnumerable(Of UInteger), numCaptures As UInteger, numBuffers As UInteger) As UInteger()
        Dim MISOData As New List(Of UInteger)
        Dim BytesPerUsbBuffer As UInteger
        Dim numTransfers As UInteger
        Dim validTransfer As Boolean

        'Sends the transfer start command to the FX3
        BytesPerUsbBuffer = ISpi32TransferStreamSetup(WriteData, numCaptures, numBuffers)

        'Calculate number of transfers needed
        numTransfers = CUInt(Math.Ceiling(WriteData.Count() * 4 * numCaptures * numBuffers / BytesPerUsbBuffer))

        'Find transfer size and create data buffer
        Dim transferSize As Integer
        If m_ActiveFX3.bSuperSpeed Then
            transferSize = 1024
        ElseIf m_ActiveFX3.bHighSpeed Then
            transferSize = 512
        Else
            Throw New FX3Exception("ERROR: Streaming application requires USB 2.0 or 3.0 connection to function")
        End If

        'Buffer to hold data from the FX3
        Dim buf(transferSize - 1) As Byte

        'Acquire stream endpoint mutex
        m_StreamMutex.WaitOne()

        For transferCount As UInteger = 0 To numTransfers - 1UI
            validTransfer = USB.XferData(buf, transferSize, StreamingEndPt)
            If validTransfer Then
                For byteCount As UInteger = 0 To BytesPerUsbBuffer - 4UI Step 4
                    MISOData.Add(BitConverter.ToUInt32(buf, CInt(byteCount)))
                    If MISOData.Count >= WriteData.Count() * numBuffers * numCaptures Then
                        Exit For
                    End If
                Next
            End If
        Next

        'Release stream mutex
        m_StreamMutex.ReleaseMutex()

        'Send stream done command to FX3
        ISpi32TransferStreamDone()

        Return MISOData.ToArray()
    End Function

    ''' <summary>
    ''' Array transfer which performs an initial SPI transmit (MOSI only, no read back) then
    ''' starts a read stream. This is useful if you need to issue a write to the DUT, then 
    ''' immediately start reading back data, without the USB transfer overhead of approx 150us
    ''' </summary>
    ''' <param name="InitialMOSIData">MOSI data to transmit before read (SPI words)</param>
    ''' <param name="InitialDrActive">If the initial transmission is DR active or not</param>
    ''' <param name="MOSIReadData">Data sent over the MOSI line during the transfer</param>
    ''' <param name="NumBuffers">Number of buffers (samples) to read</param>
    ''' <returns>MISO data from the DUT during the whole transfer</returns>
    Public Function WriteReadTransferArray(InitialMOSIData As IEnumerable(Of UInteger), InitialDrActive As Boolean, MOSIReadData As IEnumerable(Of UInteger), NumBuffers As UInteger) As UInteger()
        'set up MOSI array
        m_SPI32InitialMOSI.Clear()
        If Not IsNothing(InitialMOSIData) Then
            m_SPI32InitialMOSI.AddRange(InitialMOSIData)
        End If
        m_SPI32InitialDrActive = InitialDrActive
        'return lower level implementation
        Return TransferArrayImplementation(MOSIReadData, 1, NumBuffers)
    End Function

    ''' <summary>
    ''' This is similar to the most general streaming function used by the IRegInterface, and all other buffered streaming functions can be derived from 
    ''' it with a little glue logic. When a stream is started, a second thread should be started to pull buffers from the interfacing board asynchronously.
    ''' Each buffer will consist of WriteData.count() * numCaptures 32-bit words, which are just the raw data read back from the DUT over the MISO line. 
    ''' The stream is expected to produce numBuffers total buffers.
    ''' </summary>
    ''' <param name="WriteData">The data to send over the MOSI line</param>
    ''' <param name="numCaptures">The number of iterations of the WriteData array to perform in a single buffer</param>
    ''' <param name="numBuffers">The total number of buffers to capture</param>
    ''' <param name="timeoutSeconds">The time to wait on the interfacing board before stopping the stream</param>
    ''' <param name="worker">A background worker used to notify the caller of progress made in the stream. You MUST check that this parameter has been initialized</param>
    Private Sub ISpi32Interface_StartBufferedStream(WriteData As IEnumerable(Of UInteger), numCaptures As UInteger, numBuffers As UInteger, timeoutSeconds As Integer, worker As BackgroundWorker) Implements ISpi32Interface.StartBufferedStream

        Dim BytesPerUsbBuffer As UInteger
        Dim streamArgs As New List(Of UInteger)

        'ensure there is no initial transmit data
        m_SPI32InitialMOSI.Clear()

        'Set the total number of buffers to read
        m_TotalBuffersToRead = numBuffers

        'Setup the stream
        BytesPerUsbBuffer = ISpi32TransferStreamSetup(WriteData, numCaptures, numBuffers)
        streamArgs.Add(BytesPerUsbBuffer)
        streamArgs.Add(CUInt(WriteData.Count() * 4 * numCaptures))

        'Set the stream type
        m_StreamType = StreamType.TransferStream

        'Start the streaming thread
        m_StreamThread = New Thread(AddressOf ISpi32InterfaceStreamWorker)
        m_StreamThread.Start(streamArgs)

        'If there is a background worker, monitor and generate the Report Progress and Cancel events as needed
        If IsNothing(worker) Then
            Exit Sub
        End If

        'Flag for when stream is done
        Dim streamDone As Boolean = False

        'Tracking the progress (in the range of 0 - 100)
        Dim progress, oldProgress As Integer

        'Init the progress tracking
        progress = 0
        oldProgress = 0
        While Not streamDone
            If worker.WorkerSupportsCancellation Then
                'If the worker supports cancellation then monitor the flag
                streamDone = ((GetNumBuffersRead >= numBuffers) Or worker.CancellationPending)
            Else
                streamDone = (GetNumBuffersRead >= numBuffers)
            End If

            If worker.WorkerReportsProgress Then
                progress = CInt((GetNumBuffersRead * 100) / numBuffers)
                If progress > oldProgress Then
                    worker.ReportProgress(progress)
                    oldProgress = progress
                End If
            End If

            'Sleep to avoid using too much CPU time
            Thread.Sleep(25)

        End While

        If worker.WorkerSupportsCancellation And worker.CancellationPending Then
            m_StreamThreadRunning = False
        End If

    End Sub

    ''' <summary>
    ''' ISpi32 StopStream implementation. Calls generic version.
    ''' </summary>
    Public Sub ISpi32StopStream() Implements ISpi32Interface.StopStream
        CancelStreamImplementation(USBCommands.ADI_TRANSFER_STREAM)
    End Sub

    Private Sub ISpi32TransferStreamDone()
        'Buffer to hold command data
        Dim buf(3) As Byte

        'Configure the control endpoint
        ConfigureControlEndpoint(USBCommands.ADI_TRANSFER_STREAM, True)
        m_ActiveFX3.ControlEndPt.Value = 0
        m_ActiveFX3.ControlEndPt.Index = CUShort(StreamCommands.ADI_STREAM_DONE_CMD)

        'Send command to the DUT to stop streaming data
        If Not XferControlData(buf, 4, 2000) Then
            Throw New FX3CommunicationException("ERROR: Timeout occurred when cleaning up a transfer stream thread on the FX3")
        End If

    End Sub

    Private Function ISpi32TransferStreamSetup(WriteData As IEnumerable(Of UInteger), numCaptures As UInteger, numBuffers As UInteger) As UInteger
        'Validate write data  parameters
        If IsNothing(WriteData) Then
            Throw New FX3ConfigurationException("ERROR: WriteData must not be null")
        End If

        If WriteData.Count() = 0 Then
            Throw New FX3ConfigurationException("ERROR: WriteData must contain at least one element")
        End If

        'Buffer to send config data to the FX3
        Dim buf As New List(Of Byte)
        Dim bytesPerUsbBuffer As UInteger
        Dim bytesPerDrTransfer As UInteger
        Dim transferSize As UInteger
        Dim bytesMOSIData As UShort

        'Add numCaptures
        buf.Add(CByte(numCaptures And &HFFUI))                  '0
        buf.Add(CByte((numCaptures And &HFF00UI) >> 8))         '1
        buf.Add(CByte((numCaptures And &HFF0000UI) >> 16))      '2
        buf.Add(CByte((numCaptures And &HFF000000UI) >> 24))    '3

        'Add numBuffers
        buf.Add(CByte(numBuffers And &HFFUI))                   '4
        buf.Add(CByte((numBuffers And &HFF00UI) >> 8))          '5
        buf.Add(CByte((numBuffers And &HFF0000UI) >> 16))       '6
        buf.Add(CByte((numBuffers And &HFF000000UI) >> 24))     '7

        'Calculate the number of bytes per "register" buffer (iterating through write data numcapture times)
        bytesPerDrTransfer = CUInt(WriteData.Count() * 4UI * numCaptures)

        'Get the USB transfer size
        If m_ActiveFX3.bSuperSpeed Then
            transferSize = 1024
        ElseIf m_ActiveFX3.bHighSpeed Then
            transferSize = 512
        Else
            Throw New FX3Exception("ERROR: Streaming application requires USB 2.0 or 3.0 connection to function")
        End If

        'Clamp the bytes per USB buffer at the transfer size of a single packet
        If bytesPerDrTransfer > transferSize Then
            bytesPerUsbBuffer = transferSize
        Else
            bytesPerUsbBuffer = CUInt(Math.Floor(transferSize / bytesPerDrTransfer) * bytesPerDrTransfer)
        End If

        'Add bytes per buffer
        buf.Add(CByte(bytesPerUsbBuffer And &HFFUI))                '8
        buf.Add(CByte((bytesPerUsbBuffer And &HFF00UI) >> 8))       '9
        buf.Add(CByte((bytesPerUsbBuffer And &HFF0000UI) >> 16))    '10
        buf.Add(CByte((bytesPerUsbBuffer And &HFF000000UI) >> 24))  '11

        'Add Number of bytes of write (MOSI) data
        bytesMOSIData = CUShort(WriteData.Count() * 4UI)
        buf.Add(CByte(bytesMOSIData And &HFFUI))                    '12
        buf.Add(CByte((bytesMOSIData And &HFF00UI) >> 8))           '13

        'add number of bytes of initial transmit data
        bytesMOSIData = CUShort(m_SPI32InitialMOSI.Count() * 4UI)
        buf.Add(CByte(bytesMOSIData And &HFFUI))                    '14
        buf.Add(CByte((bytesMOSIData And &HFF00UI) >> 8))           '15

        'add initial transmit data Dr active setting
        buf.Add(CByte(m_SPI32InitialDrActive))                      '16

        'Add the MOSI data (repeated per transfer)
        For Each MOSIWord In WriteData
            buf.Add(CByte(MOSIWord And &HFFUI))
            buf.Add(CByte((MOSIWord And &HFF00UI) >> 8))
            buf.Add(CByte((MOSIWord And &HFF0000UI) >> 16))
            buf.Add(CByte((MOSIWord And &HFF000000UI) >> 24))
        Next

        'Add the initial MOSI data (sent once)
        For Each MOSIWord In m_SPI32InitialMOSI
            buf.Add(CByte(MOSIWord And &HFFUI))
            buf.Add(CByte((MOSIWord And &HFF00UI) >> 8))
            buf.Add(CByte((MOSIWord And &HFF0000UI) >> 16))
            buf.Add(CByte((MOSIWord And &HFF000000UI) >> 24))
        Next

        'Check that the transmit buffer isn't too large (>4096)
        If buf.Count() > 4096 Then
            Throw New FX3ConfigurationException("ERROR: Invalid WriteData provided to StartTransferStream. Transfer count of " + buf.Count().ToString() + "bytes exceeds allowed amount of 4096 bytes")
        End If

        'Configure control endpoint
        ConfigureControlEndpoint(USBCommands.ADI_TRANSFER_STREAM, True)
        m_ActiveFX3.ControlEndPt.Index = CUShort(StreamCommands.ADI_STREAM_START_CMD)

        'Send stream start command
        If Not XferControlData(buf.ToArray(), buf.Count(), 2000) Then
            Throw New FX3CommunicationException("ERROR: Timeout occurred during control endpoint transfer for SPI transfer stream")
        End If

        'Return the bytes per buffer so it doesn't have to be calculated again
        Return bytesPerUsbBuffer

    End Function

    Private Sub ISpi32InterfaceStreamWorker(StreamArgs As Object)

        Dim BytesPerBuffer, BytesPerUsbBuffer As UInteger
        'Bool to track if transfer from FX3 board is successful
        Dim validTransfer As Boolean = True
        'Variable to track number of buffers read
        Dim numBuffersRead As Integer = 0
        'List to build output buffer in UInteger format
        Dim bufferBuilder As New List(Of UInteger)
        'Int to track buffer index
        Dim bufIndex As UInteger = 0
        'stream parameter list
        Dim StreamArgsList As List(Of UInteger) = CType(StreamArgs, List(Of UInteger))

        'Parse thread arguments
        BytesPerUsbBuffer = StreamArgsList(0)
        BytesPerBuffer = StreamArgsList(1)

        'Find transfer size and create data buffer
        Dim transferSize As Integer
        If m_ActiveFX3.bSuperSpeed Then
            transferSize = 1024
        ElseIf m_ActiveFX3.bHighSpeed Then
            transferSize = 512
        Else
            Throw New FX3Exception("ERROR: Streaming application requires USB 2.0 or 3.0 connection to function")
        End If

        'Create the 32 bit data queue
        m_TransferStreamData = New ConcurrentQueue(Of UInteger())

        'Buffer to hold data from the FX3
        Dim buf(transferSize - 1) As Byte

        'Wait for previous stream thread to exit, if any
        m_StreamThreadRunning = False

        'Wait until a lock can be acquired on the streaming end point
        m_StreamMutex.WaitOne()

        'Reset frames read counter to 0
        m_FramesRead = 0

        'Set the thread state flags
        m_StreamThreadRunning = True

        While m_StreamThreadRunning
            'Read data from FX3
            validTransfer = USB.XferData(buf, transferSize, StreamingEndPt)
            'Check that the data was read correctly
            If validTransfer Then
                For bufIndex = 0 To (BytesPerUsbBuffer - 3UI) Step 4UI
                    'Add the value at the current index position
                    bufferBuilder.Add(BitConverter.ToUInt32(buf, CInt(bufIndex)))
                    If bufferBuilder.Count() * 4 >= BytesPerBuffer Then
                        m_TransferStreamData.Enqueue(bufferBuilder.ToArray())
                        'Raise event indicating new data available
                        RaiseEvent NewBufferAvailable(m_TransferStreamData.Count())
                        Interlocked.Increment(m_FramesRead)
                        bufferBuilder.Clear()
                        numBuffersRead = numBuffersRead + 1
                        'Finish the stream if the total number of buffers has been read
                        If numBuffersRead >= m_TotalBuffersToRead Then
                            'Stop the stream
                            ISpi32TransferStreamDone()
                            'Exit the while loop
                            Exit While
                        End If
                    End If
                Next
            ElseIf m_StreamThreadRunning Then
                'Exit for a failed data transfer
                Console.WriteLine("Transfer failed during transfer stream. Error code: " + StreamingEndPt.LastError.ToString() + " (0x" + StreamingEndPt.LastError.ToString("X4") + ")")
                'send cancel
                CancelStreamImplementation(USBCommands.ADI_TRANSFER_STREAM)
                Exit While
            Else
                'exit due to stream cancel
                Exit While
            End If
        End While

        'Clean up
        ExitStreamThread()

    End Sub

    ''' <summary>
    ''' Gets a buffer from the TransferStreamData thread safe queue. Same implementation as GetBuffer.
    ''' </summary>
    ''' <returns></returns>
    Private Function ISpi32Interface_GetBufferedStreamDataPacket() As UInteger() Implements ISpi32Interface.GetBufferedStreamDataPacket
        'Ensure that the queue has been initialized
        If IsNothing(m_TransferStreamData) Then
            Return Nothing
        End If

        'Return nothing if there is no data in the queue and the producer thread is idle
        If (m_TransferStreamData.Count = 0) And (Not m_StreamThreadRunning) Then
            Return Nothing
        End If

        'Set up variables for return buffer
        Dim buffer() As UInteger = Nothing

        'Try to dequeue a buffer
        m_TransferStreamData.TryDequeue(buffer)

        Return buffer
    End Function

    ''' <summary>
    ''' Set the timeout period used for dequeuing a buffer from the thread safe queue.
    ''' </summary>
    ''' <returns></returns>
    Private Property ISpi32Interface_StreamTimeoutSeconds As Integer Implements ISpi32Interface.StreamTimeoutSeconds
        Get
            Return Me.StreamTimeoutSeconds
        End Get
        Set(value As Integer)
            Me.StreamTimeoutSeconds = value
        End Set
    End Property

    ''' <summary>
    ''' Sets of data ready triggering is used for the ISpi32Interface
    ''' </summary>
    ''' <returns></returns>
    Private Property ISpi32Interface_DrActive As Boolean Implements ISpi32Interface.DrActive
        Get
            Return Me.DrActive
        End Get
        Set(value As Boolean)
            Me.DrActive = value
        End Set
    End Property

    ''' <summary>
    ''' This property is used to get or set the data ready pin. Is tied to the ReadyPin property
    ''' </summary>
    ''' <returns></returns>
    Public Property DrPin As IPinObject Implements ISpi32Interface.DrPin
        Get
            Return ReadyPin
        End Get
        Set(value As IPinObject)
            ReadyPin = value
        End Set
    End Property

    ''' <summary>
    ''' Sets/Gets the data ready polarity for the ISpi32Interface.
    ''' </summary>
    ''' <returns></returns>
    Private Property ISpi32Interface_DrPolarity As Boolean Implements ISpi32Interface.DrPolarity
        Get
            Return DrPolarity
        End Get
        Set(value As Boolean)
            DrPolarity = value
        End Set
    End Property

End Class
