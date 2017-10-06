#include "karereWaiter.h"
#include "appCtx.h"

namespace karere
{
static void break_libuv_loop(uv_async_t* handle)
{
    uv_stop(handle->loop);
}

LibuvWaiter::LibuvWaiter()
{
    uv_loop_init(&mEventLoop);
    uv_async_init(&mEventLoop, &mAsyncHandle, break_libuv_loop);
    setupControlChannel();
}

LibuvWaiter::~LibuvWaiter()
{
    uv_close((uv_handle_t*)&mAsyncHandle, NULL);
    uv_close((uv_handle_t*)&mCtrlPipe, NULL);
    uv_run(&mEventLoop, UV_RUN_DEFAULT);
    uv_loop_close(&mEventLoop);
}

int LibuvWaiter::wait()
{
    uv_run(&mEventLoop, UV_RUN_DEFAULT);
    return NEEDEXEC;
}
void LibuvWaiter::stop()
{
    ctrlSendCommand(nullptr);
}
void LibuvWaiter::notify()
{
    ctrlSendCommand(nullptr);
}

void LibuvWaiter::uvCtrlAllocCb(uv_handle_t* handle, size_t size, uv_buf_t* buf)
{
    buf->base = (char*)malloc(size);
    buf->len = size;
}
void LibuvWaiter::uvCtrlReadCb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf)
{
    if (nread <= 0)
        return;
    assert(buf->base);
    assert(buf->len >= nread);
    assert(nread == sizeof(void*));
    CmdBase* cmd = *reinterpret_cast<CmdBase**>(buf->base);
    free(buf->base);
    if (cmd)
    {
        cmd->exec(); //never throws
    }
    else
    {
        auto self = reinterpret_cast<LibuvWaiter*>(handle->data);
        assert(self);
        uv_stop(&(self->mEventLoop));
    }
}

void LibuvWaiter::setupControlChannel()
{
    if (pipe(mCtrlPipeFds))
        throw std::runtime_error(std::string("Error creating pipes: ")+strerror(errno));
    int inputFd = mCtrlPipeFds[0];
    fcntl(inputFd, F_SETFL, fcntl(inputFd, F_GETFL, 0) | O_NONBLOCK);

    uv_pipe_init(&mEventLoop, &mCtrlPipe, 0);
    uv_pipe_open(&mCtrlPipe, inputFd);
    mCtrlPipe.data = this;
    uv_read_start((uv_stream_t*) &mCtrlPipe, uvCtrlAllocCb, uvCtrlReadCb);
    printf("Successfully set up libuv control pipe: %d\n", mCtrlPipeFds[1]);
}
void LibuvWaiter::ctrlSendCommand(CmdBase* cmd)
{
    auto r = write(mCtrlPipeFds[1], &cmd, sizeof(cmd));
    if (r < 0)
    {
        KR_LOG_ERROR("Error writing to control pipe (fd=%d): %s", mCtrlPipeFds[1], strerror(errno));
        assert(false);
    }
}

void LibuvWaiter::timerDel(void*& timer)
{
    execSync([this, &timer]()
    {
        assert(timer);
        uv_timer_stop((uv_timer_t*)timer);
        uv_close((uv_handle_t*)timer, [](uv_handle_t* handle)
        {
            delete handle;
        });
    });
    timer = nullptr;
}

void* LibuvWaiter::timerAdd(uint64_t period, bool repeat, karere::TimerMessage* msg)
{
    return execSync([this, repeat, period, msg]()
    {
        auto t = new uv_timer_t();
        t->data = msg;
        uv_timer_init(&mEventLoop, t);
        uv_timer_start(t, [](uv_timer_t* handle)
        {
            auto timerMsg = reinterpret_cast<karere::TimerMessage*>(handle->data);
            timerMsg->appCtx.postMessage(timerMsg);
        }, period, repeat ? period : 0);
        return (void*)t;
    });
}
int64_t timestampMs()
{
    return uv_hrtime() / 1000000; //nanoseconds to milliseconds
}

} // namespace
