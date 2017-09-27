#include "libwebsocketsIO.h"
#include "../waiter/libuvWaiter.h"

#include <mega/http.h>
#include <assert.h>
#include "libwebsocketsIO.h"

using namespace std;
namespace libwebsockets
{
static struct lws_protocols protocols[] =
{
    {
        "MEGAchat",
        Socket::wsCallback,
        0,
        128 * 1024, // Rx buffer size
    },
    { NULL, NULL, 0, 0 } /* terminator */
};

IO::IO(::mega::Mutex *mutex, karere::AppCtx& ctx): ws::IO(mutex, ctx)
{
    struct lws_context_creation_info info;
    memset( &info, 0, sizeof(info) );
    
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.options |= LWS_SERVER_OPTION_DISABLE_OS_CA_CERTS;
    
#ifdef LWS_USE_LIBUV
    info.options |= LWS_SERVER_OPTION_LIBUV;
#endif
    
    lws_set_log_level(LLL_ERR | LLL_INFO | LLL_USER | LLL_WARN | LLL_COUNT
                      | LLL_CLIENT | LLL_HEADER | LLL_NOTICE
                      | LLL_LATENCY, NULL);

    wscontext = lws_create_context(&info);
#ifdef LWS_USE_LIBUV
    lws_uv_sigint_cfg(wscontext, 0, nullptr);
#endif
}

IO::~IO()
{
    lws_context_destroy(wscontext);
}

void IO::addevents(::mega::Waiter* waiter, int)
{    
    if (mIsInitialized)
        return;

    karere::LibuvWaiter *libuvWaiter = dynamic_cast<karere::KarereWaiter*>(waiter);
    assert(libuvWaiter);
    registerWithEventLoop(libuvWaiter->loop());
}

void IO::registerWithEventLoop(void* eventloop)
{
    lws_uv_initloop(wscontext, (uv_loop_t*)eventloop, 0);
    WS_LOG_DEBUG("Libwebsockets is using libuv");
    mIsInitialized = true;
}

ws::Socket *IO::connect(ws::wsClient& client, const char *ip, const char *host, int port, const char *path,
    bool ssl, ws::EventHandler *handler)
{
    std::unique_ptr<Socket> socket(new Socket(client, handler));
    
    std::string cip = ip;
    if (cip[0] == '[')
    {
        // remove brackets in IPv6 addresses
        cip = cip.substr(1, cip.size() - 2);
    }
    
    struct lws_client_connect_info i;
    memset(&i, 0, sizeof(i));
    i.context = wscontext;
    i.address = cip.c_str();
    i.port = port;
    i.ssl_connection = ssl ? 2 : 0;
    string urlpath = "/";
    urlpath.append(path);
    i.path = urlpath.c_str();
    i.host = host;
    i.ietf_version_or_minus_one = -1;
    i.userdata = socket.get();
    
    socket->wsi = lws_client_connect_via_info(&i);
    if (!socket->wsi)
    {
        return NULL;
    }
    return socket.release();
}

Socket::~Socket()
{
    disconnect(true);
}

void Socket::appendMessageFragment(char *data, size_t len, size_t remaining)
{
    if (!recbuffer.size() && remaining)
    {
        recbuffer.reserve(len + remaining);
    }
    recbuffer.append(data, len);
}

bool Socket::hasFragments()
{
    return recbuffer.size();
}

const char *Socket::getMessage()
{
    return recbuffer.data();
}

size_t Socket::getMessageLength()
{
    return recbuffer.size();
}

void Socket::resetMessage()
{
    recbuffer.clear();
}

bool Socket::sendMessage(char *msg, size_t len)
{
    assert(wsi);
    
    if (!wsi)
    {
        return false;
    }
    
    if (!sendbuffer.size())
    {
        sendbuffer.reserve(LWS_PRE + len);
        sendbuffer.resize(LWS_PRE);
    }
    sendbuffer.append(msg, len);
    return lws_callback_on_writable(wsi) > 0;
}

void Socket::disconnect(bool immediate)
{
    if (!wsi)
    {
        return;
    }

    if (immediate)
    {
        struct lws *dwsi = wsi;
        wsi = NULL;
        lws_set_wsi_user(dwsi, NULL);
        WS_LOG_DEBUG("Pointer detached from libwebsockets");
        
        if (!disconnecting)
        {
            lws_callback_on_writable(dwsi);
            WS_LOG_DEBUG("Requesting a forced disconnection to libwebsockets");
        }
        else
        {
            disconnecting = false;
            WS_LOG_DEBUG("Already disconnecting from libwebsockets");
        }
    }
    else
    {
        disconnecting = true;
        lws_callback_on_writable(wsi);
        WS_LOG_DEBUG("Requesting a graceful disconnection to libwebsockets");
    }
}

bool Socket::isConnected()
{
    return wsi != NULL;
}

const char *Socket::getOutputBuffer()
{
    return sendbuffer.size() ? sendbuffer.data() + LWS_PRE : NULL;
}

size_t Socket::getOutputBufferLength()
{
    return sendbuffer.size() ? sendbuffer.size() - LWS_PRE : 0;
}

void Socket::resetOutputBuffer()
{
    sendbuffer.clear();
}

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
#define X509_STORE_CTX_get0_cert(ctx) (ctx->cert)
#define X509_STORE_CTX_get0_untrusted(ctx) (ctx->untrusted)
#define EVP_PKEY_get0_DSA(_pkey_) ((_pkey_)->pkey.dsa)
#define EVP_PKEY_get0_RSA(_pkey_) ((_pkey_)->pkey.rsa)
#endif

const BIGNUM *RSA_get0_n(const RSA *rsa)
{
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    return rsa->n;
#else
    const BIGNUM *result;
    RSA_get0_key(rsa, &result, NULL, NULL);
    return result;
#endif
}

const BIGNUM *RSA_get0_e(const RSA *rsa)
{
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    return rsa->e;
#else
    const BIGNUM *result;
    RSA_get0_key(rsa, NULL, &result, NULL);
    return result;
#endif
}

const BIGNUM *RSA_get0_d(const RSA *rsa)
{
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    return rsa->d;
#else
    const BIGNUM *result;
    RSA_get0_key(rsa, NULL, NULL, &result);
    return result;
#endif
}

static bool check_public_key(X509_STORE_CTX* ctx)
{
    unsigned char buf[sizeof(APISSLMODULUS1) - 1];
    EVP_PKEY* evp;
    if ((evp = X509_PUBKEY_get(X509_get_X509_PUBKEY(X509_STORE_CTX_get0_cert(ctx)))))
    {
        if (BN_num_bytes(RSA_get0_n(EVP_PKEY_get0_RSA(evp))) == sizeof APISSLMODULUS1 - 1
            && BN_num_bytes(RSA_get0_e(EVP_PKEY_get0_RSA(evp))) == sizeof APISSLEXPONENT - 1)
        {
            BN_bn2bin(RSA_get0_n(EVP_PKEY_get0_RSA(evp)), buf);
            
            if (!memcmp(buf, CHATSSLMODULUS, sizeof CHATSSLMODULUS - 1))
            {
                BN_bn2bin(RSA_get0_e(EVP_PKEY_get0_RSA(evp)), buf);
                if (!memcmp(buf, APISSLEXPONENT, sizeof APISSLEXPONENT - 1))
                {
                    EVP_PKEY_free(evp);
                    return true;
                }
            }
        }
        EVP_PKEY_free(evp);
    }

    return false;
}

int Socket::wsCallback(struct lws *wsi, enum lws_callback_reasons reason,
                                    void *user, void *data, size_t len)
{
    switch (reason)
    {
        case LWS_CALLBACK_OPENSSL_PERFORM_SERVER_CERT_VERIFICATION:
        {
            if (check_public_key((X509_STORE_CTX*)user))
            {
                X509_STORE_CTX_set_error((X509_STORE_CTX*)user, X509_V_OK);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            Socket* self = static_cast<Socket*>(user);
            if (!self)
            {
                return -1;
            }

            self->wsConnectCb();
            break;
        }
        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            auto self = static_cast<Socket*>(user);
            if (!self)
            {
                WS_LOG_DEBUG("Forced disconnect completed");
                return -1;
            }

            WS_LOG_DEBUG("Graceful disconnect completed");
            self->disconnecting = false;
            self->wsCloseCb(0, 0, "", 0);
            break;
        }
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            auto self = static_cast<Socket*>(user);
            if (!self)
            {
                return -1;
            }

            const size_t remaining = lws_remaining_packet_payload(wsi);
            self->appendMessageFragment((char *)data, len, remaining);

            if (!remaining && lws_is_final_fragment(wsi))
            {
                if (self->hasFragments())
                {
                    WS_LOG_DEBUG("Fragmented data completed");
                    // We send the (rvalue ref) string itself to the callback, because if it needs
                    // to post it to another thread, the string would avoid copying of the data
                    self->wsHandleMsgCb(std::forward<std::string>(self->recbuffer));
                    // After  a std::move, the source is left in valid, but unspecified state.
                    // We must clear it
                    self->resetMessage();
                }
            }
            else
            {
                WS_LOG_DEBUG("Managing fragmented data");
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            auto* self = static_cast<Socket*>(user);
            if (!self)
            {
                WS_LOG_DEBUG("Completing forced disconnect");
                return -1;
            }

            if (self->disconnecting)
            {
                WS_LOG_DEBUG("Completing graceful disconnect");
                return -1;
            }
            
            data = (void *)self->getOutputBuffer();
            len = self->getOutputBufferLength();
            if (len && data)
            {
                lws_write(wsi, (unsigned char *)data, len, LWS_WRITE_BINARY);
                self->resetOutputBuffer();
            }
            break;
        }
        default:
            break;
    }
    
    return 0;
}

}
