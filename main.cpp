#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <shellapi.h>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>
#include "AppState.hpp"
#include "Optimizer.hpp"
#include "Renderer.hpp"
#include "License.hpp"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "resource.h"
#include <shlobj.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "Shell32.lib")

#define WM_TRAYICON (WM_APP + 2)
#define ID_TRAY_OPEN 1001
#define ID_TRAY_EXIT 1002

static NOTIFYICONDATAW g_trayIconData = {};
static bool g_trayActive = false;

// Генерирует маленькую иконку — сплошной цветной кружок с альфа-сглаженным краем — для
// индикации нагрузки системы прямо в трее без открытия окна (зелёный/жёлтый/красный).
static HICON CreateStatusDotIcon(COLORREF color) {
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size <= 0) size = 16;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size; // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hColorBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hColorBmp);
    ZeroMemory(bits, (size_t)size * size * 4);

    unsigned char* pixels = (unsigned char*)bits;
    float cx = size / 2.0f, cy = size / 2.0f;
    float r = size * 0.38f;
    unsigned char R = GetRValue(color), G = GetGValue(color), B = GetBValue(color);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = x + 0.5f - cx;
            float dy = y + 0.5f - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            float alpha = 0.0f;
            if (dist < r - 1.0f) alpha = 1.0f;
            else if (dist < r + 1.0f) alpha = (r + 1.0f - dist) * 0.5f;
            if (alpha > 0.0f) {
                int idx = (y * size + x) * 4;
                // BGRA, premultiplied alpha (требование иконок с альфа-каналом в Windows)
                pixels[idx + 0] = (unsigned char)(B * alpha);
                pixels[idx + 1] = (unsigned char)(G * alpha);
                pixels[idx + 2] = (unsigned char)(R * alpha);
                pixels[idx + 3] = (unsigned char)(255 * alpha);
            }
        }
    }

    HBITMAP hMaskBmp = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = hColorBmp;
    iconInfo.hbmMask = hMaskBmp;
    HICON hIcon = CreateIconIndirect(&iconInfo);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hColorBmp);
    DeleteObject(hMaskBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    return hIcon;
}

static HICON g_trayIconGreen = nullptr;
static HICON g_trayIconYellow = nullptr;
static HICON g_trayIconRed = nullptr;
static int g_trayLoadLevel = -1; // -1 = ещё не выставлялся

// Проверяется каждый кадр из главного цикла (дёшево — просто сравнение), но реально
// вызывает Shell_NotifyIconW только при смене уровня нагрузки, а не каждый раз.
static void UpdateTrayIconByLoad() {
    if (!g_trayActive) return;
    if (!g_trayIconGreen) g_trayIconGreen = CreateStatusDotIcon(RGB(34, 197, 94));
    if (!g_trayIconYellow) g_trayIconYellow = CreateStatusDotIcon(RGB(245, 158, 11));
    if (!g_trayIconRed) g_trayIconRed = CreateStatusDotIcon(RGB(239, 68, 68));

    float load = (std::max)(AppState::currentCPUUsagePercent.load(), AppState::currentRAMUsagePercent.load());
    int level = load < 60.0f ? 0 : (load < 85.0f ? 1 : 2);
    if (level == g_trayLoadLevel) return;
    g_trayLoadLevel = level;

    g_trayIconData.hIcon = (level == 0) ? g_trayIconGreen : (level == 1 ? g_trayIconYellow : g_trayIconRed);
    Shell_NotifyIconW(NIM_MODIFY, &g_trayIconData);
}

static void AddTrayIcon(HWND hwnd) {
    if (g_trayActive) return;
    ZeroMemory(&g_trayIconData, sizeof(g_trayIconData));
    g_trayIconData.cbSize = sizeof(NOTIFYICONDATAW);
    g_trayIconData.hWnd = hwnd;
    g_trayIconData.uID = 1;
    g_trayIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_trayIconData.uCallbackMessage = WM_TRAYICON;
    g_trayIconData.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON),
        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!g_trayIconData.hIcon) g_trayIconData.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_trayIconData.szTip, L"Pulse — оптимизатор системы");
    Shell_NotifyIconW(NIM_ADD, &g_trayIconData);
    g_trayActive = true;

    // Сразу переключаем на цветной индикатор нагрузки, а не оставляем логотип, пока
    // не сменится уровень (сброс — иначе UpdateTrayIconByLoad решит, что уровень не менялся).
    g_trayLoadLevel = -1;
    UpdateTrayIconByLoad();
}

static void RemoveTrayIcon() {
    if (!g_trayActive) return;
    Shell_NotifyIconW(NIM_DELETE, &g_trayIconData);
    g_trayActive = false;
}

static void RestoreFromTray(HWND hwnd) {
    RemoveTrayIcon();
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#define ULTRA_DWMWCP_ROUND 2

void ApplyWin11WindowStyle(HWND hwnd) {
    DWORD corner = ULTRA_DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
}

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Типографическая иерархия (Segoe UI Regular/Semibold в нескольких кеглях)
ImFont* g_FontBody = nullptr;   // 14px — основной текст
ImFont* g_FontSmall = nullptr;   // 12px — подписи, метки времени
ImFont* g_FontCardTitle = nullptr;   // 16px semibold — заголовки карточек
ImFont* g_FontSubtitle = nullptr;   // 20px semibold — подзаголовки секций
ImFont* g_FontTitle = nullptr;   // 28px semibold — заголовок страницы
ImFont* g_FontLucide = nullptr;  // Lucide Icons (ISC license) — большая часть иконок вместо ручных векторов

void CreateRenderTarget();
void CleanupRenderTarget();
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void TelemetryCollectorThread();
void ProcessListCollectorThread();

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DPICHANGED: {
        // Windows предлагает новую позицию/размер окна для целевого монитора при смене DPI
        RECT* suggested = (RECT*)lParam;
        SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_LBUTTONDOWN:
        // Перетаскивание безрамочного окна за кастомный заголовок (Y < 44)
        // Исключаем область кнопок "Свернуть/Закрыть" на правом краю (X > width - 100),
        // а также любой клик, который в этот момент попадает на интерактивный элемент
        // ImGui (например плитки профилей по центру титлбара) — иначе окно перехватывает
        // клик под перетаскивание раньше, чем до него доходит InvisibleButton.
    {
        int x = (int)(short)LOWORD(lParam);
        int y = (int)(short)HIWORD(lParam);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int width = rc.right - rc.left;
        if (y < 44 && x < width - 100 && !IsPointOnTitleBarWidget(x, y)) {
            ReleaseCapture();
            SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
    }
    break;
    case WM_APP_TRAY_MINIMIZE:
        ShowWindow(hWnd, SW_HIDE);
        AddTrayIcon(hWnd);
        return 0;
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
            RestoreFromTray(hWnd);
        }
        else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN, L"Открыть Pulse");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Выход");
            SetForegroundWindow(hWnd); // чтобы меню закрывалось по клику мимо
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(menu);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_OPEN) {
            RestoreFromTray(hWnd);
        }
        else if (LOWORD(wParam) == ID_TRAY_EXIT) {
            PostMessage(hWnd, WM_CLOSE, 0, 0);
        }
        return 0;
    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);

    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);

    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void TelemetryCollectorThread() {
    InitPerformanceCounters();

    while (true) {
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            AppState::currentRAMUsagePercent = (float)memInfo.dwMemoryLoad;
            AppState::currentRAMUsedGB = (float)((double)(memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0));

            double totalPhysGB = (double)memInfo.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
            float maxRealisticStandby = (float)(totalPhysGB * 0.30);

            if (!AppState::isOptimizing) {
                float currentStandby = AppState::standbyMemoryGB.load();
                if (currentStandby < maxRealisticStandby) {
                    AppState::standbyMemoryGB = currentStandby + 0.015f;
                }
                else if (currentStandby > maxRealisticStandby) {
                    AppState::standbyMemoryGB = maxRealisticStandby;
                }
            }
        }

        CollectLiveTelemetry();

        {
            std::lock_guard<std::mutex> lock(AppState::telemetryHistoryMutex);
            int idx = AppState::historyOffset;
            AppState::ramUsageHistory[idx] = AppState::currentRAMUsagePercent;
            AppState::standbyHistory[idx] = AppState::standbyMemoryGB.load();
            AppState::cpuUsageHistory[idx] = AppState::currentCPUUsagePercent.load();
            AppState::diskUsageHistory[idx] = AppState::currentDiskUsagePercent.load();
            AppState::netUsageHistory[idx] = AppState::currentNetUsagePercent.load();
            AppState::historyOffset = (idx + 1) % 100;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

// Раньше RefreshSystemProcesses() вызывался прямо в главном цикле рендера каждые 3 секунды —
// это перечисление ВСЕХ процессов с OpenProcess/GetProcessMemoryInfo/GetProcessTimes на каждый
// (может быть 100+ процессов, видно на скриншотах с десятками chrome.exe), которое блокировало
// кадр на ощутимое время и создавало периодический "рывок" в анимациях ровно раз в 3 секунды.
// Вынесено в отдельный фоновый поток — рендер-цикл больше ничего тяжёлого не ждёт.
void ProcessListCollectorThread() {
    while (true) {
        RefreshSystemProcesses();
        PruneProcessIconCache();
        CheckAutoBoost();
        ApplyGamePriorityBoostPass();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    // Per-monitor DPI awareness (V2) — окно и текст остаются чёткими на любом
    // масштабе Windows, вместо растягивания растрового буфера системным скейлером.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Запуск по расписанию (задача Планировщика заданий от вкладки "Оптимизация",
    // см. Optimizer::ApplyAutoCleanOnBootSetting) — только очистка, без UI/лицензии.
    if (wcsstr(GetCommandLineW(), L"--boot-cleanup") != nullptr) {
        RunBootCleanupAndExit();
        return 0;
    }

    // Лицензия проверяется до создания окна DX11: если ключ не активирован
    // на этом ПК, показываем блокирующий диалог ввода ключа. Отмена — выход.
    if (!License::EnsureLicensed(hInstance)) {
        return 0;
    }

    // Иконка окна (Alt-Tab, панель задач) — вшитый ресурс IDI_APPICON, а не системная заглушка.
    HICON hAppIconLarge = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON),
        IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    HICON hAppIconSmall = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON),
        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    // Внедряем CS_DROPSHADOW для красивой размытой тени вокруг безрамочного окна
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC | CS_DROPSHADOW, WndProc, 0L, 0L, GetModuleHandle(nullptr), hAppIconLarge, nullptr, nullptr, nullptr, L"UltraCompOptimizer", hAppIconSmall };
    RegisterClassEx(&wc);

    // Стиль WS_POPUP полностью удаляет заголовок и стандартные рамки Windows.
    // Окно намеренно НЕ резинается (без WS_THICKFRAME) — вся раскладка карточек
    // просчитана под один фиксированный размер, чтобы исключить переполнения при ресайзе.
    // Сам размер подбирается под фактическое разрешение экрана (но не крупнее эталонного),
    // чтобы окно гарантированно помещалось и на Full HD, и на более скромных мониторах.
    UINT startupDpi = GetDpiForSystem();
    float dpiScale = startupDpi / 96.0f;

    const int idealWidth = (int)(1440 * dpiScale);
    const int idealHeight = (int)(820 * dpiScale);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int windowWidth = (std::min)(idealWidth, screenW - 60);
    int windowHeight = (std::min)(idealHeight, screenH - 100);
    int posX = (std::max)(0, (screenW - windowWidth) / 2);
    int posY = (std::max)(0, (screenH - windowHeight) / 2);

    HWND hwnd = CreateWindowExW(
        0,
        L"UltraCompOptimizer",
        L"Pulse",
        WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
        posX, posY, windowWidth, windowHeight,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    // Монитор, на котором реально создалось окно, может иметь другой DPI, чем
    // системный (например ноутбук + внешний монитор) — уточняем и подгоняем размер/позицию.
    UINT actualDpi = GetDpiForWindow(hwnd);
    if (actualDpi != startupDpi) {
        dpiScale = actualDpi / 96.0f;
        windowWidth = (std::min)((int)(1440 * dpiScale), screenW - 60);
        windowHeight = (std::min)((int)(820 * dpiScale), screenH - 100);
        posX = (std::max)(0, (screenW - windowWidth) / 2);
        posY = (std::max)(0, (screenH - windowHeight) / 2);
        SetWindowPos(hwnd, nullptr, posX, posY, windowWidth, windowHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ApplyWin11WindowStyle(hwnd);

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // Защитная синхронизация swapchain с реальным client rect окна: на некоторых
    // машинах (в частности с нестандартным DPI/масштабом) размер, использованный при
    // создании свопчейна в CreateDeviceD3D, может разойтись с фактическим размером
    // окна после ShowWindow/применения стиля — из-за этого ImGui считает холст больше
    // реального видимого окна, и контент у нижнего края обрезается. Пересчитываем на всякий случай.
    {
        RECT realClientRect;
        GetClientRect(hwnd, &realClientRect);
        UINT realW = (UINT)(realClientRect.right - realClientRect.left);
        UINT realH = (UINT)(realClientRect.bottom - realClientRect.top);
        if (realW > 0 && realH > 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, realW, realH, DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
    }

    // Первоначальное сканирование реального железа устройства
    DetectHardwareSpecs();

    // Восстанавливаем сохранённый профиль и пользовательские исключения из прошлого запуска
    AppState::LoadSettings();

    // Переутверждаем персистентные твики вкладки "Оптимизация" — на случай, если сторонняя
    // программа или обновление Windows сбросили значения реестра между запусками.
    // Не трогаем задачу автоочистки (schtasks) и службы — они меняются только по клику
    // тумблера, чтобы не дёргать SCM/Планировщик на каждом старте приложения.
    std::thread([]() {
        ApplyNetworkThrottlingSetting(AppState::optDisableNetworkThrottling.load());
        ApplySystemResponsivenessSetting(AppState::optSystemResponsivenessZero.load());
        ApplyCoreParkingSetting(AppState::optDisableCoreParking.load());
    }).detach();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Системная папка шрифтов через SHGetFolderPathW вместо хардкода "C:\Windows\Fonts" —
    // корректно работает и на нестандартных установках Windows (другой системный диск и т.п.).
    wchar_t fontsDirBuf[MAX_PATH];
    std::wstring fontsDir = L"C:\\Windows\\Fonts"; // запасной вариант, если SHGetFolderPathW вдруг откажет
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_FONTS, nullptr, 0, fontsDirBuf))) {
        fontsDir = fontsDirBuf;
    }
    std::string segoeuiPath = AppState::WStringToString(fontsDir + L"\\segoeui.ttf");
    std::string seguisbPath = AppState::WStringToString(fontsDir + L"\\seguisb.ttf");

    const ImWchar* cyrillicRanges = io.Fonts->GetGlyphRangesCyrillic();
    ImFont* bodyFont = io.Fonts->AddFontFromFileTTF(segoeuiPath.c_str(), 16.0f * dpiScale, nullptr, cyrillicRanges);
    g_FontBody = bodyFont;
    g_FontSmall = io.Fonts->AddFontFromFileTTF(segoeuiPath.c_str(), 12.0f * dpiScale, nullptr, cyrillicRanges);
    g_FontCardTitle = io.Fonts->AddFontFromFileTTF(seguisbPath.c_str(), 16.0f * dpiScale, nullptr, cyrillicRanges);
    g_FontSubtitle = io.Fonts->AddFontFromFileTTF(seguisbPath.c_str(), 20.0f * dpiScale, nullptr, cyrillicRanges);
    g_FontTitle = io.Fonts->AddFontFromFileTTF(seguisbPath.c_str(), 27.0f * dpiScale, nullptr, cyrillicRanges);
    if (!bodyFont || !g_FontCardTitle || !g_FontSubtitle || !g_FontTitle || !g_FontSmall) {
        AppState::LogToUI("[WARN] Не найден системный шрифт Segoe UI (" + segoeuiPath + ") — используется запасной шрифт ImGui.");
        AppState::startupWarningText = "Не найден системный шрифт Windows (Segoe UI) — интерфейс использует запасной шрифт.";
        AppState::hasStartupWarning = true;
        OutputDebugStringW(L"[Pulse] System font NOT FOUND — falling back to default ImGui font.\n");
    }
    if (!g_FontCardTitle) g_FontCardTitle = g_FontBody;
    if (!g_FontSubtitle) g_FontSubtitle = g_FontBody;
    if (!g_FontTitle) g_FontTitle = g_FontBody;
    if (!g_FontSmall) g_FontSmall = g_FontBody;
    if (!g_FontBody) g_FontBody = io.Fonts->AddFontDefault();

    // Иконочный шрифт Lucide (ISC license, cdn.jsdelivr.net/npm/lucide-static) —
    // вшит в сам exe как ресурс RCDATA (см. app.rc), внешних файлов рядом с программой
    // не требуется.
    {
        static const ImWchar lucideRanges[] = { 0xE000, 0xE6FF, 0 };

        HMODULE hModule = GetModuleHandleW(nullptr);
        HRSRC hRes = FindResourceW(hModule, MAKEINTRESOURCEW(IDR_LUCIDE_FONT), RT_RCDATA);
        if (hRes) {
            HGLOBAL hData = LoadResource(hModule, hRes);
            DWORD dataSize = SizeofResource(hModule, hRes);
            if (hData && dataSize > 0) {
                void* resPtr = LockResource(hData);
                // ImGui берёт владение буфером и освобождает его сам (font_cfg.FontDataOwnedByAtlas
                // по умолчанию true) — копируем данные ресурса в отдельный буфер через ImGui::MemAlloc,
                // так как память LockResource принадлежит модулю и не должна быть передана на free().
                void* fontDataCopy = ImGui::MemAlloc(dataSize);
                memcpy(fontDataCopy, resPtr, dataSize);
                g_FontLucide = io.Fonts->AddFontFromMemoryTTF(fontDataCopy, (int)dataSize, 16.0f * dpiScale, nullptr, lucideRanges);
            }
        }

        if (!g_FontLucide) {
            OutputDebugStringW(L"[Pulse] Lucide font FAILED TO LOAD from resource — falling back to hand-drawn vector icons.\n");
        }
        else {
            OutputDebugStringW(L"[Pulse] Lucide font loaded OK from embedded resource.\n");
        }
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    SetCustomImGuiStyle();

    RefreshSystemProcesses();
    PruneProcessIconCache();

    std::thread telemetry(TelemetryCollectorThread);
    telemetry.detach();

    std::thread processList(ProcessListCollectorThread);
    processList.detach();

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        UpdateTrayIconByLoad();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderUI();

        ImGui::Render();
        const float clear_color[4] = { 0.043f, 0.043f, 0.059f, 1.00f }; // Тёмный фон Obsidian
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    AppState::SaveSettings();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}