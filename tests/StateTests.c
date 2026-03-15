#include <stdio.h>
#include <windows.h>

#include "..\src\State.h"

static int g_failures = 0;

static void ExpectBool(BOOL expected, BOOL actual, const char *name)
{
    if (expected == actual)
    {
        printf("PASS: %s\n", name);
        return;
    }

    fprintf(stderr, "FAIL: %s (expected %d, got %d)\n", name, expected, actual);
    g_failures++;
}

int main(void)
{
    ExpectBool(FALSE, ShouldDisableWebHelper(2531310, TRUE, FALSE, WEBHELPER_AUTO),
               "stale RunningAppID does not disable Steam");
    ExpectBool(TRUE, ShouldDisableWebHelper(2531310, TRUE, TRUE, WEBHELPER_AUTO),
               "active game disables webhelper");
    ExpectBool(FALSE, ShouldDisableWebHelper(0, FALSE, FALSE, WEBHELPER_AUTO),
               "idle Steam keeps webhelper enabled");
    ExpectBool(FALSE, ShouldDisableWebHelper(2531310, TRUE, TRUE, WEBHELPER_FORCE_ENABLE),
               "manual On overrides auto-disable");
    ExpectBool(TRUE, ShouldDisableWebHelper(0, FALSE, FALSE, WEBHELPER_FORCE_DISABLE),
               "manual Off overrides auto-enable");

    return g_failures == 0 ? 0 : 1;
}
