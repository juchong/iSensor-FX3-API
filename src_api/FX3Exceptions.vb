'File:          FX3Exceptions.vb
'Author:        Alex Nolan (alex.nolan@analog.com), Juan Chong (juan.chong@analog.com)
'Date:          6/19/2019
'Description:   This file defines the custom exceptions used by the FX3Connection.
'               These exceptions allow for greater granularity and consistency in
'               exception handling up the stack.

'The following block is an example catch chain which can be used with projects which use the FX3Interface

'Try
''Your code here
'Catch ex As FX3BadStatusException
''Handle bad status code
'Catch ex As FX3CommunicationException
''Handle communication failure
'Catch ex As FX3ConfigurationException
''Handle bad configuration value
'Catch ex As FX3ProgrammingException
''Handle failed board programming
'Catch ex As FX3Exception
''Handle general failure originating in FX3Interface
'Catch ex As Exception
''Handle all other cases
'End Try

''' <summary>
''' This exception is used when the FX3 is configured with an invalid setting. Typically, these exceptions will not cause the board or interface
''' to enter an invalid state, since they are caught before the setting is applied.
''' </summary>
Public Class FX3ConfigurationException : Inherits FX3Exception

    ''' <summary>
    ''' Create a new exception
    ''' </summary>
    Public Sub New()
        MyBase.New()
    End Sub

    ''' <summary>
    ''' Create a new exception with a specified message
    ''' </summary>
    ''' <param name="message">The message to pass with the exception</param>
    Public Sub New(message As String)
        MyBase.New(message)
    End Sub

    ''' <summary>
    ''' Create a new exception with a specified message and the previous exception from down the stack
    ''' </summary>
    ''' <param name="message">The message to pass with the exception</param>
    ''' <param name="innerException">The lower level exception to pass up</param>
    Public Sub New(message As String,  innerException As System.Exception)
        MyBase.New(message, innerException)
    End Sub
End Class

''' <summary>
''' This exception is used when there is a communication failure with the FX3 board during
''' a data transfer.
''' </summary>
Public Class FX3CommunicationException : Inherits FX3Exception

    ''' <summary>
    ''' Create a new exception
    ''' </summary>
    Public Sub New()
        MyBase.New()
    End Sub

    ''' <summary>
    ''' Create a new exception with a specified message
    ''' </summary>
    ''' <param name="message">The message to pass with the exception</param>
    Public Sub New(message As String)
        MyBase.New(message)
    End Sub

    ''' <summary>
    ''' Create a new exception with a specified message and the previous exception from down the stack
    ''' </summary>
    ''' <param name="message">The message to pass with the exception</param>
    ''' <param name="innerException">The lower level exception to pass up</param>
    Public Sub New(message As String,  innerException As System.Exception)
        MyBase.New(message, innerException)
    End Sub
End Class

''' <summary>
''' This exception is used when the status returned from the FX3 board is not success (0). This typically
''' indicates some sort of failure in the FX3 application firmware, which may require a board reset. The
''' status codes are defined in the Cypress FX3 SDK.
''' </summary>
Public Class FX3BadStatusException : Inherits FX3Exception

    ''' <summary>
    ''' Create a new exception
    ''' </summary>
    Public Sub New()
        MyBase.New()
    End Sub

    ''' <summary>
    ''' Create a new exception with a specified message
    ''' </summary>
    ''' <param name="message">The message to pass with the exception</param>
    Public Sub New(message As String)
        MyBase.New(message)
    End Sub

    ''' <summary>
    ''' Create a new exception with a specified message and the previous exception from down the stack
    ''' </summary>
    ''' <param name="message">The message to pass with the exception</param>
    ''' <param name="innerException">The lower level exception to pass up</param>
    Public Sub New(message As String,  innerException As System.Exception)
        MyBase.New(message, innerException)
    End Sub
End Class

''' <summary>
''' This exception is used when the FX3 board enumeration and programming process fails. This typically
''' indicates a flash failure at the cypress driver level, or a timeout when re-enumerating a programmed board.
''' </summary>
Public Class FX3ProgrammingException : Inherits FX3Exception

    ''' <summary>
    ''' Create a new exception
    ''' </summary>
    Public Sub New()
        MyBase.New()
    End Sub

    ''' <summary>
    ''' Create a new exception with a specified message
    ''' </summary>
    ''' <param name="message">The message to pass with the exception</param>
    Public Sub New(message As String)
        MyBase.New(message)
    End Sub

    ''' <summary>
    ''' Create a new exception with a specified message and the previous exception from down the stack
    ''' </summary>
    ''' <param name="message">The message to pass with the exception</param>
    ''' <param name="innerException">The lower level exception to pass up</param>
    Public Sub New(message As String,  innerException As System.Exception)
        MyBase.New(message, innerException)
    End Sub
End Class

''' <summary>
''' This exception is used for general faults which do not fit with the other defined exception types.
''' These exceptions are still generated within the FX3 interface, and are not system exceptions.
''' </summary>
Public Class FX3Exception : Inherits System.Exception

    ''' <summary>
    ''' Create a new exception
    ''' </summary>
    Public Sub New()
        MyBase.New()
    End Sub

    ''' <summary>
    ''' Create a new exception with a specified message
    ''' </summary>
    ''' <param name="message">The message to pass with the exception</param>
    Public Sub New(message As String)
        MyBase.New(message)
    End Sub

    ''' <summary>
    ''' Create a new exception with a specified message and the previous exception from down the stack
    ''' </summary>
    ''' <param name="message">The message to pass with the exception</param>
    ''' <param name="innerException">The lower level exception to pass up</param>
    Public Sub New(message As String,  innerException As System.Exception)
        MyBase.New(message, innerException)
    End Sub
End Class