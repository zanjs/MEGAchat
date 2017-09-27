#include "net/libwsIO.h"
#include <arpa/inet.h>
#include <libws_log.h>
#include "base/gcmpp.h"

#include "waiter/libeventWaiter.h"

using namespace std;
namespace ws
{
LibwsIO::LibwsIO(::mega::Mutex *mutex, void *ctx) : IO(mutex, ctx)
{}

LibwsIO::~LibwsIO()
{
    // ws_global_destroy() is not consistent with ws_global_init()
    // ws_global_init() expect wscontext to be externally allocated, but
    // ws_global_destroy() expect to delete it using free()
}

void LibwsIO::addevents(::mega::Waiter* waiter, int)
{
    assert(!mIsInitialized);
    ::mega::LibeventWaiter *libeventWaiter = dynamic_cast<::mega::LibeventWaiter *>(waiter);
    ws_global_init(&wscontext, libeventWaiter ? libeventWaiter->eventloop : services_get_event_loop(), NULL,
    [](struct bufferevent* bev, void* userp)
    {
        karere::marshallCall([bev, userp]()
        {
            ws_read_callback(bev, userp);
        }, NULL);
    },
    [](struct bufferevent* bev, short events, void* userp)
    {
        karere::marshallCall([bev, events, userp]()
        {
            ws_event_callback(bev, events, userp);
        }, NULL);
    },
    [](int fd, short events, void* userp)
    {
        karere::marshallCall([events, userp]()
        {
            ws_handle_marshall_timer_cb(0, events, userp);
        }, NULL);
    });
    //ws_set_log_level(LIBWS_TRACE);
    mIsInitialized = true;
}

Socket *LibwsIO::wsConnect(Client& client, const char *ip, const char *host, int port, const char *path, bool ssl, EventHandler *handler)
{
    if (!mIsInitialized)   // check required for compatibility with Qt app, which is not initialized by default
    {
        addevents(NULL, 0);
    }

    int result;
    std::unique_ptr<Socket> socket(new Socket(client, handler));
    
    result = ws_init(&socket->mWebSocket, &wscontext);
    if (result)
    {        
        WEBSOCKETS_LOG_DEBUG("Failed to initialize libws at wsConnect()");
        return NULL;
    }
    
    ws_set_onconnect_cb(libwsClient->mWebSocket, &LibwsClient::websockConnectCb, libwsClient);
    ws_set_onclose_cb(libwsClient->mWebSocket, &LibwsClient::websockCloseCb, libwsClient);
    ws_set_onmsg_cb(libwsClient->mWebSocket, &LibwsClient::websockMsgCb, libwsClient);
    
    if (ssl)
    {
        ws_set_ssl_state(libwsClient->mWebSocket, LIBWS_SSL_ON);
    }
    
    if (ip[0] == '[')
    {
        string ipv6 = ip;
        struct sockaddr_in6 ipv6addr = { 0 };
        ipv6 = ipv6.substr(1, ipv6.size() - 2);
        ipv6addr.sin6_family = AF_INET6;
        ipv6addr.sin6_port = htons(port);
        inet_pton(AF_INET6, ipv6.c_str(), &ipv6addr.sin6_addr);
        result = ws_connect_addr(libwsClient->mWebSocket, host,
                        (struct sockaddr *)&ipv6addr, sizeof(ipv6addr),
                        port, path);
    }
    else
    {
        struct sockaddr_in ipv4addr = { 0 };
        ipv4addr.sin_family = AF_INET;
        ipv4addr.sin_port = htons(port);
        inet_pton(AF_INET, ip, &ipv4addr.sin_addr);
        result = ws_connect_addr(libwsClient->mWebSocket, host,
                        (struct sockaddr *)&ipv4addr, sizeof(ipv4addr),
                        port, path);
    }
    
    if (result)
    {
        WEBSOCKETS_LOG_DEBUG("Failed to connect with libws");
        return NULL;
    }
    return socket.release();
}

LibwsSocket::LibwsSocket(Client& client, EventHandler *handler)
: Socket(client, handler)
{}

LibwsSocket::~LibwsSocket()
{
    disconnect(true);
}

void LibwsSocket::connectCb(ws_t ws, void* arg)
{
    LibwsSocket* self = static_cast<LibwsSocket*>(arg);
    assert (ws == self->mWebSocket);

    auto wptr = self->getDelTracker();
    karere::marshallCall([self, wptr]()
    {
        if (wptr.deleted())
            return;

        self->wsConnectCb();
    }, self->mAppCtx);
}

void LibwsClient::closeCb(ws_t ws, int errcode, int errtype, const char *preason, size_t reason_len, void *arg)
{
    LibwsSocket* self = static_cast<LibwsSocket*>(arg);
    assert (ws == self->mWebSocket);

    std::string reason;
    if (preason)
        reason.assign(preason, reason_len);

    auto wptr = self->getDelTracker();
    karere::marshallCall([self, wptr, reason, errcode, errtype]()
    {
        if (wptr.deleted())
            return;
        
        if (self->mWebSocket)
        {
            ws_destroy(&self->mWebSocket);
        }
        
        self->wsCloseCb(errcode, errtype, reason.data(), reason.size());
    }, self->mAppCtx);
}

void LibwsSocket::websockMsgCb(ws_t ws, char *msg, uint64_t len, int binary, void *arg)
{
    LibwsSocket* self = static_cast<LibwsSocket*>(arg);
    assert (ws == self->mWebSocket);

    string data;
    data.assign(msg, (size_t)len);
    self->wsHandleMsgCb(data);
}
                         
bool LibwsClient::sendMessage(char *msg, size_t len)
{    
    if (!mWebSocket)
    {
        return false;
    }
    
    return !ws_send_msg_ex(mWebSocket, msg, len, 1);
}

void LibwsClient::disconnect(bool immediate)
{
    if (!mWebSocket)
    {
        return;
    }
    
    if (immediate)
    {
        ws_close_immediately(mWebSocket);
        ws_destroy(&mWebSocket);
        assert(!mWebSocket);
    }
    else
    {
        ws_close(mWebSocket);
    }
}

bool LibwsSocket::isConnected()
{
    return mWebSocket;
}

