#ifndef libwebsocketsIO_h
#define libwebsocketsIO_h

#include <libwebsockets.h>
#include <openssl/ssl.h>
#include <iostream>

#include "websocketsIO.h"
namespace libwebsockets
{
// Websockets network layer implementation based on libwebsocket
class IO : public ws::IO
{
public:
    struct lws_context *wscontext;
    IO(::mega::Mutex *mutex, karere::AppCtx& ctx);
    virtual ~IO();
    virtual void registerWithEventLoop(void* eventloop);
// mega::EventTrigger interface
    virtual void addevents(::mega::Waiter*, int);
//==
protected:
    virtual ws::Socket *connect(ws::wsClient& client, const char *ip, const char *host,
        int port, const char *path, bool ssl, ws::EventHandler *handler);
};

class Socket : public ws::Socket
{
public:
    using ws::Socket::Socket;
    virtual ~Socket();
    
protected:
    std::string recbuffer;
    std::string sendbuffer;
    bool disconnecting = false;

    void appendMessageFragment(char *data, size_t len, size_t remaining);
    bool hasFragments();
    const char *getMessage();
    size_t getMessageLength();
    void resetMessage();
    const char *getOutputBuffer();
    size_t getOutputBufferLength();
    void resetOutputBuffer();
    
    virtual bool sendMessage(char *msg, size_t len);
    virtual void disconnect(bool immediate);
    virtual bool isConnected();
    
public:
    struct lws *wsi = nullptr;
    static int wsCallback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *data, size_t len);
};
}


#endif /* libwebsocketsIO_h */
