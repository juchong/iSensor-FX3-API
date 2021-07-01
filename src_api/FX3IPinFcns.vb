'Copyright (c) 2018-2020 Analog Devices, Inc. All Rights Reserved.
'This software is proprietary to Analog Devices, Inc. and its licensors.
'
'File:          FX3IPinFcns.vb
'Author:        Alex Nolan (alex.nolan@analog.com), Juan Chong (juan.chong@analog.com)
'Date:          8/1/2018
'Description:   Extension of the FX3Connection class. Has all the functions needed to 
'               implement the IPinFcns interface defined in the AdisApi.

Imports AdisApi
Imports FX3USB

Partial Class FX3Connection

#Region "IPinFunctions Implementation"

    ''' <summary>
    ''' This function drives a pin to the specified level for a given time interval in ms
    ''' </summary>
    ''' <param name="pin">The FX3PinObject for the pin to drive</param>
    ''' <param name="polarity">The level to drive the pin to. 1 - high, 0 - low</param>
    ''' <param name="pperiod">The time to drive the pin for, in ms. Minimum of 3us.</param>
    ''' <param name="mode">Not implemented</param>
    Public Sub PulseDrive(pin As IPinObject, polarity As UInteger, pperiod As Double, mode As UInteger) Implements IPinFcns.PulseDrive

        'Send a vendor command to driv               e pin (returns immediately)
        ConfigureControlEndpoint(USBCommands.ADI_PULSE_DRIVE, True)
        Dim buf(10) As Byte
        Dim status As UInteger
        Dim ticksDouble As Double
        Dim timerTicks As UInteger
        Dim timerRollovers As UInteger

        'Validate that the pin isn't acting as a PWM pin
        If isPWMPin(pin) Then
            Throw New FX3ConfigurationException("ERROR: The selected pin is currently configured to drive a PWM signal. Please call StopPWM(pin) before interfacing with the pin further")
        End If

        'compensate for 2.8us period mis-alignment in the firmware - maybe this should be done in firmware
        If pperiod < 0.003 Then
            Throw New FX3ConfigurationException("ERROR: Invalid Pulse Drive period " + pperiod.ToString() + "ms. Minimum possible drive time is 3 microseconds")
        End If
        pperiod = pperiod - 0.0028

        'Find ticks and rollover
        ticksDouble = (pperiod / 1000) * m_FX3SPIConfig.SecondsToTimerTicks
        timerRollovers = CUInt(Math.Floor(ticksDouble / UInteger.MaxValue))
        timerTicks = CUInt(ticksDouble Mod UInteger.MaxValue)

        'sanitize polarity (if not 0, then 1)
        If polarity <> 0 Then polarity = 1

        'Set the GPIO pin number (only 1 byte in FX3PinObject)
        buf(0) = CByte(pin.pinConfig And &HFFUI)
        buf(1) = 0
        'Set the polarity (1 for high, 0 for low)
        buf(2) = CByte(polarity And &H1UI)
        'Set the drive time
        buf(3) = CByte(timerTicks And &HFFUI)
        buf(4) = CByte((timerTicks And &HFF00UI) >> 8)
        buf(5) = CByte((timerTicks And &HFF0000UI) >> 16)
        buf(6) = CByte((timerTicks And &HFF000000UI) >> 24)
        buf(7) = CByte(timerRollovers And &HFFUI)
        buf(8) = CByte((timerRollovers And &HFF00UI) >> 8)
        buf(9) = CByte((timerRollovers And &HFF0000UI) >> 16)
        buf(10) = CByte((timerRollovers And &HFF000000UI) >> 24)

        'Start data transfer
        If Not XferControlData(buf, 11, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control endpoint transfer to start pulse drive timed out!")
        End If

        'Function should block until end of pin drive
        Threading.Thread.Sleep(CInt(pperiod))

        'Clear buffer
        Array.Clear(buf, 0, buf.Length)

        'Wait for the status to be returned over BULK-In
        If Not USB.XferData(buf, 4, DataInEndPt) Then
            Throw New FX3CommunicationException("ERROR: Transfer from FX3 after pulse drive failed!")
        End If

        'Get the status from the buffer
        status = BitConverter.ToUInt32(buf, 0)

        'Throw exception if the operation failed
        If Not status = 0 Then
            Throw New FX3BadStatusException("ERROR: Pin Drive Failed, Status: " + status.ToString("X4"))
        End If

    End Sub

    ''' <summary>
    ''' This function waits for a pin to reach a specified level
    ''' </summary>
    ''' <param name="pin">The pin to poll</param>
    ''' <param name="polarity">The level to wait for. 1 - high, 0 - low</param>
    ''' <param name="delayInMs">The delay from the start of the function call to when the pin polling starts</param>
    ''' <param name="timeoutInMs">The timeout from when the pin polling starts to when the function returns, if the desired level is never reached</param>
    ''' <returns>The total time waited (including delay) in ms</returns>
    Public Function PulseWait(pin As IPinObject, polarity As UInteger, delayInMs As UInteger, timeoutInMs As UInteger) As Double Implements IPinFcns.PulseWait

        'Waits for a pin level and returns the time waited
        'In the FX3 implementation, the maximum time allowed for timeoutInMs + delayInMs is 4.497 * 10^6 ms
        'Any values set over that amount will have no timeout (blocks forever until level is detected)

        'Declare variables needed for transfer
        Dim buf(15) As Byte
        Dim totalTime As Long
        Dim transferStatus As Boolean
        Dim timeoutTimer As New Stopwatch
        Dim convertedTime As Double
        Dim status, currentTime, rollOverCount As UInteger
        Dim totalTicks, rollOverCountULong As ULong
        Dim delayOverflow As Boolean
        Dim delayScaled As UInteger
        Dim timeoutTicks, timeoutRollovers As UInteger
        Dim totalTimeout As ULong

        'Validate that the pin isn't acting as a PWM pin
        If isPWMPin(pin) Then
            Throw New FX3ConfigurationException("ERROR: The selected pin is currently configured to drive a PWM signal. Please call StopPWM(pin) before interfacing with the pin further")
        End If

        'Set the total time for the operation
        totalTime = delayInMs + timeoutInMs

        'Set the pin
        buf(0) = CByte(pin.pinConfig And &HFFUI)
        buf(1) = 0

        'sanitize polarity (if not 0, then 1)
        If polarity <> 0 Then polarity = 1

        'Set the polarity
        buf(2) = CByte(polarity)

        'Sleep on PC side if delay is too large (approx. 7 minutes)
        delayOverflow = False
        delayScaled = delayInMs
        If delayInMs > (UInteger.MaxValue / (m_FX3SPIConfig.SecondsToTimerTicks / 1000)) Then
            delayOverflow = True
            Threading.Thread.Sleep(CInt(delayInMs))
            delayScaled = 0
            totalTime = timeoutInMs
        End If

        'Add delay
        buf(3) = CByte(delayScaled And &HFFUI)
        buf(4) = CByte((delayScaled And &HFF00UI) >> 8)
        buf(5) = CByte((delayScaled And &HFF0000UI) >> 16)
        buf(6) = CByte((delayScaled And &HFF000000UI) >> 24)

        'Calculate number of timer rollovers
        totalTimeout = CULng(timeoutInMs * (m_FX3SPIConfig.SecondsToTimerTicks / 1000))
        timeoutRollovers = CUInt(Math.Floor(totalTimeout / UInteger.MaxValue))
        timeoutTicks = CUInt(totalTimeout Mod UInteger.MaxValue)

        'Add timeout ticks
        buf(7) = CByte(timeoutTicks And &HFFUI)
        buf(8) = CByte((timeoutTicks And &HFF00UI) >> 8)
        buf(9) = CByte((timeoutTicks And &HFF0000UI) >> 16)
        buf(10) = CByte((timeoutTicks And &HFF000000UI) >> 24)

        'Add rollover count for timeout
        buf(11) = CByte(timeoutRollovers And &HFFUI)
        buf(12) = CByte((timeoutRollovers And &HFF00UI) >> 8)
        buf(13) = CByte((timeoutRollovers And &HFF0000UI) >> 16)
        buf(14) = CByte((timeoutRollovers And &HFF000000UI) >> 24)

        'Start stopwatch
        timeoutTimer.Start()

        'Send a vendor command to start a pulse wait operation (returns immediately)
        ConfigureControlEndpoint(USBCommands.ADI_PULSE_WAIT, True)
        If Not XferControlData(buf, 15, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control endpoint transfer for pulse wait timed out!")
        End If

        'Clear buffer
        Array.Clear(buf, 0, buf.Length)

        'Start bulk transfer
        transferStatus = False
        If totalTime = 0 Then
            transferStatus = USB.XferData(buf, 12, DataInEndPt)
        Else
            While ((Not transferStatus) And (timeoutTimer.ElapsedMilliseconds() < totalTime))
                transferStatus = USB.XferData(buf, 12, DataInEndPt)
            End While
        End If

        'stop stopwatch
        timeoutTimer.Stop()

        'If the transfer failed return the timeout value
        If Not transferStatus Then
            If delayOverflow Then
                'The delay was inserted as a sleep on the PC side
                Return Convert.ToDouble(timeoutTimer.ElapsedMilliseconds() + delayInMs)
            Else
                Return Convert.ToDouble(timeoutTimer.ElapsedMilliseconds())
            End If
        End If

        'Read status from the buffer and throw exception for bad status
        status = BitConverter.ToUInt32(buf, 0)
        If Not status = 0 Then
            Throw New FX3BadStatusException("ERROR: Failed to configure PulseWait pin FX3 GPIO " + pin.pinConfig.ToString() + " as input, error code: 0x" + status.ToString("X4"))
        End If

        'Read current time
        currentTime = BitConverter.ToUInt32(buf, 4)

        'Read roll over counter
        rollOverCount = BitConverter.ToUInt32(buf, 8)
        rollOverCountULong = rollOverCount

        'Calculate the total time, in timer ticks
        totalTicks = rollOverCountULong * UInteger.MaxValue
        totalTicks += currentTime

        'Scale the time waited to MS
        convertedTime = Convert.ToDouble(totalTicks)
        convertedTime = Math.Round((1000 * convertedTime) / m_FX3SPIConfig.SecondsToTimerTicks, 3)

        'Return the actual time waited
        Return convertedTime

    End Function

    ''' <summary>
    ''' Reads the value of a GPIO pin on the FX3
    ''' </summary>
    ''' <param name="pin">The pin to read, as a FX3PinObject</param>
    ''' <returns>The pin value - 1 is high, 0 is low</returns>
    Public Function ReadPin(pin As IPinObject) As UInteger Implements IPinFcns.ReadPin

        Dim buf(4) As Byte
        Dim status, shiftedValue As UInteger

        'Validate that the pin isn't acting as a PWM pin
        If isPWMPin(pin) Then
            Throw New FX3ConfigurationException("ERROR: The selected pin is currently configured to drive a PWM signal. Please call StopPWM(pin) before interfacing with the pin further")
        End If

        'Configure control endpoint for pin read
        ConfigureControlEndpoint(USBCommands.ADI_READ_PIN, False)
        FX3ControlEndPt.Index = CUShort(pin.pinConfig And &HFFUI)

        'Transfer data
        If Not XferControlData(buf, 5, 1000) Then
            'Throw an exception if the transaction times out
            Throw New FX3CommunicationException("ERROR: Pin read timed out")
        End If

        'Get the status from the buffer
        status = buf(1)
        shiftedValue = buf(2)
        shiftedValue = shiftedValue << 8
        status = status + shiftedValue
        shiftedValue = buf(3)
        shiftedValue = shiftedValue << 16
        status = status + shiftedValue
        shiftedValue = buf(4)
        shiftedValue = shiftedValue << 24
        status = status + shiftedValue

        'If the status is not success throw an exception
        If Not status = 0 Then
            Throw New FX3BadStatusException("ERROR: Pin read failed, status - " + status.ToString("X4"))
        End If

        'Return the input bit of the pin register
        Return buf(0)

    End Function

    ''' <summary>
    ''' Reads a list of FX3 GPIO pins. This function calls the overload which takes an IEnumerable
    ''' </summary>
    ''' <param name="pins">An array of FX3PinObjects to read</param>
    ''' <returns>The pin values, as a UInteger. The first pin is in bit 0, second is in bit 1, and so on</returns>
    Public Function ReadPins(ParamArray pins() As IPinObject) As UInteger Implements IPinFcns.ReadPins

        'Call the overload which takes IEnumerable
        Dim enumerablePins As IEnumerable(Of IPinObject)
        enumerablePins = pins
        Return ReadPins(enumerablePins)

    End Function

    ''' <summary>
    ''' Reads a list of FX3 GPIO pins
    ''' </summary>
    ''' <param name="pins">An enumerable list of FX3PinObjects to read (maximum of 32)</param>
    ''' <returns>The pin values, as a UInteger. The first pin is in bit 0, second is in bit 1, and so on</returns>
    Public Function ReadPins(pins As IEnumerable(Of IPinObject)) As UInteger Implements IPinFcns.ReadPins

        'Validate input
        If pins.Count > 32 Then
            Throw New FX3ConfigurationException("ERROR: Cannot read more than 32 pins in one call to ReadPins")
        End If

        Dim shiftValue As Integer = 0
        Dim pinResult As UInteger = 0
        'Build the pin values into a UInteger
        For Each pin In pins
            pinResult = pinResult And (ReadPin(pin) << shiftValue)
            shiftValue = shiftValue + 1
        Next

        Return pinResult

    End Function

    ''' <summary>
    ''' Not implemented
    ''' </summary>
    ''' <param name="start_pin">Not implemented</param>
    ''' <param name="start_polarity">Not implemented</param>
    ''' <param name="stop_pin">Not implemented</param>
    ''' <param name="stop_polarity">Not implemented</param>
    ''' <param name="delay">Not implemented</param>
    ''' <returns>Not implemented</returns>
    Public Function ReadTime(start_pin As UInteger, start_polarity As UInteger, stop_pin As UInteger, stop_polarity As UInteger, delay As UInteger) As UShort() Implements IPinFcns.ReadTime
        Throw New NotImplementedException()
    End Function

    ''' <summary>
    ''' Sets the value of a FX3 GPIO pin. This value will persist until the pin is set to a different value, or read from
    ''' </summary>
    ''' <param name="pin">The FX3PinObject pin to read</param>
    ''' <param name="value">The polarity to set the pin to, 1 - high, 0 - low</param>
    Public Sub SetPin(pin As IPinObject, value As UInteger) Implements IPinFcns.SetPin

        Dim buf(3) As Byte
        Dim status, shiftedValue As UInteger

        'Validate that the pin isn't acting as a PWM pin
        If isPWMPin(pin) Then
            Throw New FX3ConfigurationException("ERROR: The selected pin is currently configured to drive a PWM signal. Please call StopPWM(pin) before interfacing with the pin further")
        End If

        'Valid pin values are 1 and 0
        If Not value = 0 Then
            value = 1
        End If

        'Setup a set pin command
        ConfigureControlEndpoint(USBCommands.ADI_SET_PIN, False)
        FX3ControlEndPt.Index = CUShort(pin.pinConfig And &HFFUI)
        FX3ControlEndPt.Value = CUShort(value)

        'Transfer data
        If Not XferControlData(buf, 4, 2000) Then
            Throw New FX3CommunicationException("ERROR: Pin set operation timed out!")
        End If

        'Get the status from the buffer
        status = buf(0)
        shiftedValue = buf(1)
        shiftedValue = shiftedValue << 8
        status = status + shiftedValue
        shiftedValue = buf(2)
        shiftedValue = shiftedValue << 16
        status = status + shiftedValue
        shiftedValue = buf(3)
        shiftedValue = shiftedValue << 24
        status = status + shiftedValue

        'If the status is not success throw an exception
        If Not status = 0 Then
            Throw New FX3BadStatusException("ERROR: Pin set failed, status: 0x" + status.ToString("X4"))
        End If

    End Sub

#End Region

#Region "Other Pin Functions"

    ''' <summary>
    ''' Measures the frequency of an input signal to the selected pin.
    ''' </summary>
    ''' <param name="pin">The pin to measure. Must be an FX3 pin object</param>
    ''' <param name="polarity">THe edge to measure from. 0 - falling edge, 1 - rising edge</param>
    ''' <param name="timeoutInMs">The time to wait for the FX3 to return values before defaulting to infinity (in ms)</param>
    ''' <param name="numPeriods">THe number of periods to sample for. Minimum value of 1</param>
    ''' <returns>The signal frequency, in Hz. Goes to infinity if no signal found.</returns>
    Public Function MeasurePinFreq(pin As IPinObject, polarity As UInteger, timeoutInMs As UInteger, numPeriods As UShort) As Double
        'Variable initialization
        Dim buf(12) As Byte
        Dim timeoutTimer As New Stopwatch()
        Dim timeoutTicks, timeoutRollovers, timerTicks, timerRollovers, status As UInteger
        Dim timeoutLng, totalTicks As ULong
        Dim freq, period As Double
        Dim transferStatus As Boolean

        'Validate pin type
        If Not IsFX3Pin(pin) Then
            Throw New FX3Exception("ERROR: Data ready pin type must be an FX3PinObject")
        End If

        'Validate periods
        If numPeriods = 0 Then
            Throw New FX3ConfigurationException("ERRROR: NumPeriods cannot be 0")
        End If

        'Calculate the timeout ticks and timeout rollovers

        'Validate the polarity (must be 0 or 1)
        If polarity <> 0 Then
            polarity = 1
        End If

        'Set the pin
        buf(0) = CByte(pin.pinConfig And &HFFUI)
        buf(1) = CByte((pin.pinConfig And &HFF00UI) >> 8)

        'Set the polarity
        buf(2) = CByte(polarity)

        'Calculate the timeout values
        timeoutLng = CULng(timeoutInMs * (m_FX3SPIConfig.SecondsToTimerTicks / 1000))

        timeoutRollovers = CUInt(Math.Floor(timeoutLng / UInteger.MaxValue))
        timeoutTicks = CUInt(timeoutLng Mod UInteger.MaxValue)

        'Add timeouts to buffer
        buf(3) = CByte(timeoutTicks And &HFFUI)
        buf(4) = CByte((timeoutTicks And &HFF00UI) >> 8)
        buf(5) = CByte((timeoutTicks And &HFF0000UI) >> 16)
        buf(6) = CByte((timeoutTicks And &HFF000000UI) >> 24)
        buf(7) = CByte(timeoutRollovers And &HFFUI)
        buf(8) = CByte((timeoutRollovers And &HFF00UI) >> 8)
        buf(9) = CByte((timeoutRollovers And &HFF0000UI) >> 16)
        buf(10) = CByte((timeoutRollovers And &HFF000000UI) >> 24)

        'Add number of periods to average to buffer
        buf(11) = CByte(numPeriods And &HFFUI)
        buf(12) = CByte((numPeriods And &HFF00UI) >> 8)

        'Start stopwatch
        timeoutTimer.Start()

        'Send vendor command to request DR frequency
        ConfigureControlEndpoint(USBCommands.ADI_MEASURE_DR, True)
        If Not XferControlData(buf, 13, 2000) Then
            Throw New FX3CommunicationException("ERROR: DR frequency read timed out")
        End If

        'Clear buffer
        Array.Clear(buf, 0, buf.Length)

        'Start bulk transfer
        transferStatus = False
        If timeoutInMs = 0 Then
            transferStatus = USB.XferData(buf, 12, DataInEndPt)
        Else
            While ((Not transferStatus) And (timeoutTimer.ElapsedMilliseconds() < timeoutInMs))
                transferStatus = USB.XferData(buf, 12, DataInEndPt)
            End While
        End If

        'stop stopwatch
        timeoutTimer.Stop()

        'Read the return values from the buffer
        status = BitConverter.ToUInt32(buf, 0)
        timerTicks = BitConverter.ToUInt32(buf, 4)
        timerRollovers = BitConverter.ToUInt32(buf, 8)

        'find the total time
        totalTicks = CULng(timerRollovers) * UInteger.MaxValue
        totalTicks += timerTicks

        'Find one period time
        period = totalTicks / m_FX3SPIConfig.SecondsToTimerTicks
        period = period / numPeriods

        'Convert ticks to freq
        freq = 1 / period

        'If the transfer failed return infinity
        If Not transferStatus Then
            freq = Double.PositiveInfinity
        End If

        'If the status is a timeout return infinity
        If status = &H45 Then
            freq = Double.PositiveInfinity
        ElseIf status <> 0 Then
            freq = Double.PositiveInfinity
            Throw New FX3BadStatusException("ERROR: Bad status code after pin frequency measure. Status: 0x" + status.ToString("X"))
        End If

        'Return freq measured on specified pin
        Return freq
    End Function

    ''' <summary>
    ''' Reads the measured DR value
    ''' </summary>
    ''' <param name="pin">The DR pin to measure</param>
    ''' <param name="polarity">The edge to measure from. 1 - low to high, 0 - high to low</param>
    ''' <param name="timeoutInMs">The timeout from when the pin measurement starts to when the function returns if the signal cannot be found</param>
    ''' <returns>The DR frequency in Hz</returns>
    Public Function ReadDRFreq(pin As IPinObject, polarity As UInteger, timeoutInMs As UInteger) As Double
        Return MeasurePinFreq(pin, polarity, timeoutInMs, 8)
    End Function

    ''' <summary>
    ''' This function measures the time delay between toggling a trigger pin, and a state change on the busy pin. This can be used to measure
    ''' the propagation delay between a sync edge and data ready being de-asserted.
    ''' </summary>
    ''' <param name="TriggerPin">The pin to toggle. When this pin is driven to the selected polarity the delay timer starts</param>
    ''' <param name="TriggerDrivePolarity">The polarity to drive the trigger pin to. 1- high, 0 - low</param>
    ''' <param name="BusyPin">The pin to measure.</param>
    ''' <param name="Timeout">Operation timeout period, in ms</param>
    ''' <returns>The delay time, in ms</returns>
    Public Function MeasurePinDelay(TriggerPin As IPinObject, TriggerDrivePolarity As UInteger, BusyPin As IPinObject, Timeout As UInteger) As Double

        Dim buf(15) As Byte
        Dim transferStatus As Boolean
        Dim timeoutTimer As New Stopwatch
        Dim convertedTime As Double
        Dim status, currentTime, rollOverCount As UInteger
        Dim totalTicks, rollOverCountULong As ULong
        Dim FX3Timeout As UInteger

        'Validate that the pin isn't acting as a PWM pin
        If isPWMPin(BusyPin) Or isPWMPin(TriggerPin) Then
            Throw New FX3ConfigurationException("ERROR: The selected pin is currently configured to drive a PWM signal. Please call StopPWM(pin) before interfacing with the pin further")
        End If

        'Validate that the trigger pin is not the busy pin
        If TriggerPin.pinConfig = BusyPin.pinConfig Then
            Throw New FX3ConfigurationException("ERROR: The BUSY pin cannot be used as the TRIGGER pin in a MeasurePinDelay function call")
        End If

        'Set the trigger pin
        buf(0) = CByte(TriggerPin.pinConfig And &HFFUI)
        buf(1) = CByte((TriggerPin.pinConfig And &HFF00UI) >> 8)

        'Set the trigger pin polarity
        If TriggerDrivePolarity <> 0 Then TriggerDrivePolarity = 1
        buf(2) = CByte(TriggerDrivePolarity)

        'set the busy pin
        buf(3) = CByte(BusyPin.pinConfig And &HFFUI)
        buf(4) = CByte((BusyPin.pinConfig And &HFF00UI) >> 8)

        'If the timeout is too large (greater than one timer period) set to 0 -> no timeout on firmware
        FX3Timeout = Timeout
        If Timeout > ((UInteger.MaxValue / m_FX3SPIConfig.SecondsToTimerTicks) * 1000) Then
            FX3Timeout = 0
        End If

        'Load timeout into the buffer
        buf(5) = CByte(FX3Timeout And &HFFUI)
        buf(6) = CByte((FX3Timeout And &HFF00UI) >> 8)
        buf(7) = CByte((FX3Timeout And &HFF0000UI) >> 16)
        buf(8) = CByte((FX3Timeout And &HFF000000UI) >> 24)

        'Send a vendor command to start a pulse wait operation (returns immediately)
        ConfigureControlEndpoint(USBCommands.ADI_PIN_DELAY_MEASURE, True)
        If Not XferControlData(buf, 9, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control endpoint transfer to measure pin delay timed out!")
        End If

        'Clear buffer
        Array.Clear(buf, 0, buf.Length)

        'Start bulk transfer
        transferStatus = False
        If Timeout = 0 Then
            transferStatus = USB.XferData(buf, 12, DataInEndPt)
        Else
            While ((Not transferStatus) And (timeoutTimer.ElapsedMilliseconds() < Timeout))
                transferStatus = USB.XferData(buf, 12, DataInEndPt)
            End While
        End If

        'stop stopwatch
        timeoutTimer.Stop()

        'If the transfer failed return infinity (to indicate failure)
        If Not transferStatus Then
            Return Double.PositiveInfinity
        End If

        'Read status from the buffer and throw exception for bad status
        status = BitConverter.ToUInt32(buf, 0)
        If Not status = 0 Then
            Throw New FX3BadStatusException("ERROR: Failed to configure pin as input, error code: " + status.ToString("X4"))
        End If

        'Read current time
        currentTime = BitConverter.ToUInt32(buf, 4)

        'Read roll over counter
        rollOverCount = BitConverter.ToUInt32(buf, 8)
        rollOverCountULong = rollOverCount

        'Calculate the total time, in timer ticks
        totalTicks = rollOverCountULong * UInteger.MaxValue
        totalTicks += currentTime

        'Scale the time waited to MS
        convertedTime = Convert.ToDouble(totalTicks) * 1000
        convertedTime = Math.Round(convertedTime / m_FX3SPIConfig.SecondsToTimerTicks, 4)

        'Return the actual time waited
        Return convertedTime

    End Function

    ''' <summary>
    ''' This function triggers a DUT action using a pulse drive, and then measures the following pulse width on a separate busy line.
    ''' The pulse time on the busy pin is measured using a 10MHz timer with approx. 0.1us accuracy.
    ''' </summary>
    ''' <param name="TriggerPin">The pin to drive for the trigger condition (for example a sync pin)</param>
    ''' <param name="TriggerDriveTime">The time, in ms, to drive the trigger pin for</param>
    ''' <param name="TriggerDrivePolarity">The polarity to drive the trigger pin at (0 - low, 1 - high)</param>
    ''' <param name="BusyPin">The pin to measure a busy pulse on</param>
    ''' <param name="BusyPolarity">The polarity of the pulse being measured (0 will measure a low pulse, 1 will measure a high pulse)</param>
    ''' <param name="Timeout">The timeout, in ms, to wait before canceling, if the pulse is never detected</param>
    ''' <returns>The pulse width, in ms. Accurate to approx. 1us</returns>
    Public Function MeasureBusyPulse(TriggerPin As IPinObject, TriggerDriveTime As UInteger, TriggerDrivePolarity As UInteger, BusyPin As IPinObject, BusyPolarity As UInteger, Timeout As UInteger) As Double
        'Declare variables needed for transfer
        Dim buf(14) As Byte
        Dim transferStatus As Boolean
        Dim timeoutTimer As New Stopwatch
        Dim convertedTime As Double
        Dim status, MaxTime As UInteger
        Dim result As ULong

        'max time for timeout or drive
        MaxTime = CUInt(1000 * (UInteger.MaxValue / m_FX3SPIConfig.SecondsToTimerTicks))

        'Validate that the pin isn't acting as a PWM pin
        If isPWMPin(BusyPin) Then
            Throw New FX3ConfigurationException("ERROR: The selected busy pin is currently configured to drive a PWM signal. Please call StopPWM(pin) before interfacing with the pin further")
        End If

        'check that complex timer block for busy is not in use by timer
        If (((BusyPin.pinConfig) And &HFFUI) Mod 8) = 0 Then
            Throw New FX3ConfigurationException("ERROR: The selected busy pin shares a timer peripheral with the FX3 10MHz timebase timer. This pin cannot be used for measurements")
        End If

        'Validate that the trigger drive time isn't too long (less than one timer period)
        If TriggerDriveTime > MaxTime Then
            Throw New FX3ConfigurationException("ERROR: Invalid trigger pin drive time of " + TriggerDriveTime.ToString() + "ms. Max allowed is " + MaxTime.ToString() + "ms")
        End If

        'Validate that the timeout isn't too long (less than timer period)
        If Timeout > MaxTime Then
            Throw New FX3ConfigurationException("ERROR: Invalid timeout time of " + Timeout.ToString() + "ms. Max allowed is " + MaxTime.ToString() + "ms")
        End If

        'Validate that the trigger pin is not the busy pin
        If TriggerPin.pinConfig = BusyPin.pinConfig Then
            Throw New FX3ConfigurationException("ERROR: The BUSY pin cannot be used as the TRIGGER pin in a MeasureBusyPulse function call")
        End If

        'Buffer will contain the following, in order:
        'BusyPin(0-1), BusyPinPolarity(2), TimeoutMs(3-6), TriggerMode(7), TriggerPin(8-9), TriggerDrivePolarity(10), TriggerDriveTime(11-14) 

        'Set the pin
        buf(0) = CByte(BusyPin.pinConfig And &HFFUI)
        buf(1) = CByte((BusyPin.pinConfig And &HFF00UI) >> 8)

        'sanitize polarity
        If BusyPolarity <> 0 Then BusyPolarity = 1

        'Set the polarity
        buf(2) = CByte(BusyPolarity)

        'Load timeout into the buffer
        buf(3) = CByte(Timeout And &HFFUI)
        buf(4) = CByte((Timeout And &HFF00UI) >> 8)
        buf(5) = CByte((Timeout And &HFF0000UI) >> 16)
        buf(6) = CByte((Timeout And &HFF000000UI) >> 24)

        'Put mode in buffer (1 for trigger on SPI word, 10for trigger on pin drive)
        buf(7) = 0

        'Put trigger pin in buffer
        buf(8) = CByte(TriggerPin.pinConfig And &HFFUI)
        buf(9) = CByte((TriggerPin.pinConfig And &HFF00UI) >> 8)

        'Put trigger drive polarity in buffer
        buf(10) = CByte(TriggerDrivePolarity)

        'Put trigger drive time (in MS) in buffer
        buf(11) = CByte(TriggerDriveTime And &HFFUI)
        buf(12) = CByte((TriggerDriveTime And &HFF00UI) >> 8)
        buf(13) = CByte((TriggerDriveTime And &HFF0000UI) >> 16)
        buf(14) = CByte((TriggerDriveTime And &HFF000000UI) >> 24)

        'Start stopwatch
        timeoutTimer.Start()

        'Send a vendor command to start a pulse wait operation (returns immediately)
        ConfigureControlEndpoint(USBCommands.ADI_BUSY_MEASURE, True)
        If Not XferControlData(buf, 15, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control Endpoint transfer timed out")
        End If

        'Clear buffer
        Array.Clear(buf, 0, buf.Length)

        'Start bulk transfer
        transferStatus = False
        If Timeout = 0 Then
            transferStatus = USB.XferData(buf, 8, DataInEndPt)
        Else
            While ((Not transferStatus) And (timeoutTimer.ElapsedMilliseconds() < Timeout))
                transferStatus = USB.XferData(buf, 8, DataInEndPt)
            End While
        End If

        'stop stopwatch
        timeoutTimer.Stop()

        'If the transfer failed return infinity to signify timeout
        If Not transferStatus Then
            Return Double.PositiveInfinity
        End If

        'Read status from the buffer and throw exception for bad status (except timeout)
        status = BitConverter.ToUInt32(buf, 0)
        If status = &H45 Then
            Return Double.PositiveInfinity
        ElseIf status <> 0 Then
            Throw New FX3BadStatusException("ERROR: Busy pin pulse measure failed, error code: 0x" + status.ToString("X4"))
        End If

        'Read result
        result = BitConverter.ToUInt32(buf, 4)

        'Scale the result to ms (based on 10MHz timer)
        convertedTime = 1000 * result / m_FX3SPIConfig.SecondsToTimerTicks
        convertedTime = Math.Round(convertedTime, 4)

        'Return the actual time waited
        Return convertedTime
    End Function

    ''' <summary>
    ''' Overload of measure busy pulse which triggers the DUT event using a SPI write instead of a pin drive.
    ''' </summary>
    ''' <param name="SpiTriggerData">The data to transmit on the MOSI line, to trigger the operation being measured</param>
    ''' <param name="BusyPin">The pin to measure a busy pulse on</param>
    ''' <param name="BusyPolarity">The polarity of the pulse being measured (0 will measure a low pulse, 1 will measure a high pulse)</param>
    ''' <param name="Timeout">The timeout, in ms, to wait before canceling, if the pulse is never detected</param>
    ''' <returns>The pulse width, in ms. Accurate to approx. 1us</returns>
    Public Function MeasureBusyPulse(SpiTriggerData As Byte(), BusyPin As IPinObject, BusyPolarity As UInteger, Timeout As UInteger) As Double
        'Declare variables needed for transfer
        Dim buf As New List(Of Byte)
        Dim respBuf(7) As Byte
        Dim transferStatus As Boolean
        Dim timeoutTimer As New Stopwatch
        Dim convertedTime As Double
        Dim status, MaxTime As UInteger
        Dim result As ULong

        'max time for timeout or drive
        MaxTime = CUInt(1000 * (UInteger.MaxValue / m_FX3SPIConfig.SecondsToTimerTicks))

        'Validate that the pin isn't acting as a PWM pin
        If isPWMPin(BusyPin) Then
            Throw New FX3ConfigurationException("ERROR: The selected pin is currently configured to drive a PWM signal. Please call StopPWM(pin) before interfacing with the pin further")
        End If

        'check that SPI data is in multiple of SPI word size
        If ((SpiTriggerData.Count * 8) Mod m_FX3SPIConfig.WordLength) <> 0 Then
            Throw New FX3ConfigurationException("ERROR: SPI trigger data must be a multiple of the SPI word length.")
        End If

        'Validate that the pin isn't acting as a PWM pin
        If isPWMPin(BusyPin) Then
            Throw New FX3ConfigurationException("ERROR: The selected busy pin is currently configured to drive a PWM signal. Please call StopPWM(pin) before interfacing with the pin further")
        End If

        'check that complex timer block for busy is not in use by timer
        If (((BusyPin.pinConfig) And &HFFUI) Mod 8) = 0 Then
            Throw New FX3ConfigurationException("ERROR: The selected busy pin shares a timer peripheral with the FX3 10MHz timebase timer. This pin cannot be used for measurements")
        End If

        'Validate that the timeout isn't too long (less than timer period)
        If Timeout > MaxTime Then
            Throw New FX3ConfigurationException("ERROR: Invalid timeout time of " + Timeout.ToString() + "ms. Max allowed is " + MaxTime.ToString() + "ms")
        End If

        'Buffer will contain the following, in order:
        'BusyPin(0-1), BusyPinPolarity(2), TimeoutMs(3-6), TriggerMode(7), Spi Data Count(8-9), SPI Data(10 - n)

        'Set the pin (buf 0 - 1)
        buf.Add(CByte(BusyPin.pinConfig And &HFFUI))
        buf.Add(CByte((BusyPin.pinConfig And &HFF00UI) >> 8))

        'Set the polarity (buf 2)
        If BusyPolarity <> 0 Then BusyPolarity = 1
        buf.Add(CByte(BusyPolarity))

        'Load timeout into the buffer (buf 3 -6)
        buf.Add(CByte(Timeout And &HFFUI))
        buf.Add(CByte((Timeout And &HFF00UI) >> 8))
        buf.Add(CByte((Timeout And &HFF0000UI) >> 16))
        buf.Add(CByte((Timeout And &HFF000000UI) >> 24))

        'Put mode in buffer (7) (1 for trigger on SPI word, 0 for trigger on pin drive)
        buf.Add(1)

        'put SPI command word size in buf (8 - 9)
        buf.Add(CByte(SpiTriggerData.Count() And &HFFUI))
        buf.Add(CByte((SpiTriggerData.Count() And &HFF00UI) >> 8))

        'Put SPI command word in buffer
        buf.AddRange(SpiTriggerData)

        'Start stopwatch
        timeoutTimer.Start()

        'Send a vendor command to start a pulse wait operation (returns immediately)
        ConfigureControlEndpoint(USBCommands.ADI_BUSY_MEASURE, True)
        If Not XferControlData(buf.ToArray(), buf.Count, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control Endpoint transfer timed out")
        End If

        'Start bulk transfer
        transferStatus = False
        If Timeout = 0 Then
            transferStatus = USB.XferData(respBuf, 8, DataInEndPt)
        Else
            While ((Not transferStatus) And (timeoutTimer.ElapsedMilliseconds() < Timeout))
                transferStatus = USB.XferData(respBuf, 8, DataInEndPt)
            End While
        End If

        'stop stopwatch
        timeoutTimer.Stop()

        'If the transfer failed return the timeout value
        If Not transferStatus Then
            Return Double.PositiveInfinity
        End If

        'Read status from the buffer and throw exception for bad status
        status = BitConverter.ToUInt32(respBuf, 0)

        'return timeout (infinity) for timeout status code (0x45)
        If status = &H45 Then
            Return Double.PositiveInfinity
        ElseIf status <> 0 Then
            Throw New FX3BadStatusException("ERROR: Busy pin pulse measure failed, error code: 0x" + status.ToString("X4"))
        End If

        'Read current time
        result = BitConverter.ToUInt32(respBuf, 4)

        'Scale the result to ms (based on 10MHz timer)
        convertedTime = 1000 * result / m_FX3SPIConfig.SecondsToTimerTicks
        convertedTime = Math.Round(convertedTime, 4)

        'Return the converted time
        Return convertedTime

    End Function

    ''' <summary>
    ''' This function configures the selected pin to drive a pulse width modulated output.
    ''' </summary>
    ''' <param name="Frequency">The desired PWM frequency, in Hz. Valid values are in the range of 0.05Hz (0.05) - 50MHz (20000000.0)</param>
    ''' <param name="DutyCycle">The PWM duty cycle. Valid values are in the range 0.0 - 1.0. To achieve a "clock" signal set the duty cycle to 0.5</param>
    ''' <param name="Pin">The pin to configure as a PWM signal.</param>
    Public Sub StartPWM(Frequency As Double, DutyCycle As Double, Pin As IPinObject)

        'pin timer hardware block (0 - 7)
        Dim pinTimerBlock As UInteger

        'Check that pin is an fx3pin
        If Not IsFX3Pin(Pin) Then
            Throw New FX3Exception("ERROR: All pin objects used with the FX3 API must be of type FX3PinObject")
        End If

        'Check that the timer complex GPIO isn't being used
        pinTimerBlock = Pin.pinConfig Mod 8UI
        For Each PWMPin In m_PinPwmInfoList
            If Not (PWMPin.FX3GPIONumber = Pin.pinConfig) And (pinTimerBlock = PWMPin.FX3TimerBlock) Then
                Throw New FX3ConfigurationException("ERROR: The PWM hardware for the pin selected is currently being used by pin number " + PWMPin.FX3GPIONumber.ToString())
            End If
        Next

        'Validate frequency
        If Frequency < 0.05 Or Frequency > 50000000 Then
            Throw New FX3ConfigurationException("ERROR: Invalid PWM frequency: " + Frequency.ToString() + "Hz")
        End If

        'Validate duty cycle
        If DutyCycle < 0 Or DutyCycle > 1 Then
            Throw New FX3ConfigurationException("ERROR: Invalid duty cycle: " + (100 * DutyCycle).ToString() + "%")
        End If

        'Validate that the complex GPIO 0 block is not being used. This block drives the timer subsystem and is unavailable for use as a PWM.
        If pinTimerBlock = 0 Then
            Throw New FX3ConfigurationException("ERROR: The selected " + Pin.ToString() + " pin cannot be used as a PWM")
        End If

        'Calculate the needed period and threshold value for the given setting
        Dim period, threshold As UInteger

        'The base clock is 201.6MHz (403.2MHz / 2) 'tweaked to 201.5677MHz
        Dim baseClock As Double = 201567700

        period = Convert.ToUInt32(baseClock / Frequency) - 1UI
        threshold = Convert.ToUInt32((baseClock / Frequency) * DutyCycle)
        'Decrement threshold, but clamp at 0
        If Not threshold = 0 Then
            threshold = threshold - 1UI
        End If

        'If the threshold is 0 throw an exception, this particular setting is not achievable by the board (min 1)
        If threshold < 1 Then
            Throw New FX3ConfigurationException("ERROR: The selected PWM setting (Freq: " + Frequency.ToString() + "Hz, Duty Cycle: " + (DutyCycle * 100).ToString() + "%) is not achievable using a 200MHz clock")
        End If

        'Create transfer buffer
        Dim buf(9) As Byte

        'Place PWM settings in the buffer
        buf(0) = CByte(Pin.pinConfig And &HFFUI)
        buf(1) = 0
        buf(2) = CByte(period And &HFFUI)
        buf(3) = CByte((period And &HFF00UI) >> 8)
        buf(4) = CByte((period And &HFF0000UI) >> 16)
        buf(5) = CByte((period And &HFF000000UI) >> 24)
        buf(6) = CByte(threshold And &HFFUI)
        buf(7) = CByte((threshold And &HFF00UI) >> 8)
        buf(8) = CByte((threshold And &HFF0000UI) >> 16)
        buf(9) = CByte((threshold And &HFF000000UI) >> 24)

        ConfigureControlEndpoint(USBCommands.ADI_PWM_CMD, True)
        FX3ControlEndPt.Index = 1

        'Transfer control data
        If Not XferControlData(buf, 10, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control endpoint transfer timed out while setting up a PWM signal!")
        End If

        'Add the selected pin to the list of active PWM pins
        Dim currentPinInfo As New PinPWMInfo
        Dim realFreq, realDutyCycle As Double
        realFreq = baseClock / period
        realDutyCycle = threshold / period
        currentPinInfo.SetValues(Pin, Frequency, realFreq, DutyCycle, realDutyCycle)
        m_PinPwmInfoList.AddReplace(currentPinInfo)

    End Sub

    ''' <summary>
    ''' This function call disables the PWM output from the FX3 and returns the pin to a tri-stated mode.
    ''' </summary>
    Public Sub StopPWM(Pin As IPinObject)

        'Exit if the pin isn't acting as a PWM
        If Not isPWMPin(Pin) Then
            Exit Sub
        End If

        'Create buffer
        Dim buf(1) As Byte

        'Place pin settings in the buffer
        buf(0) = CByte(Pin.pinConfig And &HFFUI)
        buf(1) = 0

        'Configure control endpoint
        ConfigureControlEndpoint(USBCommands.ADI_PWM_CMD, True)
        FX3ControlEndPt.Index = 0

        'Send stop command
        If Not XferControlData(buf, 2, 2000) Then
            Throw New FX3CommunicationException("ERROR: Control endpoint transfer timed out while stopping a PWM signal")
        End If

        'Remove pin from the PWM pin list
        For Each PWMPin In m_PinPwmInfoList
            If PWMPin.FX3GPIONumber = Pin.pinConfig Then
                m_PinPwmInfoList.Remove(PWMPin)
                Exit For
            End If
        Next

    End Sub

    ''' <summary>
    ''' This function checks to see if the selected pin has already been configured to act as a PWM output pin.
    ''' </summary>
    ''' <param name="Pin">The pin to check. Must be an FX3PinObject pin</param>
    ''' <returns>True if the pin is configured as a PWM pin, false otherwise</returns>
    Public Function isPWMPin(Pin As IPinObject) As Boolean

        'Check that its an FX3 pin
        If Not IsFX3Pin(Pin) Then
            Return False
        End If

        'Check that the pin is in PWM mode
        Return m_PinPwmInfoList.Contains(Pin)

    End Function

    ''' <summary>
    ''' Allows the user to retrieve a set of information about the current pin PWM configuration.
    ''' </summary>
    ''' <param name="Pin">The pin to pull from the PinPWMInfo List</param>
    ''' <returns>The PinPWMInfo corresponding to the selected pin. If the pin is not found all fields will be -1</returns>
    Public Function GetPinPWMInfo(Pin As IPinObject) As PinPWMInfo
        Return m_PinPwmInfoList.GetPinPWMInfo(Pin)
    End Function

    ''' <summary>
    ''' This function determines if the pin object being passed is an FX3 version of the IPinObject (as opposed to a blackfin pin for the SDP).
    ''' </summary>
    ''' <param name="Pin">The pin to check</param>
    ''' <returns>True if Pin is an FX3 pin, false if not</returns>
    Private Function IsFX3Pin(Pin As IPinObject) As Boolean
        Dim validPin As Boolean = False
        'Check the toString overload and type
        If Pin.ToString().Substring(0, 3) = "FX3" And (Pin.GetType() = GetType(FX3PinObject)) Then
            validPin = True
        End If
        Return validPin

    End Function

#End Region

#Region "User LED Functions"

    ''' <summary>
    ''' Turn on user LED (not available on Cypress Explorer FX3 board)
    ''' </summary>
    Public Sub UserLEDOn()
        Dim pin As FX3PinObject = New FX3PinObject(13)
        If isPWMPin(pin) Then StopPWM(pin)
        SetPin(pin, 0)
    End Sub

    ''' <summary>
    ''' Turn off user LED (not available on Cypress Explorer FX3 board)
    ''' </summary>
    Public Sub UserLEDOff()
        Dim pin As FX3PinObject = New FX3PinObject(13)
        If isPWMPin(pin) Then StopPWM(pin)
        SetPin(pin, 1)
    End Sub

    Public Sub FlashLED() Implements AdisApi.USBInterface.FlashLed
        Me.UserLEDBlink(3)
    End Sub

    ''' <summary>
    ''' Blink user LED using timer hardware (not available on Cypress Explorer FX3 board)
    ''' </summary>
    ''' <param name="BlinkFreq">Frequency to blink LED at</param>
    Public Sub UserLEDBlink(BlinkFreq As Double)
        StartPWM(BlinkFreq, 0.5, New FX3PinObject(13))
    End Sub

#End Region

#Region "Pin Properties"

    ''' <summary>
    ''' Read-only property to get the reset pin
    ''' </summary>
    ''' <returns>The reset pin, as an IPinObject</returns>
    Public ReadOnly Property ResetPin As IPinObject
        Get
            Return New FX3PinObject(RESET_PIN)
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get the DIO1 pin
    ''' </summary>
    ''' <returns>Returns the DIO1 pin, as an IPinObject</returns>
    Public ReadOnly Property DIO1 As IPinObject
        Get
            Return New FX3PinObject(DIO1_PIN)
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get the DIO2 pin
    ''' </summary>
    ''' <returns>Returns the DIO2 pin, as an IPinObject</returns>
    Public ReadOnly Property DIO2 As IPinObject
        Get
            Return New FX3PinObject(DIO2_PIN)
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get the DIO3 pin
    ''' </summary>
    ''' <returns>Returns the DIO3 pin, as an IPinObject</returns>
    Public ReadOnly Property DIO3 As IPinObject
        Get
            Return New FX3PinObject(DIO3_PIN)
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get the DIO4 pin
    ''' </summary>
    ''' <returns>Returns the DIO4 pin, as an IPinObject</returns>
    Public ReadOnly Property DIO4 As IPinObject
        Get
            Return New FX3PinObject(DIO4_PIN)
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get the FX3_GPIO1 pin. This pin does not map to the standard iSensor breakout,
    ''' and should be used for other general purpose interfacing.
    ''' </summary>
    ''' <returns>Returns the GPIO pin, as an IPinObject</returns>
    Public ReadOnly Property FX3_GPIO1 As IPinObject
        Get
            Return New FX3PinObject(FX3_GPIO1_PIN)
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get the FX3_GPIO2 pin. This pin does not map to the standard iSensor breakout,
    ''' and should be used for other general purpose interfacing.
    ''' </summary>
    ''' <returns>Returns the GPIO pin, as an IPinObject</returns>
    Public ReadOnly Property FX3_GPIO2 As IPinObject
        Get
            Return New FX3PinObject(FX3_GPIO2_PIN)
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get the FX3_GPIO3 pin. This pin does not map to the standard iSensor breakout,
    ''' and should be used for other general purpose interfacing.
    ''' </summary>
    ''' <returns>Returns the GPIO pin, as an IPinObject</returns>
    Public ReadOnly Property FX3_GPIO3 As IPinObject
        Get
            Return New FX3PinObject(FX3_GPIO3_PIN)
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get the FX3_GPIO4 pin. This pin does not map to the standard iSensor breakout,
    ''' and should be used for other general purpose interfacing. This pin shares a complex GPIO block with DIO1. If DIO1 is being used
    ''' as a clock source, via the StartPWM function, then this pin cannot be used as a clock source.
    ''' </summary>
    ''' <returns>Returns the GPIO pin, as an IPinObject</returns>
    Public ReadOnly Property FX3_GPIO4 As IPinObject
        Get
            Return New FX3PinObject(FX3_GPIO4_PIN)
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get loop back pin 1. This pin (CTL8) is wired directly to 
    ''' loop back pin 2 (CTL9) on the iSensor FX3 Board, Revision C or newer. These loop
    ''' back pins allow for fixed transaction timing on "asynchronous" SPI/I2C reads. One 
    ''' of the loop back pins can be configured as a PWM output using the "StartPWM" API, 
    ''' and the other loop back pin can be set as the DrPin, allowing the FX3 to trigger 
    ''' itself at a known rate.
    ''' </summary>
    ''' <returns>The first loop back pin, as an IPinObject</returns>
    Public ReadOnly Property FX3_LOOPBACK1 As IPinObject
        Get
            Return New FX3PinObject(FX3_LOOP1_PIN)
        End Get
    End Property

    ''' <summary>
    ''' Read-only property to get loop back pin 2. This pin (CTL9) is wired directly to 
    ''' loop back pin 1 (CTL8) on the iSensor FX3 Board, Revision C or newer. These loop
    ''' back pins allow for fixed transaction timing on "asynchronous" SPI/I2C reads. One 
    ''' of the loop back pins can be configured as a PWM output using the "StartPWM" API, 
    ''' and the other loop back pin can be set as the DrPin, allowing the FX3 to trigger 
    ''' itself at a known rate.
    ''' </summary>
    ''' <returns>The second loop back pin, as an IPinObject</returns>
    Public ReadOnly Property FX3_LOOPBACK2 As IPinObject
        Get
            Return New FX3PinObject(FX3_LOOP2_PIN)
        End Get
    End Property

#End Region

End Class