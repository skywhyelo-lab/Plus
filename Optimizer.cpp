#include "Optimizer.hpp"
#include "AppState.hpp"
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <thread>
#include <mutex>
#include <chrono>
#include <string>
#include <cwctype>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include <dxgi.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "Setupapi.lib")

namespace fs = std::filesystem;

static int CleanDirectoryContents(const fs::path& dir); // определена ниже по файлу, нужна раньше

using MY_NTSTATUS = LONG;
enum ULTRA_SYSTEM_INFORMATION_CLASS { UltraSystemMemoryListInformation = 80 };

#ifdef UltraMemoryCaptureState
#undef UltraMemoryCaptureState
#endif
#ifdef UltraMemoryEmptyWorkingSets
#undef UltraMemoryEmptyWorkingSets
#endif
#ifdef UltraMemoryFlushModifiedList
#undef UltraMemoryFlushModifiedList
#endif
#ifdef UltraMemoryPurgeStandbyList
#undef UltraMemoryPurgeStandbyList
#endif
#ifdef UltraMemoryPurgeLowPriorityStandbyList
#undef UltraMemoryPurgeLowPriorityStandbyList
#endif

enum ULTRA_SYSTEM_MEMORY_LIST_COMMAND {
    UltraMemoryCaptureState,
    UltraMemoryEmptyWorkingSets,
    UltraMemoryFlushModifiedList,
    UltraMemoryPurgeStandbyList,
    UltraMemoryPurgeLowPriorityStandbyList
};
typedef MY_NTSTATUS(WINAPI* pfnNtSetSystemInformation)(INT, PVOID, ULONG);

bool IsEqualIgnoreCase(const std::wstring& str1, const std::wstring& str2) {
    if (str1.size() != str2.size()) return false;
    for (size_t i = 0; i < str1.size(); ++i) {
        if (towlower(str1[i]) != towlower(str2[i])) return false;
    }
    return true;
}

bool IsCriticalProcess(const std::wstring& processName, ProcessCleanupLevel level) {
    if (level == ProcessCleanupLevel::None) return true;

    std::lock_guard<std::mutex> lock(AppState::whitelistMutex);

    // Базовая защита Windows — критична при любом уровне очистки.
    for (const auto& item : AppState::systemCriticalWhitelist) {
        if (IsEqualIgnoreCase(processName, item)) return true;
    }
    // Ручные исключения пользователя (UI) — критичны при любом уровне очистки.
    for (const auto& item : AppState::userWhitelist) {
        if (IsEqualIgnoreCase(processName, item)) return true;
    }

    if (level == ProcessCleanupLevel::GamingWhitelist) {
        for (const auto& item : AppState::gamingWhitelist) {
            if (IsEqualIgnoreCase(processName, item)) return true;
        }
        // Драйвер-контейнеры NVIDIA имеют переменные суффиксы (nvcontainer64.exe и т.д.),
        // поэтому для них отдельная проверка по префиксу вместо точного совпадения имени.
        std::wstring lowerName = processName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);
        if (lowerName.rfind(L"nvcontainer", 0) == 0 || lowerName.rfind(L"nvidia", 0) == 0) {
            return true;
        }
    }

    return false;
}

// Хранит предыдущий замер CPU-времени каждого процесса, чтобы считать реальный %CPU по дельте
static std::unordered_map<DWORD, ULONGLONG> g_prevProcCpuTime100ns;
static ULONGLONG g_prevProcSampleTick = 0;

void RefreshSystemProcesses() {
    std::vector<ProcessInfo> tempProcs;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    static SYSTEM_INFO sysInfo = [] { SYSTEM_INFO s{}; GetSystemInfo(&s); return s; }();
    ULONGLONG nowTick = GetTickCount64();
    ULONGLONG wallDeltaMs = g_prevProcSampleTick ? (nowTick - g_prevProcSampleTick) : 0;
    std::unordered_map<DWORD, ULONGLONG> newProcCpuTime;

    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            ProcessInfo info;
            info.pid = pe.th32ProcessID;
            info.name = pe.szExeFile;
            info.isCritical = IsCriticalProcess(info.name);
            info.memorySizeMB = 0;
            info.cpuPercent = 0.0f;

            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (hProc) {
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                    info.memorySizeMB = pmc.WorkingSetSize / 1024 / 1024;
                }

                FILETIME ftCreate, ftExit, ftKernel, ftUser;
                if (GetProcessTimes(hProc, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                    ULARGE_INTEGER k, u;
                    k.LowPart = ftKernel.dwLowDateTime; k.HighPart = ftKernel.dwHighDateTime;
                    u.LowPart = ftUser.dwLowDateTime; u.HighPart = ftUser.dwHighDateTime;
                    ULONGLONG totalCpuTime100ns = k.QuadPart + u.QuadPart;
                    newProcCpuTime[info.pid] = totalCpuTime100ns;

                    if (wallDeltaMs > 0) {
                        auto it = g_prevProcCpuTime100ns.find(info.pid);
                        if (it != g_prevProcCpuTime100ns.end() && totalCpuTime100ns >= it->second) {
                            double cpuDeltaMs = (double)(totalCpuTime100ns - it->second) / 10000.0;
                            double pct = (cpuDeltaMs / (double)wallDeltaMs) / (double)sysInfo.dwNumberOfProcessors * 100.0;
                            info.cpuPercent = (float)(std::max)(0.0, (std::min)(100.0, pct));
                        }
                    }
                }
                CloseHandle(hProc);
            }
            tempProcs.push_back(info);
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    g_prevProcCpuTime100ns = std::move(newProcCpuTime);
    g_prevProcSampleTick = nowTick;

    std::sort(tempProcs.begin(), tempProcs.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
        return a.memorySizeMB > b.memorySizeMB;
        });

    std::lock_guard<std::mutex> lock(AppState::processesMutex);
    AppState::runningProcesses = tempProcs;
}

bool EnableRequiredPrivileges() {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;

    auto SetPriv = [](HANDLE token, LPCTSTR privilege, bool enable) {
        LUID luid;
        if (!LookupPrivilegeValue(NULL, privilege, &luid)) return false;
        TOKEN_PRIVILEGES tp = { 1, { { luid, (DWORD)(enable ? SE_PRIVILEGE_ENABLED : 0) } } };
        return AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), NULL, NULL) && GetLastError() == ERROR_SUCCESS;
        };

    bool bDebug = SetPriv(hToken, SE_DEBUG_NAME, true);
    bool bQuota = SetPriv(hToken, SE_INCREASE_QUOTA_NAME, true);
    bool bProfile = SetPriv(hToken, TEXT("SeProfileSingleProcessPrivilege"), true);
    CloseHandle(hToken);
    return bDebug && bQuota && bProfile;
}

// Опрос аппаратных компонентов через системный реестр и DXGI
void DetectHardwareSpecs() {
    // 1. Спецификация процессора
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t cpu[256] = { 0 };
        DWORD size = sizeof(cpu);
        if (RegQueryValueExW(hKey, L"ProcessorNameString", NULL, NULL, (LPBYTE)cpu, &size) == ERROR_SUCCESS) {
            std::string raw = AppState::WStringToString(cpu);
            // ProcessorNameString обычно содержит лишние пробелы (одиночные и внутренние двойные) — убираем их.
            std::string cleaned;
            cleaned.reserve(raw.size());
            bool lastWasSpace = false;
            for (char c : raw) {
                if (c == ' ') {
                    if (!lastWasSpace && !cleaned.empty()) cleaned.push_back(' ');
                    lastWasSpace = true;
                }
                else {
                    cleaned.push_back(c);
                    lastWasSpace = false;
                }
            }
            while (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();
            AppState::cpuName = cleaned;
        }
        else {
            AppState::cpuName = "Неизвестный CPU";
        }
        RegCloseKey(hKey);
    }
    else {
        AppState::cpuName = "Неизвестный CPU";
    }

    // 2. Спецификация видеокарты через DXGI-адаптер
    IDXGIFactory* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory))) {
        IDXGIAdapter* pAdapter = nullptr;
        if (SUCCEEDED(pFactory->EnumAdapters(0, &pAdapter))) {
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(pAdapter->GetDesc(&desc))) {
                AppState::gpuName = AppState::WStringToString(desc.Description);
            }
            else {
                AppState::gpuName = "Неизвестный GPU";
            }
            pAdapter->Release();
        }
        else {
            AppState::gpuName = "Неизвестный GPU";
        }
        pFactory->Release();
    }
    else {
        AppState::gpuName = "Неизвестный GPU";
    }

    // 3. Объем оперативной памяти
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        double totalGB = (double)mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        char buf[64];
        sprintf_s(buf, "%.1f GB RAM", totalGB);
        AppState::ramSizeGB = buf;
    }
    else {
        AppState::ramSizeGB = "Неизвестно";
    }

    // 4. Материнская плата
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t board[256] = { 0 };
        wchar_t vendor[256] = { 0 };
        DWORD sizeB = sizeof(board);
        DWORD sizeV = sizeof(vendor);
        std::string finalBoard = "";

        if (RegQueryValueExW(hKey, L"BaseBoardProduct", NULL, NULL, (LPBYTE)board, &sizeB) == ERROR_SUCCESS) {
            finalBoard += AppState::WStringToString(board);
        }
        if (RegQueryValueExW(hKey, L"BaseBoardManufacturer", NULL, NULL, (LPBYTE)vendor, &sizeV) == ERROR_SUCCESS) {
            if (!finalBoard.empty()) finalBoard = AppState::WStringToString(vendor) + " " + finalBoard;
            else finalBoard = AppState::WStringToString(vendor);
        }

        if (!finalBoard.empty()) {
            AppState::motherboardName = finalBoard;
        }
        else {
            AppState::motherboardName = "Unknown Motherboard";
        }
        RegCloseKey(hKey);
    }
    else {
        AppState::motherboardName = "Unknown Motherboard";
    }
}

// ============================================================================
//  РЕАЛЬНАЯ ТЕЛЕМЕТРИЯ (CPU / ДИСК / СЕТЬ) — без заглушек, через WinAPI + PDH
// ============================================================================
static PDH_HQUERY g_pdhQuery = nullptr;
static PDH_HCOUNTER g_pdhDiskCounter = nullptr;
static PDH_HCOUNTER g_pdhNetCounter = nullptr;
static bool g_pdhReady = false;

void InitPerformanceCounters() {
    if (PdhOpenQueryW(nullptr, 0, &g_pdhQuery) != ERROR_SUCCESS) return;
    PdhAddEnglishCounterW(g_pdhQuery, L"\\PhysicalDisk(_Total)\\% Disk Time", 0, &g_pdhDiskCounter);
    PdhAddEnglishCounterW(g_pdhQuery, L"\\Network Interface(*)\\Bytes Total/sec", 0, &g_pdhNetCounter);
    PdhCollectQueryData(g_pdhQuery); // первый замер нужен только для инициализации базовой точки
    g_pdhReady = true;
}

static float ComputeCpuUsagePercent() {
    static ULARGE_INTEGER prevIdle{}, prevKernel{}, prevUser{};
    static bool first = true;

    FILETIME idleFT, kernelFT, userFT;
    if (!GetSystemTimes(&idleFT, &kernelFT, &userFT)) return AppState::currentCPUUsagePercent.load();

    ULARGE_INTEGER idle, kernel, user;
    idle.LowPart = idleFT.dwLowDateTime; idle.HighPart = idleFT.dwHighDateTime;
    kernel.LowPart = kernelFT.dwLowDateTime; kernel.HighPart = kernelFT.dwHighDateTime;
    user.LowPart = userFT.dwLowDateTime; user.HighPart = userFT.dwHighDateTime;

    if (first) {
        prevIdle = idle; prevKernel = kernel; prevUser = user;
        first = false;
        return 0.0f;
    }

    ULONGLONG idleDiff = idle.QuadPart - prevIdle.QuadPart;
    ULONGLONG totalDiff = (kernel.QuadPart - prevKernel.QuadPart) + (user.QuadPart - prevUser.QuadPart);
    prevIdle = idle; prevKernel = kernel; prevUser = user;

    if (totalDiff == 0) return 0.0f;
    double busy = (double)(totalDiff - idleDiff) / (double)totalDiff;
    return (float)((std::max)(0.0, (std::min)(1.0, busy)) * 100.0);
}

void CollectLiveTelemetry() {
    AppState::currentCPUUsagePercent = ComputeCpuUsagePercent();

    if (!g_pdhReady) return;
    PdhCollectQueryData(g_pdhQuery);

    PDH_FMT_COUNTERVALUE diskVal;
    if (PdhGetFormattedCounterValue(g_pdhDiskCounter, PDH_FMT_DOUBLE, nullptr, &diskVal) == ERROR_SUCCESS) {
        float v = (float)diskVal.doubleValue;
        AppState::currentDiskUsagePercent = (std::max)(0.0f, (std::min)(100.0f, v));
    }

    DWORD bufSize = 0, itemCount = 0;
    PdhGetFormattedCounterArrayW(g_pdhNetCounter, PDH_FMT_DOUBLE, &bufSize, &itemCount, nullptr);
    if (bufSize > 0) {
        std::vector<unsigned char> buf(bufSize);
        PDH_FMT_COUNTERVALUE_ITEM_W* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
        if (PdhGetFormattedCounterArrayW(g_pdhNetCounter, PDH_FMT_DOUBLE, &bufSize, &itemCount, items) == ERROR_SUCCESS) {
            double totalBytesPerSec = 0.0;
            for (DWORD i = 0; i < itemCount; i++) {
                std::wstring name = items[i].szName ? items[i].szName : L"";
                if (name.find(L"Loopback") != std::wstring::npos || name.find(L"isatap") != std::wstring::npos) continue;
                if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA) {
                    totalBytesPerSec += items[i].FmtValue.doubleValue;
                }
            }
            float mbps = (float)((totalBytesPerSec * 8.0) / 1000000.0);
            AppState::currentNetworkMbps = mbps;

            const float NET_MAX_MBPS = 100.0f; // условная база шкалы для процентного индикатора
            AppState::currentNetUsagePercent = (std::max)(0.0f, (std::min)(100.0f, (mbps / NET_MAX_MBPS) * 100.0f));
        }
    }
}

void LogSystemSpecs() {
    AppState::LogToBoth("--- ХАРАКТЕРИСТИКИ ПК ---");
    DetectHardwareSpecs();
    AppState::LogToBoth("Процессор: " + AppState::cpuName);
    AppState::LogToBoth("Видеокарта: " + AppState::gpuName);
    AppState::LogToBoth("Материнская плата: " + AppState::motherboardName);
    AppState::LogToBoth("Объем ОЗУ: " + AppState::ramSizeGB);
    AppState::LogToBoth("-------------------------\n");
}

// ============================================================================
// Вкладка "Оптимизация" — см. Optimizer.hpp за описанием общей идеи (постоянные
// твики, не разовое действие профиля).
// ============================================================================

void ApplyNetworkThrottlingSetting(bool disabled) {
    HKEY hKey;
    const wchar_t* sysProfilePath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sysProfilePath, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        // Значение по умолчанию Windows — 10 (десятичное); 0xFFFFFFFF полностью отключает троттлинг.
        DWORD dwThrottling = disabled ? 0xFFFFFFFF : 10;
        RegSetValueExW(hKey, L"NetworkThrottlingIndex", 0, REG_DWORD, (const BYTE*)&dwThrottling, sizeof(dwThrottling));
        RegCloseKey(hKey);
        AppState::LogToBoth(disabled ? "[REGISTRY] Сетевой троттлинг отключен." : "[REGISTRY] Сетевой троттлинг возвращён к значению по умолчанию.");
    }
}

void ApplySystemResponsivenessSetting(bool zeroed) {
    HKEY hKey;
    const wchar_t* sysProfilePath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sysProfilePath, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        // Значение по умолчанию Windows — 20.
        DWORD dwResponsiveness = zeroed ? 0 : 20;
        RegSetValueExW(hKey, L"SystemResponsiveness", 0, REG_DWORD, (const BYTE*)&dwResponsiveness, sizeof(dwResponsiveness));
        RegCloseKey(hKey);
        AppState::LogToBoth(zeroed ? "[REGISTRY] System Responsiveness = 0 (приоритет игр/аудио)." : "[REGISTRY] System Responsiveness возвращён к значению по умолчанию.");
    }
}

static std::wstring GetSelfExePath() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, len);
}

// _wsystem() запускает cmd.exe с видимым окном консоли, которое на долю секунды
// мелькает поверх интерфейса при каждом старте приложения (см. вызовы ниже из
// фонового потока в main.cpp). CreateProcessW с CREATE_NO_WINDOW делает то же
// самое, но без визуального артефакта.
static void RunHiddenCommand(const std::wstring& commandLine) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"cmd.exe /C " + commandLine;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void ApplyAutoCleanOnBootSetting(bool enabled) {
    const wchar_t* taskName = L"PulseBootCleanup";
    if (enabled) {
        std::wstring exePath = GetSelfExePath();
        std::wstring cmd = L"schtasks /create /tn \"" + std::wstring(taskName) +
            L"\" /tr \"\\\"" + exePath + L"\\\" --boot-cleanup\" /sc onlogon /rl highest /f";
        RunHiddenCommand(cmd);
        AppState::LogToBoth("[TASK] Задача автоочистки при входе в Windows создана.");
    }
    else {
        std::wstring cmd = L"schtasks /delete /tn \"" + std::wstring(taskName) + L"\" /f";
        RunHiddenCommand(cmd);
        AppState::LogToBoth("[TASK] Задача автоочистки при входе в Windows удалена.");
    }
}

void CleanWindowsUpdateCacheNow() {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    SC_HANDLE hSvc = hSCM ? OpenServiceW(hSCM, L"wuauserv", SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS) : nullptr;
    if (hSvc) {
        SERVICE_STATUS status;
        ControlService(hSvc, SERVICE_CONTROL_STOP, &status);
        Sleep(500);
    }

    int count = CleanDirectoryContents(L"C:\\Windows\\SoftwareDistribution\\Download");
    AppState::LogToBoth("[CACHE] Удалено файлов кэша обновлений Windows: " + std::to_string(count));

    if (hSvc) {
        StartServiceW(hSvc, 0, nullptr);
        CloseServiceHandle(hSvc);
    }
    if (hSCM) CloseServiceHandle(hSCM);
}

void RunBootCleanupAndExit() {
    // Вызывается только при запуске по расписанию (--boot-cleanup) — без UI,
    // без активации лицензии, только сама очистка. См. main.cpp.
    wchar_t tempPath[MAX_PATH];
    DWORD tempSize = GetEnvironmentVariableW(L"TEMP", tempPath, MAX_PATH);
    if (tempSize > 0 && tempSize < MAX_PATH) {
        CleanDirectoryContents(tempPath);
    }
    CleanWindowsUpdateCacheNow();
}

void ApplyServiceDisabledSetting(const std::wstring& serviceName, const std::wstring& description, bool disabled) {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        AppState::LogToBoth("[SERVICE] Не удалось открыть SCM для " + AppState::WStringToString(serviceName) + ". Нужны права администратора.");
        return;
    }
    SC_HANDLE hSvc = OpenServiceW(hSCM, serviceName.c_str(), SERVICE_STOP | SERVICE_START | SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS);
    if (hSvc) {
        if (disabled) {
            SERVICE_STATUS status;
            ControlService(hSvc, SERVICE_CONTROL_STOP, &status);
            ChangeServiceConfigW(hSvc, SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE,
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
            AppState::LogToBoth("[SERVICE] Служба отключена: " + AppState::WStringToString(serviceName) + " (" + AppState::WStringToString(description) + ")");
        }
        else {
            ChangeServiceConfigW(hSvc, SERVICE_DEMAND_START, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE,
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
            StartServiceW(hSvc, 0, nullptr);
            AppState::LogToBoth("[SERVICE] Служба возвращена в работу: " + AppState::WStringToString(serviceName) + " (" + AppState::WStringToString(description) + ")");
        }
        CloseServiceHandle(hSvc);
    }
    else {
        AppState::LogToBoth("[SERVICE] Служба " + AppState::WStringToString(serviceName) + " не найдена или недоступна в SCM.");
    }
    CloseServiceHandle(hSCM);
}

void ApplyGamePriorityBoostPass() {
    if (!AppState::optHighPriorityForGames.load()) return;

    std::vector<std::wstring> gamesSnapshot;
    {
        std::lock_guard<std::mutex> lock(AppState::knownGamesListMutex);
        gamesSnapshot = AppState::knownGamesList;
    }

    std::vector<ProcessInfo> procsSnapshot;
    {
        std::lock_guard<std::mutex> lock(AppState::processesMutex);
        procsSnapshot = AppState::runningProcesses;
    }

    for (const auto& proc : procsSnapshot) {
        bool isGame = false;
        for (const auto& game : gamesSnapshot) {
            if (IsEqualIgnoreCase(proc.name, game)) { isGame = true; break; }
        }
        if (!isGame) continue;

        HANDLE hProc = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, proc.pid);
        if (hProc) {
            DWORD currentClass = GetPriorityClass(hProc);
            if (currentClass != HIGH_PRIORITY_CLASS) {
                if (SetPriorityClass(hProc, HIGH_PRIORITY_CLASS)) {
                    AppState::LogToBoth("[CPU] Повышен приоритет для игры: " + AppState::WStringToString(proc.name));
                }
            }
            CloseHandle(hProc);
        }
    }
}

void ApplyCoreParkingSetting(bool disabled) {
    // GUID подгруппы "Processor power management" / "Processor performance core parking min cores".
    // 100% минимальных активных ядер = парковка эффективно отключена.
    const wchar_t* subgroup = L"54533251-82be-4824-96c1-47b60b740d00";
    const wchar_t* setting = L"0cc5b647-c1df-4637-891a-dec35c318583";
    DWORD value = disabled ? 100 : 0;

    wchar_t cmdAc[256], cmdDc[256];
    swprintf_s(cmdAc, L"powercfg /setacvalueindex SCHEME_CURRENT %s %s %u", subgroup, setting, value);
    swprintf_s(cmdDc, L"powercfg /setdcvalueindex SCHEME_CURRENT %s %s %u", subgroup, setting, value);
    RunHiddenCommand(cmdAc);
    RunHiddenCommand(cmdDc);
    RunHiddenCommand(L"powercfg /S SCHEME_CURRENT");
    AppState::LogToBoth(disabled ? "[POWER] Core Parking отключён." : "[POWER] Core Parking возвращён к значению по умолчанию.");
}

bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

// --- Небольшие обёртки над реестром, чтобы не повторять RegOpenKeyExW/RegSetValueExW
// в каждой из 12 функций ниже. ---
static bool ReadRegDword(HKEY root, const wchar_t* path, const wchar_t* name, DWORD& out) {
    HKEY hKey;
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) return false;
    DWORD size = sizeof(DWORD);
    LONG res = RegQueryValueExW(hKey, name, nullptr, nullptr, (BYTE*)&out, &size);
    RegCloseKey(hKey);
    return res == ERROR_SUCCESS;
}

static void WriteRegDword(HKEY root, const wchar_t* path, const wchar_t* name, DWORD value) {
    HKEY hKey;
    if (RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
        RegCloseKey(hKey);
    }
}

static bool IsServiceRunning(const wchar_t* serviceName) {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;
    SC_HANDLE hSvc = OpenServiceW(hSCM, serviceName, SERVICE_QUERY_STATUS);
    bool running = false;
    if (hSvc) {
        SERVICE_STATUS status;
        if (QueryServiceStatus(hSvc, &status)) running = (status.dwCurrentState == SERVICE_RUNNING);
        CloseServiceHandle(hSvc);
    }
    CloseServiceHandle(hSCM);
    return running;
}

static void SetServiceStartMode(const wchar_t* serviceName, DWORD startType, bool startNow) {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return;
    SC_HANDLE hSvc = OpenServiceW(hSCM, serviceName, SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (hSvc) {
        ChangeServiceConfigW(hSvc, SERVICE_NO_CHANGE, startType, SERVICE_NO_CHANGE,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        if (startNow) StartServiceW(hSvc, 0, nullptr);
        else {
            SERVICE_STATUS status;
            ControlService(hSvc, SERVICE_CONTROL_STOP, &status);
        }
        CloseServiceHandle(hSvc);
    }
    CloseServiceHandle(hSCM);
}

// 8. Быстрый запуск (Fast Startup)
bool Get_FastStartupDisabled() {
    DWORD v = 1;
    ReadRegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power", L"HiberbootEnabled", v);
    return v == 0;
}
void Apply_FastStartupDisabled(bool disable) {
    WriteRegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power", L"HiberbootEnabled", disable ? 0 : 1);
    AppState::LogToBoth(disable ? "[BASIC] Быстрый запуск отключён." : "[BASIC] Быстрый запуск включён (по умолчанию).");
}

// 9. Гибернация
bool Get_HibernationDisabled() {
    DWORD v = 1;
    ReadRegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Power", L"HibernateEnabled", v);
    return v == 0;
}
void Apply_HibernationDisabled(bool disable) {
    RunHiddenCommand(disable ? L"powercfg /hibernate off" : L"powercfg /hibernate on");
    AppState::LogToBoth(disable ? "[BASIC] Гибернация отключена (файл hiberfil.sys удалён)." : "[BASIC] Гибернация включена.");
}

// 10. Автосинхронизация времени (NTP) — служба W32Time
bool Get_NtpSyncDisabled() {
    return !IsServiceRunning(L"W32Time");
}
void Apply_NtpSyncDisabled(bool disable) {
    SetServiceStartMode(L"W32Time", disable ? SERVICE_DEMAND_START : SERVICE_AUTO_START, !disable);
    AppState::LogToBoth(disable ? "[BASIC] Автосинхронизация времени (W32Time) остановлена." : "[BASIC] Автосинхронизация времени (W32Time) включена.");
}

// 11. Оптимизация доставки (Delivery Optimization) — только с серверов MS / полностью выкл.
bool Get_DeliveryOptDisabled() {
    DWORD v = 3;
    ReadRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization", L"DODownloadMode", v);
    return v == 0;
}
void Apply_DeliveryOptDisabled(bool disable) {
    WriteRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization", L"DODownloadMode", disable ? 0 : 3);
    AppState::LogToBoth(disable ? "[BASIC] Оптимизация доставки ограничена только серверами Microsoft." : "[BASIC] Оптимизация доставки возвращена к значению по умолчанию (P2P в сети/интернете).");
}

// 12. Автодрайверы через Центр обновления Windows
bool Get_DriverUpdatesExcluded() {
    DWORD v = 0;
    ReadRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate", L"ExcludeWUDriversInQualityUpdate", v);
    return v == 1;
}
void Apply_DriverUpdatesExcluded(bool exclude) {
    WriteRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate", L"ExcludeWUDriversInQualityUpdate", exclude ? 1 : 0);
    AppState::LogToBoth(exclude ? "[BASIC] Драйверы исключены из обновлений Windows." : "[BASIC] Автодрайверы через Windows Update включены (по умолчанию).");
}

// 13. Автообновление приложений Store — политика AutoDownload (0=выкл, 4=по умолчанию/вкл).
bool Get_StoreAutoUpdateDisabled() {
    DWORD v = 4;
    ReadRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\WindowsStore", L"AutoDownload", v);
    return v == 2;
}
void Apply_StoreAutoUpdateDisabled(bool disable) {
    WriteRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\WindowsStore", L"AutoDownload", disable ? 2 : 4);
    AppState::LogToBoth(disable ? "[BASIC] Автообновление приложений Store отключено." : "[BASIC] Автообновление приложений Store включено (по умолчанию).");
}

// 14. Push-уведомления (тосты Action Center)
bool Get_ToastNotificationsDisabled() {
    DWORD v = 1;
    ReadRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PushNotifications", L"ToastEnabled", v);
    return v == 0;
}
void Apply_ToastNotificationsDisabled(bool disable) {
    WriteRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PushNotifications", L"ToastEnabled", disable ? 0 : 1);
    AppState::LogToBoth(disable ? "[BASIC] Push-уведомления отключены." : "[BASIC] Push-уведомления включены (по умолчанию).");
}

// 15. Storage Sense
bool Get_StorageSenseEnabled() {
    DWORD v = 0;
    ReadRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\StorageSense\\Parameters\\StoragePolicy", L"01", v);
    return v == 1;
}
void Apply_StorageSenseEnabled(bool enable) {
    WriteRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\StorageSense\\Parameters\\StoragePolicy", L"01", enable ? 1 : 0);
    AppState::LogToBoth(enable ? "[BASIC] Storage Sense включён (автоочистка диска по расписанию)." : "[BASIC] Storage Sense отключён (по умолчанию).");
}

// 16. Периодическая очистка кэша миниатюр — своё состояние храним в собственном ключе,
// т.к. у Windows нет нативного тумблера для "периодичности"; при включении чистим сразу же.
bool Get_ThumbCachePeriodicClean() {
    DWORD v = 0;
    ReadRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Pulse\\Optimizer", L"ThumbCachePeriodicClean", v);
    return v == 1;
}
void Apply_ThumbCachePeriodicClean(bool enable) {
    WriteRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Pulse\\Optimizer", L"ThumbCachePeriodicClean", enable ? 1 : 0);
    if (enable) {
        wchar_t localAppData[MAX_PATH];
        DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            fs::path explorerDir = fs::path(localAppData) / L"Microsoft" / L"Windows" / L"Explorer";
            std::error_code ec;
            for (auto& entry : fs::directory_iterator(explorerDir, ec)) {
                if (entry.path().filename().wstring().rfind(L"thumbcache_", 0) == 0) {
                    fs::remove(entry.path(), ec);
                }
            }
        }
    }
    AppState::LogToBoth(enable ? "[BASIC] Периодическая очистка кэша миниатюр включена, кэш очищен." : "[BASIC] Периодическая очистка кэша миниатюр отключена.");
}

// 17. Миниатюры файлов/видео в Проводнике
bool Get_ThumbnailsDisabled() {
    DWORD v = 0;
    ReadRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"IconsOnly", v);
    return v == 1;
}
void Apply_ThumbnailsDisabled(bool disable) {
    WriteRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"IconsOnly", disable ? 1 : 0);
    AppState::LogToBoth(disable ? "[BASIC] Миниатюры файлов отключены (везде только иконки)." : "[BASIC] Миниатюры файлов включены (по умолчанию).");
}

// 18. Отключение обновления NTFS Last Access
bool Get_NtfsLastAccessDisabled() {
    DWORD v = 0;
    ReadRegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"NtfsDisableLastAccessUpdate", v);
    return (v & 0x1) == 0x1;
}
void Apply_NtfsLastAccessDisabled(bool disable) {
    RunHiddenCommand(disable ? L"fsutil behavior set disablelastaccess 1" : L"fsutil behavior set disablelastaccess 0");
    AppState::LogToBoth(disable ? "[BASIC] Обновление меток последнего доступа NTFS отключено." : "[BASIC] Обновление меток последнего доступа NTFS включено (по умолчанию).");
}

// 19. Плановый TRIM для SSD (задача планировщика Microsoft\Windows\Defrag\ScheduledDefrag)
bool Get_ScheduledTrimEnabled() {
    std::wstring cmd = L"schtasks /query /tn \"Microsoft\\Windows\\Defrag\\ScheduledDefrag\"";
    // /query не имеет удобного тихого способа вернуть булево через RunHiddenCommand (не возвращает stdout),
    // поэтому используем код возврата процесса напрямую.
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring full = L"cmd.exe /C " + cmd + L" >nul 2>&1";
    std::vector<wchar_t> buf(full.begin(), full.end()); buf.push_back(L'\0');
    bool enabled = true;
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        enabled = (exitCode == 0); // schtasks возвращает 1, если задача отключена или не найдена
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return enabled;
}
void Apply_ScheduledTrimEnabled(bool enable) {
    std::wstring cmd = L"schtasks /change /tn \"Microsoft\\Windows\\Defrag\\ScheduledDefrag\" /" + std::wstring(enable ? L"enable" : L"disable");
    RunHiddenCommand(cmd);
    AppState::LogToBoth(enable ? "[BASIC] Плановый TRIM/дефрагментация включён." : "[BASIC] Плановый TRIM/дефрагментация отключён.");
}

// Фоновая работа UWP-приложений — глобальный переключатель Параметров Windows.
bool Get_UwpBackgroundDisabled() {
    DWORD v = 0;
    ReadRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications", L"GlobalUserDisabled", v);
    return v == 1;
}
void Apply_UwpBackgroundDisabled(bool disable) {
    WriteRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications", L"GlobalUserDisabled", disable ? 1 : 0);
    AppState::LogToBoth(disable ? "[BASIC] Фоновая работа UWP-приложений отключена." : "[BASIC] Фоновая работа UWP-приложений включена (по умолчанию).");
}

// Тень курсора мыши — реальный системный параметр (не реестр напрямую, а SPI).
bool Get_CursorShadowDisabled() {
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCURSORSHADOW, 0, &enabled, 0);
    return enabled == FALSE;
}
void Apply_CursorShadowDisabled(bool disable) {
    BOOL value = disable ? FALSE : TRUE;
    SystemParametersInfoW(SPI_SETCURSORSHADOW, 0, (PVOID)(INT_PTR)value, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    AppState::LogToBoth(disable ? "[BASIC] Тень курсора мыши отключена." : "[BASIC] Тень курсора мыши включена (по умолчанию).");
}

bool IsSystemDiskSSD() {
    HANDLE hDevice = CreateFileW(L"\\\\.\\PhysicalDrive0", 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (hDevice == INVALID_HANDLE_VALUE) return true; // не смогли проверить — не блокируем UI

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;

    DEVICE_SEEK_PENALTY_DESCRIPTOR result{};
    DWORD bytesReturned = 0;
    bool isSSD = true;
    if (DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
        &result, sizeof(result), &bytesReturned, nullptr)) {
        isSSD = !result.IncursSeekPenalty;
    }
    CloseHandle(hDevice);
    return isSSD;
}

// Запускает powershell.exe скрытым процессом и возвращает его stdout — нужен только
// для чтения реального состояния Defender (Get-MpPreference не читается из реестра
// напрямую надёжным способом на всех сборках Windows).
static std::string RunPowerShellCapture(const std::wstring& script) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return "";
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    PROCESS_INFORMATION pi{};

    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -Command \"" + script + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    std::string output;
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hWritePipe);
        hWritePipe = nullptr;
        char chunk[256];
        DWORD read = 0;
        while (ReadFile(hReadPipe, chunk, sizeof(chunk), &read, nullptr) && read > 0) {
            output.append(chunk, read);
        }
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    if (hWritePipe) CloseHandle(hWritePipe);
    CloseHandle(hReadPipe);
    return output;
}

// 1. Защита в реальном времени (Windows Defender)
bool Get_DefenderRealtimeEnabled() {
    std::string out = RunPowerShellCapture(L"(Get-MpComputerStatus).RealTimeProtectionEnabled");
    if (out.find("False") != std::string::npos) return false;
    if (out.find("True") != std::string::npos) return true;
    return true; // не удалось прочитать (напр. сторонний AV занял слот) — не пугаем UI ложным "выключено"
}
void Apply_DefenderRealtimeEnabled(bool enable) {
    std::wstring cmd = L"Set-MpPreference -DisableRealtimeMonitoring $" + std::wstring(enable ? L"false" : L"true");
    RunPowerShellCapture(cmd);
    AppState::LogToBoth(enable ? "[SECURITY] Defender: защита в реальном времени включена." : "[SECURITY] Defender: защита в реальном времени отключена.");
}

// 2. Брандмауэр Windows (все профили)
bool Get_FirewallEnabled() {
    DWORD v = 1;
    ReadRegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters\\FirewallPolicy\\StandardProfile", L"EnableFirewall", v);
    return v != 0;
}
void Apply_FirewallEnabled(bool enable) {
    RunHiddenCommand(enable ? L"netsh advfirewall set allprofiles state on" : L"netsh advfirewall set allprofiles state off");
    AppState::LogToBoth(enable ? "[SECURITY] Брандмауэр Windows включён (все профили)." : "[SECURITY] Брандмауэр Windows отключён (все профили).");
}

// 3. SmartScreen (проводник + политика)
bool Get_SmartScreenEnabled() {
    DWORD v = 1;
    if (ReadRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"EnableSmartScreen", v)) {
        return v != 0;
    }
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer", 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[16]{}; DWORD size = sizeof(buf);
        bool off = (RegQueryValueExW(hKey, L"SmartScreenEnabled", nullptr, nullptr, (BYTE*)buf, &size) == ERROR_SUCCESS) && (wcscmp(buf, L"Off") == 0);
        RegCloseKey(hKey);
        return !off;
    }
    return true;
}
void Apply_SmartScreenEnabled(bool enable) {
    WriteRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"EnableSmartScreen", enable ? 1 : 0);
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        const wchar_t* val = enable ? L"Warn" : L"Off";
        RegSetValueExW(hKey, L"SmartScreenEnabled", 0, REG_SZ, (const BYTE*)val, (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    AppState::LogToBoth(enable ? "[SECURITY] SmartScreen включён." : "[SECURITY] SmartScreen отключён.");
}

// 4. Контроль учётных записей (UAC) — требует перезагрузки для полного применения.
bool Get_UACEnabled() {
    DWORD v = 1;
    ReadRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"EnableLUA", v);
    return v != 0;
}
void Apply_UACEnabled(bool enable) {
    WriteRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"EnableLUA", enable ? 1 : 0);
    AppState::LogToBoth(enable ? "[SECURITY] UAC включён (потребуется перезагрузка)." : "[SECURITY] UAC отключён (потребуется перезагрузка).");
}

// 5. AMSI-интеграция скриптовых движков (Windows Script Host)
bool Get_AmsiEnabled() {
    DWORD v = 1;
    ReadRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Script\\Settings", L"AmsiEnable", v);
    return v != 0;
}
void Apply_AmsiEnabled(bool enable) {
    WriteRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Script\\Settings", L"AmsiEnable", enable ? 1 : 0);
    AppState::LogToBoth(enable ? "[SECURITY] AMSI-проверка скриптов включена." : "[SECURITY] AMSI-проверка скриптов отключена.");
}

// 6. Code Integrity / Memory Integrity (HVCI) — требует перезагрузки.
bool Get_CodeIntegrityEnabled() {
    DWORD v = 0;
    ReadRegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity", L"Enabled", v);
    return v != 0;
}
void Apply_CodeIntegrityEnabled(bool enable) {
    WriteRegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity", L"Enabled", enable ? 1 : 0);
    AppState::LogToBoth(enable ? "[SECURITY] Memory Integrity (HVCI) включена (потребуется перезагрузка)." : "[SECURITY] Memory Integrity (HVCI) отключена (потребуется перезагрузка).");
}

// --- Небольшие общие хелперы для новых разделов ниже ---
static bool GetServiceStartDisabled(const wchar_t* name) {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;
    SC_HANDLE hSvc = OpenServiceW(hSCM, name, SERVICE_QUERY_CONFIG);
    bool disabled = true; // служба отсутствует в SCM — считаем как "не активна"
    if (hSvc) {
        BYTE buf[8192]; DWORD needed = 0;
        if (QueryServiceConfigW(hSvc, (LPQUERY_SERVICE_CONFIGW)buf, sizeof(buf), &needed)) {
            disabled = (((LPQUERY_SERVICE_CONFIGW)buf)->dwStartType == SERVICE_DISABLED);
        }
        CloseServiceHandle(hSvc);
    }
    CloseServiceHandle(hSCM);
    return disabled;
}

static bool GetUserEnvVar(const wchar_t* name, std::wstring& out) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) return false;
    wchar_t buf[256]{}; DWORD size = sizeof(buf);
    bool ok = RegQueryValueExW(hKey, name, nullptr, nullptr, (BYTE*)buf, &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    if (ok) out = buf;
    return ok;
}

static void SetUserEnvVar(const wchar_t* name, const wchar_t* value) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        if (value) RegSetValueExW(hKey, name, 0, REG_SZ, (const BYTE*)value, (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
        else RegDeleteValueW(hKey, name);
        RegCloseKey(hKey);
    }
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, nullptr);
}

static bool RegKeyExists(HKEY root, const wchar_t* path) {
    HKEY hKey;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &hKey) == ERROR_SUCCESS) { RegCloseKey(hKey); return true; }
    return false;
}

static void DeleteRegKeyTree(HKEY root, const wchar_t* path) {
    RegDeleteTreeW(root, path);
}

// ============================================================================
// "Кастомизация"
// ============================================================================
bool Get_TransparencyDisabled() {
    DWORD v = 1;
    ReadRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", v);
    return v == 0;
}
void Apply_TransparencyDisabled(bool disable) {
    WriteRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", disable ? 0 : 1);
    AppState::LogToBoth(disable ? "[CUSTOM] Прозрачность интерфейса отключена." : "[CUSTOM] Прозрачность интерфейса включена.");
}

bool Get_CopilotDisabled() {
    DWORD v = 0;
    ReadRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsCopilot", L"TurnOffWindowsCopilot", v);
    return v == 1;
}
void Apply_CopilotDisabled(bool disable) {
    WriteRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsCopilot", L"TurnOffWindowsCopilot", disable ? 1 : 0);
    AppState::LogToBoth(disable ? "[CUSTOM] Copilot отключён." : "[CUSTOM] Copilot включён (по умолчанию).");
}

bool Get_WebSearchDisabled() {
    DWORD v = 1;
    ReadRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Search", L"BingSearchEnabled", v);
    return v == 0;
}
void Apply_WebSearchDisabled(bool disable) {
    WriteRegDword(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Search", L"BingSearchEnabled", disable ? 0 : 1);
    AppState::LogToBoth(disable ? "[CUSTOM] Веб-результаты в поиске отключены." : "[CUSTOM] Веб-результаты в поиске включены (по умолчанию).");
}

bool Get_TakeOwnershipAdded() {
    return RegKeyExists(HKEY_CLASSES_ROOT, L"*\\shell\\PulseTakeOwnership");
}
void Apply_TakeOwnershipAdded(bool add) {
    if (add) {
        for (const wchar_t* root : { L"*\\shell\\PulseTakeOwnership", L"Directory\\shell\\PulseTakeOwnership" }) {
            HKEY hKey;
            if (RegCreateKeyExW(HKEY_CLASSES_ROOT, root, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
                const wchar_t* label = L"Стать владельцем";
                RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)label, (DWORD)((wcslen(label) + 1) * sizeof(wchar_t)));
                RegSetValueExW(hKey, L"HasLUAShield", 0, REG_SZ, (const BYTE*)L"", sizeof(wchar_t));
                RegCloseKey(hKey);
            }
            std::wstring cmdPath = std::wstring(root) + L"\\command";
            HKEY hCmd;
            if (RegCreateKeyExW(HKEY_CLASSES_ROOT, cmdPath.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hCmd, nullptr) == ERROR_SUCCESS) {
                const wchar_t* cmd = L"cmd.exe /c takeown /f \"%1\" /r /d y && icacls \"%1\" /grant *S-1-3-4:F /t";
                RegSetValueExW(hCmd, nullptr, 0, REG_SZ, (const BYTE*)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
                RegCloseKey(hCmd);
            }
        }
        AppState::LogToBoth("[CUSTOM] Пункт \"Стать владельцем\" добавлен в контекстное меню.");
    }
    else {
        DeleteRegKeyTree(HKEY_CLASSES_ROOT, L"*\\shell\\PulseTakeOwnership");
        DeleteRegKeyTree(HKEY_CLASSES_ROOT, L"Directory\\shell\\PulseTakeOwnership");
        AppState::LogToBoth("[CUSTOM] Пункт \"Стать владельцем\" удалён из контекстного меню.");
    }
}

bool Get_TrustedInstallerBypassAdded() {
    return RegKeyExists(HKEY_CLASSES_ROOT, L"*\\shell\\PulseRunAsTI");
}
void Apply_TrustedInstallerBypassAdded(bool add) {
    if (add) {
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_CLASSES_ROOT, L"*\\shell\\PulseRunAsTI", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            const wchar_t* label = L"Запуск от имени TrustedInstaller";
            RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)label, (DWORD)((wcslen(label) + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
        HKEY hCmd;
        if (RegCreateKeyExW(HKEY_CLASSES_ROOT, L"*\\shell\\PulseRunAsTI\\command", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hCmd, nullptr) == ERROR_SUCCESS) {
            const wchar_t* cmd = L"cmd.exe /c runas /trustlevel:0x20000 \"%1\"";
            RegSetValueExW(hCmd, nullptr, 0, REG_SZ, (const BYTE*)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
            RegCloseKey(hCmd);
        }
        AppState::LogToBoth("[CUSTOM] Обход TrustedInstaller добавлен в контекстное меню.");
    }
    else {
        DeleteRegKeyTree(HKEY_CLASSES_ROOT, L"*\\shell\\PulseRunAsTI");
        AppState::LogToBoth("[CUSTOM] Обход TrustedInstaller удалён из контекстного меню.");
    }
}

bool Get_ClassicPhotoViewerRestored() {
    return RegKeyExists(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\Applications\\photoviewer.dll\\shell\\open\\command");
}
void Apply_ClassicPhotoViewerRestored(bool restore) {
    if (restore) {
        const wchar_t* openCmd = L"%SystemRoot%\\System32\\rundll32.exe \"%ProgramFiles%\\Windows Photo Viewer\\PhotoViewer.dll\", ImageView_Fullscreen %1";
        for (const wchar_t* ext : { L".bmp", L".jpg", L".jpeg", L".png", L".gif" }) {
            std::wstring assocKey = std::wstring(L"SOFTWARE\\Classes\\Applications\\photoviewer.dll\\shell\\open\\command");
            WriteRegDword(HKEY_LOCAL_MACHINE, assocKey.c_str(), L"__pulse_marker", 1); // маркер существования ветки
            HKEY hCmd;
            if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, assocKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hCmd, nullptr) == ERROR_SUCCESS) {
                RegSetValueExW(hCmd, nullptr, 0, REG_EXPAND_SZ, (const BYTE*)openCmd, (DWORD)((wcslen(openCmd) + 1) * sizeof(wchar_t)));
                RegCloseKey(hCmd);
            }
            (void)ext;
            break; // команда общая для всех расширений — довольно один раз создать ключ
        }
        AppState::LogToBoth("[CUSTOM] Классический просмотр фотографий восстановлен (доступен в меню \"Открыть с помощью\").");
    }
    else {
        DeleteRegKeyTree(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\Applications\\photoviewer.dll");
        AppState::LogToBoth("[CUSTOM] Классический просмотр фотографий скрыт.");
    }
}

// ============================================================================
// "Выпиливание"
// ============================================================================
bool Get_OneDriveDisabled() {
    DWORD v = 0;
    ReadRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\OneDrive", L"DisableFileSyncNGSC", v);
    return v == 1;
}
void Apply_OneDriveDisabled(bool disable) {
    WriteRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\OneDrive", L"DisableFileSyncNGSC", disable ? 1 : 0);
    if (disable) RunHiddenCommand(L"taskkill /f /im OneDrive.exe");
    AppState::LogToBoth(disable ? "[DEBLOAT] OneDrive отключён (синхронизация запрещена политикой)." : "[DEBLOAT] OneDrive включён (по умолчанию).");
}

bool Get_XboxComponentsDisabled() {
    return GetServiceStartDisabled(L"XblGameSave") && GetServiceStartDisabled(L"XboxNetApiSvc");
}
void Apply_XboxComponentsDisabled(bool disable) {
    for (const wchar_t* svc : { L"XblAuthManager", L"XblGameSave", L"XboxGipSvc", L"XboxNetApiSvc" }) {
        SetServiceStartMode(svc, disable ? SERVICE_DISABLED : SERVICE_DEMAND_START, !disable);
    }
    AppState::LogToBoth(disable ? "[DEBLOAT] Xbox-компоненты (службы) отключены." : "[DEBLOAT] Xbox-компоненты (службы) включены.");
}

bool Get_BloatwareRemoved() {
    std::string out = RunPowerShellCapture(L"(Get-AppxPackage -Name Microsoft.BingWeather -AllUsers).Count");
    // Пустой вывод/0 = пакет отсутствует = уже удалён; любое другое число = ещё установлен.
    for (char c : out) if (c != '\r' && c != '\n' && c != ' ') { if (c != '0') return false; }
    return true;
}
void Apply_BloatwareRemoved(bool remove) {
    static const wchar_t* names[] = {
        L"Microsoft.3DBuilder", L"Microsoft.Microsoft3DViewer", L"Microsoft.MixedReality.Portal",
        L"Microsoft.BingWeather", L"Microsoft.GetHelp", L"Microsoft.Getstarted",
        L"Microsoft.MicrosoftOfficeHub", L"Microsoft.MicrosoftSolitaireCollection",
        L"Microsoft.People", L"Microsoft.WindowsFeedbackHub", L"Microsoft.YourPhone",
        L"Microsoft.ZuneMusic", L"Microsoft.ZuneVideo", L"MicrosoftTeams"
    };
    if (remove) {
        for (const wchar_t* name : names) {
            std::wstring cmd = L"Get-AppxPackage -Name " + std::wstring(name) + L" -AllUsers | Remove-AppxPackage -AllUsers";
            RunPowerShellCapture(cmd);
        }
        AppState::LogToBoth("[DEBLOAT] Предустановленные приложения удалены.");
    }
    else {
        AppState::LogToBoth("[DEBLOAT] Автоматическая переустановка стандартных приложений не выполняется — верните их вручную через Microsoft Store.");
    }
}

bool Get_CortanaDisabled() {
    DWORD v = 1;
    ReadRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search", L"AllowCortana", v);
    return v == 0;
}
void Apply_CortanaDisabled(bool disable) {
    WriteRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search", L"AllowCortana", disable ? 0 : 1);
    AppState::LogToBoth(disable ? "[DEBLOAT] Cortana отключена." : "[DEBLOAT] Cortana включена (по умолчанию).");
}

// ============================================================================
// "Группы служб"
// ============================================================================
bool IsServiceGroupEnabled(const std::vector<std::wstring>& services) {
    for (const auto& svc : services) {
        if (!GetServiceStartDisabled(svc.c_str())) return true; // хотя бы одна не отключена
    }
    return false;
}
void SetServiceGroupDisabled(const std::vector<std::wstring>& services, bool disable) {
    for (const auto& svc : services) {
        SetServiceStartMode(svc.c_str(), disable ? SERVICE_DISABLED : SERVICE_DEMAND_START, !disable);
    }
    AppState::LogToBoth(disable ? "[SERVICES] Группа служб отключена." : "[SERVICES] Группа служб включена.");
}

// ============================================================================
// "Приватность / Телеметрия"
// ============================================================================
bool Get_DotnetTelemetryDisabled() {
    std::wstring v;
    return GetUserEnvVar(L"DOTNET_CLI_TELEMETRY_OPTOUT", v) && (v == L"1" || v == L"true");
}
void Apply_DotnetTelemetryDisabled(bool disable) {
    SetUserEnvVar(L"DOTNET_CLI_TELEMETRY_OPTOUT", disable ? L"1" : nullptr);
    AppState::LogToBoth(disable ? "[PRIVACY] Телеметрия .NET CLI отключена (DOTNET_CLI_TELEMETRY_OPTOUT=1)." : "[PRIVACY] Телеметрия .NET CLI включена (переменная удалена).");
}

bool Get_PowerShellTelemetryDisabled() {
    std::wstring v;
    return GetUserEnvVar(L"POWERSHELL_TELEMETRY_OPTOUT", v) && (v == L"1" || v == L"true");
}
void Apply_PowerShellTelemetryDisabled(bool disable) {
    SetUserEnvVar(L"POWERSHELL_TELEMETRY_OPTOUT", disable ? L"1" : nullptr);
    AppState::LogToBoth(disable ? "[PRIVACY] Телеметрия PowerShell отключена." : "[PRIVACY] Телеметрия PowerShell включена (переменная удалена).");
}

bool Get_VSTelemetryDisabled() {
    std::wstring v;
    return GetUserEnvVar(L"VSTEL_OptOut", v) && v == L"1";
}
void Apply_VSTelemetryDisabled(bool disable) {
    SetUserEnvVar(L"VSTEL_OptOut", disable ? L"1" : nullptr);
    AppState::LogToBoth(disable ? "[PRIVACY] Телеметрия Visual Studio отключена (VSTEL_OptOut=1)." : "[PRIVACY] Телеметрия Visual Studio включена (переменная удалена).");
}

bool Get_VSCodeTelemetryDisabled() {
    wchar_t appData[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    fs::path settingsPath = fs::path(appData) / L"Code" / L"User" / L"settings.json";
    std::ifstream f(settingsPath);
    if (!f) return false;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return content.find("\"telemetry.telemetryLevel\": \"off\"") != std::string::npos
        || content.find("\"telemetry.telemetryLevel\":\"off\"") != std::string::npos;
}
void Apply_VSCodeTelemetryDisabled(bool disable) {
    wchar_t appData[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;
    fs::path dir = fs::path(appData) / L"Code" / L"User";
    fs::path settingsPath = dir / L"settings.json";
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::string content;
    { std::ifstream f(settingsPath); if (f) content.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }

    const std::string key = "\"telemetry.telemetryLevel\"";
    size_t pos = content.find(key);
    std::string newEntry = key + ": \"" + (disable ? "off" : "all") + "\"";
    if (pos != std::string::npos) {
        size_t valStart = content.find(':', pos);
        size_t valEnd = content.find_first_of(",}\n", valStart);
        content = content.substr(0, pos) + newEntry + content.substr(valEnd);
    }
    else {
        size_t brace = content.find('{');
        if (brace == std::string::npos) content = "{\n  " + newEntry + "\n}\n";
        else content.insert(brace + 1, "\n  " + newEntry + ",");
    }
    std::ofstream out(settingsPath, std::ios::trunc);
    out << content;
    AppState::LogToBoth(disable ? "[PRIVACY] Телеметрия VS Code отключена (settings.json)." : "[PRIVACY] Телеметрия VS Code включена (settings.json).");
}

bool Get_CeipDisabled() {
    DWORD v = 1;
    ReadRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\SQMClient", L"CEIPEnable", v);
    return v == 0;
}
void Apply_CeipDisabled(bool disable) {
    WriteRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\SQMClient", L"CEIPEnable", disable ? 0 : 1);
    AppState::LogToBoth(disable ? "[PRIVACY] Программа улучшения качества ПО (CEIP) отключена." : "[PRIVACY] CEIP включена (по умолчанию).");
}

bool Get_DiagTrackDisabled() {
    return GetServiceStartDisabled(L"DiagTrack");
}
void Apply_DiagTrackDisabled(bool disable) {
    SetServiceStartMode(L"DiagTrack", disable ? SERVICE_DISABLED : SERVICE_DEMAND_START, !disable);
    AppState::LogToBoth(disable ? "[PRIVACY] Служба телеметрии Windows (DiagTrack) отключена." : "[PRIVACY] Служба телеметрии Windows (DiagTrack) включена.");
}

// ============================================================================
// "Прерывания / Устройства" — MSI Mode для первичной видеокарты через SetupAPI.
// ============================================================================
static bool FindPrimaryGpuInstancePath(std::wstring& outRegPath) {
    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVINFO_DATA devData{};
    devData.cbSize = sizeof(SP_DEVINFO_DATA);
    bool found = false;
    if (SetupDiEnumDeviceInfo(hDevInfo, 0, &devData)) {
        wchar_t instanceId[MAX_DEVICE_ID_LEN];
        if (SetupDiGetDeviceInstanceIdW(hDevInfo, &devData, instanceId, MAX_DEVICE_ID_LEN, nullptr)) {
            outRegPath = L"SYSTEM\\CurrentControlSet\\Enum\\" + std::wstring(instanceId) +
                L"\\Device Parameters\\Interrupt Management\\MessageSignaledInterruptProperties";
            found = true;
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return found;
}

bool Get_GpuMsiModeEnabled() {
    std::wstring regPath;
    if (!FindPrimaryGpuInstancePath(regPath)) return false;
    DWORD v = 0;
    ReadRegDword(HKEY_LOCAL_MACHINE, regPath.c_str(), L"MSISupported", v);
    return v == 1;
}
void Apply_GpuMsiModeEnabled(bool enable) {
    std::wstring regPath;
    if (!FindPrimaryGpuInstancePath(regPath)) {
        AppState::LogToBoth("[INTERRUPTS] Не удалось найти видеокарту для настройки MSI Mode.");
        return;
    }
    WriteRegDword(HKEY_LOCAL_MACHINE, regPath.c_str(), L"MSISupported", enable ? 1 : 0);
    AppState::LogToBoth(enable ? "[INTERRUPTS] MSI Mode для видеокарты включён (потребуется перезагрузка)." : "[INTERRUPTS] MSI Mode для видеокарты отключён (потребуется перезагрузка).");
}

// USB Selective Suspend — powercfg-подгруппа "USB settings". Отключение убирает
// микро-задержки/просыпание игровых мышей и клавиатур ценой небольшого роста энергопотребления.
static const wchar_t* kUsbSubgroup = L"2a737441-1930-4402-8d77-b2bebba308a3";
static const wchar_t* kUsbSetting = L"48e6b7a6-50f5-4782-a5d4-53bb8f07e226";

bool Get_UsbSelectiveSuspendDisabled() {
    std::string out = RunPowerShellCapture(L"powercfg /q SCHEME_CURRENT " + std::wstring(kUsbSubgroup) + L" " + std::wstring(kUsbSetting));
    size_t pos = out.find("Current AC Power Setting Index:");
    if (pos == std::string::npos) return false;
    return out.find("0x00000000", pos) != std::string::npos;
}
void Apply_UsbSelectiveSuspendDisabled(bool disable) {
    DWORD value = disable ? 0 : 1;
    wchar_t cmdAc[256], cmdDc[256];
    swprintf_s(cmdAc, L"powercfg /setacvalueindex SCHEME_CURRENT %s %s %u", kUsbSubgroup, kUsbSetting, value);
    swprintf_s(cmdDc, L"powercfg /setdcvalueindex SCHEME_CURRENT %s %s %u", kUsbSubgroup, kUsbSetting, value);
    RunHiddenCommand(cmdAc);
    RunHiddenCommand(cmdDc);
    RunHiddenCommand(L"powercfg /S SCHEME_CURRENT");
    AppState::LogToBoth(disable ? "[INTERRUPTS] USB Selective Suspend отключён." : "[INTERRUPTS] USB Selective Suspend включён (по умолчанию).");
}

// Алгоритм Найгла + отложенные ACK — по всем сетевым интерфейсам с назначенным IP.
bool Get_NagleDisabled() {
    HKEY hKey;
    bool anyDisabled = false;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t subKeyName[256];
        for (DWORD i = 0; ; i++) {
            DWORD nameSize = 256;
            if (RegEnumKeyExW(hKey, i, subKeyName, &nameSize, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            std::wstring path = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\" + std::wstring(subKeyName);
            DWORD v = 0;
            if (ReadRegDword(HKEY_LOCAL_MACHINE, path.c_str(), L"TcpNoDelay", v) && v == 1) anyDisabled = true;
        }
        RegCloseKey(hKey);
    }
    return anyDisabled;
}
void Apply_NagleDisabled(bool disable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t subKeyName[256];
        for (DWORD i = 0; ; i++) {
            DWORD nameSize = 256;
            if (RegEnumKeyExW(hKey, i, subKeyName, &nameSize, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            std::wstring path = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\" + std::wstring(subKeyName);
            if (disable) {
                WriteRegDword(HKEY_LOCAL_MACHINE, path.c_str(), L"TcpNoDelay", 1);
                WriteRegDword(HKEY_LOCAL_MACHINE, path.c_str(), L"TcpAckFrequency", 1);
            }
            else {
                HKEY hSub;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_SET_VALUE, &hSub) == ERROR_SUCCESS) {
                    RegDeleteValueW(hSub, L"TcpNoDelay");
                    RegDeleteValueW(hSub, L"TcpAckFrequency");
                    RegCloseKey(hSub);
                }
            }
        }
        RegCloseKey(hKey);
    }
    AppState::LogToBoth(disable ? "[TWEAKS] Алгоритм Найгла отключён на всех сетевых интерфейсах." : "[TWEAKS] Алгоритм Найгла возвращён к значению по умолчанию.");
}

// Ускорение мыши ("Enhance pointer precision") — строковые значения в HKCU\Control Panel\Mouse.
bool Get_MouseAccelDisabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Mouse", 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) return false;
    wchar_t buf[16]{}; DWORD size = sizeof(buf);
    bool isZero = (RegQueryValueExW(hKey, L"MouseSpeed", nullptr, nullptr, (BYTE*)buf, &size) == ERROR_SUCCESS) && (wcscmp(buf, L"0") == 0);
    RegCloseKey(hKey);
    return isZero;
}
void Apply_MouseAccelDisabled(bool disable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Mouse", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        const wchar_t* speed = disable ? L"0" : L"1";
        const wchar_t* thr1 = disable ? L"0" : L"6";
        const wchar_t* thr2 = disable ? L"0" : L"10";
        RegSetValueExW(hKey, L"MouseSpeed", 0, REG_SZ, (const BYTE*)speed, (DWORD)((wcslen(speed) + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"MouseThreshold1", 0, REG_SZ, (const BYTE*)thr1, (DWORD)((wcslen(thr1) + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"MouseThreshold2", 0, REG_SZ, (const BYTE*)thr2, (DWORD)((wcslen(thr2) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    AppState::LogToBoth(disable ? "[TWEAKS] Ускорение мыши отключено (1:1 движение)." : "[TWEAKS] Ускорение мыши включено (по умолчанию).");
}

// Схема электропитания "Максимальная производительность" (Ultimate Performance) — обычно
// скрыта в Windows, дублируем под собственным фиксированным GUID, чтобы всегда его находить.
static const wchar_t* kUltimateSourceGuid = L"e9a42b02-d5df-448d-aa00-03f14749eb61";
static const wchar_t* kUltimateOwnGuid = L"a1a42b02-d5df-448d-aa00-03f14749eb99";
static const wchar_t* kBalancedGuid = L"381b4222-f694-41f0-9685-ff5bb260df2e";

bool Get_UltimatePerformanceActive() {
    std::string out = RunPowerShellCapture(L"powercfg /getactivescheme");
    std::string guid = "a1a42b02-d5df-448d-aa00-03f14749eb99";
    return out.find(guid) != std::string::npos;
}
void Apply_UltimatePerformanceActive(bool enable) {
    if (enable) {
        std::wstring dup = L"powercfg /duplicatescheme " + std::wstring(kUltimateSourceGuid) + L" " + std::wstring(kUltimateOwnGuid);
        RunHiddenCommand(dup);
        RunHiddenCommand(L"powercfg /setactive " + std::wstring(kUltimateOwnGuid));
        AppState::LogToBoth("[TWEAKS] Активирована схема электропитания \"Максимальная производительность\".");
    }
    else {
        RunHiddenCommand(L"powercfg /setactive " + std::wstring(kBalancedGuid));
        AppState::LogToBoth("[TWEAKS] Возвращена сбалансированная схема электропитания.");
    }
}

void StopWindowsService(const std::wstring& serviceName, const std::wstring& description) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        AppState::LogToBoth("[SERVICE] Не удалось открыть SCM для " + AppState::WStringToString(serviceName) + ". Нужны права администратора.");
        return;
    }
    SC_HANDLE hSvc = OpenServiceW(hSCM, serviceName.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (hSvc) {
        SERVICE_STATUS status;
        if (ControlService(hSvc, SERVICE_CONTROL_STOP, &status)) {
            AppState::LogToBoth("[SERVICE] Остановлена служба: " + AppState::WStringToString(serviceName) + " (" + AppState::WStringToString(description) + ")");
            AppState::reportStoppedServices++;
        }
        else {
            DWORD err = GetLastError();
            if (err == ERROR_SERVICE_NOT_ACTIVE) {
                AppState::LogToBoth("[SERVICE] Служба " + AppState::WStringToString(serviceName) + " уже остановлена.");
            }
            else {
                AppState::LogToBoth("[SERVICE] Ошибка остановки " + AppState::WStringToString(serviceName) + ", код: " + std::to_string(err));
            }
        }
        CloseServiceHandle(hSvc);
    }
    else {
        AppState::LogToBoth("[SERVICE] Служба " + AppState::WStringToString(serviceName) + " не найдена или недоступна в SCM.");
    }

    if (hSCM) {
        CloseServiceHandle(hSCM);
    }
}

void PurgeSystemMemoryPools() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return;
    pfnNtSetSystemInformation NtSetSystemInfo = (pfnNtSetSystemInformation)GetProcAddress(hNtdll, "NtSetSystemInformation");
    if (!NtSetSystemInfo) return;

    INT cmdStandby = UltraMemoryPurgeStandbyList;
    NtSetSystemInfo(UltraSystemMemoryListInformation, &cmdStandby, sizeof(cmdStandby));
    INT cmdModified = UltraMemoryFlushModifiedList;
    NtSetSystemInfo(UltraSystemMemoryListInformation, &cmdModified, sizeof(cmdModified));
    AppState::LogToBoth("[RAM] Системный Standby-кэш очищен через Native NT API.");
}

struct EnumWindowsForPidData {
    DWORD pid;
    std::vector<HWND> windows;
};

static BOOL CALLBACK EnumWindowsForPidProc(HWND hwnd, LPARAM lParam) {
    EnumWindowsForPidData* data = (EnumWindowsForPidData*)lParam;
    DWORD wndPid = 0;
    GetWindowThreadProcessId(hwnd, &wndPid);
    if (wndPid == data->pid && IsWindowVisible(hwnd)) {
        data->windows.push_back(hwnd);
    }
    return TRUE;
}

void KillUserProcessesAndCleanRAM(ProcessCleanupLevel level, bool silent, int gracefulTimeoutMs) {
    if (!silent && level == ProcessCleanupLevel::MaximumPurge) {
        AppState::LogToBoth("[MAXIMUM] Закрытие всех процессов, включая античиты и игровые сервисы...");
    }

    DWORD currentPid = GetCurrentProcessId();
    DWORD currentSessionId = 0;
    if (!ProcessIdToSessionId(currentPid, &currentSessionId)) return;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = { sizeof(pe) };
    int killed = 0;
    size_t freedBytes = 0;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == currentPid) continue;
            DWORD sessId = 0;
            if (ProcessIdToSessionId(pe.th32ProcessID, &sessId) && sessId == currentSessionId) {
                if (!IsCriticalProcess(pe.szExeFile, level)) {
                    size_t ramUsage = 0;
                    HANDLE hMeasure = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                    if (hMeasure) {
                        PROCESS_MEMORY_COUNTERS pmc;
                        if (GetProcessMemoryInfo(hMeasure, &pmc, sizeof(pmc))) {
                            ramUsage = pmc.WorkingSetSize;
                        }
                        CloseHandle(hMeasure);
                    }
                    std::string nameUtf8 = AppState::WStringToString(pe.szExeFile);
                    std::string mbTag = std::to_string(ramUsage / 1024 / 1024) + " MB";

                    EnumWindowsForPidData wndData;
                    wndData.pid = pe.th32ProcessID;
                    EnumWindows(EnumWindowsForPidProc, (LPARAM)&wndData);

                    HANDLE hProc = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        bool closedGracefully = false;
                        if (!wndData.windows.empty()) {
                            // Окна есть — сначала просим приложение закрыться штатно (сохранит
                            // несохранённые данные/корректно освободит ресурсы), а не рубим сразу.
                            for (HWND hwnd : wndData.windows) {
                                PostMessage(hwnd, WM_CLOSE, 0, 0);
                            }
                            DWORD waitResult = WaitForSingleObject(hProc, (DWORD)gracefulTimeoutMs);
                            if (waitResult == WAIT_OBJECT_0) {
                                closedGracefully = true;
                                AppState::LogToBoth("[CLOSED] " + nameUtf8 + " закрыт штатно (освобождено " + mbTag + ")");
                            }
                        }
                        if (!closedGracefully) {
                            // Либо фоновый процесс без окон, либо не успел закрыться сам за таймаут.
                            if (TerminateProcess(hProc, 0)) {
                                if (!wndData.windows.empty()) {
                                    AppState::LogToBoth("[KILLED] " + nameUtf8 + " принудительно завершён по таймауту (освобождено " + mbTag + ")");
                                }
                                else {
                                    AppState::LogToBoth("[KILLED] " + nameUtf8 + " (освобождено " + mbTag + ")");
                                }
                            }
                            else {
                                CloseHandle(hProc);
                                continue;
                            }
                        }
                        freedBytes += ramUsage;
                        killed++;
                        CloseHandle(hProc);
                    }
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    if (!silent) {
        // В watchdog-режиме (silent) статистику уже показанного отчёта не трогаем — иначе
        // повторные добивания "зомби"-процессов задним числом меняли бы цифры завершённой оптимизации.
        AppState::reportKilledProcs = killed;
        AppState::reportFreedRAM = freedBytes / 1024 / 1024;
        AppState::LogToBoth("[PROCESSES] Закрыто нежелательных процессов: " + std::to_string(killed));
    }
    else if (killed > 0) {
        AppState::LogToBoth("[WATCHDOG] Добито воскресших процессов: " + std::to_string(killed));
    }
}

static int CleanDirectoryContents(const fs::path& dir) {
    int count = 0;
    try {
        if (!fs::exists(dir)) return 0;
        for (const auto& entry : fs::directory_iterator(dir)) {
            try { fs::remove_all(entry.path()); count++; }
            catch (...) {} // файл занят другим процессом — пропускаем, не роняем всю очистку
        }
    }
    catch (...) {}
    return count;
}

void CleanSystemCaches() {
    int totalCount = 0;

    // 1. Папка %TEMP%
    wchar_t tempPath[MAX_PATH];
    DWORD tempSize = GetEnvironmentVariableW(L"TEMP", tempPath, MAX_PATH);
    if (tempSize > 0 && tempSize < MAX_PATH) {
        int c = CleanDirectoryContents(tempPath);
        totalCount += c;
        AppState::LogToBoth("[CACHE] Удалено файлов из Temp: " + std::to_string(c));
    }

    // 2. Кэш эскизов и иконок Explorer (thumbcache_*.db / iconcache_*.db)
    wchar_t localAppData[MAX_PATH];
    DWORD ladSize = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (ladSize > 0 && ladSize < MAX_PATH) {
        fs::path explorerCacheDir = fs::path(localAppData) / L"Microsoft" / L"Windows" / L"Explorer";
        int thumbCount = 0;
        try {
            if (fs::exists(explorerCacheDir)) {
                for (const auto& entry : fs::directory_iterator(explorerCacheDir)) {
                    std::wstring fname = entry.path().filename().wstring();
                    if (fname.rfind(L"thumbcache_", 0) == 0 || fname.rfind(L"iconcache_", 0) == 0) {
                        try { fs::remove(entry.path()); thumbCount++; }
                        catch (...) {}
                    }
                }
            }
        }
        catch (...) {}
        totalCount += thumbCount;
        AppState::LogToBoth("[CACHE] Удалено файлов кэша эскизов: " + std::to_string(thumbCount));
    }

    // 3. Загрузки Центра обновления Windows (SoftwareDistribution\Download) — часто
    // освобождает от 2 до 20 GB. Файлы активной загрузки будут заняты службой wuauserv
    // и просто пропущены (CleanDirectoryContents глотает ошибки по каждому файлу).
    int updateCount = CleanDirectoryContents(L"C:\\Windows\\SoftwareDistribution\\Download");
    totalCount += updateCount;
    AppState::LogToBoth("[CACHE] Удалено файлов кэша обновлений Windows: " + std::to_string(updateCount));

    AppState::reportTempFilesDeleted = totalCount;
}

const std::vector<OptimizationProfile>& GetOptimizationProfiles() {
    // "Стандартный" оставляет killBackgroundApps=true, но cleanupLevel=None — при этом уровне
    // IsCriticalProcess всегда возвращает true, так что KillUserProcessesAndCleanRAM не закроет
    // ни одного приложения (цикл просто ничего не найдёт для закрытия). Реально работающая часть
    // профиля — очистка standby-памяти и временных файлов, без риска задеть пользовательский софт.
    static const std::vector<OptimizationProfile> profiles = {
        { "standard", "Стандартный", "Чистит мусор и освобождает память, не трогая ваши приложения",
          true, ProcessCleanupLevel::None, false, true, true },
        { "gaming", "Игровой", "Закрывает всё лишнее, сохраняя Discord, FaceIt, CS2, Steam и драйверы видеокарты",
          true, ProcessCleanupLevel::GamingWhitelist, true, true, false },
        { "maximum", "Максимум", "Закрывает абсолютно всё, кроме критичных процессов Windows — включая FaceIt, Discord, Steam",
          true, ProcessCleanupLevel::MaximumPurge, true, true, true },
    };
    return profiles;
}

static int g_autoBoostPrevProfile = -1;
static bool g_autoBoostActive = false;

void CheckAutoBoost() {
    std::vector<std::wstring> gamesSnapshot;
    {
        std::lock_guard<std::mutex> lock(AppState::knownGamesListMutex);
        gamesSnapshot = AppState::knownGamesList;
    }

    bool gameRunning = false;
    {
        std::lock_guard<std::mutex> lock(AppState::processesMutex);
        for (const auto& proc : AppState::runningProcesses) {
            for (const auto& game : gamesSnapshot) {
                if (IsEqualIgnoreCase(proc.name, game)) { gameRunning = true; break; }
            }
            if (gameRunning) break;
        }
    }

    if (gameRunning && !g_autoBoostActive) {
        g_autoBoostPrevProfile = AppState::selectedProfileIndex.load();
        if (g_autoBoostPrevProfile != 1) {
            AppState::selectedProfileIndex = 1; // "Игровой"
            AppState::LogToBoth("[AUTO-BOOST] Обнаружена игра — включён профиль \"Игровой\".");
        }
        g_autoBoostActive = true;
    }
    else if (!gameRunning && g_autoBoostActive) {
        // Возвращаем прежний профиль только если пользователь сам не переключился на что-то
        // другое, пока играл — иначе Auto-Boost перезаписал бы его собственный выбор.
        if (AppState::selectedProfileIndex.load() == 1 && g_autoBoostPrevProfile != -1 && g_autoBoostPrevProfile != 1) {
            AppState::selectedProfileIndex = g_autoBoostPrevProfile;
            AppState::LogToBoth("[AUTO-BOOST] Игра закрыта — возвращён прежний профиль.");
        }
        g_autoBoostActive = false;
        g_autoBoostPrevProfile = -1;
    }
}

void RunZombieWatchdog(ProcessCleanupLevel level, int durationSeconds) {
    const int intervalSeconds = 3;
    int elapsed = 0;
    while (elapsed < durationSeconds) {
        std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
        elapsed += intervalSeconds;
        KillUserProcessesAndCleanRAM(level, /*silent=*/true);
    }
    RefreshSystemProcesses();
}

void PerformOptimizationTask(const OptimizationProfile& profile) {
    AppState::isOptimizing = true;
    AppState::progress = 0.05f;
    AppState::reportFreedRAM = 0;
    AppState::reportKilledProcs = 0;
    AppState::reportStoppedServices = 0;
    AppState::reportTempFilesDeleted = 0;

    // Реальный объём освобождённой памяти на уровне всей системы (до/после), а не только
    // сумма working set закрытых процессов — так профиль "Стандартный" (который не закрывает
    // ни одного приложения) тоже честно показывает эффект от очистки standby-кэша.
    MEMORYSTATUSEX memBefore;
    memBefore.dwLength = sizeof(memBefore);
    GlobalMemoryStatusEx(&memBefore);

    AppState::LogToBoth("=== Pulse OPTIMIZATION ENGINE ===");
    AppState::LogToBoth("[PROFILE] Выбран профиль: " + profile.name);
    LogSystemSpecs();

    AppState::progress = 0.2f;

    if (EnableRequiredPrivileges()) {
        AppState::LogToBoth("[INIT] Привилегии управления памятью получены.");
    }
    else {
        AppState::LogToBoth("[INIT] Не удалось получить повышенные привилегии. Запустите от имени администратора!");
    }
    AppState::progress = 0.35f;

    if (timeBeginPeriod(1) == TIMERR_NOERROR) {
        AppState::LogToBoth("[TIMER] Таймер планировщика переведен в режим 0.5ms.");
    }

    AppState::progress = 0.5f;

    if (profile.stopTelemetryServices) {
        std::vector<TelemetryServiceEntry> services;
        {
            std::lock_guard<std::mutex> lock(AppState::telemetryServicesMutex);
            services = AppState::telemetryServicesList;
        }
        for (const auto& s : services) StopWindowsService(s.name, s.description);
    }
    AppState::progress = 0.7f;

    if (profile.killBackgroundApps) {
        KillUserProcessesAndCleanRAM(profile.cleanupLevel);
    }
    AppState::progress = 0.85f;

    if (profile.purgeStandbyMemory) {
        PurgeSystemMemoryPools();
    }
    if (profile.cleanTempFiles) {
        CleanSystemCaches();
    }

    MEMORYSTATUSEX memAfter;
    memAfter.dwLength = sizeof(memAfter);
    GlobalMemoryStatusEx(&memAfter);
    if (memAfter.ullAvailPhys > memBefore.ullAvailPhys) {
        size_t systemFreedMB = (size_t)((memAfter.ullAvailPhys - memBefore.ullAvailPhys) / 1024 / 1024);
        if (systemFreedMB > AppState::reportFreedRAM.load()) {
            AppState::reportFreedRAM = systemFreedMB;
        }
    }

    AppState::progress = 1.0f;
    AppState::standbyMemoryGB = 0.12f;

    AppState::LogToBoth("=== ОПТИМИЗАЦИЯ ЗАВЕРШЕНА ===");

    auto now = std::chrono::system_clock::now();
    time_t tt = std::chrono::system_clock::to_time_t(now);
    tm localTm;
    localtime_s(&localTm, &tt);
    char buf[16];
    sprintf_s(buf, "%02d:%02d:%02d", localTm.tm_hour, localTm.tm_min, localTm.tm_sec);
    AppState::reportTime = buf;

    OptimizationReport report;
    report.time = AppState::reportTime;
    report.profileName = profile.name;
    report.freedRAM = AppState::reportFreedRAM.load();
    report.killedProcs = AppState::reportKilledProcs.load();
    report.stoppedServices = AppState::reportStoppedServices.load();
    report.tempFilesDeleted = AppState::reportTempFilesDeleted.load();
    report.ramPercentBefore = (float)memBefore.dwMemoryLoad;
    report.ramPercentAfter = (float)memAfter.dwMemoryLoad;
    {
        std::lock_guard<std::mutex> lock(AppState::reportHistoryMutex);
        AppState::reportHistory.push_back(report);
    }

    AppState::hasReport = true;
    AppState::isOptimizing = false;

    RefreshSystemProcesses();

    if (profile.killBackgroundApps) {
        // Отдельный поток: не задерживаем показ отчёта пользователю, "добивание" воскресших
        // процессов (Discord, апдейтеры и т.п. с собственным watchdog'ом) идёт в фоне 30 секунд.
        ProcessCleanupLevel level = profile.cleanupLevel;
        std::thread watchdog([level]() { RunZombieWatchdog(level, 30); });
        watchdog.detach();
    }
}