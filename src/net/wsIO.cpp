#include "wsIO.h"
namespace ws
{
IO::IO(::mega::Mutex *mutex, karere::AppCtx& ctx)
    : AppCtxRef(ctx), mMutex(mutex)
{
    if (mutex)
        abort();
}

IO::~IO()
{}

Socket::Socket(ws::wsClient& client, EventHandler *handler)
 : mClient(client), mHandler(handler), mMutex(client.mIO.mMutex)
{}

class ScopedLock
{
    ::mega::Mutex *m;
public:
    ScopedLock(::mega::Mutex *mutex) : m(mutex)
    {
        if (m)
        {
            m->lock();
        }
    }
    ~ScopedLock()
    {
        if (m)
        {
            m->unlock();
        }
    }
};

void Socket::wsConnectCb()
{
    ScopedLock lock(mMutex);
    WS_LOG_DEBUG("Connection established");
    if (std::this_thread::get_id() == mClient.postThreadId())
    {
        assert(false);
        mHandler->wsConnectCb();
    }
    else
    {
        auto wptr = weakHandle();
        mClient.marshallCall([wptr, this]
        {
            if (wptr.deleted())
                return;
            mHandler->wsConnectCb();
        });
    }
}

void Socket::wsCloseCb(int errcode, int errtype, const char *preason, size_t reason_len)
{
    ScopedLock lock(mMutex);
    WS_LOG_DEBUG("Connection closed");
    std::string reason;
    if (preason)
    {
        reason.assign(preason, reason_len);
    }
    if (std::this_thread::get_id() == mClient.postThreadId())
    {
        assert(false);
        mHandler->wsCloseCb(errcode, errtype, reason);
    }
    else
    {
        auto wptr = weakHandle();
        mClient.marshallCall([wptr, this, errcode, errtype, reason]
        {
            if (wptr.deleted())
                return;
            mHandler->wsCloseCb(errcode, errtype, reason);
        });
    }
}

void Socket::wsHandleMsgCb(std::string&& data)
{
    ScopedLock lock(mMutex);
    if (std::this_thread::get_id() == mClient.postThreadId())
    {
        assert(false);
        mHandler->wsHandleMsgCb(std::forward<std::string>(data));
    }
    else
    {
        auto wptr = weakHandle();
        std::string marshalledData(std::forward<std::string>(data));
        mClient.marshallCall([wptr, this, marshalledData]() mutable
        {
            if (wptr.deleted())
                return;
            mHandler->wsHandleMsgCb(std::forward<std::string>(marshalledData));
        });
    }
}

wsClient::wsClient(IO& io, karere::AppCtx& ctx): AppCtxRef(ctx), mIO(io), mSocket(NULL){}

bool wsClient::wsConnect(const char *ip, const char *host, int port, const char *path, bool ssl)
{
    WS_LOG_DEBUG("Connecting to %s (%s), port %d, path: %s, ssl: %d", host, ip, port, path, ssl);
    mSocket = mIO.connect(*this, ip, host, port, path, ssl, this);
    if (!mSocket)
    {
        WS_LOG_WARNING("Immediate error in wsConnect");
    }
    return mSocket != NULL;
}

bool wsClient::wsSendMessage(char *msg, size_t len)
{
    if (!mSocket)
    {
        WS_LOG_ERROR("Trying to send a message without a previous initialization");
        return false;
    }

    WS_LOG_DEBUG("Sending %d bytes", len);

    if (std::this_thread::get_id() == waiter().loopThreadId())
    {
        bool result = mSocket->sendMessage(msg, len);
        if (!result)
        {
            WS_LOG_WARNING("wsClient::wsSendMessage: Immediate error");
        }
        return result;
    }
    else
    {
       return waiter().execSync([this, msg, len]()
       {
           bool result = mSocket->sendMessage(msg, len);
           if (!result)
           {
               WS_LOG_WARNING("wsClient::wsSendMessage: Immediate error");
           }
           return result;
       });
    }
}

void wsClient::wsDisconnect(bool immediate)
{
    WS_LOG_DEBUG("Disconnecting. Immediate: %d", immediate);
    if (!mSocket)
    {
        WS_LOG_WARNING("wsClient::wsDisconnect: No socket");
        return;
    }
    
    if (std::this_thread::get_id() == waiter().loopThreadId())
    {
        mSocket->disconnect(immediate);
    }
    else
    {
        waiter().execSync([this, immediate]()
        {
            mSocket->disconnect(immediate);
        });
    }
}

bool wsClient::wsIsConnected()
{
    if (!mSocket)
    {
        return false;
    }
//mSocket::isConnected must marshall the call if it needs to. But generally this
//would just read a flag, so marshalling is not done by default
    return mSocket->isConnected();
}

}
