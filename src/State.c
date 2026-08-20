#include "State.h"

BOOL ShouldParkWebHelper(DWORD runningAppId, BOOL appMarkedRunning)
{
    /*
     * RunningAppID plus the per-app Running flag is Steam's own
     * authoritative game state. A process-tree check is deliberately
     * avoided because modern Steam always has non-game helper children
     * and because it introduces a launch race on older builds.
     */
    return runningAppId != 0 && appMarkedRunning;
}
