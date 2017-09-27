#ifndef websocketsIO_h
#define websocketsIO_h

#include <iostream>
//on MacOS, a system header defined a macro named 'verify', which conflicts with a function defined in a libsodium header, included by mega headers, so we need to undefine it before including mega headers.
//TODO: Maybe #undef verify in mega headers before inclusion of libsodium (at least on MacOS)
#undef verify
#include <../base/appCtx.h>
#include <mega/thread.h> //for mega::Mutex
#include <logger.h>

#define WEBSOCKETS_LOG_DEBUG(fmtString,...) KARERE_LOG_DEBUG(krLogChannel_websockets, fmtString, ##__VA_ARGS__)
#define WEBSOCKETS_LOG_INFO(fmtString,...) KARERE_LOG_INFO(krLogChannel_websockets, fmtString, ##__VA_ARGS__)
#define WEBSOCKETS_LOG_WARNING(fmtString,...) KARERE_LOG_WARNING(krLogChannel_websockets, fmtString, ##__VA_ARGS__)
#define WEBSOCKETS_LOG_ERROR(fmtString,...) KARERE_LOG_ERROR(krLogChannel_websockets, fmtString, ##__VA_ARGS__)

namespace ws
{
class EventHandler;
class Socket;
class wsClient;

// Generic websockets network layer (client factory)
class IO : public mega::EventTrigger, karere::AppCtxRef
{
public:
    IO(::mega::Mutex *mutex, karere::AppCtx& ctx);
    virtual ~IO();
    virtual void registerWithEventLoop(void* eventloop) = 0;
protected:
    ::mega::Mutex *mMutex;
    bool mIsInitialized = false;
    // This function is protected to prevent a wrong direct usage
    // It must be only used from wsClient
    virtual Socket *connect(wsClient& client, const char *ip, const char *host, int port,
            const char *path, bool ssl, EventHandler *handler) = 0;
    friend wsClient;
};

/** @brief Event handler interface that receives events from a websocket client
 *  It's needed to implement this interface in order to receive callbacks
 */
class EventHandler
{
public:
    virtual void wsConnectCb() = 0;
    virtual void wsCloseCb(int errcode, int errtype, const std::string& reason) = 0;
    virtual void wsHandleMsgCb(std::string&& data) = 0;
};

/** @brief Abstract class that allows to manage a websocket connection.
 */
class wsClient: public EventHandler, public karere::AppCtxRef
{
protected:
    Socket* mSocket;
    std::thread::id mThreadId;
    friend class Socket; //needs access to mThreadId
public:
    wsClient(karere::AppCtx& ctx);
    bool wsConnect(IO *io, const char *ip,
                   const char *host, int port, const char *path, bool ssl);
    bool wsSendMessage(char *msg, size_t len);  // returns true on success, false if error
    void wsDisconnect(bool immediate);
    bool wsIsConnected();
};

class Socket: public karere::DeleteTrackable
{
protected:
    wsClient& mClient;
    EventHandler *mHandler;
    ::mega::Mutex *mMutex;
public:
    Socket(wsClient& client, EventHandler *handler);
    void wsConnectCb();
    void wsCloseCb(int errcode, int errtype, const char *preason, size_t reason_len);
    void wsHandleMsgCb(std::string&& data);
    
    virtual bool sendMessage(char *msg, size_t len) = 0;
    virtual void disconnect(bool immediate) = 0;
    virtual bool isConnected() = 0;
};
}
#ifdef KR_USE_LIBWEBSOCKETS
#include "libwebsocketsIO.h"
#else
#include "libwsIO.h"
#endif

#endif /* websocketsIO_h */
