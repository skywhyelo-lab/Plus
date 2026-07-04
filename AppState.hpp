#pragma once
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <fstream>

struct ProcessInfo {
    unsigned long pid;
    std::wstring name;
    size_t memorySizeMB;
    bool isCritical;
    float cpuPercent = 0.0f;
};

struct TelemetryServiceEntry {
    std::wstring name;
    std::wstring description;
    bool disabled = false; // персистентный тумблер вкладки "Оптимизация" — служба выключена пользователем
};

struct OptimizationReport {
    std::string time;
    std::string profileName;
    size_t freedRAM = 0;
    int killedProcs = 0;
    int stoppedServices = 0;
    int tempFilesDeleted = 0;
    float ramPercentBefore = 0.0f;
    float ramPercentAfter = 0.0f;
};

namespace AppState {
    // Потокобезопасные логи
    extern std::mutex logMutex;
    // Кольцевой буфер: не растёт бесконечно, старые строки вытесняются (см. MAX_UI_LOG_LINES в .cpp)
    extern std::deque<std::string> uiLogs;

    // Атомарные флаги для UI и потоков
    extern std::atomic<bool> isOptimizing;
    extern std::atomic<float> progress;
    extern std::atomic<float> standbyMemoryGB;
    extern std::atomic<float> currentRAMUsagePercent;
    extern std::atomic<float> currentRAMUsedGB;
    extern std::atomic<float> currentCPUUsagePercent;
    extern std::atomic<float> currentDiskUsagePercent;
    extern std::atomic<float> currentNetUsagePercent;
    extern std::atomic<float> currentNetworkMbps;

    // Статистика последнего отчета
    extern std::atomic<size_t> reportFreedRAM;
    extern std::atomic<int> reportKilledProcs;
    extern std::atomic<int> reportStoppedServices;
    extern std::atomic<int> reportTempFilesDeleted;
    extern std::atomic<bool> hasReport;
    extern std::string reportTime;

    // Полная история отчетов оптимизации
    extern std::vector<OptimizationReport> reportHistory;
    extern std::mutex reportHistoryMutex;

    // Списки процессов и белый список
    extern std::vector<ProcessInfo> runningProcesses;
    extern std::mutex processesMutex;

    extern std::vector<std::wstring> userWhitelist;
    extern std::mutex whitelistMutex;

    // Единая база критичных процессов Windows — только то, что реально ставится/запускается
    // самой ОС. Защищена при любом профиле очистки (см. Optimizer.hpp: IsCriticalProcess).
    extern std::vector<std::wstring> systemCriticalWhitelist;

    // Игровой софт/лаунчеры/анти-читы — защищены только на профиле "Игровой"
    // (ProcessCleanupLevel::GamingWhitelist), на "Максимуме" не защищены.
    extern std::vector<std::wstring> gamingWhitelist;

    // Индекс текущего выбранного профиля оптимизации (см. Optimizer::GetOptimizationProfiles)
    extern std::atomic<int> selectedProfileIndex;

    // Список процессов популярных игр — используется Auto-Boost (см. Optimizer::CheckAutoBoost)
    // для автопереключения на "Игровой" профиль при запуске игры и возврата обратно при выходе.
    // Список зашит в код и не редактируется через UI (пользователь просил убрать настройку как
    // лишнюю) — только для внутренней логики Auto-Boost.
    extern std::vector<std::wstring> knownGamesList;
    extern std::mutex knownGamesListMutex;

    // Службы, останавливаемые профилем при stopTelemetryServices=true (см. PerformOptimizationTask).
    // Тоже редактируется через Settings-UI.
    extern std::vector<TelemetryServiceEntry> telemetryServicesList;
    extern std::mutex telemetryServicesMutex;

    // Данные для графиков телеметрии. Пишутся из TelemetryCollectorThread, читаются из
    // потока рендеринга — любой доступ к этим 5 массивам и historyOffset обязан идти
    // под telemetryHistoryMutex (без этого — data race/UB на чтении во время записи).
    extern std::mutex telemetryHistoryMutex;
    extern float ramUsageHistory[100];
    extern float standbyHistory[100];
    extern float cpuUsageHistory[100];
    extern float diskUsageHistory[100];
    extern float netUsageHistory[100];
    extern int historyOffset;

    // Реальные аппаратные спецификации
    extern std::string cpuName;
    extern std::string gpuName;
    extern std::string ramSizeGB;
    extern std::string motherboardName;

    // Персистентные тумблеры вкладки "Оптимизация" (см. Optimizer::Apply*Setting) — в
    // отличие от профилей это не разовое действие, а постоянная системная настройка.
    extern std::atomic<bool> optAutoCleanOnBoot;
    extern std::atomic<bool> optDisableNetworkThrottling;
    extern std::atomic<bool> optSystemResponsivenessZero;
    extern std::atomic<bool> optHighPriorityForGames;
    extern std::atomic<bool> optDisableCoreParking;

    // Разовый баннер предупреждения при старте (например, не найден системный шрифт) —
    // показывается в UI, пока пользователь его не закроет.
    extern std::string startupWarningText;
    extern std::atomic<bool> hasStartupWarning;

    // Утилиты логирования (пишут и в UI, и в файл отчета)
    void LogToUI(const std::string& text);
    void LogToBoth(const std::string& text);
    void LogToBoth(const std::wstring& wtext);

    // Конвертация строк
    std::string WStringToString(const std::wstring& wstr);
    std::wstring StringToWString(const std::string& str);

    // Персистентность настроек (выбранный профиль + ручные исключения пользователя) —
    // простой текстовый файл рядом с exe, читается при старте, пишется при выходе.
    void LoadSettings();
    void SaveSettings();
}