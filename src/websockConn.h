#ifndef WEBSOCK_CONN_H
#define WEBSOCK_CONN_H

#include <libws.h>
#include <trackDelete.h>
#include <url.h>
#include <promise.h>
#include <logger.h>

class StaticBuffer;
class Buffer;
enum { kPromiseErrtype_websocket = 0x3e9a3eb5 }; //megawebs
namespace ws
{
class Client: public karere::DeleteTrackable
{
public:
    enum ConnState { kDisconnected = 0, kDisconnecting, kConnecting, kConnected, kLoggedIn };
    class ConnStateListener
    {
    public:
        virtual void onConnStateChange(ConnState newState) {}
    };
private:
    ConnState mConnState = kDisconnected;
protected:
    static ws_base_s gContext;
    static bool gContextInitialized;
    ws_t mWebSocket = nullptr;
    ConnStateListener& mConnStateListener;
    karere::Url mUrl;
    krLogChannelNo mLogChan;
    std::string mName;
    promise::Promise<void> mConnectPromise;
    promise::Promise<void> mDisconnectPromise;
    promise::Promise<void> mLoginPromise;
    bool mKeepaliveEnabled = false;
    void setConnState(ConnState newState);
    void initWebsocketCtx();
    static void websockConnectCb(ws_t ws, void* arg);
    static void websockCloseCb(ws_t ws, int errcode, int errtype, const char *reason,
        size_t reason_len, void *arg);
    static void websockMsgCb(ws_t ws, char *msg, uint64_t len, int binary, void *arg);
    void onSocketClose(int ercode, int errtype, const std::string& reason);
public:
    Client(ConnStateListener& listener, krLogChannelNo logChan, const std::string& name=std::string());
    ConnState connState() const { return mConnState; }
    bool isOnline() const { return mConnState >= kConnected; }
    void setUrl(const std::string& url) { mUrl.parse(url); }
    promise::Promise<void> reconnect(const std::string& url=std::string());
    bool sendBuf(Buffer&& buf);
    promise::Promise<void> disconnect(int timeoutMs=2000);
    promise::Promise<void> retryPendingConnect();
    void reset(const std::string &msg=std::string());
    void notifyLoggedIn();
    void notifyDisconnected();
    void checkEnableKeepalive();
    void checkDisableKeepalive();
    static const char* connStateToStr(ConnState state);
    const char* connStateStr() const { return connStateToStr(mConnState); }
    virtual void onState(ConnState state) {}
    virtual void onMessage(const StaticBuffer& msg) {}
    virtual void onConnect() = 0;
    virtual void onDisconnect() {}
    virtual void disableKeepalive() {}
    virtual void enableKeepalive() {}
};
}
#endif
