#include "websocketsIO.h"
namespace ws
{
IO::IO(::mega::Mutex *mutex, karere::AppCtx& ctx)
    : AppCtxRef(ctx), mMutex(mutex)
{}

IO::~IO()
{}

Socket::Socket(ws::wsClient& client, EventHandler *handler)
 : mClient(client), mHandler(handler)
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
//    ScopedLock lock(mMutex);
    WEBSOCKETS_LOG_DEBUG("Connection established");
    if (std::this_thread::get_id() == mClient.mThreadId)
    {
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
//    ScopedLock lock(mMutex);
    WEBSOCKETS_LOG_DEBUG("Connection closed");
    if (std::this_thread::get_id() == mClient.mThreadId)
    {
        std::string reason;
        if (preason)
        {
            reason.assign(preason, reason_len);
        }
        mHandler->wsCloseCb(errcode, errtype, reason);
    }
    else
    {
        std::string reason;
        if (preason)
        {
            reason.assign(preason, reason_len);
        }
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
//    ScopedLock lock(mMutex);
    WEBSOCKETS_LOG_DEBUG("Received %zu bytes", data.size());
    if (std::this_thread::get_id() == mClient.mThreadId)
    {
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

wsClient::wsClient(karere::AppCtx& ctx): AppCtxRef(ctx), mSocket(NULL){}

bool wsClient::wsConnect(IO *io, const char *ip, const char *host, int port, const char *path, bool ssl)
{
    mThreadId = std::this_thread::get_id();
    
    WEBSOCKETS_LOG_DEBUG("Connecting to %s (%s)  port %d  path: %s   ssl: %d", host, ip, port, path, ssl);
    mSocket = io->connect(*this, ip, host, port, path, ssl, this);
    if (!mSocket)
    {
        WEBSOCKETS_LOG_WARNING("Immediate error in wsConnect");
    }
    return mSocket != NULL;
}

bool wsClient::wsSendMessage(char *msg, size_t len)
{
    if (!mSocket)
    {
        WEBSOCKETS_LOG_ERROR("Trying to send a message without a previous initialization");
        return false;
    }

    assert (std::this_thread::get_id() == mThreadId);
    
    WEBSOCKETS_LOG_DEBUG("Sending %d bytes", len);
    bool result = mSocket->sendMessage(msg, len);
    if (!result)
    {
        WEBSOCKETS_LOG_WARNING("Immediate error in wsSendMessage");
    }
    return result;
}

void wsClient::wsDisconnect(bool immediate)
{
    WEBSOCKETS_LOG_DEBUG("Disconnecting. Immediate: %d", immediate);
    
    if (!mSocket)
    {
        return;
    }
    
    assert (std::this_thread::get_id() == mThreadId);
    mSocket->disconnect(immediate);
}

bool wsClient::wsIsConnected()
{
    if (!mSocket)
    {
        return false;
    }
    
    assert (std::this_thread::get_id() == mThreadId);
    return mSocket->isConnected();
}

}
