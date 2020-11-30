'Copyright (c) 2018-2020 Analog Devices, Inc. All Rights Reserved.
'This software is proprietary to Analog Devices, Inc. and its licensors.
'
'File           FX3Burst.vb
'Author:        Alex Nolan (alex.nolan@analog.com), Juan Chong (juan.chong@analog.com)
'Date:          6/20/2019
'Description:   Implementation for all burst mode streaming functions in the FX3Connection class.

Imports RegMapClasses

Partial Class FX3Connection

    ''' <summary>
    ''' Gets or sets the index of the first burst data word used in CRC calculations.
    ''' </summary>
    ''' <value></value>
    ''' <returns></returns>
    ''' <remarks></remarks>
    Public Property CrcFirstIndex As Integer
        Get
            Return m_CrcFirstIndex
        End Get
        Set(value As Integer)
            m_CrcFirstIndex = value
        End Set
    End Property
    Private m_CrcFirstIndex As Integer


    ''' <summary>
    ''' Gets or sets the index of the last burst data word used in CRC calculations.
    ''' </summary>
    ''' <value></value>
    ''' <returns></returns>
    ''' <remarks></remarks>
    Public Property CrcLastIndex As Integer
        Get
            Return m_CrcLastIndex
        End Get
        Set(value As Integer)
            m_CrcLastIndex = value
        End Set
    End Property
    Private m_CrcLastIndex As Integer

    ''' <summary>
    ''' Gets or sets the index of the word that contains the CRC result.
    ''' </summary>
    ''' <value></value>
    ''' <returns></returns>
    ''' <remarks></remarks>
    Public Property CrcResultIndex As Integer
        Get
            Return m_CrcResultIndex
        End Get
        Set(value As Integer)
            m_CrcResultIndex = value
        End Set
    End Property
    Private m_CrcResultIndex As Integer

    ''' <summary>
    ''' Gets or sets the number of 16 bit words that are read during the burst. Does not include trigger, real transfer will be 2 bytes larger.
    ''' </summary>
    ''' <value></value>
    ''' <returns></returns>
    ''' <remarks></remarks>
    Public Property WordCount As Integer
        Get
            Return CInt((m_BurstByteCount - 2) / 2)
        End Get
        Set(value As Integer)
            ' Validate and that we have a valid UShort value, as we must convert to UShort for SPI object.  
            If value < 1 Or value > UShort.MaxValue Then
                Throw New ArgumentException("WordCount must be between 1 and " & UShort.MaxValue.ToString() & ".")
            End If
            '2 bytes per word, plus trigger word
            m_BurstByteCount = (value + 1) * 2
        End Set
    End Property

    ''' <summary>
    ''' Get or set the burst word length, in bytes. Is the total count of bytes transfered, including trigger
    ''' </summary>
    ''' <returns></returns>
    Public Property BurstByteCount As Integer
        Get
            Return m_BurstByteCount
        End Get
        Set(value As Integer)
            m_BurstByteCount = value
        End Set
    End Property
    Private m_BurstByteCount As Integer

    ''' <summary>
    ''' Gets or sets register that is used to trigger burst operation.
    ''' </summary>
    ''' <value></value>
    ''' <returns></returns>
    ''' <remarks></remarks>
    Public Property TriggerReg As RegClass
        Get
            Return m_TriggerReg
        End Get
        Set(value As RegClass)
            m_TriggerReg = value
            'Set up the MOSI data
            Dim burstMosi As List(Of Byte) = New List(Of Byte)
            burstMosi.Add(CByte(TriggerReg.Address And &HFFUI))
            burstMosi.Add(CByte((TriggerReg.Address And &HFF00UI) >> 8))
            m_burstMOSIData = burstMosi.ToArray()
        End Set
    End Property
    Private m_TriggerReg As RegClass

    ''' <summary>
    ''' Data to transmit on the MOSI line during a burst read operation. This value is over written
    ''' if you set the trigger reg, since trigger reg is given priority.
    ''' </summary>
    ''' <returns></returns>
    Public Property BurstMOSIData As Byte()
        Get
            Return m_burstMOSIData
        End Get
        Set(value As Byte())
            m_burstMOSIData = value
        End Set
    End Property
    Private m_burstMOSIData As Byte()

    ''' <summary>
    ''' Takes interface out of burst mode by setting BurstMode to zero.
    ''' </summary>
    ''' <remarks></remarks>
    Public Sub ClearBurstMode()
        BurstMode = 0
    End Sub

    ''' <summary>
    ''' Puts interface into burst mode by setting burstMode to match word count.
    ''' </summary>
    ''' <remarks></remarks>
    ''' <exception cref="System.InvalidOperationException">Thrown if word count has not been set.</exception>
    Public Sub SetupBurstMode()
        If WordCount = 0 Then
            Throw New InvalidOperationException("WordCount must be set before performing a burst read operation.")
        End If
        If TriggerReg Is Nothing Then
            Throw New InvalidOperationException("Trigger register must be set before performing a burst read operation.")
        End If
        BurstMode = CUShort(WordCount)
    End Sub

End Class
