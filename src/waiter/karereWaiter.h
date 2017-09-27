#ifndef KR_EVENTLOOP_H
#define KR_EVENTLOOP_H
#include <services-config.h>
#include <thread>
#include <trackDelete.h>
#include <type_traits>
#include <memory>
#undef verify
#include <mega/waiter.h>
#include <../karereCommon.h> //for KR_LOG_xxx
#define KR_INSIDE_KARERE_WAITER
namespace karere
{
// The WaiterBase inhericance uses CRTP, to avoid slow virtual methods
template <class Impl>
class WaiterBase: public ::mega::Waiter
{
protected:
    struct CmdBase
    {
        uint8_t mOpcode;
        virtual void exec() = 0;
        virtual ~CmdBase(){}
        CmdBase(uint8_t opcode): mOpcode(opcode) {}
    };
    struct SyncCmdBase: CmdBase
    {
        std::mutex mExecMutex;
        using CmdBase::CmdBase;
    };
    enum: uint8_t
    {
        kCmdExecAsync = 1,
        kCommandIsSync = 1 << 6,
        kCmdExecSync = 1 | kCommandIsSync
    };
    std::thread::id mLoopThreadId;
    template <class F>
    auto msgExecSync(F&& func) -> typename std::enable_if<!std::is_same<decltype(func()), void>::value, decltype(func())>::type
    {
        struct Cmd: SyncCmdBase
        {
            F mFunc;
            decltype(func()) mResult;
            Cmd(F&& func): SyncCmdBase(kCmdExecSync), mFunc(std::forward<F>(func))
            {
                this->mExecMutex.lock();
            }
            virtual void exec()
            {
                try
                {
                    mResult = mFunc();
                }
                catch (std::exception& e)
                {
                    KR_LOG_ERROR("KarereWaiter::msgExecSync: Exception in user function: %s", e.what());
                }
                this->mExecMutex.unlock();
            }
        };
        std::unique_ptr<Cmd> cmd(new Cmd(std::move(func)));
        static_cast<Impl*>(this)->ctrlSendCommand(cmd.get());
        cmd->mExecMutex.lock();
        return cmd->mResult;
    }
    template <class F>
    auto msgExecSync(F&& func) -> typename std::enable_if<std::is_same<decltype(func()), void>::value, decltype(func())>::type
    {
        struct Cmd: SyncCmdBase
        {
            F mFunc;
            Cmd(F&& func): SyncCmdBase(kCmdExecSync), mFunc(std::forward<F>(func))
            {
                this->mExecMutex.lock();
            }
            virtual void exec()
            {
              try
              {
                  mFunc();
              }
              catch (std::exception& e)
              {
                  KR_LOG_ERROR("KarereWaiter::msgExecSync: Exception in user function: %s", e.what());
              }
              this->mExecMutex.unlock();
            }
        };
        std::unique_ptr<Cmd> cmd(new Cmd(std::forward<F>(func)));
        static_cast<Impl*>(this)->ctrlSendCommand(cmd.get());
        cmd->mExecMutex.lock();
    }
public:
    std::thread::id loopThreadId() const { return mLoopThreadId; }
    void setLoopThread()
    {
        mLoopThreadId = std::this_thread::get_id();
        KR_LOG_DEBUG("setLoopThread id=%p\n", pthread_self());
    }
    template <class F>
    auto execSync(F&& func) -> decltype(func())
    {
        return (std::this_thread::get_id() == mLoopThreadId) ? func() : msgExecSync(std::move(func));
    }
    template <class F>
    void execAsync(F&& func);
    virtual void stop() = 0;
// mega::Waiter functions
    virtual void init(::mega::dstime ds)
    {
        setLoopThread();
        ::mega::Waiter::init(ds);
    }
};

/** @brief Timestamp function with millisecond precision. Uses libevent/libuv portable
 * backend */
int64_t timestampMs();
}

#ifdef KR_USE_LIBUV
    #include "libuvWaiter.h"
namespace karere
{
    typedef uv_loop_t krEventLoop;
    typedef LibuvWaiter KarereWaiter;
}
#else
    #include "libeventWaiter.h"
namespace karere
{
    typedef struct event_base krEventLoop;
    typedef karere::LibeventWaiter KarereWaiter;
}
#endif
#undef KR_INSIDE_KARERE_WAITER
#endif
