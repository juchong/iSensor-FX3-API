'Copyright (c) 2018-2020 Analog Devices, Inc. All Rights Reserved.
'This software is proprietary to Analog Devices, Inc. and its licensors.
'
'File:         FX3ErrorLog.vb
'Author:       Alex Nolan (alex.nolan@analog.com)
'Date:         5/1/2020     
'Description:  Error logging for FX3 firmware

#Region "FX3 Error Log Class"

''' <summary>
''' FX3 flash error log class. These are generated and stored on the FX3 board
''' </summary>
Public Class FX3ErrorLog

    ''' <summary>
    ''' The FX3 firmware line which generated the error
    ''' </summary>
    Public Line As UInteger

    ''' <summary>
    ''' The FX3 firmware file which generated the error. See FileIdentifier enum in firmware docs for details
    ''' </summary>
    Public FileIdentifier As UInteger

    ''' <summary>
    ''' The FX3 boot time when the error occurred. This is a 32-bit unix timestamp
    ''' </summary>
    Public BootTimeStamp As UInteger

    ''' <summary>
    ''' The error code supplied with the error. Usually sourced from RTOS
    ''' </summary>
    Public ErrorCode As UInteger

    ''' <summary>
    ''' The firmware revision the FX3 was running when the error log was generated. Can be used with the File/Line to track down exact source of error
    ''' </summary>
    Public FirmwareRev As String

    ''' <summary>
    ''' FX3 ThreadX RTOS uptime when the error event occurred
    ''' </summary>
    Public OSUptime As UInteger

    ''' <summary>
    ''' Error log constructor
    ''' </summary>
    ''' <param name="FlashData">The 32 byte block of data read from flash which contains the error log struct</param>
    Public Sub New(FlashData As Byte())
        If FlashData.Count < 32 Then
            Throw New FX3ConfigurationException("ERROR: Flash log must be instantiated from a 32 byte array")
        End If

        'parse array
        OSUptime = BitConverter.ToUInt32(FlashData, 0)
        Line = BitConverter.ToUInt32(FlashData, 4)
        ErrorCode = BitConverter.ToUInt32(FlashData, 8)
        BootTimeStamp = BitConverter.ToUInt32(FlashData, 12)
        FileIdentifier = BitConverter.ToUInt32(FlashData, 16)
        FirmwareRev = System.Text.Encoding.UTF8.GetString(FlashData.ToList().GetRange(20, 12).ToArray())
        FirmwareRev = FirmwareRev.TrimEnd({CChar(vbNullChar)})

    End Sub

    ''' <summary>
    ''' Value-wise equality comparison
    ''' </summary>
    ''' <param name="Right">FX3ErrorLog object to compare to left for equality</param>
    ''' <param name="Left">FX3ErrorLog object to compare to right for equality</param>
    ''' <returns>True if all fields are equal, else false</returns>
    Public Shared Operator =(Right As FX3ErrorLog, Left As FX3ErrorLog) As Boolean

        If Right.BootTimeStamp <> Left.BootTimeStamp Then Return False
        If Right.ErrorCode <> Left.ErrorCode Then Return False
        If Right.FileIdentifier <> Left.FileIdentifier Then Return False
        If Right.FirmwareRev <> Left.FirmwareRev Then Return False
        If Right.Line <> Left.Line Then Return False
        If Right.OSUptime <> Left.OSUptime Then Return False

        'all fields match, return true 
        Return True
    End Operator

    Public Overloads Shared Function Equals(obj0 As Object, obj1 As Object) As Boolean

        If TypeOf (obj0) IsNot FX3ErrorLog Then Return False
        If TypeOf (obj1) IsNot FX3ErrorLog Then Return False

        Return DirectCast(obj0, FX3ErrorLog) = DirectCast(obj1, FX3ErrorLog)

    End Function

    Public Overrides Function Equals(obj As Object) As Boolean
        Return FX3ErrorLog.Equals(Me, obj)
    End Function

    ''' <summary>
    ''' Inequality operator for FX3ErrorLog object
    ''' </summary>
    ''' <param name="Right">FX3ErrorLog object to compare to left for inequality</param>
    ''' <param name="Left">FX3ErrorLog object to compare to right for inequality</param>
    ''' <returns>!(Right == Left)</returns>
    Public Shared Operator <>(Right As FX3ErrorLog, Left As FX3ErrorLog) As Boolean
        Return Not (Right = Left)
    End Operator

    Public Overrides Function toString() As String
        Return "File: " + FileIdentifier.ToString() + Environment.NewLine +
            "Line: " + Line.ToString() + Environment.NewLine +
            "Error Code: 0x" + ErrorCode.ToString("X4") + Environment.NewLine +
            "Firmware Version: " + FirmwareRev + Environment.NewLine +
            "Boot Timestamp: " + BootTimeStamp.ToString() + Environment.NewLine +
            "OS Uptime: " + OSUptime.ToString()
    End Function

End Class

#End Region

#Region "FX3 Error Log Functions"

Partial Class FX3Connection

    ''' <summary>
    ''' Read data from the FX3 non-volatile memory
    ''' </summary>
    ''' <param name="ByteAddress">The flash byte address to read from (valid range 0x0 - 0x40000)</param>
    ''' <param name="ReadLength">The number of bytes to read. Max 4096</param>
    ''' <returns>The data read from the FX3 flash memory</returns>
    Public Function ReadFlash(ByteAddress As UInteger, ReadLength As UShort) As Byte()

        'transfer buffer
        Dim buf(ReadLength - 1) As Byte

        'validate inputs
        If ByteAddress > &H40000 Then
            Throw New FX3ConfigurationException("ERROR: Invalid flash read address 0x" + ByteAddress.ToString("X4") + ". Max allowed address 0x40000")
        End If

        If ReadLength > 4096 Then
            Throw New FX3ConfigurationException("ERROR: Invalid flash read length of " + ReadLength.ToString() + "bytes. Max allowed 4096 bytes per transfer")
        End If

        'configure for flash read command
        ConfigureControlEndpoint(USBCommands.ADI_READ_FLASH, False)

        'address is passed in value/index
        FX3ControlEndPt.Value = CUShort(ByteAddress And &HFFFFUI)
        FX3ControlEndPt.Index = CUShort((ByteAddress >> 16) And &HFFFFUI)

        'send command
        If Not XferControlData(buf, ReadLength, 5000) Then
            Throw New FX3CommunicationException("ERROR: Control endpoint transfer failed for flash read")
        End If

        'return data placed in buffer
        Return buf

    End Function

    ''' <summary>
    ''' Clear the error log stored in flash
    ''' </summary>
    Public Sub ClearErrorLog()

        'transfer buffer
        Dim buf(3) As Byte

        'configure for clear flash command
        ConfigureControlEndpoint(USBCommands.ADI_CLEAR_FLASH_LOG, True)

        'send command
        If Not XferControlData(buf, 4, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control endpoint transfer failed for flash log clear!")
        End If

    End Sub

    ''' <summary>
    ''' Get the number of errors logged to the FX3 flash memory
    ''' </summary>
    ''' <returns>The error log count in flash</returns>
    Public Function GetErrorLogCount() As UInteger

        'log count address
        Const LOG_COUNT_ADDRESS As UInteger = &H34000

        Dim count As UInteger = BitConverter.ToUInt32(ReadFlash(LOG_COUNT_ADDRESS, 4), 0)

        'If no error log ever stored then flash will be cleared (0xFF)
        If count = &HFFFFFFFF Then
            count = 0
        End If

        Return count

    End Function

    ''' <summary>
    ''' Gets the current error log from FX3 flash memory
    ''' </summary>
    ''' <returns>The stored error log, as a list of FX3ErrorLog objects</returns>
    Public Function GetErrorLog() As List(Of FX3ErrorLog)

        'log count address
        Const LOG_COUNT_ADDRESS As UInteger = &H34000

        'base address in flash for the log
        Const LOG_BASE_ADDR As UInteger = &H34040

        'log storage capacity
        Const LOG_CAPACITY As UInteger = 1500

        'log to build
        Dim log As New List(Of FX3ErrorLog)

        'log raw byte data
        Dim rawData As New List(Of Byte)

        'log count
        Dim logCount As UInteger

        'bytes to read
        Dim bytesToRead As Integer

        'specific read length
        Dim readLen As UShort

        'read address
        Dim readAddress As UInteger

        'get the count from flash
        logCount = BitConverter.ToUInt32(ReadFlash(LOG_COUNT_ADDRESS, 4), 0)

        'return for empty log
        If logCount = 0 Then
            Return log
        End If

        'cap at capacity
        If logCount > LOG_CAPACITY Then
            logCount = LOG_CAPACITY
        End If

        bytesToRead = CInt(32 * logCount)
        readAddress = LOG_BASE_ADDR
        While bytesToRead > 0
            readLen = CUShort(Math.Min(4096, bytesToRead))
            rawData.AddRange(ReadFlash(readAddress, readLen))
            readAddress += readLen
            bytesToRead -= readLen
        End While

        'convert raw byte array to error log object array
        readAddress = 0
        For i As UInteger = 1 To logCount
            log.Add(New FX3ErrorLog(rawData.GetRange(CInt(readAddress), 32).ToArray()))
            readAddress += 32UI
        Next

        Return log
    End Function

End Class

#End Region
