/*
 * Vita network — initial scaffold.
 *
 * STATUS: stubs. The Vita exposes BSD sockets via vitasdk's libc shim but
 * SceNet must be initialised first (SceSysmodule + sceNetInit with a memory
 * pool). Once that is done, most of LinuxNetwork.cpp can be adapted almost
 * verbatim — sockets, getaddrinfo, select, recv/send all map across.
 *
 * Initialise SceNet from VitaCore.cpp::PlatformInitNonGraphicsCommon() once,
 * then port LinuxNetwork.cpp method-by-method here.
 */

#include "agk.h"
#include "VitaNetwork.h"

namespace AGK
{
    /* AGKSocketTimeout */
    AGKSocketTimeout::AGKSocketTimeout()           { m_pSocket = 0; m_iTimeout = 0; }
    AGKSocketTimeout::~AGKSocketTimeout()          { Stop(); }
    void AGKSocketTimeout::SetData( AGKSocket *p, UINT t ) { m_pSocket = p; m_iTimeout = t; }
    UINT AGKSocketTimeout::Run()                   { return 0; }

    /* AGKSocket */
    AGKSocket::AGKSocket()                         { Reset(); }
    AGKSocket::AGKSocket( int s )                  { Reset(); m_client = s; m_bConnected = true; }
    AGKSocket::~AGKSocket()                        { Close( false ); }

    void AGKSocket::Reset()
    {
        m_pNext = 0;
        m_client = -1;
        m_szIP[ 0 ] = 0;
        m_port = 0;
        m_bConnected = false;
        m_bConnecting = false;
        m_bDisconnected = false;
        m_iTimeout = 0;
        m_bASync = false;
        m_fProgress = 0;
        m_bResult = false;
        m_iSendWritePtr = 0;
    }

    UINT AGKSocket::Run()                          { return 0; }
    bool AGKSocket::Flush()                        { return true; }
    void AGKSocket::Close( bool )                  { m_bConnected = false; m_bDisconnected = true; }
    void AGKSocket::ForceClose()                   { Close( false ); }
    bool AGKSocket::Connect( const char*, UINT, UINT )      { return false; }
    bool AGKSocket::ConnectASync( const char*, UINT, UINT ) { return false; }
    bool AGKSocket::SendFile( const char* )                 { return false; }
    bool AGKSocket::SendData( const char*, int )            { return false; }
    bool AGKSocket::SendString( const char* )               { return false; }
    bool AGKSocket::SendChar( char )                        { return false; }
    bool AGKSocket::SendUInt( UINT )                        { return false; }
    bool AGKSocket::SendInt( int )                          { return false; }
    bool AGKSocket::SendFloat( float )                      { return false; }
    int  AGKSocket::GetBytes()                              { return 0; }
    int  AGKSocket::RecvData( char*, int )                  { return 0; }
    int  AGKSocket::RecvString( uString& )                  { return 0; }
    char AGKSocket::RecvChar()                              { return 0; }
    int  AGKSocket::RecvInt()                               { return 0; }
    UINT AGKSocket::RecvUInt()                              { return 0; }
    float AGKSocket::RecvFloat()                            { return 0.0f; }

    /* UDPManager */
    UDPManager::UDPManager( const char*, UINT ) : m_socket(-1), m_port(0), m_iIPv6(0), m_iValid(0) {}
    UDPManager::~UDPManager()                                       {}
    bool UDPManager::SendPacket( const char*, UINT, const AGKPacket* ) { return false; }
    bool UDPManager::RecvPacket( char*, int*, AGKPacket* )             { return false; }
    bool UDPManager::PacketReady()                                  { return false; }

    /* cNetworkListener */
    cNetworkListener::cNetworkListener() : m_socket(-1), m_port(0), m_pConnections(0) {}
    cNetworkListener::~cNetworkListener()                                 { Stop(); }
    AGKSocket* cNetworkListener::GetNewConnection()                       { return 0; }
    int cNetworkListener::AcceptConnections( UINT )                       { return 0; }
    int cNetworkListener::AcceptConnections( const char*, UINT )          { return 0; }
    void cNetworkListener::Stop()                                         {}
    UINT cNetworkListener::Run()                                          { return 0; }

    /* BroadcastListener */
    BroadcastListener::BroadcastListener() : m_socket(-1)             { memset(&addr, 0, sizeof(addr)); }
    BroadcastListener::~BroadcastListener()                           {}
    void BroadcastListener::Close()                                   {}
    bool BroadcastListener::SetListenPort( const char*, UINT )        { return false; }
    bool BroadcastListener::SetListenPort( UINT )                     { return false; }
    bool BroadcastListener::ReceivedBroadcast()                       { return false; }
    bool BroadcastListener::GetPacket( AGKPacket&, UINT&, char* )     { return false; }

    /* Broadcaster */
    Broadcaster::Broadcaster() : m_interval(0), m_max(0), m_port(0), m_ipv6(0) {}
    void Broadcaster::SetData( int, UINT, const AGKPacket*, UINT, int )       {}
    UINT Broadcaster::Run()                                                    { return 0; }

    /* cHTTPConnection */
    cHTTPConnection::cHTTPConnection()
        : request(0), m_iSecure(0), m_iTimeout(0), m_iVerifyMode(1),
          m_bConnected(false), m_fProgress(0), m_iStatusCode(0),
          m_bSaveToFile(false), m_pFile(0), m_iReceived(0), m_iSent(0),
          m_iSendLength(0), m_pUploadFile(0), m_bFailed(false)
    {
        m_szContentType[0] = 0;
    }
    cHTTPConnection::~cHTTPConnection()                                            { Close(); }
    int  cHTTPConnection::RecvData( void*, int )                                   { return 0; }
    int  cHTTPConnection::SendData( void*, int )                                   { return 0; }
    void cHTTPConnection::Stop()                                                   {}
    bool cHTTPConnection::SetHost( const char*, int, const char*, const char* )    { return false; }
    void cHTTPConnection::Close()                                                  { m_bConnected = false; }
    void cHTTPConnection::SetTimeout( int t )                                      { m_iTimeout = t; }
    void cHTTPConnection::SetVerifyCertificate( int m )                            { m_iVerifyMode = m; }
    void cHTTPConnection::AddHeader( const char*, const char* )                    {}
    void cHTTPConnection::RemoveHeader( const char* )                              {}
    char* cHTTPConnection::SendRequest( const char*, const char* )                 { return 0; }
    bool cHTTPConnection::SendRequestASync( const char*, const char* )             { return false; }
    bool cHTTPConnection::SendFile( const char*, const char*, const char* )        { return false; }
    int  cHTTPConnection::GetResponseReady()                                       { return 0; }
    const char* cHTTPConnection::GetResponse()                                     { return ""; }
    const char* cHTTPConnection::GetContentType()                                  { return m_szContentType; }
    bool cHTTPConnection::DownloadFile( const char*, const char*, const char* )    { return false; }
    bool cHTTPConnection::DownloadComplete()                                       { return false; }
    void cHTTPConnection::SendRequestInternal()                                    {}
    void cHTTPConnection::SendFileInternal()                                       {}
    UINT cHTTPConnection::Run()                                                    { return 0; }
}
