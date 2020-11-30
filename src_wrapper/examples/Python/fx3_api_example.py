#requires pythonnet to be installed (pip install pythonnet)

import clr
from time import sleep

#Load FX3 API Wrapper DLL
clr.AddReference('C:\\Users\\anolan3\\Documents\\iSensor-FX3-API\\FX3ApiWrapper\\bin\\Debug\\FX3ApiWrapper.dll')

#Allows wrapper to be treated like standard python library
from FX3ApiWrapper import *
from System import Array
from System import String

#Create FX3 Wrapper and load ADIS1650x regmap
Dut = Wrapper('C:\\Users\\anolan3\\Documents\\iSensor-FX3-API\\Resources','C:\\Users\\anolan3\Documents\\iSensor-FX3-ExampleGui\\src\ADIS1650x_Regmap.csv',0)

print(Dut.FX3.GetFirmwareVersion)
Dut.UserLEDBlink(2.0)

#Create reg list

regs_py = ['STATUS','DATA_CNTR','XGYRO_UPR','YGYRO_UPR','ZGYRO_UPR','XACCL_UPR','YACCL_UPR','ZACCL_UPR']
regs = Array[String](regs_py)
data = []

while True:
    data = Dut.ReadSigned(regs)
    for i in (data): 
        print(i, end =" ") 
    print()
    sleep(0.5)



