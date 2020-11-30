This simple Matlab script connects to an FX3 and reads the primary XL output registers from an ADIS1650x DUT. Each set of reads are data ready synchronized, and the resulting data is FFT'd and plotted.

Reference for .NET <-> Matlab data conversions can be found here: https://www.mathworks.com/help/matlab/matlab_external/passing-net-data-in-matlab.html

This example uses relative paths to locate resources based on the FX3 API repository structure. To function "As-Is" the FX3 API library must be built and placed in the "Resources" folder of the repo.