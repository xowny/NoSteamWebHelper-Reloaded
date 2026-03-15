#ifndef NOSTEAMWEBHELPER_STATE_H
#define NOSTEAMWEBHELPER_STATE_H

#include <windows.h>

typedef enum WEBHELPER_OVERRIDE
{
    WEBHELPER_AUTO = 0,
    WEBHELPER_FORCE_ENABLE = 1,
    WEBHELPER_FORCE_DISABLE = 2
} WEBHELPER_OVERRIDE;

BOOL ShouldDisableWebHelper(DWORD runningAppId, BOOL appMarkedRunning, BOOL liveGameProcess,
                            WEBHELPER_OVERRIDE overrideMode);

#endif
