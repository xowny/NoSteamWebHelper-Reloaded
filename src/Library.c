#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include "State.h"

#define ID_TRAY_AUTO 100
#define ID_TRAY_ON 101
#define ID_TRAY_OFF 102

typedef struct PROCESS_NODE
{
    DWORD processId;
    DWORD parentProcessId;
    WCHAR imageName[MAX_PATH];
} PROCESS_NODE;

#define MAX_TRACKED_PROCESSES 2048

static volatile LONG g_manualOverride = WEBHELPER_AUTO;
static volatile LONG g_webHelperDisabled = FALSE;
static HANDLE g_stopEvent = NULL;

static BOOL IsSteamClientProcess(void)
{
    WCHAR modulePath[MAX_PATH] = {};
    LPCWSTR fileName = NULL;

    if (GetModuleFileNameW(NULL, modulePath, ARRAYSIZE(modulePath)) == 0)
        return FALSE;

    fileName = wcsrchr(modulePath, L'\\');
    fileName = (fileName == NULL) ? modulePath : fileName + 1;

    return CompareStringOrdinal(fileName, -1, L"steam.exe", -1, TRUE) == CSTR_EQUAL;
}

static DWORD ReadSteamDwordValue(LPCWSTR subKey, LPCWSTR valueName, DWORD fallbackValue)
{
    DWORD value = fallbackValue;
    DWORD size = sizeof(value);
    LONG status = RegGetValueW(HKEY_CURRENT_USER, subKey, valueName, RRF_RT_REG_DWORD, NULL, &value, &size);

    if (status != ERROR_SUCCESS)
        return fallbackValue;

    return value;
}

static BOOL ReadSteamAppRunning(DWORD appId)
{
    WCHAR subKey[128] = {};
    DWORD running = FALSE;
    DWORD size = sizeof(running);

    wsprintfW(subKey, L"SOFTWARE\\Valve\\Steam\\Apps\\%lu", appId);

    return RegGetValueW(HKEY_CURRENT_USER, subKey, L"Running", RRF_RT_REG_DWORD, NULL, &running, &size) ==
               ERROR_SUCCESS &&
           running != FALSE;
}

static BOOL IsIgnoredChildProcessName(LPCWSTR imageName)
{
    static const LPCWSTR kIgnoredNames[] = {L"steam.exe",       L"steamwebhelper.exe", L"gameoverlayui.exe",
                                            L"crashpad_handler.exe", L"steamservice.exe"};
    DWORD index = 0;

    for (; index < ARRAYSIZE(kIgnoredNames); index++)
    {
        if (CompareStringOrdinal(imageName, -1, kIgnoredNames[index], -1, TRUE) == CSTR_EQUAL)
            return TRUE;
    }

    return FALSE;
}

static DWORD SnapshotProcesses(PROCESS_NODE *processes, DWORD capacity)
{
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    PROCESSENTRY32W entry = {};
    DWORD count = 0;

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (count >= capacity)
                break;

            processes[count].processId = entry.th32ProcessID;
            processes[count].parentProcessId = entry.th32ParentProcessID;
            lstrcpynW(processes[count].imageName, entry.szExeFile, ARRAYSIZE(processes[count].imageName));
            count++;
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return count;
}

static const PROCESS_NODE *FindProcessNode(const PROCESS_NODE *processes, DWORD count, DWORD processId)
{
    DWORD index = 0;

    for (; index < count; index++)
    {
        if (processes[index].processId == processId)
            return &processes[index];
    }

    return NULL;
}

static BOOL IsDescendantProcess(const PROCESS_NODE *processes, DWORD count, DWORD processId, DWORD ancestorProcessId)
{
    DWORD currentProcessId = processId;
    DWORD depth = 0;

    while (currentProcessId != 0 && depth++ < count)
    {
        const PROCESS_NODE *node = NULL;

        if (currentProcessId == ancestorProcessId)
            return TRUE;

        node = FindProcessNode(processes, count, currentProcessId);
        if (node == NULL || node->parentProcessId == currentProcessId)
            break;

        currentProcessId = node->parentProcessId;
    }

    return FALSE;
}

static BOOL HasLiveGameProcess(DWORD steamProcessId)
{
    PROCESS_NODE *processes = NULL;
    DWORD count = 0;
    DWORD index = 0;

    processes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*processes) * MAX_TRACKED_PROCESSES);
    if (processes == NULL)
        return FALSE;

    count = SnapshotProcesses(processes, MAX_TRACKED_PROCESSES);

    for (; index < count; index++)
    {
        if (processes[index].processId == steamProcessId)
            continue;

        if (!IsDescendantProcess(processes, count, processes[index].processId, steamProcessId))
            continue;

        if (IsIgnoredChildProcessName(processes[index].imageName))
            continue;

        HeapFree(GetProcessHeap(), 0, processes);
        return TRUE;
    }

    HeapFree(GetProcessHeap(), 0, processes);
    return FALSE;
}

static void KillSteamWebHelpers(DWORD steamProcessId)
{
    PROCESS_NODE *processes = NULL;
    DWORD count = 0;
    DWORD index = 0;

    processes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*processes) * MAX_TRACKED_PROCESSES);
    if (processes == NULL)
        return;

    count = SnapshotProcesses(processes, MAX_TRACKED_PROCESSES);

    for (; index < count; index++)
    {
        HANDLE processHandle = NULL;

        if (CompareStringOrdinal(processes[index].imageName, -1, L"steamwebhelper.exe", -1, TRUE) != CSTR_EQUAL)
            continue;

        if (!IsDescendantProcess(processes, count, processes[index].processId, steamProcessId))
            continue;

        processHandle = OpenProcess(PROCESS_TERMINATE, FALSE, processes[index].processId);
        if (processHandle == NULL)
            continue;

        TerminateProcess(processHandle, EXIT_SUCCESS);
        CloseHandle(processHandle);
    }

    HeapFree(GetProcessHeap(), 0, processes);
}

static void ApplyWebHelperState(BOOL shouldDisable, DWORD steamProcessId)
{
    if (shouldDisable)
    {
        InterlockedExchange(&g_webHelperDisabled, TRUE);
        KillSteamWebHelpers(steamProcessId);
        return;
    }

    InterlockedExchange(&g_webHelperDisabled, FALSE);
}

static DWORD WINAPI MonitorThreadProc(LPVOID parameter)
{
    DWORD steamProcessId = GetCurrentProcessId();
    UNREFERENCED_PARAMETER(parameter);

    while (WaitForSingleObject(g_stopEvent, 1000) == WAIT_TIMEOUT)
    {
        DWORD runningAppId = ReadSteamDwordValue(L"SOFTWARE\\Valve\\Steam", L"RunningAppID", 0);
        BOOL appMarkedRunning = runningAppId != 0 && ReadSteamAppRunning(runningAppId);
        BOOL liveGameProcess = HasLiveGameProcess(steamProcessId);
        WEBHELPER_OVERRIDE overrideMode = (WEBHELPER_OVERRIDE)InterlockedCompareExchange(&g_manualOverride, 0, 0);
        BOOL shouldDisable = ShouldDisableWebHelper(runningAppId, appMarkedRunning, liveGameProcess, overrideMode);

        ApplyWebHelperState(shouldDisable, steamProcessId);
    }

    return 0;
}

static void ShowTrayMenu(HWND windowHandle)
{
    HMENU menuHandle = CreatePopupMenu();
    POINT cursorPos = {};
    UINT selectedCommand = 0;
    WEBHELPER_OVERRIDE overrideMode = (WEBHELPER_OVERRIDE)InterlockedCompareExchange(&g_manualOverride, 0, 0);

    AppendMenuW(menuHandle, MF_STRING | (overrideMode == WEBHELPER_AUTO ? MF_CHECKED : 0), ID_TRAY_AUTO, L"Auto");
    AppendMenuW(menuHandle, MF_STRING | (overrideMode == WEBHELPER_FORCE_ENABLE ? MF_CHECKED : 0), ID_TRAY_ON, L"On");
    AppendMenuW(menuHandle, MF_STRING | (overrideMode == WEBHELPER_FORCE_DISABLE ? MF_CHECKED : 0), ID_TRAY_OFF,
                L"Off");

    SetForegroundWindow(windowHandle);
    GetCursorPos(&cursorPos);
    selectedCommand = TrackPopupMenu(menuHandle, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD,
                                     cursorPos.x, cursorPos.y, 0, windowHandle, NULL);

    switch (selectedCommand)
    {
    case ID_TRAY_AUTO:
        InterlockedExchange(&g_manualOverride, WEBHELPER_AUTO);
        break;

    case ID_TRAY_ON:
        InterlockedExchange(&g_manualOverride, WEBHELPER_FORCE_ENABLE);
        break;

    case ID_TRAY_OFF:
        InterlockedExchange(&g_manualOverride, WEBHELPER_FORCE_DISABLE);
        break;

    default:
        break;
    }

    DestroyMenu(menuHandle);
}

static LRESULT CALLBACK WndProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam)
{
    static NOTIFYICONDATAW notifyIconData = {};
    static UINT taskbarCreatedMessage = WM_NULL;

    switch (message)
    {
    case WM_CREATE:
        taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

        notifyIconData.cbSize = sizeof(notifyIconData);
        notifyIconData.hWnd = windowHandle;
        notifyIconData.uCallbackMessage = WM_USER;
        notifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        notifyIconData.hIcon = LoadIconW(NULL, IDI_APPLICATION);
        lstrcpynW(notifyIconData.szTip, L"Steam WebHelper", ARRAYSIZE(notifyIconData.szTip));
        Shell_NotifyIconW(NIM_ADD, &notifyIconData);
        return 0;

    case WM_USER:
        if (lParam == WM_RBUTTONDOWN || lParam == WM_CONTEXTMENU)
            ShowTrayMenu(windowHandle);
        return 0;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &notifyIconData);
        PostQuitMessage(0);
        return 0;

    default:
        if (message == taskbarCreatedMessage)
        {
            Shell_NotifyIconW(NIM_ADD, &notifyIconData);
            return 0;
        }
        break;
    }

    return DefWindowProcW(windowHandle, message, wParam, lParam);
}

static DWORD WINAPI TrayThreadProc(LPVOID parameter)
{
    HINSTANCE instanceHandle = (HINSTANCE)parameter;
    WNDCLASSW windowClass = {};
    MSG message = {};

    windowClass.lpszClassName = L"NoSteamWebHelperTray";
    windowClass.hInstance = instanceHandle;
    windowClass.lpfnWndProc = WndProc;

    if (RegisterClassW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 0;

    if (CreateWindowExW(WS_EX_LEFT | WS_EX_LTRREADING, windowClass.lpszClassName, L"NoSteamWebHelper", WS_OVERLAPPED,
                        0, 0, 0, 0, NULL, NULL, instanceHandle, NULL) == NULL)
        return 0;

    while (GetMessageW(&message, NULL, 0, 0))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return 0;
}

static DWORD WINAPI InitializeThreadProc(LPVOID parameter)
{
    HANDLE threadHandle = NULL;
    UNREFERENCED_PARAMETER(parameter);

    if (WaitForSingleObject(g_stopEvent, 5000) != WAIT_TIMEOUT)
        return 0;

    threadHandle = CreateThread(NULL, 0, MonitorThreadProc, NULL, 0, NULL);
    if (threadHandle != NULL)
        CloseHandle(threadHandle);

    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instanceHandle, DWORD reason, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(reserved);

    if (reason == DLL_PROCESS_ATTACH)
    {
        HANDLE threadHandle = NULL;

        DisableThreadLibraryCalls(instanceHandle);

        if (!IsSteamClientProcess())
            return TRUE;

        g_stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (g_stopEvent == NULL)
            return TRUE;

        threadHandle = CreateThread(NULL, 0, InitializeThreadProc, instanceHandle, 0, NULL);
        if (threadHandle != NULL)
            CloseHandle(threadHandle);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (g_stopEvent != NULL)
        {
            SetEvent(g_stopEvent);
            CloseHandle(g_stopEvent);
            g_stopEvent = NULL;
        }
    }

    return TRUE;
}
