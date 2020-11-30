'Copyright (c) 2018-2020 Analog Devices, Inc. All Rights Reserved.
'This software is proprietary to Analog Devices, Inc. and its licensors.
'
'File:          FX3PinObject.vb
'Author:        Alex Nolan (alex.nolan@analog.com), Juan Chong (juan.chong@analog.com)
'Date:          6/21/2019
'Description:   This is an implementation of the ADIS API IPinObject specific to the FX3 pin structure.

Imports AdisApi

''' <summary>
''' Object to store configuration information for a Cypress FX3 GPIO pin.
''' </summary>
<DebuggerDisplay("ToString")>
    Public Class FX3PinObject
    Implements AdisApi.IPinObject

    Private _pinNumber As UInteger = 0 ' internal storage field for pin number.  Access through PinNumber property

#Region "Constructors"
        ''' <summary>
        ''' Creates a new instance of FX3PinObject
        ''' </summary>
        Public Sub New()
        End Sub

        ''' <summary>
        ''' Creates a new instance of PinObject with the given pin Number.
        ''' </summary>
        ''' <param name="pinNumber">Number of FX3 GPIO Pin to Use</param>
        Public Sub New(pinNumber As UInteger)
            Me.PinNumber = pinNumber
        End Sub

    ''' <summary>
    ''' creates a new instance of PinObject with the given pin number and inversion.
    ''' </summary>
    ''' <param name="pinNumber">Number of FX3 GPIO Pin to Use</param>
    ''' <param name="invert"></param>
    Public Sub New(pinNumber As UInteger, invert As Boolean)
            Me.PinNumber = pinNumber
            Me.Invert = invert
        End Sub
#End Region

#Region "FX3 Specific Implementation"
        ''' <summary>
        ''' GPIO pin number for the FX3.
        ''' </summary>
        ''' <returns></returns>
        Public Property PinNumber As UInteger
            Get
                Return _pinNumber
            End Get
            Set(value As UInteger)
                If value > 63 Then Throw New ArgumentOutOfRangeException("Pin must be in the range of 0-63")
                _pinNumber = value
            End Set
        End Property
#End Region

#Region "IPin Object Implementation"
        ''' <summary>
        ''' True if pin logic is to be inverted.
        ''' </summary>
        ''' <returns></returns>
        Public Property Invert As Boolean = False Implements IPinObject.Invert

        ''' <summary>
        ''' Provides a FX3 Configuration word for the parameter array.
        ''' </summary>
        ''' <returns></returns>
        Public ReadOnly Property pinConfig As UInteger Implements IPinObject.pinConfig
        Get
            Dim cfg As UInteger = Me.PinNumber    ' cfg[7:0] is pin number
            If Me.Invert Then cfg = cfg Or &H200UI  ' cfg[ 9 ] is invert bit
            Return cfg
        End Get
    End Property

        ''' <summary>
        ''' Returns true if instances contain the same pin configuration.
        ''' </summary>
        ''' <param name="obj">Object to be compared.</param>
        ''' <returns></returns>
        Private Function IPinObject_Equals(obj As Object) As Boolean Implements IPinObject.Equals
            If IsNothing(obj) Then Return False   ' return false if object is null
            Try
                Dim p As FX3PinObject = DirectCast(obj, FX3PinObject)
                Return p.pinConfig = Me.pinConfig  ' Return true if contents equal
            Catch ex As InvalidCastException
                Return False  ' return false if obj is not if type FX3PinObject
            End Try
        End Function

        ''' <summary>
        ''' Returns a hash code
        ''' </summary>
        ''' <returns></returns>
        Private Function IPinObject_GetHashCode() As Integer Implements IPinObject.GetHashCode
        Return CInt(Me.pinConfig)  ' uint to int conversion OK, as MSB should always be zero
    End Function

        ''' <summary>
        ''' Returns a string representation of the FX3PinObject.
        ''' </summary>
        ''' <returns></returns>
        Private Function IPinObject_ToString() As String Implements IPinObject.ToString
        Return String.Format("FX3 Pin: {0}  Invert: {1}.", Me.PinNumber, Me.Invert)
    End Function
#End Region

    End Class
