#include <base/cservices.h>
#include <gcmpp.h>
#include <retryHandler.h>
#include <libws_log.h>
#include <event2/dns.h>
#include <event2/dns_compat.h>

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

namespace ws
{
// message storage subsystem
// the message buffer can grow in two directions and is always contiguous, i.e. there are no "holes"
// there is no guarantee as to ordering

ws_base_s Client::gContext;
bool Client::gContextInitialized = false;

Client::Client()
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
        WS_LOG_WARNING("Websocket '" event "' callback: ws param is not equal to self->mWebSocket, ignoring"); \
    }

void Client::websockConnectCb(ws_t ws, void* arg)
{
    Client* self = static_cast<Client*>(arg);
    ASSERT_NOT_ANOTHER_WS("connect");
    ::marshallCall([self]()
    {
        self->setState(kStateConnected);
        assert(!self->mConnectPromise.done());
        self->mConnectPromise.resolve();
    });
}
void Client::websockMsgCb(ws_t ws, char *msg, uint64_t len, int binary, void *arg)
{
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
    CHATD_LOG_WARNING("Socket close on connection to shard %d. Reason: %s",
        mShardNo, reason.c_str());
    if (errtype == WS_ERRTYPE_DNS)
    {
        CHATD_LOG_WARNING("->DNS error: forcing libevent to re-read /etc/resolv.conf");
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
    if (mState == kDisconnected)
        return; //already disconnected forcibly do to timeout
    disableInactivityTimer();
    auto oldState = mState;
    setState(kStateDisconnected);
    if (mWebSocket)
    {
        ws_destroy(&mWebSocket);
    }
    try
    {
        onDisconnect();
    }
    catch(std::exception& e)
    {
        WS_LOG_ERROR("Exception thrown from onDisconnect user handler: %s", e.what());
    }
    if (oldState == kDisconnecting)
    {
        if (!mDisconnectPromise.done())
            mDisconnectPromise.resolve(); //may delete this
        return;
    }

    if (oldState < kStateLoggedIn) //tell retry controller that the connect attempt failed
    {
        assert(!mLoginPromise.done());
        mConnectPromise.reject(reason, errcode, errtype);
        mLoginPromise.reject(reason, errcode, errtype);
    }
    else
    {
        WS_LOG_DEBUG("Socket close and state is not kLoggedIn (but %d), start retry controller", mState);
        reconnect(); //start retry controller
    }
}

void Client::setState(State newState)
{
    mState = newState;
    WS_LOG_DEBUG("Connection state changed to %s", stateToStr(newState));
}

Promise<void> Client::reconnect(const std::string& url)
{
    try
    {
        if (mState >= kStateConnecting) //would be good to just log and return, but we have to return a promise
            throw std::runtime_error(std::string("Already connecting/connected to shard ")+std::to_string(mShardNo));
        if (!url.empty())
        {
            mUrl.parse(url);
        }
        else
        {
            if (!mUrl.isValid())
                throw std::runtime_error("No valid URL provided and current URL is not valid");
        }

        setState(kStateConnecting);
        return retry(mName, [this](int no)
        {
            reset();
            mConnectPromise = Promise<void>();
            mLoginPromise = Promise<void>();
            mDisconnectPromise = Promise<void>();
            CHATD_LOG_DEBUG("Connecting to %s...", mName.c_str());
            checkLibwsCall((ws_init(&mWebSocket, &Client::sWebsocketContext)), "create socket");
            ws_set_onconnect_cb(mWebSocket, &websockConnectCb, this);
            ws_set_onclose_cb(mWebSocket, &websockCloseCb, this);
            ws_set_onmsg_cb(mWebSocket, &websockMsgCb, this);

            if (mUrl.isSecure)
            {
                ws_set_ssl_state(mWebSocket, LIBWS_SSL_SELFSIGNED);
            }
            onConnecting();
            checkLibwsCall((ws_connect(mWebSocket, mUrl.host.c_str(), mUrl.port, (mUrl.path).c_str(), services_http_use_ipv6)), "connect");
            return mConnectPromise
            .then([this]() -> promise::Promise<void>
            {
                assert(mState >= kStateConnected);
                enableInactivityTimer();
                return onConnected();
            });
        }, nullptr, 0, 0, KARERE_RECONNECT_DELAY_MAX, KARERE_RECONNECT_DELAY_INITIAL);
    }
    KR_EXCEPTION_TO_PROMISE(kPromiseErrtype_websocket);
}

promise::Promise<void> Connection::disconnect(int timeoutMs) //should be graceful disconnect
{
    if (mState == kDisconnected)
        return promise::_Void();
    else if (mState == kDisconnecting)
        return mDisconnectPromise;

    setState(kDisconnecting);
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
            onSocketClose(0, 0 , 'disconnect timeout');
    }, timeoutMs);
    ws_close(mWebSocket);
    return mDisconnectPromise;
}

bool Connection::retryPendingConnection()
{
    if (!mUrl.isValid())
        return false;
    setState(kStateDisconnected);
    disableInactivityTimer();
    WS_LOG_WARNING("Retrying pending connection...");
    reconnect();
}

void Client::reset() //immediate disconnect
{
    if (!mWebSocket)
    {
        assert(mState == kDisconnected);
        return;
    }
    setState(kDisconnecting);
    ws_close_immediately(mWebSocket);
    ws_destroy(&mWebSocket);
    setState(kDisconnected);
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


#define RET_ENUM_NAME(name) case OP_##name: return #name;
const char* Client::stateToStr(uint8_t opcode)
{
    switch (opcode)
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
