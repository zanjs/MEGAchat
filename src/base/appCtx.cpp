#include "appCtx.h"
namespace karere
{
Timer::~Timer()
{
    if (mTimerEvent)
        mAppCtx.waiter().timerDel(mTimerEvent);
}
/** If we delete the timer immediately and we are called from within the timer callback,
 * the callback lambda itself will me immediately deleted while executing, this for
 * example all lambda captured values will be destroyed.
 */
bool TimerHandle::cancel()
{
    TimerHandle htimer = *this;
    Timer* t = htimer.weakPtr();
    if (!t)
        return false;
    t->mAppCtx.marshallCall([htimer]()
    {
        auto timer = htimer.weakPtr();
        if (!timer)
            return;
        delete timer; //unregisters the timer event from the eventoop
        assert(!htimer.isValid());
    });
    return true;
}

}
