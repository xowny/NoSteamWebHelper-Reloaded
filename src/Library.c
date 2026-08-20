#include <windows.h>
#include <tlhelp32.h>

#include "State.h"

typedef BOOL(WINAPI *pfnGetProcessInformation)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
typedef BOOL(WINAPI *pfnSetProcessInformation)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);

typedef struct PARKED_PROCESS
{
    DWORD processId;
    HANDLE processHandle;
    DWORD originalPriorityClass;
    BOOL originalEfficiencyMode;
    BOOL hasOriginalEfficiencyMode;
} PARKED_PROCESS;

#define MAX_PARKED_PROCESSES 64
static PARKED_PROCESS g_parkedProcesses[MAX_PARKED_PROCESSES] = {};
static LONG g_parkedProcessCount = 0;
static pfnGetProcessInformation g_GetProcessInformation = NULL;
static pfnSetProcessInformation g_SetProcessInformation = NULL;

static void InitProcessInformation(void)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");

    if (kernel32 == NULL)
        return;

    g_GetProcessInformation =
        (pfnGetProcessInformation)GetProcAddress(kernel32, "GetProcessInformation");
    g_SetProcessInformation =
        (pfnSetProcessInformation)GetProcAddress(kernel32, "SetProcessInformation");
}

typedef struct PROCESS_NODE
{
    DWORD processId;
    DWORD parentProcessId;
    WCHAR imageName[MAX_PATH];
} PROCESS_NODE;

#define MAX_TRACKED_PROCESSES 2048

static volatile LONG g_webHelperParked = FALSE;
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
            if (lstrcpynW(processes[count].imageName, entry.szExeFile,
                          ARRAYSIZE(processes[count].imageName)) == NULL)
            {
                processes[count].imageName[0] = L'\0';
            }
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

static void SetEfficiencyMode(HANDLE processHandle, BOOL enable)
{
    /*
     * SetProcessInformation with ProcessPowerThrottling enables the
     * "Efficiency mode" (EcoQoS) badge in Task Manager and asks the
     * scheduler to deprioritise the process for background operation.
     *
     * Available since Windows 10 (build 1511+).  We load the function
     * dynamically so the DLL degrades gracefully on older systems.
     */
    PROCESS_POWER_THROTTLING_STATE ppt = {};

    if (g_SetProcessInformation == NULL)
        return;

    ppt.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    ppt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    ppt.StateMask   = enable ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;

    g_SetProcessInformation(processHandle, ProcessPowerThrottling, &ppt, sizeof(ppt));
}

static BOOL IsAlreadyParked(DWORD processId)
{
    LONG index = 0;

    for (; index < g_parkedProcessCount; index++)
    {
        if (g_parkedProcesses[index].processId == processId)
            return TRUE;
    }

    return FALSE;
}

static void ParkSteamWebHelpers(const PROCESS_NODE *processes, DWORD count, DWORD steamProcessId)
{
    DWORD processIndex = 0;

    if (processes == NULL || count == 0)
        return;

    for (; processIndex < count; processIndex++)
    {
        HANDLE processHandle = NULL;
        PARKED_PROCESS *parked = NULL;
        PROCESS_POWER_THROTTLING_STATE originalPowerThrottling = {};

        if (CompareStringOrdinal(processes[processIndex].imageName, -1,
                                 L"steamwebhelper.exe", -1, TRUE) != CSTR_EQUAL)
            continue;

        if (!IsDescendantProcess(processes, count, processes[processIndex].processId, steamProcessId))
            continue;

        if (IsAlreadyParked(processes[processIndex].processId))
            continue;

        if (g_parkedProcessCount >= MAX_PARKED_PROCESSES)
            continue;

        processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION |
                                        SYNCHRONIZE,
                                    FALSE, processes[processIndex].processId);
        if (processHandle == NULL)
            continue;

        parked = &g_parkedProcesses[g_parkedProcessCount];
        parked->processId = processes[processIndex].processId;
        parked->processHandle = processHandle;
        parked->originalPriorityClass = GetPriorityClass(processHandle);
        parked->hasOriginalEfficiencyMode = FALSE;

        if (g_GetProcessInformation != NULL)
        {
            originalPowerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            parked->hasOriginalEfficiencyMode =
                g_GetProcessInformation(processHandle, ProcessPowerThrottling,
                                        &originalPowerThrottling,
                                        sizeof(originalPowerThrottling));

            if (parked->hasOriginalEfficiencyMode)
            {
                parked->originalEfficiencyMode =
                    (originalPowerThrottling.StateMask &
                     PROCESS_POWER_THROTTLING_EXECUTION_SPEED) != 0;
            }
        }

        /*
         * Keep CEF responsive so Steam IPC calls do not block the game.
         * BELOW_NORMAL plus EcoQoS reduces background contention without
         * the frametime spikes caused by suspending all helper threads.
        */
        SetPriorityClass(processHandle, BELOW_NORMAL_PRIORITY_CLASS);
        if (parked->hasOriginalEfficiencyMode)
            SetEfficiencyMode(processHandle, TRUE);
        g_parkedProcessCount++;
    }
}

static void RestoreParkedWebHelpers(void)
{
    LONG index = 0;
    LONG parkedCount = g_parkedProcessCount;

    if (parkedCount < 0)
        parkedCount = 0;
    else if (parkedCount > MAX_PARKED_PROCESSES)
        parkedCount = MAX_PARKED_PROCESSES;

    for (; index < parkedCount; index++)
    {
        PARKED_PROCESS *parked = &g_parkedProcesses[index];

        if (parked->processHandle == NULL)
        {
            ZeroMemory(parked, sizeof(*parked));
            continue;
        }

#pragma warning(suppress : 6001) /* Guarded above; array storage is statically zero-initialized. */
        if (WaitForSingleObject(parked->processHandle, 0) == WAIT_TIMEOUT)
        {
            if (parked->originalPriorityClass != 0)
                SetPriorityClass(parked->processHandle, parked->originalPriorityClass);

            if (parked->hasOriginalEfficiencyMode)
                SetEfficiencyMode(parked->processHandle, parked->originalEfficiencyMode);
        }

        CloseHandle(parked->processHandle);
        ZeroMemory(parked, sizeof(*parked));
    }

    g_parkedProcessCount = 0;
}

static DWORD WINAPI MonitorThreadProc(LPVOID parameter)
{
    DWORD steamProcessId = GetCurrentProcessId();
    HMODULE pinnedModule = NULL;
    PROCESS_NODE *processes = NULL;
    UNREFERENCED_PARAMETER(parameter);

    /*
     * The proxy is intended to live for the lifetime of steam.exe.
     * Pin it from the worker, after DllMain has released the loader lock,
     * so Steam cannot unload code while this thread is still executing.
     */
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)MonitorThreadProc, &pinnedModule) ||
        pinnedModule == NULL)
    {
        return 0;
    }

    if (WaitForSingleObject(g_stopEvent, 5000) != WAIT_TIMEOUT)
        return 0;

    processes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*processes) * MAX_TRACKED_PROCESSES);

    while (WaitForSingleObject(g_stopEvent, 1000) == WAIT_TIMEOUT)
    {
        DWORD runningAppId = 0;
        BOOL appMarkedRunning = FALSE;
        BOOL shouldPark;

        /* AUTO mode: detect whether a game is running. */
        runningAppId = ReadSteamDwordValue(L"SOFTWARE\\Valve\\Steam", L"RunningAppID", 0);

        if (runningAppId != 0)
            appMarkedRunning = ReadSteamAppRunning(runningAppId);

        shouldPark = ShouldParkWebHelper(runningAppId, appMarkedRunning);

        if (shouldPark &&
            !InterlockedCompareExchange(&g_webHelperParked, TRUE, FALSE))
        {
            if (processes != NULL)
            {
                DWORD count = SnapshotProcesses(processes, MAX_TRACKED_PROCESSES);
                OutputDebugStringW(L"umpdc: game detected, parking webhelpers");
                ParkSteamWebHelpers(processes, count, steamProcessId);
            }

        }
        else if (!shouldPark &&
                 InterlockedCompareExchange(&g_webHelperParked, FALSE, TRUE))
        {
            OutputDebugStringW(L"umpdc: game ended, restoring webhelpers");
            RestoreParkedWebHelpers();
        }
    }

    if (processes != NULL)
        HeapFree(GetProcessHeap(), 0, processes);

    RestoreParkedWebHelpers();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instanceHandle, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instanceHandle);

        if (!IsSteamClientProcess())
            return TRUE;

        InitProcessInformation();

        OutputDebugStringW(L"umpdc: loaded in steam.exe, monitor thread starting");

        g_stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (g_stopEvent == NULL)
            return TRUE;

        {
            HANDLE monitorThreadHandle =
                CreateThread(NULL, 0, MonitorThreadProc, instanceHandle, 0, NULL);

            if (monitorThreadHandle == NULL)
            {
                CloseHandle(g_stopEvent);
                g_stopEvent = NULL;
                return TRUE;
            }

            CloseHandle(monitorThreadHandle);
        }

        return TRUE;
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        /*
         * A successfully started worker pins this proxy until process exit.
         * Windows has already stopped the other threads at that point, and
         * Microsoft recommends an empty process-detach handler. In particular,
         * waiting for a worker here can deadlock on the loader lock.
         */
        if (reserved == NULL && g_stopEvent != NULL)
            SetEvent(g_stopEvent);
    }

    return TRUE;
}
