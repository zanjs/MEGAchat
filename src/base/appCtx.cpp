#include "appCtx.h"
namespace karere
{
Timer::~Timer()
{
    if (mTimerEvent)
        mAppCtx.waiter().timerDel(mTimerEvent);
}
}
