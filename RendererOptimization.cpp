#include "RendererOptimization.hpp"
#include "RendererStyle.hpp"
#include "AppState.hpp"
#include "Optimizer.hpp"
#include "imgui.h"
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cmath>

extern ImFont* g_FontBody;
extern ImFont* g_FontSmall;
extern ImFont* g_FontCardTitle;
extern ImFont* g_FontTitle;

#ifndef IM_PI
#define IM_PI 3.14159265358979323846f
#endif

// ============================================================================
// Вкладка "Оптимизация": разделы слева, единая анатомия строки-тумблера,
// пресеты по разделу + глобальные, батч-применение, health-индикатор.
//
// Часть тумблеров ЗАВЕДЕНА на реальный бэкенд (Optimizer::Apply*) — диск/сеть/
// службы/CPU. Остальные — UI-заглушки (меняют только локальный pending-стейт)
// до отдельного захода по каждой категории, особенно "Безопасность"/"Приватность".
// ============================================================================

namespace {

// --- Обёртки над реальным бэкендом (см. Optimizer.hpp) ---
bool Get_AutoCleanOnBoot() { return AppState::optAutoCleanOnBoot.load(); }
void Apply_AutoCleanOnBoot(bool v) { AppState::optAutoCleanOnBoot = v; std::thread([v]() { ApplyAutoCleanOnBootSetting(v); }).detach(); AppState::SaveSettings(); }

bool Get_NetThrottle() { return AppState::optDisableNetworkThrottling.load(); }
void Apply_NetThrottle(bool v) { AppState::optDisableNetworkThrottling = v; std::thread([v]() { ApplyNetworkThrottlingSetting(v); }).detach(); AppState::SaveSettings(); }

bool Get_Responsiveness() { return AppState::optSystemResponsivenessZero.load(); }
void Apply_Responsiveness(bool v) { AppState::optSystemResponsivenessZero = v; std::thread([v]() { ApplySystemResponsivenessSetting(v); }).detach(); AppState::SaveSettings(); }

bool Get_GamePriority() { return AppState::optHighPriorityForGames.load(); }
void Apply_GamePriority(bool v) { AppState::optHighPriorityForGames = v; AppState::SaveSettings(); }

bool Get_CoreParking() { return AppState::optDisableCoreParking.load(); }
void Apply_CoreParking(bool v) { AppState::optDisableCoreParking = v; std::thread([v]() { ApplyCoreParkingSetting(v); }).detach(); AppState::SaveSettings(); }

// Раздел "Базовое", пп. 8-19 — getReal читает систему напрямую (без кэша AppState),
// applyReal пишет в фоновом потоке, чтобы не подвешивать кадр на RegSetValueExW/schtasks/fsutil.
void Apply_FastStartup(bool v) { std::thread([v]() { Apply_FastStartupDisabled(v); }).detach(); }
void Apply_Hibernation(bool v) { std::thread([v]() { Apply_HibernationDisabled(v); }).detach(); }
void Apply_NtpSync(bool v) { std::thread([v]() { Apply_NtpSyncDisabled(v); }).detach(); }
void Apply_DeliveryOpt(bool v) { std::thread([v]() { Apply_DeliveryOptDisabled(v); }).detach(); }
void Apply_DriverUpdates(bool v) { std::thread([v]() { Apply_DriverUpdatesExcluded(v); }).detach(); }
void Apply_StoreAutoUpdate(bool v) { std::thread([v]() { Apply_StoreAutoUpdateDisabled(v); }).detach(); }
void Apply_ToastNotif(bool v) { std::thread([v]() { Apply_ToastNotificationsDisabled(v); }).detach(); }
void Apply_StorageSense(bool v) { std::thread([v]() { Apply_StorageSenseEnabled(v); }).detach(); }
void Apply_ThumbCacheClean(bool v) { std::thread([v]() { Apply_ThumbCachePeriodicClean(v); }).detach(); }
void Apply_Thumbnails(bool v) { std::thread([v]() { Apply_ThumbnailsDisabled(v); }).detach(); }
void Apply_NtfsLastAccess(bool v) { std::thread([v]() { Apply_NtfsLastAccessDisabled(v); }).detach(); }
void Apply_ScheduledTrim(bool v) { std::thread([v]() { Apply_ScheduledTrimEnabled(v); }).detach(); }
void Apply_UwpBackground(bool v) { std::thread([v]() { Apply_UwpBackgroundDisabled(v); }).detach(); }
void Apply_CursorShadow(bool v) { std::thread([v]() { Apply_CursorShadowDisabled(v); }).detach(); }

// Раздел "Безопасность" — раньше были UI-заглушки без бэкенда (valueDefault захардкожен
// в true), из-за чего экран показывал "Включено" даже при реально выключенном антивирусе.
// Теперь getReal читает систему напрямую, applyReal реально переключает защиту.
void Apply_DefenderRt(bool v) { std::thread([v]() { Apply_DefenderRealtimeEnabled(v); }).detach(); }
void Apply_Firewall(bool v) { std::thread([v]() { Apply_FirewallEnabled(v); }).detach(); }
void Apply_SmartScreen(bool v) { std::thread([v]() { Apply_SmartScreenEnabled(v); }).detach(); }
void Apply_UAC(bool v) { std::thread([v]() { Apply_UACEnabled(v); }).detach(); }
void Apply_Amsi(bool v) { std::thread([v]() { Apply_AmsiEnabled(v); }).detach(); }
void Apply_CodeIntegrity(bool v) { std::thread([v]() { Apply_CodeIntegrityEnabled(v); }).detach(); }

// "Кастомизация" / "Выпиливание" — раньше были UI-заглушки (значения не читались/не
// применялись нигде), теперь реально читают и пишут реестр/службы/Appx.
void Apply_Transparency(bool v) { std::thread([v]() { Apply_TransparencyDisabled(v); }).detach(); }
void Apply_Copilot(bool v) { std::thread([v]() { Apply_CopilotDisabled(v); }).detach(); }
void Apply_WebSearch(bool v) { std::thread([v]() { Apply_WebSearchDisabled(v); }).detach(); }
void Apply_TakeOwnership(bool v) { std::thread([v]() { Apply_TakeOwnershipAdded(v); }).detach(); }
void Apply_TrustedInstaller(bool v) { std::thread([v]() { Apply_TrustedInstallerBypassAdded(v); }).detach(); }
void Apply_PhotoViewer(bool v) { std::thread([v]() { Apply_ClassicPhotoViewerRestored(v); }).detach(); }

void Apply_OneDrive(bool v) { std::thread([v]() { Apply_OneDriveDisabled(v); }).detach(); }
void Apply_XboxComponents(bool v) { std::thread([v]() { Apply_XboxComponentsDisabled(v); }).detach(); }
void Apply_Bloatware(bool v) { std::thread([v]() { Apply_BloatwareRemoved(v); }).detach(); }
void Apply_Cortana(bool v) { std::thread([v]() { Apply_CortanaDisabled(v); }).detach(); }

// "Группы служб" — значение тумблера = "группа включена".
bool Get_GrpBluetooth() { return IsServiceGroupEnabled({ L"bthserv", L"BthAvctpSvc", L"BluetoothUserService" }); }
void Apply_GrpBluetooth(bool v) { std::thread([v]() { SetServiceGroupDisabled({ L"bthserv", L"BthAvctpSvc", L"BluetoothUserService" }, !v); }).detach(); }
bool Get_GrpVpn() { return IsServiceGroupEnabled({ L"RasMan", L"RemoteAccess", L"IKEEXT", L"PolicyAgent" }); }
void Apply_GrpVpn(bool v) { std::thread([v]() { SetServiceGroupDisabled({ L"RasMan", L"RemoteAccess", L"IKEEXT", L"PolicyAgent" }, !v); }).detach(); }
bool Get_GrpRemote() { return IsServiceGroupEnabled({ L"TermService", L"UmRdpService", L"WinRM", L"SessionEnv" }); }
void Apply_GrpRemote(bool v) { std::thread([v]() { SetServiceGroupDisabled({ L"TermService", L"UmRdpService", L"WinRM", L"SessionEnv" }, !v); }).detach(); }
bool Get_GrpHyperv() { return IsServiceGroupEnabled({ L"vmms", L"vmcompute", L"HvHost" }); }
void Apply_GrpHyperv(bool v) { std::thread([v]() { SetServiceGroupDisabled({ L"vmms", L"vmcompute", L"HvHost" }, !v); }).detach(); }
bool Get_GrpPrint() { return IsServiceGroupEnabled({ L"Spooler", L"PrintNotify", L"Fax" }); }
void Apply_GrpPrint(bool v) { std::thread([v]() { SetServiceGroupDisabled({ L"Spooler", L"PrintNotify", L"Fax" }, !v); }).detach(); }

// "Приватность / Телеметрия"
void Apply_DotnetTelemetry(bool v) { std::thread([v]() { Apply_DotnetTelemetryDisabled(v); }).detach(); }
void Apply_PowerShellTelemetry(bool v) { std::thread([v]() { Apply_PowerShellTelemetryDisabled(v); }).detach(); }
void Apply_VSCodeTelemetry(bool v) { std::thread([v]() { Apply_VSCodeTelemetryDisabled(v); }).detach(); }
void Apply_Ceip(bool v) { std::thread([v]() { Apply_CeipDisabled(v); }).detach(); }
void Apply_VSTelemetry(bool v) { std::thread([v]() { Apply_VSTelemetryDisabled(v); }).detach(); }
void Apply_DiagTrack(bool v) { std::thread([v]() { Apply_DiagTrackDisabled(v); }).detach(); }

// "Прерывания / Устройства"
void Apply_GpuMsiMode(bool v) { std::thread([v]() { Apply_GpuMsiModeEnabled(v); }).detach(); }
void Apply_UsbSelectiveSuspend(bool v) { std::thread([v]() { Apply_UsbSelectiveSuspendDisabled(v); }).detach(); }

// Дополнительные "Твики"
void Apply_Nagle(bool v) { std::thread([v]() { Apply_NagleDisabled(v); }).detach(); }
void Apply_MouseAccel(bool v) { std::thread([v]() { Apply_MouseAccelDisabled(v); }).detach(); }
void Apply_UltimatePerf(bool v) { std::thread([v]() { Apply_UltimatePerformanceActive(v); }).detach(); }

// --- Модель одной строки-тумблера ---
struct TweakDef {
    const char* id;
    const char* icon;
    const char* title;
    const char* desc;
    const char* dependency;   // nullptr, если зависимостей нет
    bool hasWarning;
    bool positiveMeansSafe;   // true = "включено" безопасно; false = наоборот (напр. "антивирус выключен" — риск)
    bool valueDefault, valueOptimal, valueMax;
    bool (*getReal)();        // nullptr для UI-заглушек (пока без бэкенда)
    void (*applyReal)(bool);  // nullptr для UI-заглушек
};

struct CategoryDef {
    const char* id;
    const char* icon;
    const char* title;
    std::vector<TweakDef> rows; // пусто для "Менеджер служб" (своя отрисовка)
};

const std::vector<CategoryDef>& GetCategories() {
    static const std::vector<CategoryDef> cats = {
        { "basic", "gear", "Базовое", {
            { "autoclean", "wrench", "Автоочистка при запуске Windows", "Временные файлы и кэш обновлений при каждом входе в систему", nullptr, false, true, false, true, true, Get_AutoCleanOnBoot, Apply_AutoCleanOnBoot },
            { "cursor_scheme", "monitor", "Отключить тень курсора мыши", "Убирает эффект тени под указателем — снижает нагрузку на композитор", nullptr, false, true, false, true, true, Get_CursorShadowDisabled, Apply_CursorShadow },
            { "windows_update", "gear", "Ограничить автообновления Windows", "Обновления только вручную, без фонового скачивания — меньше нагрузки на диск/сеть в фоне", "Отключится: автоматическая установка патчей безопасности", true, true, false, false, true, nullptr, nullptr },
            { "uwp_background", "square", "Отключить фоновую работу UWP-приложений", "Запрет работы магазинных приложений в фоне", nullptr, false, true, false, true, true, Get_UwpBackgroundDisabled, Apply_UwpBackground },
            { "fso", "monitor", "Отключить полноэкранную оптимизацию (FSO)", "Снижает задержку ввода в играх за счёт прямого вывода мимо композитора Windows", "Alt+Tab и оверлеи (Discord/GeForce Experience) в части игр могут работать хуже", true, true, false, false, true, nullptr, nullptr },
            { "gamebar", "gear", "Отключить Xbox Game Bar", "Оверлей записи игр и статистики — фоновый процесс, ест ОЗУ/CPU", "Отключится: захват видео Win+G", true, true, false, true, true, nullptr, nullptr },
            { "indexing", "search", "Отключить индексацию поиска Windows", "Индексирует файлы для быстрого поиска — грузит диск в фоне", "Поиск в Пуске и Проводнике станет медленнее", true, true, false, true, true, nullptr, nullptr },
            { "fast_startup", "wrench", "Отключить быстрый запуск", "Гибридный сон вместо полного выключения — иногда конфликтует с драйверами/обновлениями", nullptr, false, true, false, false, true, Get_FastStartupDisabled, Apply_FastStartup },
            { "hibernation", "wrench", "Отключить гибернацию", "Освобождает место на диске (файл hiberfil.sys ≈ размер ОЗУ)", "Пропадёт быстрый выход из сна на ноутбуках", true, true, false, false, true, Get_HibernationDisabled, Apply_Hibernation },
            { "ntp_sync", "wrench", "Отключить автосинхронизацию времени", "Останавливает службу точного времени (W32Time)", "Системные часы могут постепенно расходиться с реальным временем", true, true, false, false, true, Get_NtpSyncDisabled, Apply_NtpSync },
            { "delivery_opt", "wrench", "Ограничить оптимизацию доставки", "Обновления только с серверов Microsoft, без раздачи частей по сети/интернету другим ПК", "Отключится: P2P-раздача обновлений другим устройствам в сети", true, true, false, true, true, Get_DeliveryOptDisabled, Apply_DeliveryOpt },
            { "driver_updates", "wrench", "Исключить драйверы из Windows Update", "Драйверы устройств больше не подставляются автоматически через Центр обновления", "Драйверы придётся ставить вручную (Nvidia/AMD/производитель)", true, true, false, false, true, Get_DriverUpdatesExcluded, Apply_DriverUpdates },
            { "store_autoupdate", "wrench", "Отключить автообновление Store-приложений", "Фоновое обновление UWP-софта из Microsoft Store", nullptr, false, true, false, true, true, Get_StoreAutoUpdateDisabled, Apply_StoreAutoUpdate },
            { "push_notif", "wrench", "Отключить push-уведомления", "Общий тумблер тостов Центра уведомлений", "Пропадут всплывающие уведомления приложений", true, true, false, true, true, Get_ToastNotificationsDisabled, Apply_ToastNotif },
            { "storage_sense", "wrench", "Включить Storage Sense", "Автоматическая фоновая очистка диска по расписанию", nullptr, false, true, false, true, true, Get_StorageSenseEnabled, Apply_StorageSense },
            { "thumb_cache_clean", "wrench", "Периодическая очистка кэша миниатюр", "Регулярно чистит thumbcache_*.db в кэше Проводника", nullptr, false, true, false, true, true, Get_ThumbCachePeriodicClean, Apply_ThumbCacheClean },
            { "thumbnails", "wrench", "Отключить миниатюры файлов", "Проводник показывает только стандартные иконки без превью — быстрее в папках с большим числом файлов/видео", "Пропадут превью изображений и видео в Проводнике", true, true, false, false, true, Get_ThumbnailsDisabled, Apply_Thumbnails },
            { "ntfs_last_access", "wrench", "Отключить обновление меток доступа NTFS", "Windows не обновляет метку времени последнего доступа при каждом открытии файла", nullptr, false, true, false, true, true, Get_NtfsLastAccessDisabled, Apply_NtfsLastAccess },
            { "scheduled_trim", "wrench", "Плановый TRIM для SSD", "Еженедельная автооптимизация SSD через планировщик заданий", nullptr, false, true, true, true, true, Get_ScheduledTrimEnabled, Apply_ScheduledTrim },
        }},
        { "security", "shield_check", "Безопасность", {
            // positiveMeansSafe=false: во всей вкладке "Оптимизация" вправо=+FPS(синий), влево=-FPS(красный) —
            // единое правило по всем разделам, включая "Безопасность". Реальный риск описан в dependency ниже
            // и это отдельно от цвета статуса.
            { "defender_rt", "shield_check", "Защита в реальном времени (Defender)", "Постоянное сканирование файлов и процессов", "Отключится: автоматическая блокировка вирусов/шифровальщиков", true, false, true, true, false, Get_DefenderRealtimeEnabled, Apply_DefenderRt },
            { "firewall", "shield", "Брандмауэр Windows", "Фильтрация входящих/исходящих сетевых подключений", "Отключится: блокировка нежелательных подключений", true, false, true, true, false, Get_FirewallEnabled, Apply_Firewall },
            { "smartscreen", "shield_check", "SmartScreen", "Проверка репутации запускаемых файлов и сайтов", "Отключится: предупреждения о подозрительных файлах", true, false, true, true, false, Get_SmartScreenEnabled, Apply_SmartScreen },
            { "uac", "lock", "Контроль учётных записей (UAC)", "Запрос подтверждения на действия от имени администратора", "Отключится: защита от несанкционированных системных изменений (нужна перезагрузка)", true, false, true, true, false, Get_UACEnabled, Apply_UAC },
            { "amsi", "shield", "AMSI (антивирусный интерфейс сканирования)", "Проверка скриптов PowerShell/JS перед исполнением", "Отключится: обнаружение вредоносных скриптов", true, false, true, true, false, Get_AmsiEnabled, Apply_Amsi },
            { "code_integrity", "lock", "Code Integrity / Memory Integrity (HVCI)", "Проверка цифровых подписей драйверов и системных файлов", "Отключится: защита от неподписанных драйверов (нужна перезагрузка)", true, false, true, true, false, Get_CodeIntegrityEnabled, Apply_CodeIntegrity },
        }},
        { "customization", "monitor", "Кастомизация", {
            { "transparency", "monitor", "Отключить прозрачность интерфейса", "Эффект прозрачности в окнах и меню Windows — лёгкая нагрузка на композитор", nullptr, false, true, false, false, true, Get_TransparencyDisabled, Apply_Transparency },
            { "copilot", "square", "Отключить Copilot", "Встроенный ИИ-помощник Windows — фоновый процесс", nullptr, false, true, false, true, true, Get_CopilotDisabled, Apply_Copilot },
            { "web_search", "search", "Отключить веб-результаты в поиске", "Поиск Bing вместе с локальными файлами в меню Пуск — сетевой запрос на каждый ввод", nullptr, false, true, false, true, true, Get_WebSearchDisabled, Apply_WebSearch },
            { "take_ownership", "lock", "Пункт \"Стать владельцем\" в контекстном меню", "Быстрый доступ к смене владельца файла/папки", nullptr, false, true, false, true, true, Get_TakeOwnershipAdded, Apply_TakeOwnership },
            { "trustedinstaller", "shield", "Обход TrustedInstaller", "Упрощённое изменение защищённых системных файлов", "Риск: случайное повреждение системных файлов", true, false, false, false, true, Get_TrustedInstallerBypassAdded, Apply_TrustedInstaller },
            { "photo_viewer", "monitor", "Классический просмотр фотографий", "Возвращает старый Windows Photo Viewer в меню \"Открыть с помощью\"", nullptr, false, true, false, true, true, Get_ClassicPhotoViewerRestored, Apply_PhotoViewer },
        }},
        { "debloat", "cross", "Выпиливание", {
            { "onedrive", "cross", "Отключить OneDrive", "Облачная синхронизация файлов от Microsoft — фоновый процесс, диск/сеть", "Отключится: автосинхронизация папок с облаком", true, true, false, true, true, Get_OneDriveDisabled, Apply_OneDrive },
            { "xbox", "cross", "Отключить Xbox-компоненты", "Службы и приложения Xbox Game Bar/Xbox App", "Отключится: облачные сохранения и достижения Xbox", true, true, false, true, true, Get_XboxComponentsDisabled, Apply_XboxComponents },
            { "bloatware", "cross", "Удалить предустановленные приложения", "Удаляет магазинные приложения-спутники (3D Viewer, Paint 3D и т.п.) — необратимо без переустановки из Store", nullptr, true, true, false, true, true, Get_BloatwareRemoved, Apply_Bloatware },
            { "cortana", "cross", "Отключить Cortana", "Голосовой ассистент Windows — фоновый процесс", nullptr, false, true, false, true, true, Get_CortanaDisabled, Apply_Cortana },
        }},
        { "servicegroups", "chart_bar", "Группы служб", {
            // Значение тумблера = "группа служб включена" (как в системе по умолчанию).
            // positiveMeansSafe=false: включённая группа держит службы в памяти → влево(выкл)=+FPS=синий.
            { "grp_bluetooth", "chart_bar", "Bluetooth", "Весь стек служб Bluetooth одним переключателем", "Отключится: подключение Bluetooth-устройств", true, false, true, true, false, Get_GrpBluetooth, Apply_GrpBluetooth },
            { "grp_vpn", "chart_bar", "VPN / Proxy", "Службы маршрутизации и удалённого доступа", "Отключится: VPN-подключения", true, false, true, true, false, Get_GrpVpn, Apply_GrpVpn },
            { "grp_remote", "chart_bar", "Удалённое управление", "RDP, WinRM и связанные службы", "Отключится: удалённый рабочий стол", true, false, true, true, false, Get_GrpRemote, Apply_GrpRemote },
            { "grp_hyperv", "chart_bar", "Hyper-V", "Виртуализация Windows", "Отключится: запуск виртуальных машин Hyper-V", true, false, true, false, false, Get_GrpHyperv, Apply_GrpHyperv },
            { "grp_print", "chart_bar", "Принтер / сканер", "Службы печати и сканирования", "Отключится: печать и сканирование документов", true, false, true, true, false, Get_GrpPrint, Apply_GrpPrint },
        }},
        { "servicemanager", "user", "Менеджер служб", {} },
        { "privacy", "lock", "Приватность / Телеметрия", {
            { "dotnet_telemetry", "lock", "Отключить телеметрию .NET", "Официальная переменная окружения DOTNET_CLI_TELEMETRY_OPTOUT", nullptr, false, true, false, true, true, Get_DotnetTelemetryDisabled, Apply_DotnetTelemetry },
            { "powershell_telemetry", "lock", "Отключить телеметрию PowerShell", "Официальная переменная окружения POWERSHELL_TELEMETRY_OPTOUT", nullptr, false, true, false, true, true, Get_PowerShellTelemetryDisabled, Apply_PowerShellTelemetry },
            { "dev_telemetry", "lock", "Отключить телеметрию VS Code", "Правит telemetry.telemetryLevel в settings.json", nullptr, false, true, false, true, true, Get_VSCodeTelemetryDisabled, Apply_VSCodeTelemetry },
            { "ceip", "lock", "Отключить CEIP", "Программа улучшения качества ПО — автоматическая отправка отчётов об использовании", nullptr, false, true, false, true, true, Get_CeipDisabled, Apply_Ceip },
            { "vs_telemetry", "lock", "Отключить телеметрию Visual Studio", "Официальная переменная окружения VSTEL_OptOut", nullptr, false, true, false, true, true, Get_VSTelemetryDisabled, Apply_VSTelemetry },
            { "webcam_telemetry", "lock", "Отключить службу телеметрии Windows (DiagTrack)", "Connected User Experiences and Telemetry — основной сборщик диагностических данных ОС", nullptr, false, true, false, true, true, Get_DiagTrackDisabled, Apply_DiagTrack },
        }},
        { "tweaks", "wrench", "Твики", {
            { "net_throttle", "wrench", "Отключить сетевой троттлинг", "NetworkThrottlingIndex — снимает ограничение пропускной способности", nullptr, false, true, false, true, true, Get_NetThrottle, Apply_NetThrottle },
            { "responsiveness", "wrench", "System Responsiveness = 0", "Приоритет игрового/аудио трафика над фоновыми задачами", nullptr, false, true, false, true, true, Get_Responsiveness, Apply_Responsiveness },
            { "game_priority", "rocket", "Высокий приоритет для игр", "Автоматически поднимает приоритет процессов из списка игр", nullptr, false, true, false, true, true, Get_GamePriority, Apply_GamePriority },
            { "core_parking", "cpu", "Отключить Core Parking", "Держит все ядра CPU активными, без энергосберегающей парковки", "Возможный рост энергопотребления/нагрева в простое", true, true, false, true, true, Get_CoreParking, Apply_CoreParking },
            { "nagle", "wrench", "Отключить алгоритм Найгла", "TcpNoDelay/TcpAckFrequency на всех сетевых интерфейсах — меньше задержка сетевых пакетов в играх", nullptr, false, true, false, true, true, Get_NagleDisabled, Apply_Nagle },
            { "mouse_accel", "wrench", "Отключить ускорение мыши", "\"Enhance pointer precision\" — движение мыши 1:1 без сглаживания, важно для прицеливания", nullptr, false, true, false, true, true, Get_MouseAccelDisabled, Apply_MouseAccel },
            { "ultimate_perf", "rocket", "Схема питания \"Максимальная производительность\"", "Скрытая схема Windows без энергосбережения CPU — дублирует и активирует Ultimate Performance", "Рост энергопотребления и нагрева, не рекомендуется на ноутбуках без питания от сети", true, true, false, false, true, Get_UltimatePerformanceActive, Apply_UltimatePerf },
        }},
        { "interrupts", "cpu", "Прерывания / Устройства", {
            { "msi_mode", "cpu", "MSI Mode для видеокарты", "Message-Signaled Interrupts вместо legacy IRQ — снижает задержки (нужна перезагрузка)", nullptr, true, true, false, false, true, Get_GpuMsiModeEnabled, Apply_GpuMsiMode },
            { "usb_suspend", "cpu", "Отключить USB Selective Suspend", "Запрещает энергосбережение USB-портов — убирает микро-задержки/просыпание игровых мышей и клавиатур", "Небольшой рост энергопотребления", true, true, false, true, true, Get_UsbSelectiveSuspendDisabled, Apply_UsbSelectiveSuspend },
        }},
    };
    return cats;
}

// Состав служб в группе (для разворачивания под шевроном в разделе "Группы служб").
// Отображается только как справка — групповой тумблер пока UI-заглушка.
std::vector<const char*> GroupMembers(const std::string& groupId) {
    if (groupId == "grp_bluetooth") return { "bthserv", "BthAvctpSvc", "BluetoothUserService" };
    if (groupId == "grp_vpn") return { "RasMan", "RemoteAccess", "IKEEXT", "PolicyAgent" };
    if (groupId == "grp_remote") return { "TermService", "UmRdpService", "WinRM", "SessionEnv" };
    if (groupId == "grp_hyperv") return { "vmms", "vmcompute", "HvHost" };
    if (groupId == "grp_print") return { "Spooler", "PrintNotify", "Fax" };
    return {};
}

// --- Общее состояние экрана ---
int g_activeCategory = 0; // "Базовое"
bool g_showHints = true;
std::unordered_map<std::string, bool> g_pending;   // текущее состояние тумблеров (ещё не применено батчем)
std::unordered_map<std::string, bool> g_applied;   // то, что реально применено (для health-% и диффа несохранённого)
bool g_stateLoaded = false;
std::atomic<bool> g_isAdmin{ true };
std::atomic<bool> g_isSystemSSD{ true };

// "Обновить"/первая загрузка раньше звали getReal() для ВСЕХ тумблеров синхронно на UI-потоке —
// часть из них (Defender, список Appx-пакетов) реально запускает powershell.exe и ждёт его
// вывода, это пол-секунды-секунда на каждый вызов. Суммарно по всем разделам это давало
// ощутимый фриз интерфейса при каждом открытии вкладки/клике "Обновить". Теперь опрос системы
// идёт в фоновом потоке, а UI лишь один раз в кадр забирает готовый результат.
std::mutex g_refreshMutex;
std::atomic<bool> g_refreshInProgress{ false };
std::atomic<bool> g_refreshResultReady{ false };
std::unordered_map<std::string, bool> g_refreshResult;

// Тост "изменения применены" — короткий оверлей в правом нижнем углу.
float g_applyToastTimer = 0.0f;
int g_applyToastCount = 0;

bool GetPending(const TweakDef& t) {
    auto it = g_pending.find(t.id);
    if (it != g_pending.end()) return it->second;
    // Раньше здесь синхронно звался t.getReal() на UI-потоке — для реальных бэкендов
    // (Defender/Appx через powershell.exe) это подвешивало кадр на десятые доли секунды -
    // секунду при первой отрисовке строки. Реальное значение подтянет фоновый
    // RefreshFromSystem()/PumpRefreshResult(); до этого момента просто показываем дефолт.
    bool v = t.valueDefault;
    g_pending[t.id] = v;
    g_applied[t.id] = v;
    return v;
}

// Запускает опрос системы в фоновом потоке; результат забирается в PumpRefreshResult()
// на следующих кадрах, когда будет готов. Повторный вызов, пока предыдущий ещё не
// закончился, игнорируется (иначе клики по "Обновить" плодили бы кучу потоков с powershell.exe).
void RefreshFromSystem() {
    if (g_refreshInProgress.exchange(true)) return;
    std::thread([]() {
        bool isAdmin = IsRunningAsAdmin();
        bool isSSD = IsSystemDiskSSD();
        std::unordered_map<std::string, bool> result;
        for (const auto& cat : GetCategories()) {
            for (const auto& row : cat.rows) {
                if (row.getReal) result[row.id] = row.getReal();
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_refreshMutex);
            g_refreshResult = std::move(result);
        }
        g_isAdmin = isAdmin;
        g_isSystemSSD = isSSD;
        g_refreshResultReady = true;
        g_refreshInProgress = false;
        }).detach();
}

// Забирает результат фонового опроса (если готов) и применяет его к g_pending/g_applied
// на UI-потоке — вызывается один раз в кадр из RenderOptimizationTab().
void PumpRefreshResult() {
    if (!g_refreshResultReady.exchange(false)) return;
    std::lock_guard<std::mutex> lock(g_refreshMutex);
    for (const auto& kv : g_refreshResult) {
        g_pending[kv.first] = kv.second;
        g_applied[kv.first] = kv.second;
    }
}

// Единый примитив тумблера-пилюли — используется и строками-твиками, и менеджером
// служб, чтобы анатомия переключателя была одинаковой во всех разделах.
bool DrawTogglePill(const std::string& animId, ImVec2 center, bool value) {
    const float swW = 44.0f, swH = 24.0f;
    ImVec2 swPos = ImVec2(center.x - swW * 0.5f, center.y - swH * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = AnimToKey(ImGui::GetID((animId + "_sw").c_str()), value ? 1.0f : 0.0f, 14.0f);
    ImU32 offCol = IM_COL32(70, 70, 80, 255);
    ImU32 onCol = ImColor(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 1.0f);
    ImU32 trackCol = ImColor(LerpColor(ImColor(offCol).Value, ImColor(onCol).Value, t));
    dl->AddRectFilled(swPos, ImVec2(swPos.x + swW, swPos.y + swH), trackCol, swH * 0.5f);
    float knobR = swH * 0.5f - 3.0f;
    float knobX = swPos.x + swH * 0.5f + (swW - swH) * t;
    dl->AddCircleFilled(ImVec2(knobX, center.y), knobR, IM_COL32(255, 255, 255, 255));

    ImGui::PushID((animId + "_hit").c_str());
    ImGui::SetCursorScreenPos(swPos);
    bool clicked = ImGui::InvisibleButton("##sw", ImVec2(swW, swH));
    ImGui::PopID();
    return clicked;
}

// --- Анатомия строки-тумблера ---
// Все элементы центрируются на одной горизонтальной оси headerCy, посчитанной от
// реальной высоты заголовка, а не подобранных на глаз смещениях.
void DrawTweakRow(const TweakDef& t) {
    bool value = GetPending(t);
    bool changed = g_applied.count(t.id) && g_applied[t.id] != value;

    float rowW = ImGui::GetContentRegionAvail().x;
    ImGuiID expandKey = ImGui::GetID((std::string(t.id) + "_exp").c_str());
    ImGuiStorage* storage = ImGui::GetStateStorage();
    bool expanded = g_showHints || storage->GetBool(expandKey, false);

    const float headerH = 34.0f;
    ImVec2 rowPos = ImGui::GetCursorScreenPos();
    float headerCy = rowPos.y + headerH * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Индикатор несохранённого изменения — акцентная полоска слева + лёгкая заливка.
    if (changed) {
        dl->AddRectFilled(ImVec2(rowPos.x - 6.0f, rowPos.y), ImVec2(rowPos.x + rowW, rowPos.y + headerH),
            ImColor(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.06f), 6.0f);
        dl->AddRectFilled(ImVec2(rowPos.x - 6.0f, rowPos.y + 4.0f), ImVec2(rowPos.x - 3.0f, rowPos.y + headerH - 4.0f),
            ImColor(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.9f), 1.5f);
    }

    // Иконка
    const float iconSz = 18.0f;
    DrawVectorIconDirect(t.icon, ImVec2(rowPos.x, headerCy - iconSz * 0.5f), iconSz, ImColor(Palette::TextMuted));

    // Зона клика по заголовку (разворачивает описание) — до начала статус-текста/тумблера
    const float swW = 44.0f;
    const float rightZone = 130.0f; // резерв справа под статус-текст + тумблер
    ImGui::PushID(t.id);
    ImGui::InvisibleButton("##hdr", ImVec2((std::max)(1.0f, rowW - rightZone), headerH));
    if (ImGui::IsItemClicked()) storage->SetBool(expandKey, !expanded);
    ImGui::PopID();

    // Название
    ImGui::PushFont(g_FontBody);
    float titleH = ImGui::GetTextLineHeight();
    dl->AddText(ImVec2(rowPos.x + 28.0f, headerCy - titleH * 0.5f), ImColor(Palette::TextMain), t.title);
    float titleW = ImGui::CalcTextSize(t.title).x;
    ImGui::PopFont();

    // ⚠ на пунктах с побочными эффектами
    if (t.hasWarning) {
        float wx = rowPos.x + 28.0f + titleW + 8.0f;
        DrawVectorIconDirect("bell", ImVec2(wx, headerCy - 7.0f), 14.0f, ImColor(Palette::Warning));
    }

    // Статус-текст (синий — безопасно, красный — риск), справа от тумблера
    bool isSafe = (value == t.positiveMeansSafe);
    const char* statusText = value ? "Включено" : "Отключено";
    ImVec4 statusCol = isSafe ? Palette::Info : Palette::Danger;
    ImGui::PushFont(g_FontSmall);
    ImVec2 statusSz = ImGui::CalcTextSize(statusText);
    float statusX = rowPos.x + rowW - swW - 12.0f - statusSz.x;
    dl->AddText(ImVec2(statusX, headerCy - statusSz.y * 0.5f), ImColor(statusCol), statusText);
    ImGui::PopFont();

    // Тумблер
    if (DrawTogglePill(t.id, ImVec2(rowPos.x + rowW - swW * 0.5f, headerCy), value)) {
        g_pending[t.id] = !value;
    }

    ImGui::SetCursorScreenPos(ImVec2(rowPos.x, rowPos.y + headerH));

    if (expanded) {
        ImGui::PushFont(g_FontSmall);
        ImGui::Indent(28.0f);
        ImGui::TextColored(Palette::TextMuted, "%s", t.desc);
        if (t.dependency && !value) {
            ImGui::TextColored(Palette::Warning, "При отключении не будет работать: %s", t.dependency);
        }
        ImGui::Unindent(28.0f);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4));
    }

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));
}

void ApplyPresetToCategory(const CategoryDef& cat, int tier) { // 0=default,1=optimal,2=max
    for (const auto& row : cat.rows) {
        bool v = tier == 0 ? row.valueDefault : tier == 1 ? row.valueOptimal : row.valueMax;
        g_pending[row.id] = v;
    }
}

void ApplyPresetGlobal(int tier) { // 0=default,1=optimal,2=pro(=max),3=hardcore(=max)
    int mapped = tier >= 2 ? 2 : tier;
    for (const auto& cat : GetCategories()) ApplyPresetToCategory(cat, mapped);
}

int ComputeHealthPercent() {
    int total = 0, safe = 0;
    for (const auto& cat : GetCategories()) {
        for (const auto& row : cat.rows) {
            total++;
            if (GetPending(row) == row.positiveMeansSafe) safe++;
        }
    }
    if (total == 0) return 100;
    return (int)((safe * 100.0f) / total);
}

int CountPendingChanges() {
    int n = 0;
    for (const auto& cat : GetCategories())
        for (const auto& row : cat.rows)
            if (g_applied.count(row.id) && g_applied[row.id] != GetPending(row)) n++;
    return n;
}

// Сколько защитных (hasWarning) пунктов батч переводит в РИСКОВОЕ состояние —
// нужно, чтобы предупредить перед массовым отключением защиты.
int CountNewRiskyChanges() {
    int n = 0;
    for (const auto& cat : GetCategories())
        for (const auto& row : cat.rows) {
            if (!row.hasWarning) continue;
            bool v = GetPending(row);
            bool wasApplied = g_applied.count(row.id) ? g_applied[row.id] : v;
            if (v != wasApplied && v != row.positiveMeansSafe) n++;
        }
    return n;
}

void DoApplyAllPending() {
    int applied = 0;
    for (const auto& cat : GetCategories()) {
        for (const auto& row : cat.rows) {
            bool v = GetPending(row);
            bool wasApplied = g_applied.count(row.id) ? g_applied[row.id] : v;
            if (v != wasApplied) {
                if (row.applyReal) row.applyReal(v);
                applied++;
            }
            g_applied[row.id] = v;
        }
    }
    g_applyToastCount = applied;
    g_applyToastTimer = 4.0f;
}

// --- Здоровье системы: компактный цветной бейдж (текст+точка), без геометрии колец,
// которая на практике накладывалась на заголовок и выглядела "сломанной". ---
void DrawHealthBadge(int pct) {
    ImVec4 col = pct >= 80 ? Palette::Success : pct >= 50 ? Palette::Warning : Palette::Danger;
    char buf[32];
    sprintf_s(buf, "Здоровье: %d%%", pct);
    ImGui::PushFont(g_FontSmall);
    ImVec2 textSz = ImGui::CalcTextSize(buf);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float dotR = 4.0f;
    dl->AddCircleFilled(ImVec2(pos.x + dotR, pos.y + textSz.y * 0.5f), dotR, ImColor(col));
    dl->AddText(ImVec2(pos.x + dotR * 2.0f + 6.0f, pos.y), ImColor(col), buf);
    ImGui::Dummy(ImVec2(dotR * 2.0f + 6.0f + textSz.x, textSz.y));
    ImGui::PopFont();
}

// Верхняя панель раздела: пресеты категории + Обновить + Подсказки + Применить.
// Всё выстроено обычным левым потоком (SameLine без абсолютных смещений) — раньше
// "Подсказки"/"Применить" прибивались к правому краю через w-350/w-205, из-за чего
// при любой другой ширине контента между кнопками образовывался огромный пустой
// разрыв. Теперь разрыв всегда один и тот же модест-гэп, независимо от ширины окна.
void DrawTopBar(const CategoryDef& cat) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 6));
    ImGui::PushStyleColor(ImGuiCol_Text, ContrastTextFor(g_ThemeAccent));
    if (ImGui::Button("По умолчанию")) ApplyPresetToCategory(cat, 0);
    ImGui::SameLine();
    if (ImGui::Button("Оптимальное")) ApplyPresetToCategory(cat, 1);
    ImGui::SameLine();
    if (ImGui::Button("Максимум")) ApplyPresetToCategory(cat, 2);
    ImGui::SameLine();
    ImGui::BeginDisabled(g_refreshInProgress.load());
    if (ImGui::Button(g_refreshInProgress.load() ? "Обновление..." : "Обновить")) RefreshFromSystem();
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::SameLine(0, 24.0f);
    ImGui::Checkbox("Подсказки", &g_showHints);

    // Кнопка "Применить" сразу следом — активна и подсвечена только при наличии
    // несохранённых изменений (иначе приглушена), чтобы было видно "есть что применять".
    int pendingChanges = CountPendingChanges();
    ImGui::SameLine(0, 24.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 6));
    ImVec4 applyBg = pendingChanges > 0 ? Palette::Success : ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, applyBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(applyBg.x * 1.1f, applyBg.y * 1.1f, applyBg.z * 1.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, pendingChanges > 0 ? ImVec4(0.05f, 0.05f, 0.06f, 1.0f) : Palette::TextMuted);
    char applyLabel[48];
    if (pendingChanges > 0) sprintf_s(applyLabel, "Применить (%d)", pendingChanges);
    else sprintf_s(applyLabel, "Применить");
    if (ImGui::Button(applyLabel, ImVec2(195, 0)) && pendingChanges > 0) {
        if (CountNewRiskyChanges() >= 2) ImGui::OpenPopup("Подтверждение риска");
        else DoApplyAllPending();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

void DrawRiskConfirmPopup() {
    ImGui::SetNextWindowSize(ImVec2(420, 0));
    if (ImGui::BeginPopupModal("Подтверждение риска", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        int risky = CountNewRiskyChanges();
        ImGui::PushFont(g_FontBody);
        ImGui::TextColored(Palette::Warning, "Внимание");
        ImGui::PopFont();
        ImGui::TextWrapped("Вы отключаете %d защитных функций системы. Это снижает безопасность "
            "и может открыть систему для угроз. Продолжить?", risky);
        ImGui::Dummy(ImVec2(0, 10));

        ImGui::PushStyleColor(ImGuiCol_Button, Palette::Danger);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(Palette::Danger.x * 1.1f, Palette::Danger.y * 1.1f, Palette::Danger.z * 1.1f, 1.0f));
        if (ImGui::Button("Да, применить", ImVec2(170, 32))) {
            DoApplyAllPending();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(170, 32))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void DrawApplyToast() {
    if (g_applyToastTimer <= 0.0f) return;
    g_applyToastTimer -= ImGui::GetIO().DeltaTime;
    if (g_applyToastTimer <= 0.0f) return;

    float age = 4.0f - g_applyToastTimer;
    float fadeIn = (std::min)(1.0f, age / 0.3f);
    float fadeOut = (std::min)(1.0f, g_applyToastTimer / 0.8f);
    float alpha = fadeIn * fadeOut;

    ImVec2 sz = ImGui::GetIO().DisplaySize;
    float w = 240.0f, h = 56.0f;
    ImVec2 boxMax = ImVec2(sz.x - 20.0f, sz.y - 20.0f);
    ImVec2 boxMin = ImVec2(boxMax.x - w, boxMax.y - h);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(boxMin, boxMax, ImColor(0.094f, 0.094f, 0.125f, 0.95f * alpha), Radius::Card);
    dl->AddRect(boxMin, boxMax, ImColor(Palette::Success.x, Palette::Success.y, Palette::Success.z, 0.5f * alpha), Radius::Card, 0, 1.0f);

    DrawVectorIconDirect("check", ImVec2(boxMin.x + 16.0f, boxMin.y + h * 0.5f - 10.0f), 20.0f, ImColor(Palette::Success.x, Palette::Success.y, Palette::Success.z, alpha));
    char msg[64];
    if (g_applyToastCount > 0) sprintf_s(msg, "Применено изменений: %d", g_applyToastCount);
    else sprintf_s(msg, "Нет изменений для применения");
    ImGui::PushFont(g_FontSmall);
    dl->AddText(ImVec2(boxMin.x + 46.0f, boxMin.y + h * 0.5f - ImGui::CalcTextSize(msg).y * 0.5f), ImColor(1.0f, 1.0f, 1.0f, alpha), msg);
    ImGui::PopFont();
}

// Кнопка раздела в под-сайдбаре: иконка + подпись, чтобы раздел узнавался с одного взгляда.
void DrawCategoryNavButton(int index, const CategoryDef& cat, float width) {
    bool active = (g_activeCategory == index);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float h = 36.0f;
    ImGui::PushID(index);
    if (ImGui::InvisibleButton("##catnav", ImVec2(width, h))) g_activeCategory = index;
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float actT = AnimToKey(ImGui::GetID((std::string("catnav_") + cat.id).c_str()), active ? 1.0f : 0.0f, 14.0f);
    if (actT > 0.01f) {
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), ImColor(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.85f * actT), Radius::Button);
    }
    else if (hovered) {
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), IM_COL32(255, 255, 255, 14), Radius::Button);
    }

    ImVec4 fg = active ? ContrastTextFor(g_ThemeAccent) : Palette::TextMain;
    DrawVectorIconDirect(cat.icon, ImVec2(pos.x + 12.0f, pos.y + h * 0.5f - 8.0f), 16.0f, ImColor(fg));
    ImGui::PushFont(g_FontBody);
    dl->AddText(ImVec2(pos.x + 38.0f, pos.y + h * 0.5f - ImGui::GetTextLineHeight() * 0.5f), ImColor(fg), cat.title);
    ImGui::PopFont();
}

void RenderServiceManager() {
    std::vector<TelemetryServiceEntry> services;
    {
        std::lock_guard<std::mutex> lock(AppState::telemetryServicesMutex);
        services = AppState::telemetryServicesList;
    }
    ImGui::BeginChild("SvcList", ImVec2(0, ImGui::GetContentRegionAvail().y), false);
    for (size_t i = 0; i < services.size(); i++) {
        bool disabled = services[i].disabled;
        std::string label = AppState::WStringToString(services[i].description);
        std::string svcName = AppState::WStringToString(services[i].name);
        std::string uid = "svcmgr_" + svcName;

        const float headerH = 34.0f;
        float rowW = ImGui::GetContentRegionAvail().x;
        ImVec2 rowPos = ImGui::GetCursorScreenPos();
        float cy = rowPos.y + headerH * 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        DrawVectorIconDirect("user", ImVec2(rowPos.x, cy - 9.0f), 18.0f, ImColor(Palette::TextMuted));
        ImGui::PushFont(g_FontBody);
        dl->AddText(ImVec2(rowPos.x + 28.0f, cy - ImGui::GetTextLineHeight() * 0.5f), ImColor(Palette::TextMain), label.c_str());
        ImGui::PopFont();

        // Статус службы: единое правило вкладки — отключено (не тратит ресурсы) = +FPS = синий,
        // работает (держит память/CPU) = -FPS = красный.
        const float swW = 44.0f;
        const char* st = disabled ? "Отключена" : "Работает";
        ImGui::PushFont(g_FontSmall);
        ImVec2 stSz = ImGui::CalcTextSize(st);
        dl->AddText(ImVec2(rowPos.x + rowW - swW - 12.0f - stSz.x, cy - stSz.y * 0.5f),
            ImColor(disabled ? Palette::Info : Palette::Danger), st);
        ImGui::PopFont();

        // Значение тумблера = "служба ВКЛЮЧЕНА" (не disabled), применяется сразу (это точечный менеджер).
        bool enabled = !disabled;
        if (DrawTogglePill(uid, ImVec2(rowPos.x + rowW - swW * 0.5f, cy), enabled)) {
            bool newDisabled = enabled; // был включён → выключаем, и наоборот
            std::wstring name = services[i].name, description = services[i].description;
            {
                std::lock_guard<std::mutex> lock(AppState::telemetryServicesMutex);
                for (auto& s : AppState::telemetryServicesList)
                    if (s.name == name) { s.disabled = newDisabled; break; }
            }
            std::thread([name, description, newDisabled]() { ApplyServiceDisabledSetting(name, description, newDisabled); }).detach();
            AppState::SaveSettings();
        }

        ImGui::SetCursorScreenPos(ImVec2(rowPos.x, rowPos.y + headerH));
        ImGui::PushFont(g_FontSmall);
        ImGui::Indent(28.0f);
        ImGui::TextColored(Palette::TextMuted, "%s", svcName.c_str());
        ImGui::Unindent(28.0f);
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 2));
    }
    ImGui::EndChild();
}

} // namespace

void RenderOptimizationTab() {
    if (!g_stateLoaded) { RefreshFromSystem(); g_stateLoaded = true; }
    PumpRefreshResult();

    // Глобальный стиль приложения ставит ItemSpacing.y = 12 — это добавляется
    // автоматически между КАЖДЫМ ImGui-элементом (каждой кнопкой, Dummy, Separator).
    // На этой вкладке отступы контролируются явно через Dummy(), поэтому вертикальную
    // часть авто-отступа обнуляем на весь экран — иначе Dummy(0,4) реально давал 16px.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 0));

    const auto& cats = GetCategories();
    float subSidebarW = 200.0f;
    float totalH = ImGui::GetContentRegionAvail().y;

    // ---------------- Под-сайдбар: разделы + глобальные пресеты снизу ----------------
    // Без NoScrollbar: на маленьком окне/высоком DPI список разделов + пресеты может
    // не поместиться — тогда сайдбар прокручивается, а не обрезается.
    ImGui::BeginChild("OptSubSidebar", ImVec2(subSidebarW, totalH), false);
    float navBtnW = subSidebarW - 8.0f;
    for (int i = 0; i < (int)cats.size(); i++) {
        DrawCategoryNavButton(i, cats[i], navBtnW);
        ImGui::Dummy(ImVec2(0, 4));
    }

    // "Быстрые методы" — глобальные пресеты по всем разделам сразу (раньше жили в
    // отдельном разделе "Мои твики", который убрали как ненужный пользователю).
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushFont(g_FontSmall);
    ImGui::TextColored(Palette::TextMuted, "  Быстрые методы");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushStyleColor(ImGuiCol_Text, ContrastTextFor(g_ThemeAccent));
    if (ImGui::Button("Базовая", ImVec2(navBtnW, 30))) ApplyPresetGlobal(0);
    ImGui::Dummy(ImVec2(0, 3));
    if (ImGui::Button("Оптимальная", ImVec2(navBtnW, 30))) ApplyPresetGlobal(1);
    ImGui::Dummy(ImVec2(0, 3));
    if (ImGui::Button("Расширенная PRO", ImVec2(navBtnW, 30))) ApplyPresetGlobal(2);
    ImGui::Dummy(ImVec2(0, 3));
    if (ImGui::Button("Хардкор PRO", ImVec2(navBtnW, 30))) ApplyPresetGlobal(3);
    ImGui::PopStyleColor();
    ImGui::EndChild();

    ImGui::SameLine();

    // ---------------- Правая колонка: заголовок + health-кольцо + список ----------------
    ImGui::BeginChild("OptContent", ImVec2(0, totalH), false, ImGuiWindowFlags_NoScrollbar);

    int healthPct = ComputeHealthPercent();
    ImGui::PushFont(g_FontCardTitle);
    ImGui::TextColored(Palette::TextMain, "%s", cats[g_activeCategory].title);
    ImGui::PopFont();
    ImGui::SameLine(0, 16.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f); // выравниваем мелкий шрифт бейджа по низу крупного заголовка
    DrawHealthBadge(healthPct);

    ImGui::Dummy(ImVec2(0, 6));

    const CategoryDef& cat = cats[g_activeCategory];

    if (cat.id == std::string("servicemanager")) {
        // У менеджера служб нет батча/пресетов — только пояснение, изменения применяются сразу.
        ImGui::PushFont(g_FontSmall);
        ImGui::TextColored(Palette::TextMuted, "Точечное управление отдельными службами — изменения применяются сразу");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 8));
        RenderServiceManager();
    }
    else {
        DrawTopBar(cat);
        ImGui::Dummy(ImVec2(0, 8));
        if (!g_isAdmin) {
            ImGui::PushFont(g_FontSmall);
            ImGui::TextColored(Palette::Warning, "Нужны права администратора — запустите приложение от имени администратора, иначе изменения не применятся");
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 6));
        }
        ImGui::BeginChild("TweakList", ImVec2(0, ImGui::GetContentRegionAvail().y), false);
        for (const auto& row : cat.rows) {
            if (std::string(row.id) == "scheduled_trim" && !g_isSystemSSD) {
                ImGui::PushFont(g_FontSmall);
                ImGui::TextColored(Palette::TextMuted, "Плановый TRIM недоступен: системный диск определён как HDD (нужна обычная дефрагментация, не TRIM)");
                ImGui::PopFont();
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0, 2));
                continue;
            }
            DrawTweakRow(row);
            // Для групп служб — разворачиваемый состав служб под шевроном.
            if (cat.id == std::string("servicegroups")) {
                ImGuiStorage* storage = ImGui::GetStateStorage();
                bool expanded = g_showHints || storage->GetBool(ImGui::GetID((std::string(row.id) + "_exp").c_str()), false);
                if (expanded) {
                    auto members = GroupMembers(row.id);
                    if (!members.empty()) {
                        ImGui::Indent(28.0f);
                        ImGui::PushFont(g_FontSmall);
                        std::string list = "Службы: ";
                        for (size_t m = 0; m < members.size(); m++) { list += members[m]; if (m + 1 < members.size()) list += ", "; }
                        ImGui::TextColored(Palette::TextMuted, "%s", list.c_str());
                        ImGui::PopFont();
                        ImGui::Unindent(28.0f);
                        ImGui::Dummy(ImVec2(0, 4));
                    }
                }
            }
        }
        ImGui::EndChild();
    }

    DrawRiskConfirmPopup();
    DrawApplyToast();

    ImGui::EndChild();
    ImGui::PopStyleVar();
}
