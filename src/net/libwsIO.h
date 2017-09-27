#ifndef libwsIO_h
#define libwsIO_h

#include <libws.h>
#include "net/websocketsIO.h"
#include "trackDelete.h"
#include <mega/waiter.h>
namespace ws
{
// Websockets network layer implementation based on libws
class LibwsIO : public IO
{
public:
    LibwsIO(::mega::Mutex *mutex = NULL, void *ctx = NULL);
    virtual ~LibwsIO();
    
    virtual void addevents(::mega::Waiter*, int);

protected:
    bool mIsInitialized = false;
    ws_base_s mWsContext;
    virtual Socket *connect(Client& client, const char *ip, const char *host,
                                           int port, const char *path, bool ssl,
                                           EventHandler *handler);
};

class Socket : public ws::Socket, public karere::DeleteTrackable
{
public:
    ws_t mWebSocket = nullptr;
    void *mAppCtx;

    static void websockConnectCb(ws_t ws, void* arg);
    static void websockCloseCb(ws_t ws, int errcode, int errtype, const char *reason, size_t reason_len, void *arg);
    static void websockMsgCb(ws_t ws, char *msg, uint64_t len, int binary, void *arg);
    
    Socket(Client& client, EventHandler *handler);
    virtual ~Socket();
    
    virtual bool sendMessage(char *msg, size_t len);
    virtual void disconnect(bool immediate);
    virtual bool isConnected();
};

#endif /* libwsIO_h */
