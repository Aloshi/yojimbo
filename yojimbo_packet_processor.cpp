/*
    Yojimbo Client/Server Network Library.

    Copyright © 2016, The Network Protocol Company, Inc.
    
    All rights reserved.
*/

#include "yojimbo_packet_processor.h"
#include "yojimbo_protocol.h"
#include "yojimbo_crypto.h"
#include "yojimbo_util.h"
#include <stdio.h>
#include <sodium.h>

namespace yojimbo
{
    PacketProcessor::PacketProcessor( PacketFactory & packetFactory, uint32_t protocolId, int maxPacketSize, void * context )
    {
        m_packetFactory = &packetFactory;

        m_protocolId = protocolId;

        m_error = PACKET_PROCESSOR_ERROR_NONE;

        m_maxPacketSize = maxPacketSize + ( ( maxPacketSize % 4 ) ? ( 4 - ( maxPacketSize % 4 ) ) : 0 );
        
        assert( m_maxPacketSize % 4 == 0 );
        assert( m_maxPacketSize >= maxPacketSize );

        const int MaxPrefixBytes = 9;
        const int CryptoOverhead = MacBytes;

        m_absoluteMaxPacketSize = maxPacketSize + MaxPrefixBytes + CryptoOverhead;

        m_context = context;

        m_packetBuffer = new uint8_t[m_absoluteMaxPacketSize];
        m_scratchBuffer = new uint8_t[m_absoluteMaxPacketSize];
    }

    PacketProcessor::~PacketProcessor()
    {
        delete [] m_packetBuffer;
        delete [] m_scratchBuffer;

        m_packetBuffer = NULL;
        m_scratchBuffer = NULL;
    }

    static const int ENCRYPTED_PACKET_FLAG = (1<<7);

    const uint8_t * PacketProcessor::WritePacket( Packet * packet, uint64_t sequence, int & packetBytes, bool encrypt, const uint8_t * key )
    {
        m_error = PACKET_PROCESSOR_ERROR_NONE;

        if ( encrypt )
        {
            if ( !key )
            {
                m_error = PACKET_PROCESSOR_ERROR_KEY_IS_NULL;
                return NULL;
            }

            int prefixBytes;
            CompressPacketSequence( sequence, m_scratchBuffer[0], prefixBytes, m_scratchBuffer+1 );
            m_scratchBuffer[0] |= ENCRYPTED_PACKET_FLAG;
            prefixBytes++;

            PacketInfo info;
            info.context = m_context;
            info.protocolId = m_protocolId;
            info.packetFactory = m_packetFactory;
//            info.rawFormat = 1;

            packetBytes = yojimbo::WritePacket( info, packet, m_packetBuffer, m_maxPacketSize );
            if ( packetBytes <= 0 )
            {
                m_error = PACKET_PROCESSOR_ERROR_WRITE_PACKET_FAILED;
                return NULL;
            }

            assert( packetBytes <= m_maxPacketSize );

            int encryptedPacketSize;

            if ( !Encrypt( m_packetBuffer,
                           packetBytes,
                           m_scratchBuffer + prefixBytes,
                           encryptedPacketSize, 
                           (uint8_t*) &sequence, key ) )
            {
                m_error = PACKET_PROCESSOR_ERROR_ENCRYPT_FAILED;
                return NULL;
            }
            
            packetBytes = prefixBytes + encryptedPacketSize;

            assert( packetBytes <= m_absoluteMaxPacketSize );

            return m_scratchBuffer;
        }
        else
        {
            PacketInfo info;
            info.context = m_context;
            info.protocolId = m_protocolId;
            info.packetFactory = m_packetFactory;
            info.prefixBytes = 1;

            packetBytes = yojimbo::WritePacket( info, packet, m_packetBuffer, m_maxPacketSize );

            if ( packetBytes <= 0 )
            {
                m_error = PACKET_PROCESSOR_ERROR_WRITE_PACKET_FAILED;
                return NULL;
            }

            assert( packetBytes <= m_maxPacketSize );

            return m_packetBuffer;
        }
    }

    Packet * PacketProcessor::ReadPacket( const uint8_t * packetData, uint64_t & sequence, int packetBytes, bool & encrypted,  
                                          const uint8_t * key, const uint8_t * encryptedPacketTypes, const uint8_t * unencryptedPacketTypes )
    {
        m_error = PACKET_PROCESSOR_ERROR_NONE;

        const uint8_t prefixByte = packetData[0];

        encrypted = ( prefixByte & ENCRYPTED_PACKET_FLAG ) != 0;

        if ( encrypted )
        {
            if ( !key )
            {
                m_error = PACKET_PROCESSOR_ERROR_KEY_IS_NULL;
                return NULL;
            }

            const int sequenceBytes = GetPacketSequenceBytes( prefixByte );

            const int prefixBytes = 1 + sequenceBytes;

            if ( packetBytes <= prefixBytes + MacBytes )
            {
                m_error = PACKET_PROCESSOR_ERROR_PACKET_TOO_SMALL;
                return NULL;
            }

            sequence = DecompressPacketSequence( prefixByte, packetData + 1 );

            int decryptedPacketBytes;

            if ( !Decrypt( packetData + prefixBytes, packetBytes - prefixBytes, m_scratchBuffer, decryptedPacketBytes, (uint8_t*)&sequence, key ) )
            {
                m_error = PACKET_PROCESSOR_ERROR_DECRYPT_FAILED;
                return NULL;
            }

            PacketInfo info;
            info.context = m_context;
            info.protocolId = m_protocolId;
            info.packetFactory = m_packetFactory;
            info.allowedPacketTypes = encryptedPacketTypes;

            int readError;
            
            Packet * packet = yojimbo::ReadPacket( info, m_scratchBuffer, decryptedPacketBytes, NULL, &readError );

            if ( !packet )
            {
                m_error = PACKET_PROCESSOR_ERROR_READ_PACKET_FAILED;
                return NULL;
            }
            
            return packet;
        }
        else
        {
            PacketInfo info;
            info.context = m_context;
            info.protocolId = m_protocolId;
            info.packetFactory = m_packetFactory;
            info.allowedPacketTypes = unencryptedPacketTypes;
            info.prefixBytes = 1;

            sequence = 0;
            
            int readError;

            Packet * packet = yojimbo::ReadPacket( info, packetData, packetBytes, NULL, &readError );

            if ( !packet )
            {
                m_error = PACKET_PROCESSOR_ERROR_READ_PACKET_FAILED;
                return NULL;
            }
            
            return packet;
        }
    }
}
