#include "State.h"

BOOL ShouldDisableWebHelper(DWORD runningAppId, BOOL appMarkedRunning, BOOL liveGameProcess,
                            WEBHELPER_OVERRIDE overrideMode)
{
    switch (overrideMode)
    {
    case WEBHELPER_FORCE_ENABLE:
        return FALSE;

    case WEBHELPER_FORCE_DISABLE:
        return TRUE;

    case WEBHELPER_AUTO:
    default:
        return runningAppId != 0 && appMarkedRunning && liveGameProcess;
    }
}
