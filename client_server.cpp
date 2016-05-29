/*
    Example source code for "Securing Dedicated Servers"

    Copyright © 2016, The Network Protocol Company, Inc.
    
    All rights reserved.
*/

#include "yojimbo.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <map>

using namespace std;
using namespace yojimbo;

const uint32_t ProtocolId = 0x12341651;

const int ClientPort = 40000;
const int ServerPort = 50000;

static uint8_t private_key[KeyBytes];

struct MatcherServerData
{
    Address serverAddress;                              // IP address of this server

    int numConnectedClients;                            // number of connected clients on this dedi

    uint64_t connectedClients[MaxClients];              // client ids connected to this server (tight array)
};

class Matcher
{
    uint64_t m_nonce;                                   // increments with each match request

    map<Address,MatcherServerData*> m_serverMap;        // maps network address to data for that server

public:

    Matcher()
    {
        m_nonce = 0;
    }

    bool RequestMatch( uint64_t clientId, uint8_t * tokenData, uint8_t * tokenNonce, uint8_t * clientToServerKey, uint8_t * serverToClientKey, int & numServerAddresses, Address * serverAddresses )
    {
        if ( clientId == 0 )
            return false;

        numServerAddresses = 1;
        serverAddresses[0] = Address( "::1", ServerPort );

        ConnectToken token;
        GenerateConnectToken( token, clientId, numServerAddresses, serverAddresses, ProtocolId );

        memcpy( clientToServerKey, token.clientToServerKey, KeyBytes );
        memcpy( serverToClientKey, token.serverToClientKey, KeyBytes );

        if ( !EncryptConnectToken( token, tokenData, NULL, 0, (const uint8_t*) &m_nonce, private_key ) )
            return false;

        assert( NonceBytes == 8 );

        memcpy( tokenNonce, &m_nonce, NonceBytes );

        m_nonce++;

        return true;
    }
};

class ClientServerNetworkInterface : public SocketInterface
{   
public:

    ClientServerNetworkInterface( ClientServerPacketFactory & packetFactory, uint16_t port ) : SocketInterface( memory_default_allocator(), packetFactory, ProtocolId, port )
    {
        EnablePacketEncryption();
        DisableEncryptionForPacketType( PACKET_CONNECTION_REQUEST );
    }

    ~ClientServerNetworkInterface()
    {
        ClearSendQueue();
        ClearReceiveQueue();
    }
};

int main()
{
    printf( "\nclient/server\n" );

    if ( !InitializeYojimbo() )
    {
        printf( "error: failed to initialize Yojimbo!\n" );
        return 1;
    }

    memory_initialize();
    {
        srand( (unsigned int) time( NULL ) );

        Matcher matcher;

        uint64_t clientId = 1;

        uint8_t connectTokenData[ConnectTokenBytes];
        uint8_t connectTokenNonce[NonceBytes];

        uint8_t clientToServerKey[KeyBytes];
        uint8_t serverToClientKey[KeyBytes];

        int numServerAddresses;
        Address serverAddresses[MaxServersPerConnectToken];

        memset( connectTokenNonce, 0, NonceBytes );

        printf( "requesting match\n\n" );

        GenerateKey( private_key );

        if ( !matcher.RequestMatch( clientId, connectTokenData, connectTokenNonce, clientToServerKey, serverToClientKey, numServerAddresses, serverAddresses ) )
        {
            printf( "error: request match failed\n" );
            return 1;
        }

        InitializeNetwork();

        ClientServerPacketFactory packetFactory;

        Address clientAddress( "::1", ClientPort );
        Address serverAddress( "::1", ServerPort );

        ClientServerNetworkInterface clientInterface( packetFactory, ClientPort );
        ClientServerNetworkInterface serverInterface( packetFactory, ServerPort );

        if ( clientInterface.GetError() != SOCKET_ERROR_NONE || serverInterface.GetError() != SOCKET_ERROR_NONE )
        {
            printf( "error: failed to initialize sockets\n" );
            return 1;
        }
        
        const int NumIterations = 20;

        double time = 0.0;

        Client client( clientInterface );

        Server server( serverInterface );

        server.SetPrivateKey( private_key );
        
        server.SetServerAddress( serverAddress );
        
        client.Connect( serverAddress, time, clientId, connectTokenData, connectTokenNonce, clientToServerKey, serverToClientKey );

        printf( "----------------------------------------------------------\n" );

        for ( int i = 0; i < NumIterations; ++i )
        {
            printf( "t = %f\n", time );

            client.SendPackets( time );
            server.SendPackets( time );

            clientInterface.WritePackets( time );
            serverInterface.WritePackets( time );

            clientInterface.ReadPackets( time );
            serverInterface.ReadPackets( time );

            client.ReceivePackets( time );
            server.ReceivePackets( time );

            client.CheckForTimeOut( time );
            server.CheckForTimeOut( time );

            if ( client.ConnectionFailed() )
            {
                printf( "error: client connect failed!\n" );
                break;
            }

            if ( client.IsConnected() )
                client.Disconnect( time );

            time += 0.1f;

            printf( "----------------------------------------------------------\n" );

            if ( !client.IsConnecting() && !client.IsConnected() && server.GetNumConnectedClients() )
                break;
        }

        ShutdownNetwork();
    }

    memory_shutdown();

    ShutdownYojimbo();

    printf( "\n" );

    return 0;
}
