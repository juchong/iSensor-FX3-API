using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using AdisApi;

namespace FX3ApiWrapper
{
    class GenericRegInterface : IRegInterface
    {

        #region "constructor"

        private FX3Api.FX3Connection m_FX3;

        public GenericRegInterface(ref FX3Api.FX3Connection FX3)
        {
            m_FX3 = FX3;

            /* Assign defaults for ADXL35x */
        }

        #endregion

        #region "Properties"

        public uint WriteBitPosition
        {
            get
            {
                return m_WriteBitPos;
            }
            set
            {
                m_WriteBitPos = value;
            }
        }
        private uint m_WriteBitPos;

        public bool WriteBitPolarity
        {
            get
            {
                return m_WriteBitPolarity;
            }
            set
            {
                m_WriteBitPolarity = value;
            }
        }
        private bool m_WriteBitPolarity;

        public uint DataPosition
        {
            get
            {
                return m_DataPosition;
            }
            set
            {
                m_DataPosition = value;
            }
        }
        private uint m_DataPosition;

        public uint AddressPosition
        {
            get
            {
                return m_AddressPosition;
            }
            set
            {
                m_AddressPosition = value;
            }
        }
        private uint m_AddressPosition;

        public uint AddressBitLength
        {
            get
            {
                return m_AddressBitLength;
            }
            set
            {
                m_AddressBitLength = value;
            }
        }
        private uint m_AddressBitLength;

        public uint WriteDataBitLength
        {
            get
            {
                return m_WriteDataBitLength;
            }
            set
            {
                m_WriteDataBitLength = value;
            }
        }
        private uint m_WriteDataBitLength;

        public uint ReadDataBitLength
        {
            get
            {
                return m_ReadDataBitLength;
            }
            set
            {
                m_ReadDataBitLength = value;
            }
        }
        private uint m_ReadDataBitLength;

        public uint SpiWordBitLength
        {
            get
            {
                return m_SpiWordBitLength;
            }
            set
            {
                m_SpiWordBitLength = value;
            }
        }
        private uint m_SpiWordBitLength;

        public uint DeviceAddressIncrement
        {
            get
            {
                return m_DeviceAddressIncrement;
            }
            set
            {
                m_DeviceAddressIncrement = value;
            }
        }
        private uint m_DeviceAddressIncrement;

        public bool ReadImmediate
        {
            get
            {
                return m_ReadImmediate;
            }
            set
            {
                m_ReadImmediate = value;
            }
        }
        private bool m_ReadImmediate;

        #endregion

        #region "Helper functions"

        /// <summary>
        /// Validate there are no bit conflicts for current config. This function should be
        /// called before any actual DUT interfacing functions are called.
        /// </summary>
        private void ValidateSettings()
        {
            
        }

        private uint BuildReadRequest(uint addr)
        {
            return 0;
        }

        private uint BuildWriteRequest(uint writeData, uint addr)
        {
            return 0;
        }

        #endregion

        #region "IRegInterface Implementation"
        ushort IRegInterface.BurstMode
        {
            get
            {
                throw new NotImplementedException("Burst not supported for generic IRegInterface");
            }

            set
            {
                throw new NotImplementedException("Burst not supported for generic IRegInterface");
            }
        }

        /// <summary>
        /// Data ready triggering. Used data ready assigned in the underlying FX3 object
        /// </summary>
        bool IRegInterface.DrActive
        {
            get
            {
                return m_FX3.DrActive;
            }

            set
            {
                m_FX3.DrActive = true;
            }
        }

        int IRegInterface.StreamTimeoutSeconds
        {
            get
            {
                throw new NotImplementedException();
            }

            set
            {
                throw new NotImplementedException();
            }
        }

        ushort[] IRegInterface.GetBufferedStreamDataPacket()
        {
            return m_FX3.GetBufferedStreamDataPacket();
        }

        ushort[] IRegInterface.GetStreamDataPacketU16()
        {
            throw new NotImplementedException();
        }

        ushort[] IRegInterface.ReadRegArray(IEnumerable<uint> addr)
        {
            throw new NotImplementedException();
        }

        ushort[] IRegInterface.ReadRegArray(IEnumerable<AddrDataPair> addrData, uint numCaptures)
        {
            throw new NotImplementedException();
        }

        ushort[] IRegInterface.ReadRegArray(IEnumerable<uint> addr, uint numCaptures)
        {
            throw new NotImplementedException();
        }

        ushort[] IRegInterface.ReadRegArrayStream(IEnumerable<AddrDataPair> addrData, uint numCaptures, uint numBuffers)
        {
            throw new NotImplementedException();
        }

        ushort[] IRegInterface.ReadRegArrayStream(IEnumerable<uint> addr, uint numCaptures, uint numBuffers)
        {
            throw new NotImplementedException();
        }

        ushort IRegInterface.ReadRegByte(uint addr)
        {
            throw new NotImplementedException();
        }

        ushort IRegInterface.ReadRegWord(uint addr)
        {
            throw new NotImplementedException();
        }

        void IRegInterface.Reset()
        {
            throw new NotImplementedException();
        }

        void IRegInterface.Start()
        {
            throw new NotImplementedException();
        }

        void IRegInterface.StartBufferedStream(IEnumerable<AddrDataPair> addrData, uint numCaptures, uint numBuffers, int timeoutSeconds, BackgroundWorker worker)
        {
            throw new NotImplementedException();
        }

        void IRegInterface.StartBufferedStream(IEnumerable<uint> addr, uint numCaptures, uint numBuffers, int timeoutSeconds, BackgroundWorker worker)
        {
            throw new NotImplementedException();
        }

        void IRegInterface.StartStream(IEnumerable<uint> addr, uint numCaptures)
        {
            throw new NotImplementedException();
        }

        void IRegInterface.StopStream()
        {
            throw new NotImplementedException();
        }

        void IRegInterface.WriteRegByte(AddrDataPair addrData)
        {
            throw new NotImplementedException();
        }

        void IRegInterface.WriteRegByte(IEnumerable<AddrDataPair> addrData)
        {
            throw new NotImplementedException();
        }

        void IRegInterface.WriteRegByte(uint addr, uint data)
        {
            throw new NotImplementedException();
        }

        void IRegInterface.WriteRegByte(IEnumerable<uint> addr, IEnumerable<uint> data)
        {
            throw new NotImplementedException();
        }

        void IRegInterface.WriteRegWord(uint addr, uint data)
        {
            throw new NotImplementedException();
        }

        #endregion
    }
}
