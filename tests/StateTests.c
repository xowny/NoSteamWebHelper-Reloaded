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
    ExpectBool(TRUE, ShouldParkWebHelper(2531310, TRUE),
               "RunningAppID with Running flag parks webhelper");
    ExpectBool(FALSE, ShouldParkWebHelper(0, FALSE),
               "idle Steam keeps webhelper at its original settings");
    ExpectBool(FALSE, ShouldParkWebHelper(2531310, FALSE),
               "RunningAppID without Running flag keeps webhelper enabled");
    ExpectBool(FALSE, ShouldParkWebHelper(0, TRUE),
               "Running flag without RunningAppID keeps webhelper enabled");

    return g_failures == 0 ? 0 : 1;
}
