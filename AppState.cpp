#include "AppState.hpp"
#include <windows.h>
#include <mutex>
#include <vector>
#include <string>
#include <atomic>
#include <fstream>
#include <cstring>

namespace AppState {
    static const size_t MAX_UI_LOG_LINES = 500;
    std::mutex logMutex;
    std::deque<std::string> uiLogs;

    std::atomic<bool> isOptimizing{ false };
    std::atomic<float> progress{ 0.0f };
    std::atomic<float> standbyMemoryGB{ 3.2f };
    std::atomic<float> currentRAMUsagePercent{ 45.0f };
    std::atomic<float> currentRAMUsedGB{ 0.0f };
    std::atomic<float> currentCPUUsagePercent{ 0.0f };
    std::atomic<float> currentDiskUsagePercent{ 0.0f };
    std::atomic<float> currentNetUsagePercent{ 0.0f };
    std::atomic<float> currentNetworkMbps{ 0.0f };

    std::atomic<size_t> reportFreedRAM{ 0 };
    std::atomic<int> reportKilledProcs{ 0 };
    std::atomic<int> reportStoppedServices{ 0 };
    std::atomic<int> reportTempFilesDeleted{ 0 };
    std::atomic<bool> hasReport{ false };
    std::string reportTime = "00:00:00";

    std::vector<OptimizationReport> reportHistory;
    std::mutex reportHistoryMutex;

    std::vector<ProcessInfo> runningProcesses;
    std::mutex processesMutex;

    std::vector<std::wstring> userWhitelist;
    std::mutex whitelistMutex;

    // Единственная база критичных процессов Windows — реально ставится/запускается самой ОС,
    // без стороннего софта. Защищена при ЛЮБОМ профиле очистки.
    std::vector<std::wstring> systemCriticalWhitelist = {
        L"dwm.exe", L"explorer.exe", L"sihost.exe", L"taskhostw.exe",
        L"svchost.exe", L"RuntimeBroker.exe", L"ShellExperienceHost.exe",
        L"StartMenuExperienceHost.exe", L"SearchHost.exe", L"textinputhost.exe",
        L"ctfmon.exe", L"csrss.exe", L"winlogon.exe", L"fontdrvhost.exe",
        L"conhost.exe", L"audiodg.exe", L"wudfhost.exe", L"lsass.exe",
        L"services.exe", L"smss.exe", L"wininit.exe", L"spoolsv.exe",
        L"dllhost.exe", L"registry", L"System", L"System Idle Process",
        L"MsMpEng.exe", L"SecurityHealthService.exe", L"WUDFHost.exe",
        L"WmiPrvSE.exe", L"dasHost.exe", L"UserOOBEBroker.exe"
    };

    // Игровой софт/лаунчеры/анти-читы — защищены только на профиле "Игровой" (GamingWhitelist),
    // на "Максимуме" уже не защищены. Не относится к системным зависимостям Windows.
    std::vector<std::wstring> gamingWhitelist = {
        L"discord.exe",
        L"faceitclient.exe", L"faceitservice.exe", L"faceit.exe",
        L"cs2.exe",
        L"steam.exe", L"steamwebhelper.exe", L"steamservice.exe", L"GameOverlayUI.exe",
        L"NVIDIA Share.exe", L"nvcontainer.exe", L"nvsphelper64.exe", L"NVDisplay.Container.exe",
        L"RadeonSoftware.exe", L"AMDRSServ.exe"
    };

    std::atomic<int> selectedProfileIndex{ 0 };

    std::vector<std::wstring> knownGamesList = {
        L"cs2.exe", L"csgo.exe", L"dota2.exe", L"VALORANT-Win64-Shipping.exe",
        L"RainbowSix.exe", L"FortniteClient-Win64-Shipping.exe", L"GTA5.exe",
        L"PUBG.exe", L"TslGame.exe", L"r5apex.exe", L"League of Legends.exe",
        L"LeagueClient.exe", L"Overwatch.exe", L"witcher3.exe", L"Cyberpunk2077.exe",
        L"eldenring.exe", L"RustClient.exe", L"battlefield.exe"
    };
    std::mutex knownGamesListMutex;

    std::vector<TelemetryServiceEntry> telemetryServicesList = {
        { L"SysMain", L"SuperFetch" },
        { L"WSearch", L"Windows Search" },
        { L"DiagTrack", L"Диагностика и телеметрия" },
    };
    std::mutex telemetryServicesMutex;

    std::mutex telemetryHistoryMutex;
    float ramUsageHistory[100] = { 0 };
    float standbyHistory[100] = { 0 };
    float cpuUsageHistory[100] = { 0 };
    float diskUsageHistory[100] = { 0 };
    float netUsageHistory[100] = { 0 };
    int historyOffset = 0;

    // Определение строк реального оборудования
    std::string cpuName = "Сканирование...";
    std::string gpuName = "Сканирование...";
    std::string ramSizeGB = "Сканирование...";
    std::string motherboardName = "Сканирование...";

    std::string startupWarningText;
    std::atomic<bool> hasStartupWarning{ false };

    std::atomic<bool> optAutoCleanOnBoot{ false };
    std::atomic<bool> optDisableNetworkThrottling{ false };
    std::atomic<bool> optSystemResponsivenessZero{ false };
    std::atomic<bool> optHighPriorityForGames{ false };
    std::atomic<bool> optDisableCoreParking{ false };

    void LogToUI(const std::string& text) {
        std::lock_guard<std::mutex> lock(logMutex);
        uiLogs.push_back(text);
        while (uiLogs.size() > MAX_UI_LOG_LINES) {
            uiLogs.pop_front();
        }
    }

    void LogToBoth(const std::string& text) {
        LogToUI(text);
    }

    void LogToBoth(const std::wstring& wtext) {
        LogToBoth(WStringToString(wtext));
    }

    std::string WStringToString(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string strTo(sizeNeeded, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &strTo[0], sizeNeeded, NULL, NULL);
        return strTo;
    }

    std::wstring StringToWString(const std::string& str) {
        if (str.empty()) return std::wstring();
        int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
        std::wstring wstrTo(sizeNeeded, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstrTo[0], sizeNeeded);
        return wstrTo;
    }

    static std::wstring GetConfigFilePath() {
        wchar_t exePathBuf[MAX_PATH];
        GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
        std::wstring exeDir(exePathBuf);
        size_t slashPos = exeDir.find_last_of(L"\\/");
        if (slashPos != std::wstring::npos) exeDir = exeDir.substr(0, slashPos);
        return exeDir + L"\\pulse_config.txt";
    }

    // Разбивает строку по '|' в список непустых токенов (общий помощник для whitelist=/games=/services=)
    static std::vector<std::string> SplitPipe(const std::string& rest) {
        std::vector<std::string> out;
        size_t start = 0;
        while (start < rest.size()) {
            size_t pos = rest.find('|', start);
            std::string token = (pos == std::string::npos) ? rest.substr(start) : rest.substr(start, pos - start);
            if (!token.empty()) out.push_back(token);
            if (pos == std::string::npos) break;
            start = pos + 1;
        }
        return out;
    }

    void SaveSettings() {
        std::ofstream out(GetConfigFilePath());
        if (!out.is_open()) return;

        out << "profile=" << selectedProfileIndex.load() << "\n";
        out << "whitelist=";
        {
            std::lock_guard<std::mutex> lock(whitelistMutex);
            for (size_t i = 0; i < userWhitelist.size(); i++) {
                if (i > 0) out << "|";
                out << WStringToString(userWhitelist[i]);
            }
        }
        out << "\n";

        out << "games=";
        {
            std::lock_guard<std::mutex> lock(knownGamesListMutex);
            for (size_t i = 0; i < knownGamesList.size(); i++) {
                if (i > 0) out << "|";
                out << WStringToString(knownGamesList[i]);
            }
        }
        out << "\n";

        out << "services=";
        {
            std::lock_guard<std::mutex> lock(telemetryServicesMutex);
            for (size_t i = 0; i < telemetryServicesList.size(); i++) {
                if (i > 0) out << "|";
                // name и description внутри одной записи разделяем ';' (сами по себе не содержат
                // этот символ) — формат остаётся плоским текстом без вложенных разделителей.
                out << WStringToString(telemetryServicesList[i].name) << ";" << WStringToString(telemetryServicesList[i].description)
                    << ";" << (telemetryServicesList[i].disabled ? "1" : "0");
            }
        }
        out << "\n";

        out << "opt=" << (optAutoCleanOnBoot.load() ? "1" : "0")
            << "," << (optDisableNetworkThrottling.load() ? "1" : "0")
            << "," << (optSystemResponsivenessZero.load() ? "1" : "0")
            << "," << (optHighPriorityForGames.load() ? "1" : "0")
            << "," << (optDisableCoreParking.load() ? "1" : "0")
            << "\n";

        out << "telemetry=";
        {
            std::lock_guard<std::mutex> lock(telemetryHistoryMutex);
            out << historyOffset << ";";
            auto writeArr = [&out](const float* arr) {
                for (int i = 0; i < 100; i++) {
                    if (i > 0) out << ",";
                    out << arr[i];
                }
            };
            writeArr(ramUsageHistory); out << ";";
            writeArr(standbyHistory); out << ";";
            writeArr(cpuUsageHistory); out << ";";
            writeArr(diskUsageHistory); out << ";";
            writeArr(netUsageHistory);
        }
        out << "\n";
    }

    // Разбирает секцию "telemetry=offset;arr1;arr2;arr3;arr4;arr5" (arrN — 100 float через запятую).
    // При любой нестыковке формата (старый файл, повреждённые данные) молча оставляет массивы как есть.
    static void ParseTelemetrySection(const std::string& rest) {
        std::vector<std::string> parts;
        size_t start = 0;
        while (start <= rest.size()) {
            size_t pos = rest.find(';', start);
            std::string token = (pos == std::string::npos) ? rest.substr(start) : rest.substr(start, pos - start);
            parts.push_back(token);
            if (pos == std::string::npos) break;
            start = pos + 1;
        }
        if (parts.size() != 6) return;

        float* targets[5] = { ramUsageHistory, standbyHistory, cpuUsageHistory, diskUsageHistory, netUsageHistory };
        try {
            int offset = std::stoi(parts[0]);
            float parsed[5][100];
            for (int arrIdx = 0; arrIdx < 5; arrIdx++) {
                size_t p = 0;
                const std::string& arrStr = parts[arrIdx + 1];
                for (int i = 0; i < 100; i++) {
                    size_t comma = arrStr.find(',', p);
                    std::string numStr = (comma == std::string::npos) ? arrStr.substr(p) : arrStr.substr(p, comma - p);
                    parsed[arrIdx][i] = std::stof(numStr);
                    if (comma == std::string::npos) {
                        if (i != 99) return; // не хватает значений — формат битый, не применяем
                        break;
                    }
                    p = comma + 1;
                }
            }
            if (offset < 0 || offset >= 100) return;

            std::lock_guard<std::mutex> lock(telemetryHistoryMutex);
            historyOffset = offset;
            for (int arrIdx = 0; arrIdx < 5; arrIdx++) {
                memcpy(targets[arrIdx], parsed[arrIdx], sizeof(float) * 100);
            }
        }
        catch (...) {
            // Оставляем текущие (нулевые) массивы как есть — не крашимся на битом/старом файле.
        }
    }

    void LoadSettings() {
        std::ifstream in(GetConfigFilePath());
        if (!in.is_open()) return;

        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("profile=", 0) == 0) {
                try {
                    int idx = std::stoi(line.substr(8));
                    if (idx >= 0 && idx <= 2) selectedProfileIndex = idx;
                }
                catch (...) {}
            }
            else if (line.rfind("whitelist=", 0) == 0) {
                auto tokens = SplitPipe(line.substr(10));
                std::lock_guard<std::mutex> lock(whitelistMutex);
                userWhitelist.clear();
                for (const auto& t : tokens) userWhitelist.push_back(StringToWString(t));
            }
            else if (line.rfind("games=", 0) == 0) {
                auto tokens = SplitPipe(line.substr(6));
                if (!tokens.empty()) {
                    std::lock_guard<std::mutex> lock(knownGamesListMutex);
                    knownGamesList.clear();
                    for (const auto& t : tokens) knownGamesList.push_back(StringToWString(t));
                }
                // Если секция пуста — оставляем дефолтный список игр, а не стираем его в ноль.
            }
            else if (line.rfind("services=", 0) == 0) {
                auto tokens = SplitPipe(line.substr(9));
                if (!tokens.empty()) {
                    std::lock_guard<std::mutex> lock(telemetryServicesMutex);
                    telemetryServicesList.clear();
                    for (const auto& t : tokens) {
                        size_t sep = t.find(';');
                        size_t sep2 = sep == std::string::npos ? std::string::npos : t.find(';', sep + 1);
                        TelemetryServiceEntry entry;
                        entry.name = StringToWString(sep == std::string::npos ? t : t.substr(0, sep));
                        entry.description = sep == std::string::npos ? L"" :
                            StringToWString(sep2 == std::string::npos ? t.substr(sep + 1) : t.substr(sep + 1, sep2 - sep - 1));
                        entry.disabled = sep2 != std::string::npos && t.substr(sep2 + 1) == "1";
                        telemetryServicesList.push_back(entry);
                    }
                }
            }
            else if (line.rfind("opt=", 0) == 0) {
                // Формат "1,0,1,0,0" — по одному биту на тумблер, в порядке полей ниже.
                std::string rest = line.substr(4);
                std::vector<std::string> flags;
                size_t start = 0;
                while (start <= rest.size()) {
                    size_t pos = rest.find(',', start);
                    flags.push_back(pos == std::string::npos ? rest.substr(start) : rest.substr(start, pos - start));
                    if (pos == std::string::npos) break;
                    start = pos + 1;
                }
                if (flags.size() == 5) {
                    optAutoCleanOnBoot = flags[0] == "1";
                    optDisableNetworkThrottling = flags[1] == "1";
                    optSystemResponsivenessZero = flags[2] == "1";
                    optHighPriorityForGames = flags[3] == "1";
                    optDisableCoreParking = flags[4] == "1";
                }
            }
            else if (line.rfind("telemetry=", 0) == 0) {
                ParseTelemetrySection(line.substr(10));
            }
            // Неизвестные строки (из будущих версий формата) молча игнорируются — forward-compatible.
        }
    }
}