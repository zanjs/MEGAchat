#include <base/cservices.h>
#include <gcmpp.h>
#include <retryHandler.h>
#include <libws_log.h>
#include <event2/dns.h>
#include <event2/dns_compat.h>
#include "websockConn.h"
#include <buffer.h>
#ifdef __ANDROID__
    #include <sys/system_properties.h>
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #ifdef TARGET_OS_IPHONE
        #include <resolv.h>
    #endif
#endif

using namespace std;
using namespace promise;
using namespace karere;

#define WS_LOG_DEBUG(fmtString,...) mName.empty() ? KARERE_LOG_DEBUG(mLogChan, fmtString, ##__VA_ARGS__) : KARERE_LOG_DEBUG(mLogChan, "%s: " fmtString, mName.c_str(), ##__VA_ARGS__)
#define WS_LOG_WARNING(fmtString,...) mName.empty() ? KARERE_LOG_WARNING(mLogChan, fmtString, ##__VA_ARGS__) : KARERE_LOG_WARNING(mLogChan, "%s: " fmtString, mName.c_str(), ##__VA_ARGS__)
#define WS_LOG_ERROR(fmtString,...) mName.empty() ? KARERE_LOG_ERROR(mLogChan, fmtString, ##__VA_ARGS__) : KARERE_LOG_ERROR(mLogChan, "%s: " fmtString, mName.c_str(), ##__VA_ARGS__)

namespace ws
{
// message storage subsystem
// the message buffer can grow in two directions and is always contiguous, i.e. there are no "holes"
// there is no guarantee as to ordering

ws_base_s Client::gContext;
bool Client::gContextInitialized = false;

Client::Client(ConnStateListener& listener, krLogChannelNo logChan, const std::string& name)
: mConnStateListener(listener), mLogChan(logChan), mName(name)
{
    if (!gContextInitialized)
    {
        ws_global_init(&gContext, services_get_event_loop(), nullptr,
        [](struct bufferevent* bev, void* userp)
        {
            marshallCall([bev, userp]()
            {
                ws_read_callback(bev, userp);
            });
        },
        [](struct bufferevent* bev, short events, void* userp)
        {
            marshallCall([bev, events, userp]()
            {
                ws_event_callback(bev, events, userp);
            });
        },
        [](int fd, short events, void* userp)
        {
            marshallCall([events, userp]()
            {
                ws_handle_marshall_timer_cb(0, events, userp);
            });
        });
//        ws_set_log_cb(ws_default_log_cb);
//        ws_set_log_level(LIBWS_TRACE);
        gContextInitialized = true;
    }
}

#define checkLibwsCall(call, opname) \
    do {                             \
        int _cls_ret = (call);       \
        if (_cls_ret) throw std::runtime_error("Websocket error " +std::to_string(_cls_ret) + \
        " on operation " #opname);   \
    } while(0)

//Stale event from a previous connect attempt?
#define ASSERT_NOT_ANOTHER_WS(event)    \
    if (ws != self->mWebSocket) {       \
        KR_LOG_WARNING("Websocket '" event "' callback: ws param is not equal to self->mWebSocket, ignoring"); \
    }

void Client::websockConnectCb(ws_t ws, void* arg)
{
    Client* self = static_cast<Client*>(arg);
    auto wptr = self->weakHandle();
    ASSERT_NOT_ANOTHER_WS("connect");
    ::marshallCall([wptr, self]()
    {
        if (wptr.deleted())
            return;
        self->setConnState(kConnected);
        assert(!self->mConnectPromise.done());
        self->mConnectPromise.resolve();
    });
}
void Client::websockMsgCb(ws_t ws, char *msg, uint64_t len, int binary, void *arg)
{
    Client* self = static_cast<Client*>(arg);
    auto wptr = self->weakHandle();
    if (wptr.deleted())
    {
        return;
    }
    ASSERT_NOT_ANOTHER_WS("connect");
    self->onMessage(StaticBuffer(msg, len));
}

void Client::websockCloseCb(ws_t ws, int errcode, int errtype, const char *preason,
                                size_t reason_len, void* userp)
{
    auto self = static_cast<Client*>(userp);
    ASSERT_NOT_ANOTHER_WS("close");
    std::string reason;
    if (preason)
        reason.assign(preason, reason_len);
    auto wptr = self->weakHandle();
    //we don't want to initiate websocket reconnect from within a websocket callback
    marshallCall([wptr, self, reason, errcode, errtype]()
    {
        if (wptr.deleted())
            return;
        self->onSocketClose(errcode, errtype, reason);
    });
}

void Client::onSocketClose(int errcode, int errtype, const std::string& reason)
{
    WS_LOG_WARNING("Socket close, reason: %s", reason.c_str());
    if (errtype == WS_ERRTYPE_DNS)
    {
        WS_LOG_WARNING("->DNS error: forcing libevent to re-read /etc/resolv.conf");
        evdns_base_clear_host_addresses(services_dns_eventbase);
        //if we didn't have our network interface up at app startup, and resolv.conf is
        //genereated dynamically, dns may never work unless we re-read the resolv.conf file
#ifdef _WIN32
        evdns_config_windows_nameservers();
#elif defined (__ANDROID__)
        char server[PROP_VALUE_MAX];
        if (__system_property_get("net.dns1", server) > 0) {
            evdns_base_nameserver_ip_add(services_dns_eventbase, server);
        }
#elif defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
        struct __res_state res;
        res_ninit(&res);
        union res_sockaddr_union addrs[MAXNS];
        int count = res_getservers(&res, addrs, MAXNS);
        if (count > 0) {
            if (addrs->sin.sin_family == AF_INET) {
                if (!addrs->sin.sin_port) {
                    addrs->sin.sin_port = 53;
                }
                evdns_base_nameserver_sockaddr_add(services_dns_eventbase, (struct sockaddr*)(&addrs->sin), sizeof(struct sockaddr_in), 0);
            } else if (addrs->sin6.sin6_family == AF_INET6) {
                if (!addrs->sin6.sin6_port) {
                    addrs->sin6.sin6_port = 53;
                }
                evdns_base_nameserver_sockaddr_add(services_dns_eventbase, (struct sockaddr*)(&addrs->sin6), sizeof(struct sockaddr_in6), 0);
            } else {
                fprintf(stderr, "Unknown address family for DNS server.");
            }
        }
        res_nclose(&res);
#else
        evdns_base_resolv_conf_parse(services_dns_eventbase,
            DNS_OPTIONS_ALL & (~DNS_OPTION_SEARCH), "/etc/resolv.conf");
#endif
    }
    if (mConnState == kDisconnected)
        return; //already disconnected forcibly do to timeout
    disableKeepalive();
    auto oldState = mConnState;
    setConnState(kDisconnected);
    if (mWebSocket)
    {
        ws_destroy(&mWebSocket);
    }
    onDisconnect();
    if (oldState == kDisconnecting)
    {
        if (!mDisconnectPromise.done())
            mDisconnectPromise.resolve(); //may delete this
        return;
    }

    if (oldState < kLoggedIn) //tell retry controller that the connect attempt failed
    {
        assert(!mLoginPromise.done());
        mConnectPromise.reject(reason, errcode, errtype);
        mLoginPromise.reject(reason, errcode, errtype);
    }
    else
    {
        WS_LOG_DEBUG("Socket close and state is not kLoggedIn (but %d), start retry controller", mConnState);
        reconnect(); //start retry controller
    }
}

void Client::setConnState(ConnState newState)
{
    mConnState = newState;
    WS_LOG_DEBUG("Connection state changed to %s", connStateToStr(newState));
    try
    {
        mConnStateListener.onConnStateChange(newState);
    }
    catch(...){};
}

Promise<void> Client::reconnect(const std::string& url)
{
    try
    {
        if (mConnState >= kConnecting) //would be good to just log and return, but we have to return a promise
            return promise::Error("reconnect: Already in state "+std::string(connStateToStr(mConnState)));
        if (!url.empty())
        {
            mUrl.parse(url);
        }
        else
        {
            if (!mUrl.isValid())
                throw std::runtime_error("No valid URL provided and current URL is not valid");
        }

        return retry(mName, [this](int no)
        {
            reset();
            setConnState(kConnecting);
            mConnectPromise = Promise<void>();
            mLoginPromise = Promise<void>();
            mDisconnectPromise = Promise<void>();
            WS_LOG_DEBUG("Connecting...");
            checkLibwsCall((ws_init(&mWebSocket, &Client::gContext)), "create socket");
            ws_set_onconnect_cb(mWebSocket, &websockConnectCb, this);
            ws_set_onclose_cb(mWebSocket, &websockCloseCb, this);
            ws_set_onmsg_cb(mWebSocket, &websockMsgCb, this);

            if (mUrl.isSecure)
            {
                ws_set_ssl_state(mWebSocket, LIBWS_SSL_SELFSIGNED);
            }
            checkLibwsCall((ws_connect(mWebSocket, mUrl.host.c_str(), mUrl.port, (mUrl.path).c_str(), services_http_use_ipv6)), "connect");
            return mConnectPromise
            .then([this]() -> promise::Promise<void>
            {
                assert(mConnState >= kConnected);
                enableKeepalive();
                onConnect();
                return mLoginPromise;
            });
        }, nullptr, 0, 0, KARERE_RECONNECT_DELAY_MAX, KARERE_RECONNECT_DELAY_INITIAL);
    }
    KR_EXCEPTION_TO_PROMISE(kPromiseErrtype_websocket);
}

promise::Promise<void> Client::disconnect(int timeoutMs) //should be graceful disconnect
{
    if (mConnState == kDisconnected)
        return promise::_Void();
    else if (mConnState == kDisconnecting)
        return mDisconnectPromise;

    setConnState(kDisconnecting);
    if (!mWebSocket)
    {
        onSocketClose(0, 0, "user disconnect");
        return promise::Void();
    }
    auto wptr = getDelTracker();
    setTimeout([this, wptr]()
    {
        if (wptr.deleted())
            return;
        if (!mDisconnectPromise.done())
            onSocketClose(0, 0 , "disconnect timeout");
    }, timeoutMs);
    ws_close(mWebSocket);
    return mDisconnectPromise;
}
void Client::notifyLoggedIn()
{
    assert(mConnState < kLoggedIn);
    assert(mConnectPromise.succeeded());
    assert(!mLoginPromise.done());
    setConnState(kLoggedIn);
    mLoginPromise.resolve();
}
void Client::notifyDisconnected()
{
    mConnState = kDisconnected;
    disableKeepalive();
}

Promise<void> Client::retryPendingConnect()
{
    if (!mUrl.isValid())
        return promise::Error("No url set");
    setConnState(kDisconnected);
    disableKeepalive();
    WS_LOG_WARNING("Retrying pending connection...");
    return reconnect();
}

void Client::reset() //immediate disconnect
{
    if (!mWebSocket)
    {
        assert(mConnState == kDisconnected);
        return;
    }
    setConnState(kDisconnecting);
    ws_close_immediately(mWebSocket);
    ws_destroy(&mWebSocket);
    setConnState(kDisconnected);
    assert(!mWebSocket);
}

bool Client::sendBuf(Buffer&& buf)
{
    if (!isOnline())
        return false;
//WARNING: ws_send_msg_ex() is destructive to the buffer - it applies the websocket mask directly
//Copy the data to preserve the original
    auto rc = ws_send_msg_ex(mWebSocket, buf.buf(), buf.dataSize(), 1);
    buf.free(); //just in case, as it's content is xor-ed with the websock datamask so it's unusable
    bool result = (!rc && isOnline());
    return result;
}


#define RET_ENUM_NAME(name) case name: return #name;
const char* Client::connStateToStr(ConnState state)
{
    switch (state)
    {
        RET_ENUM_NAME(kDisconnected);
        RET_ENUM_NAME(kConnecting);
        RET_ENUM_NAME(kDisconnecting);
        RET_ENUM_NAME(kConnected);
        RET_ENUM_NAME(kLoggedIn);
        default: return "(invalid conn state)";
    }
}
}
