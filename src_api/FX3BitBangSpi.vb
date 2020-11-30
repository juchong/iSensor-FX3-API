'Copyright (c) 2018-2020 Analog Devices, Inc. All Rights Reserved.
'This software is proprietary to Analog Devices, Inc. and its licensors.
'
'File:          FX3BitBangSpi.vb
'Author:        Alex Nolan (alex.nolan@analog.com)
'Date:          08/27/2019
'Description:   This file contains all the interfacing functions for the FX3 SPI bitbang capabilities.

Imports FX3USB

Partial Class FX3Connection

    ''' <summary>
    ''' Property to get or set the bit bang SPI configuration. Can select pins, timings, etc
    ''' </summary>
    ''' <returns></returns>
    Public Property BitBangSpiConfig As BitBangSpiConfig
        Get
            Return m_BitBangSpi
        End Get
        Set(value As BitBangSpiConfig)
            m_BitBangSpi = value
            If m_BitBangSpi.UpdatePinsRequired Then
                m_BitBangSpi.SCLK = CType(FX3_GPIO1, FX3PinObject)
                m_BitBangSpi.CS = CType(FX3_GPIO2, FX3PinObject)
                m_BitBangSpi.MOSI = CType(FX3_GPIO3, FX3PinObject)
                m_BitBangSpi.MISO = CType(FX3_GPIO4, FX3PinObject)
            End If
        End Set
    End Property

    ''' <summary>
    ''' Perform a bit banged SPI transfer, using the config set in BitBangSpiConfig.
    ''' </summary>
    ''' <param name="BitsPerTransfer">The total number of bits to clock in a single transfer. Can be any number greater than 0.</param>
    ''' <param name="NumTransfers">The number of separate SPI transfers to clock out</param>
    ''' <param name="MOSIData">The MOSI data to clock out. Each SPI transfer must be byte aligned. Data is clocked out MSB first</param>
    ''' <param name="TimeoutInMs">The time to wait on the bulk endpoint for a return transfer (in ms)</param>
    ''' <returns>The data received over the selected MISO line</returns>
    Public Function BitBangSpi(BitsPerTransfer As UInteger, NumTransfers As UInteger, MOSIData As Byte(), TimeoutInMs As UInteger) As Byte()
        Dim buf As New List(Of Byte)
        Dim timeoutTimer As New Stopwatch()
        Dim transferStatus As Boolean
        Dim MOSIBits As New List(Of Byte)
        Dim byteIndex As Integer
        Dim index As Integer
        Dim resultData As New List(Of Byte)

        'Validate bits per transfer
        If BitsPerTransfer = 0 Then
            Throw New FX3ConfigurationException("ERROR: Bits per transfer must be non-zero in a bit banged SPI transfer")
        End If

        'Check size (bits per transfer * numtransfers < 4080)
        If (BitsPerTransfer * NumTransfers) > 4096 Then
            Throw New FX3ConfigurationException("ERROR: Too many bits in a single bit banged SPI transaction. Max value allowed 4080")
        End If

        'check the transmit data size
        If BitsPerTransfer * NumTransfers > (MOSIData.Count() * 8) Then
            Throw New FX3ConfigurationException("ERROR: MOSI data size must meet or exceed total transfer size")
        End If

        'Return for 0 transfers
        If NumTransfers = 0 Then
            Return buf.ToArray()
        End If

        'build MOSI bit array
        index = 0
        byteIndex = 7
        For transfer As UInteger = 0 To NumTransfers - 1UI
            For bit As UInteger = 0 To BitsPerTransfer - 1UI
                MOSIBits.Add(CByte((MOSIData(index) >> byteIndex) And &H1UI))
                byteIndex -= 1
                If byteIndex < 0 Then
                    byteIndex = 7
                    index += 1
                End If
            Next
        Next

        'Build the buffer
        buf.AddRange(m_BitBangSpi.GetParameterArray())
        buf.Add(CByte(BitsPerTransfer And &HFFUI))
        buf.Add(CByte((BitsPerTransfer And &HFF00UI) >> 8))
        buf.Add(CByte((BitsPerTransfer And &HFF0000UI) >> 16))
        buf.Add(CByte((BitsPerTransfer And &HFF000000UI) >> 24))
        buf.Add(CByte(NumTransfers And &HFF))
        buf.Add(CByte((NumTransfers And &HFF00UI) >> 8))
        buf.Add(CByte((NumTransfers And &HFF0000UI) >> 16))
        buf.Add(CByte((NumTransfers And &HFF000000UI) >> 24))
        buf.AddRange(MOSIBits)

        'Send the start command
        ConfigureControlEndpoint(USBCommands.ADI_BITBANG_SPI, True)
        If Not XferControlData(buf.ToArray(), buf.Count(), 2000) Then
            Throw New FX3CommunicationException("ERROR: Control Endpoint transfer failed for SPI bit bang setup")
        End If

        'Read data back from part
        transferStatus = False
        timeoutTimer.Start()
        Dim resultBuf(CInt(BitsPerTransfer * NumTransfers - 1UI)) As Byte
        While ((Not transferStatus) And (timeoutTimer.ElapsedMilliseconds() < TimeoutInMs))
            transferStatus = USB.XferData(resultBuf, resultBuf.Count, DataInEndPt)
        End While
        timeoutTimer.Stop()

        If Not transferStatus Then
            Console.WriteLine("ERROR: Bit bang SPI transfer timed out")
        End If

        'pre-process result buffer to just be input values
        For i As Integer = 0 To resultBuf.Count - 1
            resultBuf(i) = CByte((resultBuf(i) >> 1) And &H1UI)
        Next

        'pack result buffer to byte values
        byteIndex = 7
        index = 0
        resultData.Add(0)
        For i As Integer = 0 To resultBuf.Count - 1
            resultData(index) += (resultBuf(i) << byteIndex)
            byteIndex -= 1
            If byteIndex < 0 And (i <> (resultBuf.Count - 1)) Then
                resultData.Add(0)
                byteIndex = 7
                index += 1
            End If
        Next

        Return resultData.ToArray()
    End Function

    ''' <summary>
    ''' Read a standard iSensors 16-bit register using a bitbang SPI connection
    ''' </summary>
    ''' <param name="addr">The address of the register to read (7 bit) </param>
    ''' <returns>The register value</returns>
    Public Function BitBangReadReg16(addr As UInteger) As UShort
        Dim MOSI As New List(Of Byte)
        Dim buf() As Byte
        Dim retValue, shift As UShort
        MOSI.Add(CByte(addr And &H7FUI))
        MOSI.Add(0)
        MOSI.Add(0)
        MOSI.Add(0)
        buf = BitBangSpi(16, 2, MOSI.ToArray(), CUInt(1000 * m_StreamTimeout))
        shift = buf(2)
        shift = shift << 8
        retValue = shift + buf(3)
        Return retValue
    End Function

    ''' <summary>
    ''' Write a byte to an iSensor register using a bitbang SPI connection
    ''' </summary>
    ''' <param name="addr">The address of the register to write to</param>
    ''' <param name="data">The data to write to the register</param>
    Public Sub BitBangWriteRegByte(addr As Byte, data As Byte)
        Dim MOSI As New List(Of Byte)
        'Addr first (with write bit)
        addr = CByte(addr And &H7FUI)
        addr = CByte(addr Or &H80UI)
        MOSI.Add(addr)
        MOSI.Add(data)
        'Call bitbang SPI implementation
        BitBangSpi(16, 1, MOSI.ToArray(), CUInt(1000 * m_StreamTimeout))
    End Sub

    ''' <summary>
    ''' Resets the hardware SPI pins to their default operating mode. Can be used to recover the SPI functionality after a bit-bang SPI transaction over the hardware SPI pins
    ''' without having to reboot the FX3.
    ''' </summary>
    Public Sub RestoreHardwareSpi()
        Dim buf(3) As Byte
        Dim status As UInteger
        ConfigureControlEndpoint(USBCommands.ADI_RESET_SPI, False)
        If Not XferControlData(buf, 4, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control Endpoint transfer failed for SPI hardware controller reset")
        End If
        status = BitConverter.ToUInt32(buf, 0)
        If status <> 0 Then
            Throw New FX3BadStatusException("ERROR: Invalid status received after SPI hardware controller reset: 0x" + status.ToString("X4"))
        End If
    End Sub

    ''' <summary>
    ''' Set the SCLK frequency for a bit banged SPI connection. Overloaded to allow for a UInt
    ''' </summary>
    ''' <param name="Freq">The SPI frequency, in Hz</param>
    ''' <returns>A boolean indicating if the frequency could be set.</returns>
    Public Function SetBitBangSpiFreq(Freq As UInteger) As Boolean

        Dim Freq_Dbl As Double = Convert.ToDouble(Freq)
        Return SetBitBangSpiFreq(Freq_Dbl)

    End Function

    ''' <summary>
    ''' Set the bit bang SPI stall time. Driven by a clock with resolution of 49.3ns
    ''' </summary>
    ''' <param name="MicroSecondsStall">Stall time desired, in microseconds. Minimum of 0.7us</param>
    ''' <returns>A boolean indicating if value is good or not. Defaults to closest possible value</returns>
    Public Function SetBitBangStallTime(MicroSecondsStall As Double) As Boolean
        Dim stallNs As Double

        Const NsPerTick As Double = 49.61

        Try
            'convert to nanoseconds stall
            stallNs = MicroSecondsStall * 1000
            'convert to ticks
            stallNs = stallNs / NsPerTick
            'round to uint and apply
            m_BitBangSpi.StallTicks = CUInt(stallNs)
        Catch ex As Exception
            Return False
        End Try

        Return True
    End Function

    ''' <summary>
    ''' Sets the SCLK frequency for a bit bang SPI connection. 
    ''' </summary>
    ''' <param name="Freq">The desired SPI frequency. Can go from 1.5MHz to approx 0.001Hz</param>
    ''' <returns></returns>
    Public Function SetBitBangSpiFreq(Freq As Double) As Boolean
        Const halfPeriodNsOffset As Double = 350
        Const NsPerTick As Double = 49.61
        Dim desiredPeriodNS As Double

        'Check if freq is more than max capable freq
        If Freq > (1 / (2 * halfPeriodNsOffset * 10 ^ -9)) Then
            m_BitBangSpi.SCLKHalfPeriodTicks = 0
            Return False
        End If

        desiredPeriodNS = 10 ^ 9 / Freq
        'There is a base half clock period of 350ns (atm). Each value added to that adds an additional 49ns
        desiredPeriodNS = desiredPeriodNS / 2
        desiredPeriodNS = desiredPeriodNS - halfPeriodNsOffset
        m_BitBangSpi.SCLKHalfPeriodTicks = CUInt(Math.Floor(desiredPeriodNS / NsPerTick))
        Return True
    End Function

End Class
