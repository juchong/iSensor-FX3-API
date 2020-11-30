using CyUSB;
using System.Runtime.InteropServices;

namespace FX3USB
{
    /// <summary>
    /// This class implements a safe version of the CyUSB.NET endpoint call XferData(buf, length)
    /// </summary>
    public class USB
    {
        private static bool WaitForIO(System.IntPtr ovlapEvent, ref CyUSBEndPoint endpoint)
        {
            switch (PInvoke.WaitForSingleObject(ovlapEvent, endpoint.TimeOut))
            {
                case 0:
                    return true;
                case 258:
                    endpoint.Abort();
                    PInvoke.WaitForSingleObject(ovlapEvent, uint.MaxValue);
                    break;
            }
            return false;
        }

        /// <summary>
        /// Equivalent to the CyUSBEndPoint.XferData(...) but with pointer pinning to ensure that the garbage collector does not move buffers
        /// </summary>
        /// <param name="buf">The buffer to transfer data into or out</param>
        /// <param name="len">Length of data to transfer. Can be overwritten with actual transfer length</param>
        /// <param name="endpoint">Endpoint to perform the transfer operation on</param>
        /// <returns>Bool indicating success of the transfer operation</returns>
        static public unsafe bool XferData(ref byte[] buf, ref int len, ref CyUSBEndPoint endpoint)
        {
            byte[] ov = new byte[endpoint.OverlapSignalAllocSize];
            fixed (byte* numPtr = ov)
            {
                ((OVERLAPPED*)numPtr)->hEvent = PInvoke.CreateEvent(0U, 0U, 0U, 0U);
                byte[] singleXfer = new byte[38 + (endpoint.XferMode == XMODE.DIRECT ? 0 : len)];
                // These pinned pointers ensure that the buffers don't move in memory
                var cmd_buff_handle = GCHandle.Alloc(singleXfer, GCHandleType.Pinned);
                var data_handle = GCHandle.Alloc(buf, GCHandleType.Pinned);

                //Perform async transfer
                endpoint.BeginDataXfer(ref singleXfer, ref buf, ref len, ref ov);
                bool flag1 = WaitForIO(((OVERLAPPED*)numPtr)->hEvent, ref endpoint);
                bool flag2 = endpoint.FinishDataXfer(ref singleXfer, ref buf, ref len, ref ov);
                PInvoke.CloseHandle(((OVERLAPPED*)numPtr)->hEvent);

                //release memory
                cmd_buff_handle.Free();
                data_handle.Free();

                //return operation flags
                return flag1 && flag2;
            }
        }
    }
}
