#pragma once
#include <string>
#include <vector>

// Уровни агрессивности закрытия процессов при оптимизации (см. IsCriticalProcess/KillUserProcessesAndCleanRAM)
enum class ProcessCleanupLevel {
    None,            // "Стандартный": процессы не трогаем вообще
    GamingWhitelist, // "Игровой": защищены systemCriticalWhitelist + gamingWhitelist
    MaximumPurge     // "Максимум": защищены ТОЛЬКО systemCriticalWhitelist (+ ручной userWhitelist)
};

// Профиль теперь отвечает только за разовое действие "Очистить": закрытие процессов,
// очистка памяти/временных файлов, остановка служб. Реестровые твики и CPU-приоритеты
// вынесены в постоянные тумблеры вкладки "Оптимизация" (см. AppState::opt* + Optimizer::Apply*).
struct OptimizationProfile {
    std::string id;
    std::string name;
    std::string description;
    bool killBackgroundApps;
    ProcessCleanupLevel cleanupLevel;
    bool stopTelemetryServices;
    bool purgeStandbyMemory;
    bool cleanTempFiles;
};

// Три встроенных профиля оптимизации: "Стандартный", "Игровой", "Максимум"
const std::vector<OptimizationProfile>& GetOptimizationProfiles();

void RefreshSystemProcesses();
bool EnableRequiredPrivileges();
void LogSystemSpecs();
void StopWindowsService(const std::wstring& serviceName, const std::wstring& description);
void PurgeSystemMemoryPools();
void KillUserProcessesAndCleanRAM(ProcessCleanupLevel level, bool silent = false, int gracefulTimeoutMs = 3000);
void CleanSystemCaches();
void PerformOptimizationTask(const OptimizationProfile& profile);
bool IsCriticalProcess(const std::wstring& processName, ProcessCleanupLevel level = ProcessCleanupLevel::GamingWhitelist);

// Функция для чтения спецификаций компьютера в реальном времени
void DetectHardwareSpecs();

// Реальная телеметрия производительности (CPU/Диск/Сеть)
void InitPerformanceCounters();
void CollectLiveTelemetry();

// Auto-Boost: если среди запущенных процессов обнаружена игра из AppState::knownGamesList —
// автоматически переключает профиль на "Игровой" (запоминая прежний), а при закрытии игры
// возвращает прежний профиль (только если пользователь не переключил его вручную во время игры).
// Вызывается периодически из главного цикла (main.cpp) вместе с RefreshSystemProcesses.
void CheckAutoBoost();

// После основной оптимизации некоторые закрытые приложения (Discord, апдейтеры, всё с
// собственным watchdog'ом) сами себя перезапускают. Эта функция несколько раз повторно
// проверяет и добивает процессы, попавшие под тот же уровень очистки, в течение окна времени
// после оптимизации — запускается отдельным потоком, чтобы не задерживать завершение отчёта.
void RunZombieWatchdog(ProcessCleanupLevel level, int durationSeconds);

// ============================================================================
// Вкладка "Оптимизация" — постоянные системные твики (в отличие от профилей,
// это не разовое действие: тумблер ВКЛ применяет настройку сейчас и она
// остаётся в силе до следующего переключения, включая перезагрузки ПК).
// Каждая Apply*-функция идемпотентна: её можно звать и по клику тумблера,
// и повторно при старте приложения, чтобы переутвердить сохранённое состояние.
// ============================================================================

// Диск: временные файлы + кэш обновлений Windows при каждом входе в систему.
// Реализовано через задачу Планировщика заданий (Task Scheduler), запускающую
// этот же exe с флагом --boot-cleanup (см. main.cpp) — саму очистку выполняет он же.
void ApplyAutoCleanOnBootSetting(bool enabled);
// Разовая немедленная очистка кэша обновлений (останавливает wuauserv на время очистки).
void CleanWindowsUpdateCacheNow();
// Точка входа для запуска по расписанию (main.cpp вызывает и завершает процесс, если
// в командной строке передан --boot-cleanup).
void RunBootCleanupAndExit();

// Сеть: NetworkThrottlingIndex / SystemResponsiveness (MMCSS) — приоритет игрового/аудио трафика.
void ApplyNetworkThrottlingSetting(bool disabled);
void ApplySystemResponsivenessSetting(bool zeroed);

// Службы: точечный тумблер на каждую службу из AppState::telemetryServicesList.
// disabled=true — останавливает и переводит в SERVICE_DISABLED; disabled=false —
// возвращает в ручной запуск (SERVICE_DEMAND_START) и запускает обратно.
void ApplyServiceDisabledSetting(const std::wstring& serviceName, const std::wstring& description, bool disabled);

// CPU: высокий приоритет для процессов из AppState::knownGamesList. Не разовая
// операция — работает постоянно, пока тумблер включён: следующая функция
// вызывается из фонового потока (main.cpp: ProcessListCollectorThread) на
// каждом цикле обновления списка процессов.
void ApplyGamePriorityBoostPass();

// Core Parking — отключает парковку ядер CPU в активной схеме электропитания (powercfg).
void ApplyCoreParkingSetting(bool disabled);

// ============================================================================
// Раздел "Базовое" — дополнительные постоянные твики (пп. 8–19 спецификации).
// Каждая Get_* функция читает состояние НАПРЯМУЮ из реестра/службы/системы —
// не из кэша AppState — чтобы кнопка "Обновить" всегда показывала правду.
// ============================================================================

bool Get_FastStartupDisabled();
void Apply_FastStartupDisabled(bool disable);

bool Get_HibernationDisabled();
void Apply_HibernationDisabled(bool disable);

bool Get_NtpSyncDisabled();
void Apply_NtpSyncDisabled(bool disable);

bool Get_DeliveryOptDisabled();
void Apply_DeliveryOptDisabled(bool disable);

bool Get_DriverUpdatesExcluded();
void Apply_DriverUpdatesExcluded(bool exclude);

bool Get_StoreAutoUpdateDisabled();
void Apply_StoreAutoUpdateDisabled(bool disable);

bool Get_ToastNotificationsDisabled();
void Apply_ToastNotificationsDisabled(bool disable);

bool Get_StorageSenseEnabled();
void Apply_StorageSenseEnabled(bool enable);

bool Get_ThumbCachePeriodicClean();
void Apply_ThumbCachePeriodicClean(bool enable);

bool Get_ThumbnailsDisabled();
void Apply_ThumbnailsDisabled(bool disable);

bool Get_NtfsLastAccessDisabled();
void Apply_NtfsLastAccessDisabled(bool disable);

bool Get_ScheduledTrimEnabled();
void Apply_ScheduledTrimEnabled(bool enable);

bool Get_UwpBackgroundDisabled();
void Apply_UwpBackgroundDisabled(bool disable);

bool Get_CursorShadowDisabled();
void Apply_CursorShadowDisabled(bool disable);

// true, если системный диск (C:) — SSD (через IOCTL_STORAGE_QUERY_PROPERTY / seek penalty).
// На HDD пункт планового TRIM скрывается/блокируется в UI — там нужна классическая дефрагментация.
bool IsSystemDiskSSD();

// Единая проверка прав администратора — твики "Базового" требуют их все;
// при отсутствии прав UI показывает один и тот же статус вместо тихого бездействия.
bool IsRunningAsAdmin();

// ============================================================================
// Раздел "Безопасность" — реальные тумблеры (раньше были UI-заглушками с
// захардкоженным valueDefault=true, из-за чего показывали "Включено", даже
// если защита реально была выключена в системе).
// ============================================================================

bool Get_DefenderRealtimeEnabled();
void Apply_DefenderRealtimeEnabled(bool enable);

bool Get_FirewallEnabled();
void Apply_FirewallEnabled(bool enable);

bool Get_SmartScreenEnabled();
void Apply_SmartScreenEnabled(bool enable);

bool Get_UACEnabled();
void Apply_UACEnabled(bool enable);

bool Get_AmsiEnabled();
void Apply_AmsiEnabled(bool enable);

bool Get_CodeIntegrityEnabled(); // Memory Integrity / HVCI
void Apply_CodeIntegrityEnabled(bool enable);

// ============================================================================
// "Кастомизация" — реальные тумблеры (реестр Explorer/Personalize/контекстное меню).
// ============================================================================
bool Get_TransparencyDisabled();
void Apply_TransparencyDisabled(bool disable);
bool Get_CopilotDisabled();
void Apply_CopilotDisabled(bool disable);
bool Get_WebSearchDisabled();
void Apply_WebSearchDisabled(bool disable);
bool Get_TakeOwnershipAdded();      // пункт "Стать владельцем" в контекстном меню
void Apply_TakeOwnershipAdded(bool add);
bool Get_TrustedInstallerBypassAdded();
void Apply_TrustedInstallerBypassAdded(bool add);
bool Get_ClassicPhotoViewerRestored();
void Apply_ClassicPhotoViewerRestored(bool restore);

// ============================================================================
// "Выпиливание" — реальные тумблеры (службы/политики/удаление Appx-пакетов).
// ============================================================================
bool Get_OneDriveDisabled();
void Apply_OneDriveDisabled(bool disable);
bool Get_XboxComponentsDisabled();
void Apply_XboxComponentsDisabled(bool disable);
bool Get_BloatwareRemoved();
void Apply_BloatwareRemoved(bool remove);
bool Get_CortanaDisabled();
void Apply_CortanaDisabled(bool disable);

// ============================================================================
// "Группы служб" — обобщённая проверка/применение по списку имён служб сразу.
// Группа считается "включена", если хотя бы одна служба из списка не SERVICE_DISABLED.
// ============================================================================
bool IsServiceGroupEnabled(const std::vector<std::wstring>& services);
void SetServiceGroupDisabled(const std::vector<std::wstring>& services, bool disable);

// ============================================================================
// "Приватность / Телеметрия" — реальные тумблеры (переменные окружения — официальный
// способ Microsoft отключить телеметрию .NET/PowerShell/Visual Studio, плюс реестр/служба).
// ============================================================================
bool Get_DotnetTelemetryDisabled();
void Apply_DotnetTelemetryDisabled(bool disable);
bool Get_PowerShellTelemetryDisabled();
void Apply_PowerShellTelemetryDisabled(bool disable);
bool Get_VSTelemetryDisabled();
void Apply_VSTelemetryDisabled(bool disable);
bool Get_VSCodeTelemetryDisabled();
void Apply_VSCodeTelemetryDisabled(bool disable);
bool Get_CeipDisabled();
void Apply_CeipDisabled(bool disable);
bool Get_DiagTrackDisabled();       // служба "Connected User Experiences and Telemetry"
void Apply_DiagTrackDisabled(bool disable);

// ============================================================================
// "Прерывания / Устройства" — MSI Mode для видеокарты через реальный PnP-девайс (SetupAPI).
// ============================================================================
bool Get_GpuMsiModeEnabled();
void Apply_GpuMsiModeEnabled(bool enable);
bool Get_UsbSelectiveSuspendDisabled();
void Apply_UsbSelectiveSuspendDisabled(bool disable);

// ============================================================================
// Дополнительные "Твики" — сетевая задержка, точность мыши, план электропитания.
// ============================================================================
bool Get_NagleDisabled();
void Apply_NagleDisabled(bool disable);
bool Get_MouseAccelDisabled();
void Apply_MouseAccelDisabled(bool disable);
bool Get_UltimatePerformanceActive();
void Apply_UltimatePerformanceActive(bool enable);