#ifndef KR_APP_CTX_H
#define KR_APP_CTX_H
#include "../waiter/karereWaiter.h"
namespace karere
{
class AppCtx;

class Timer: public WeakReferenceable<Timer>
{
protected:
    void* mTimerEvent = nullptr;
    AppCtx& mAppCtx;
    Timer(AppCtx& appCtx): WeakReferenceable<Timer>(this), mAppCtx(appCtx){}
    virtual ~Timer();
    friend class AppCtx;
    friend class TimerHandle;
public:
    typedef WeakReferenceable<Timer>::WeakRefHandle Handle;
};
/** Convenience wrapper around the weak handle of Timer, to allow canceling
 * of the timer by TimerHandle.cancel()
 */
class TimerHandle: protected WeakReferenceable<Timer>::WeakRefHandle
{
protected:
    typedef WeakReferenceable<Timer>::WeakRefHandle Base;
    friend class Timer;
public:
    using Base::operator=;
    using Base::operator bool;
    using Base::reset;
    TimerHandle() = default;
    TimerHandle(const Base& other): Base(other){}
    TimerHandle& operator=(const TimerHandle& other)
    {
        Base::operator=(other);
        return *this;
    }
    TimerHandle(const TimerHandle& other): Base(other){}
    /** Cancels a previously set timer with setTimeout() or setInterval()
     * @return \c false if the handle is not valid. This can happen if the timeout
     * already triggered, then the handle is invalidated. This situation is safe and
     * considered normal
     */
    bool cancel()
    {
        auto timer = weakPtr();
        if (!timer)
            return false;
        delete timer; //unregisters the timer event from the eventoop
        assert(!isValid());
        return true;
    }
};

class AppCtx
{
protected:
    KarereWaiter* mWaiter;
    std::thread::id mPostThreadId;
public:
    struct MarshallMessage
    {
        typedef void(*Func)(void*);
        Func func;
        MarshallMessage(Func aFunc): func(aFunc){}
    };
    KarereWaiter& waiter() const { return *mWaiter; }
    std::thread::id postThreadId() const { return mPostThreadId; }
    void setPostThread() { mPostThreadId = std::this_thread::get_id(); }
    AppCtx(KarereWaiter* w=nullptr): mWaiter(w){}
    AppCtx(const AppCtx& other) = default;
    template <class CB>
    TimerHandle setTimeout(CB&& cb, uint64_t period);
    template <class CB>
    TimerHandle setInterval(CB&& cb, uint64_t period);
    template <class F>
    inline void marshallCall(F&& func);
    virtual void postMessage(MarshallMessage* msg) = 0;
    /** When the application's main (GUI) thread receives a message posted by
     * krPostMessageToGui(), the user's code must forward the \c void* pointer
     * from that message to this function for further processing. This function is
     * called by a handler in the app's (GUI) event/message loop (or equivalent).
    * \warning Must be called only from the GUI thread
    */
    static inline void processMessage(void* vptr);
protected:
    template <bool repeat, class CB>
    TimerHandle setTimer(CB&& cb, uint64_t period);
};
class AppCtxRef
{
protected:
    AppCtx& mAppCtx;
public:
    AppCtxRef(AppCtx& appCtx): mAppCtx(appCtx){}
    AppCtxRef(const AppCtxRef& other): mAppCtx(other.mAppCtx) {}
    operator AppCtx&() { return mAppCtx; }
    operator const AppCtx&() const { return mAppCtx; }
    KarereWaiter& waiter() { return mAppCtx.waiter(); }
    std::thread::id postThreadId() const { return mAppCtx.postThreadId(); }
    template <class F>
    inline void marshallCall(F&& func)
    {
        mAppCtx.marshallCall(std::forward<F>(func));
    }
    template <class CB>
    inline TimerHandle setTimeout(CB&& cb, uint64_t period)
    {
        return mAppCtx.setTimeout(std::forward<CB>(cb), period);
    }
    template <class CB>
    inline TimerHandle setInterval(CB&& cb, uint64_t period)
    {
        return mAppCtx.setInterval(std::forward<CB>(cb), period);
    }
};


struct TimerMessage: public AppCtx::MarshallMessage
{
    Timer::Handle htimer;
    AppCtx& appCtx; //We attach this message to the eventloop's timer handle, so we also need to know the app cotext when we marshall the timer callback
    TimerMessage(Timer& timer, AppCtx& ctx, AppCtx::MarshallMessage::Func cfunc)
        :MarshallMessage(cfunc), htimer(timer.weakHandle()), appCtx(ctx) {}
};

template <bool repeat, class CB>
inline TimerHandle AppCtx::setTimer(CB&& callback, uint64_t period)
{
    struct ThisTimer: public Timer
    {
        CB cb;
        ThisTimer(AppCtx& ctx, CB&& aCb): Timer(ctx), cb(std::forward<CB>(aCb)) {}
    };
    auto timer = new ThisTimer(*this, std::forward<CB>(callback));
    auto msg = new TimerMessage(*timer, *this,
    [](void* msg)
    {
        auto timerMsg = (TimerMessage*)msg;
        Timer::Handle& htimer = timerMsg->htimer;
        Timer* ptimer = htimer.weakPtr();
        if (!ptimer)
            return;
        ThisTimer* timer = static_cast<ThisTimer*>(ptimer);
        if (repeat)
        {
            timer->cb();
        }
        else
        {
            std::unique_ptr<ThisTimer> autodel(timer);
            timer->cb();
        }
    });
    timer->mTimerEvent = mWaiter->timerAdd(period, repeat, msg);
#if 0
// KR_USE_LIBUV
    pMsg->timerEvent = event_new(get_ev_loop(ctx), -1, persist,
      [](evutil_socket_t fd, short what, void* evarg)
      {
            krPostMessageToGui(evarg, ((Msg* )evarg)->appCtx);
      }, pMsg);

    struct timeval tv;
    tv.tv_sec = time / 1000;
    tv.tv_usec = (time % 1000)*1000;
    evtimer_add(pMsg->timerEvent, &tv);
#endif

    return TimerHandle(timer->weakHandle());
}
/**
 *
 *@brief Sets a one-shot timer, similar to javascript's setTimeout()
 *@param cb This is a C++11 lambda, std::function or any other object that
 * has \c operator()
 * @param timeMs - the time in milliseconds after which the callback
 * will be called one single time and the timer will be destroyed,
 * and the handle will be invalidated
 * @returns a handle that can be used to cancel the timeout
 */
template<class CB>
inline TimerHandle AppCtx::setTimeout(CB&& cb, uint64_t periodMs)
{
    return setTimer<false, CB>(std::forward<CB>(cb), periodMs);
}
/**
 @brief Sets a repeating timer, similar to javascript's setInterval
 @param callback A C++11 lambda function, std::function or any other object
 that has \c operator()
 @param timeMs - the timer's period in milliseconds. The function will be called
 releatedly until cancelInterval() is called on the returned handle
 @returns a handle that can be used to cancel the timer
*/
template <class CB>
inline TimerHandle AppCtx::setInterval(CB&& callback, uint64_t periodMs)
{
    return setTimer<true, CB>(std::forward<CB>(callback), periodMs);
}
/** This function uses the plain C Gui Call Marshaller mechanism (see gcm.h) to
 * marshal a C++11 lambda function call on the main (GUI) thread. Also it could
 * be used with a std::function or any other object with operator()). It provides
 * type safety since it generates both the message type and the code that processes
 * it. Further, it allows for code optimization as all types are known at compile time
 * and all code is in the same compilation unit, so it can be inlined
 */
template <class F>
inline void AppCtx::marshallCall(F&& func)
{
    struct Msg: public MarshallMessage
    {
        F mFunc;
        Msg(F&& aFunc, MarshallMessage::Func cHandler)
        : MarshallMessage(cHandler), mFunc(std::forward<F>(aFunc)){}
#ifndef NDEBUG
        unsigned magic = 0x3e9a3591;
#endif
    };
// Ensure that the message is deleted even if exception is thrown in the lambda.
// Although an exception should not happen here and will propagate to the
// application's message/event loop. TODO: maybe provide a try/catch block here?
// Asses the performence impact of this
// We use a custom-tailored smart ptr here to gain some performance (i.e. destructor
// always deletes, no null check needed)
    Msg* msg = new Msg(std::forward<F>(func),
    [](void* ptr)
    {
        std::unique_ptr<Msg> pMsg(static_cast<Msg*>(ptr));
        assert(pMsg->magic == 0x3e9a3591);
        if (!gCatchException)
        {
            pMsg->mFunc();
        }
        else
        {
            try
            {
                pMsg->mFunc();
            }
            catch(std::exception& e)
            {
                KR_LOG_ERROR("ERROR: Exception in a marshalled call: %s\n", e.what());
            }
        }
    });
    postMessage(msg);
}

inline void AppCtx::processMessage(void* vptr)
{
    struct MarshallMessage* msg = (struct MarshallMessage*)vptr;
    msg->func(vptr);
}

}
#endif
