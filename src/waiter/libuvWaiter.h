#ifndef LIBUV_WAITER_H
#define LIBUV_WAITER_H
#include <uv.h>
#ifndef KR_INSIDE_KARERE_WAITER
    #error You must not include this header directly
#endif

namespace karere
{
typedef uv_loop_t krEventLoop;
class Timer;
struct TimerMessage;

class LibuvWaiter : public karere::WaiterBase<LibuvWaiter>
{
public:
    LibuvWaiter();
    ~LibuvWaiter();
    const uv_loop_t* loop() const { return &mEventLoop; }
    uv_loop_t* loop() { return &mEventLoop; }
    virtual void stop();
// mega::Waiter interface
    int wait();
    void notify();
//==
protected:
    uv_loop_t mEventLoop;
    uv_async_t mAsyncHandle;
    uv_pipe_t mCtrlPipe;
    int mCtrlPipeFds[2] = {0};
    void setupControlChannel();
    void ctrlSendCommand(CmdBase* cmd);
    void* timerAdd(uint64_t period, bool repeat, karere::TimerMessage* msg);
    void timerDel(void*& timer);
//control channel libuv callbacks
    static void uvCtrlAllocCb(uv_handle_t* handle, size_t size, uv_buf_t* buf);
    static void uvCtrlReadCb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf);
    friend class WaiterBase<LibuvWaiter>;
    friend class AppCtx;
    friend class Timer;
};
}

#endif
