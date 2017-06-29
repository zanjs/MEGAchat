#ifndef WEBSOCK_CONN_H
#define WEBSOCK_CONN_H

#include <libws.h>

class Buffer;
namespace ws
{
class Client
{
public:
    enum State { kDisconnected = 0, kConnecting, kDisconnecting, kConnected, kLoggedIn };
protected:
    static ws_base_s gContext;
    static bool gContextInitialized;
    ws_t mWebSocket = nullptr;
    State mState;
    promise::Promise<void> mConnectPromise;
    promise::Promise<void> mDisconnectPromise;
    void setState(State newState);
    void initWebsocketCtx();
    static void websockConnectCb(ws_t ws, void* arg);
    static void websockCloseCb(ws_t ws, int errcode, int errtype, const char *reason,
        size_t reason_len, void *arg);
    static void websockMsgCb(ws_t ws, char *msg, uint64_t len, int binary, void *arg);
    void onSocketClose(int ercode, int errtype, const std::string& reason);
public:
    promise::Promise<void> reconnect(const std::string& url=std::string());
    bool sendBuf(Buffer&& buf);
    promise::Promise<void> disconnect();
    void retryPendingConnections();
    void reset();
    virtual void onState(State state) {}
    virtual void onMessage(const StaticBuffer& msg) {}
    virtual void onConnected() {}
    virtual void onDisconnected() {}
};
}
#endif
