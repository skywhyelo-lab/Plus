#include "RendererStyle.hpp"
#include "Renderer.hpp"
#include "AppState.hpp"
#include "Optimizer.hpp"
#include "imgui.h"
#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <psapi.h>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cfloat>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <mutex>

using AppState::WStringToString;
using AppState::StringToWString;

#ifndef IM_PI
#define IM_PI 3.14159265358979323846f
#endif

extern ID3D11Device* g_pd3dDevice;
extern ImFont* g_FontBody;
extern ImFont* g_FontSmall;
extern ImFont* g_FontCardTitle;
extern ImFont* g_FontTitle;
extern ImFont* g_FontLucide;

// ============================================================================
//  ЦВЕТОВАЯ СХЕМА
// ============================================================================
namespace Palette {
    const ImVec4 BaseBg = ImVec4(0.035f, 0.035f, 0.047f, 1.00f); // #09090C — темнее фона, чтобы карточки реально "приподнимались"
    const ImVec4 SurfaceBg = ImVec4(0.090f, 0.090f, 0.122f, 1.00f); // #17171F
    const ImVec4 CardBg = ImVec4(0.122f, 0.122f, 0.161f, 1.00f); // #1F1F29 — заметно светлее фона (было почти неотличимо)
    const ImVec4 CardBgSoft = ImVec4(0.102f, 0.102f, 0.137f, 1.00f);
    const ImVec4 CardBgTop = ImVec4(0.145f, 0.145f, 0.188f, 1.00f); // верх градиента карточки (стеклянный блик)
    const ImVec4 Accent = ImVec4(0.545f, 0.361f, 0.965f, 1.00f); // #8B5CF6
    const ImVec4 AccentHover = ImVec4(0.616f, 0.420f, 1.000f, 1.00f); // #9D6BFF
    const ImVec4 Cyan = ImVec4(0.231f, 0.510f, 0.965f, 1.00f); // используется для RAM-серии
    const ImVec4 Violet = Accent;
    const ImVec4 TextMain = ImVec4(1.000f, 1.000f, 1.000f, 1.00f); // #FFFFFF
    const ImVec4 TextMuted = ImVec4(0.624f, 0.639f, 0.722f, 1.00f); // #9FA3B8
    const ImVec4 Success = ImVec4(0.133f, 0.773f, 0.369f, 1.00f); // #22C55E
    const ImVec4 Warning = ImVec4(0.961f, 0.620f, 0.043f, 1.00f); // #F59E0B
    const ImVec4 Danger = ImVec4(0.937f, 0.267f, 0.267f, 1.00f); // #EF4444
    const ImVec4 Info = Cyan;
    const ImU32  Bg = IM_COL32(11, 11, 15, 255);
}

int activeTab = 0; // 0 Обзор, 1 Процессы, 2 Исключения, 3 История

static std::map<std::wstring, ID3D11ShaderResourceView*> g_ProcessIconCache;
static std::unordered_map<ImGuiID, float> g_AnimStore;

static float AnimTo(const char* id, float target, float speed = 12.0f);

void SetCustomImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = Radius::Card;
    style.ChildRounding = Radius::Card;
    style.FrameRounding = Radius::Input;
    style.PopupRounding = Radius::Button;
    style.ScrollbarSize = 8.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    style.WindowPadding = ImVec2(20, 18);
    style.FramePadding = ImVec2(10, 8);
    style.ItemSpacing = ImVec2(12, 12);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = Palette::TextMain;
    colors[ImGuiCol_TextDisabled] = Palette::TextMuted;
    colors[ImGuiCol_WindowBg] = ImVec4(Palette::BaseBg.x, Palette::BaseBg.y, Palette::BaseBg.z, 0.0f);
    colors[ImGuiCol_ChildBg] = Palette::CardBg;
    colors[ImGuiCol_PopupBg] = ImVec4(0.075f, 0.075f, 0.102f, 0.99f);

    colors[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.07f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.045f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.075f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(Palette::Accent.x, Palette::Accent.y, Palette::Accent.z, 0.30f);

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.18f);
    colors[ImGuiCol_ScrollbarGrabActive] = Palette::Accent;

    colors[ImGuiCol_Button] = ImVec4(Palette::Accent.x, Palette::Accent.y, Palette::Accent.z, 0.85f);
    colors[ImGuiCol_ButtonHovered] = Palette::AccentHover;
    colors[ImGuiCol_ButtonActive] = ImVec4(Palette::Accent.x * 0.85f, Palette::Accent.y * 0.85f, Palette::Accent.z * 0.85f, 1.00f);

    colors[ImGuiCol_Header] = ImVec4(Palette::Accent.x, Palette::Accent.y, Palette::Accent.z, 0.14f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(Palette::Accent.x, Palette::Accent.y, Palette::Accent.z, 0.24f);
    colors[ImGuiCol_HeaderActive] = ImVec4(Palette::Accent.x, Palette::Accent.y, Palette::Accent.z, 0.36f);

    colors[ImGuiCol_TableRowBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.018f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.035f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(1.00f, 1.00f, 1.00f, 0.05f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(1.00f, 1.00f, 1.00f, 0.05f);
}

static float AnimTo(const char* id, float target, float speed) {
    ImGuiID key = ImGui::GetID(id);
    auto it = g_AnimStore.find(key);
    if (it == g_AnimStore.end()) {
        g_AnimStore[key] = target;
        return target;
    }
    float dt = ImGui::GetIO().DeltaTime;
    float t = 1.0f - expf(-speed * dt);
    it->second += (target - it->second) * t;
    return it->second;
}

float AnimToKey(ImGuiID key, float target, float speed) {
    auto it = g_AnimStore.find(key);
    if (it == g_AnimStore.end()) {
        g_AnimStore[key] = target;
        return target;
    }
    float dt = ImGui::GetIO().DeltaTime;
    float t = 1.0f - expf(-speed * dt);
    it->second += (target - it->second) * t;
    return it->second;
}

ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 ContrastTextFor(const ImVec4& bg) {
    // Порог 0.50 подобран так, чтобы все три темы профиля попадали корректно:
    // фиолетовый (~0.485) и красный (~0.467) → белый, светло-зелёный (~0.536) → тёмный.
    float lum = 0.299f * bg.x + 0.587f * bg.y + 0.114f * bg.z;
    return lum > 0.50f ? ImVec4(0.08f, 0.08f, 0.10f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

// Акцентный цвет интерфейса зависит от выбранного профиля оптимизации:
// Стандартный — зелёный (спокойно), Игровой — фиолетовый (бренд по умолчанию),
// Максимум — красный (агрессивный режим). Плавно переходит между профилями.
ImVec4 g_ThemeAccent = ImVec4(0.545f, 0.361f, 0.965f, 1.0f);
ImVec4 g_ThemeAccentHover = ImVec4(0.616f, 0.420f, 1.000f, 1.0f);

static ImVec4 GetProfileThemeColor(int profileIndex) {
    switch (profileIndex) {
    // Стандартный — голубой (было ярко-зелёное #22C55E — слишком светлое, белый текст
    // на нём читался плохо даже после подбора порога в ContrastTextFor). Этот синий
    // достаточно тёмный, чтобы белый текст оставался контрастным без спецслучаев.
    case 0: return ImVec4(0.145f, 0.549f, 0.882f, 1.0f); // #2591E1
    case 2: return ImVec4(0.937f, 0.267f, 0.267f, 1.0f); // Максимум — красный (Palette::Danger)
    default: return ImVec4(0.545f, 0.361f, 0.965f, 1.0f); // Игровой — фиолетовый (Palette::Accent)
    }
}

void UpdateThemeAccent() {
    ImVec4 target = GetProfileThemeColor(AppState::selectedProfileIndex.load());
    float t = 1.0f - expf(-8.0f * ImGui::GetIO().DeltaTime);
    g_ThemeAccent = LerpColor(g_ThemeAccent, target, t);
    g_ThemeAccentHover = ImVec4(
        (std::min)(1.0f, g_ThemeAccent.x * 1.15f + 0.05f),
        (std::min)(1.0f, g_ThemeAccent.y * 1.15f + 0.05f),
        (std::min)(1.0f, g_ThemeAccent.z * 1.15f + 0.05f), 1.0f);

    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.30f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = g_ThemeAccent;
    style.Colors[ImGuiCol_Button] = ImVec4(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.85f);
    style.Colors[ImGuiCol_ButtonHovered] = g_ThemeAccentHover;
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(g_ThemeAccent.x * 0.85f, g_ThemeAccent.y * 0.85f, g_ThemeAccent.z * 0.85f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.14f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.24f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.36f);
}

// Уровень очистки процессов, который реально сработает, если сейчас нажать "Оптимизация
// системы" — используется для live-пересчёта бейджей "Активен"/"Системный" в таблицах
// процессов, чтобы при переключении профиля сразу было видно, что именно будет защищено.
ProcessCleanupLevel GetSelectedCleanupLevel() {
    const auto& profiles = GetOptimizationProfiles();
    int idx = AppState::selectedProfileIndex.load();
    if (idx < 0 || idx >= (int)profiles.size()) return ProcessCleanupLevel::GamingWhitelist;
    return profiles[idx].cleanupLevel;
}

// Единая фаза "сердцебиения" бренда Pulse — двойной удар (lub-dub), переиспользуется
// во всех местах интерфейса, где нужна синхронная живая пульсация (лого, гейдж, индикаторы).
float GetHeartbeatPulse() {
    const float period = 1.6f;
    float t = fmodf((float)ImGui::GetTime(), period) / period;
    auto beat = [](float x, float center, float width) {
        float d = (x - center) / width;
        return expf(-d * d * 4.0f);
        };
    float v = beat(t, 0.08f, 0.06f) * 1.0f + beat(t, 0.24f, 0.08f) * 0.65f;
    return (std::min)(1.0f, v);
}

static ID3D11ShaderResourceView* GetProcessIconSRV(const std::wstring& processName, unsigned long pid) {
    auto it = g_ProcessIconCache.find(processName);
    if (it != g_ProcessIconCache.end()) {
        return it->second;
    }

    if (!g_pd3dDevice) return nullptr;

    g_ProcessIconCache[processName] = nullptr;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) {
        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    }

    wchar_t exePath[MAX_PATH] = { 0 };
    if (hProcess) {
        DWORD size = MAX_PATH;
        QueryFullProcessImageNameW(hProcess, 0, exePath, &size);
        CloseHandle(hProcess);
    }

    if (exePath[0] == L'\0') {
        return nullptr;
    }

    SHFILEINFOW sfi = { 0 };
    DWORD_PTR hr = SHGetFileInfoW(exePath, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON);
    if (!hr || !sfi.hIcon) {
        return nullptr;
    }

    ICONINFO iconInfo = { 0 };
    GetIconInfo(sfi.hIcon, &iconInfo);

    BITMAP bmpColor = { 0 };
    GetObject(iconInfo.hbmColor, sizeof(bmpColor), &bmpColor);

    int width = bmpColor.bmWidth;
    int height = bmpColor.bmHeight;

    HDC hScreen = GetDC(NULL);
    HDC hMem = CreateCompatibleDC(hScreen);
    HBITMAP hBmpOld = (HBITMAP)SelectObject(hMem, iconInfo.hbmColor);

    BITMAPINFOHEADER bih = { 0 };
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = width;
    bih.biHeight = -height;
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;

    std::vector<unsigned char> rgba(width * height * 4);
    GetDIBits(hMem, iconInfo.hbmColor, 0, height, rgba.data(), (BITMAPINFO*)&bih, DIB_RGB_COLORS);

    for (int i = 0; i < width * height; i++) {
        unsigned char b = rgba[i * 4 + 0];
        unsigned char g = rgba[i * 4 + 1];
        unsigned char r = rgba[i * 4 + 2];
        unsigned char a = rgba[i * 4 + 3];
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = a;
    }

    SelectObject(hMem, hBmpOld);
    DeleteDC(hMem);
    ReleaseDC(NULL, hScreen);
    DeleteObject(iconInfo.hbmColor);
    DeleteObject(iconInfo.hbmMask);
    DestroyIcon(sfi.hIcon);

    D3D11_TEXTURE2D_DESC desc = { 0 };
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = { 0 };
    initData.pSysMem = rgba.data();
    initData.SysMemPitch = width * 4;

    ID3D11Texture2D* pTexture = nullptr;
    HRESULT res = g_pd3dDevice->CreateTexture2D(&desc, &initData, &pTexture);
    if (FAILED(res) || !pTexture) {
        return nullptr;
    }

    ID3D11ShaderResourceView* pSRV = nullptr;
    res = g_pd3dDevice->CreateShaderResourceView(pTexture, nullptr, &pSRV);
    pTexture->Release();

    if (FAILED(res)) {
        return nullptr;
    }

    g_ProcessIconCache[processName] = pSRV;
    return pSRV;
}

// Устраняет утечку видеопамяти/дескрипторов: без этого каждая когда-либо запущенная за время
// работы Pulse программа навсегда оседает в g_ProcessIconCache вместе со своей ID3D11ShaderResourceView,
// даже после закрытия. Вызывается после каждого обновления списка процессов (main.cpp) —
// сверяет кэш с реально живыми именами процессов и освобождает текстуры для исчезнувших.
void PruneProcessIconCache() {
    std::vector<std::wstring> liveNames;
    {
        std::lock_guard<std::mutex> lock(AppState::processesMutex);
        liveNames.reserve(AppState::runningProcesses.size());
        for (const auto& p : AppState::runningProcesses) liveNames.push_back(p.name);
    }
    std::sort(liveNames.begin(), liveNames.end());

    for (auto it = g_ProcessIconCache.begin(); it != g_ProcessIconCache.end(); ) {
        if (std::binary_search(liveNames.begin(), liveNames.end(), it->first)) {
            ++it;
        }
        else {
            if (it->second) it->second->Release();
            it = g_ProcessIconCache.erase(it);
        }
    }
}

// Кодирует один codepoint из Basic Multilingual Plane (в т.ч. Private Use Area, где
// живут иконки Lucide) в UTF-8 без использования internal-заголовков ImGui.
static void EncodeUtf8Bmp(unsigned int c, char out[4]) {
    out[0] = (char)(0xE0 | (c >> 12));
    out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[2] = (char)(0x80 | (c & 0x3F));
    out[3] = 0;
}

// Коды символов взяты из официального font/codepoints.json пакета lucide-static —
// точное соответствие имя->код, без угадывания (в отличие от иконочных шрифтов Windows).
static bool TryGetLucideCodepoint(const char* name, unsigned int& out) {
    if (strcmp(name, "grid") == 0) { out = 0xE0E9; return true; }          // grid-3x3
    if (strcmp(name, "lightning") == 0) { out = 0xE1B4; return true; }     // zap
    if (strcmp(name, "chart_bar") == 0) { out = 0xE2A3; return true; }     // bar-chart-3
    if (strcmp(name, "gear") == 0) { out = 0xE154; return true; }          // settings
    if (strcmp(name, "shield") == 0) { out = 0xE158; return true; }        // shield
    if (strcmp(name, "shield_check") == 0) { out = 0xE1FF; return true; }  // shield-check
    if (strcmp(name, "monitor") == 0) { out = 0xE11D; return true; }       // monitor
    if (strcmp(name, "lock") == 0) { out = 0xE10B; return true; }          // lock
    if (strcmp(name, "star") == 0) { out = 0xE176; return true; }          // star
    if (strcmp(name, "user") == 0) { out = 0xE19F; return true; }          // user
    if (strcmp(name, "cross") == 0) { out = 0xE1B2; return true; }         // x
    if (strcmp(name, "wrench") == 0) { out = 0xE1B1; return true; }        // wrench
    if (strcmp(name, "check") == 0) { out = 0xE06C; return true; }         // check
    if (strcmp(name, "minus") == 0) { out = 0xE11C; return true; }         // minus
    if (strcmp(name, "square") == 0) { out = 0xE167; return true; }        // square
    if (strcmp(name, "rocket") == 0) { out = 0xE286; return true; }        // rocket
    if (strcmp(name, "search") == 0) { out = 0xE151; return true; }        // search
    if (strcmp(name, "bell") == 0) { out = 0xE059; return true; }          // bell
    if (strcmp(name, "chevron_right") == 0) { out = 0xE06F; return true; } // chevron-right
    if (strcmp(name, "cpu") == 0) { out = 0xE0A9; return true; }           // cpu
    if (strcmp(name, "ram") == 0) { out = 0xE445; return true; }           // memory-stick
    if (strcmp(name, "motherboard") == 0) { out = 0xE153; return true; }   // server
    if (strcmp(name, "opt_standard") == 0) { out = 0xE1FF; return true; }  // shield-check
    if (strcmp(name, "opt_gaming") == 0) { out = 0xE286; return true; }    // rocket
    if (strcmp(name, "opt_max") == 0) { out = 0xE0D2; return true; }       // flame
    if (strcmp(name, "gpu") == 0) { out = 0xE61A; return true; }           // microchip
    if (strcmp(name, "logo_mark") == 0) { out = 0xE038; return true; }     // activity (heartbeat pulse)
    return false;
}

void DrawVectorIconDirect(const char* name, ImVec2 pos, float size, ImU32 color) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    unsigned int codepoint;
    if (g_FontLucide && TryGetLucideCodepoint(name, codepoint)) {
        char utf8[4];
        EncodeUtf8Bmp(codepoint, utf8);
        // Разные глифы Lucide не идеально одинаково отцентрованы в своём em-квадрате — без этой
        // коррекции разные иконки (например wrench и gear) визуально "плавают" на пиксель-два
        // относительно друг друга в одинаковых по размеру бейджах. Меряем реальный размер глифа
        // и центрируем его в переданном квадрате (pos, pos+size), а не просто ставим как есть.
        ImVec2 glyphSize = g_FontLucide->CalcTextSizeA(size, FLT_MAX, 0.0f, utf8);
        ImVec2 centeredPos = ImVec2(pos.x + (size - glyphSize.x) * 0.5f, pos.y + (size - glyphSize.y) * 0.5f);
        draw_list->AddText(g_FontLucide, size, centeredPos, color, utf8);
        return;
    }

    float pad = size * 0.15f;
    ImVec2 min = ImVec2(pos.x + pad, pos.y + pad);
    ImVec2 max = ImVec2(pos.x + size - pad, pos.y + size - pad);
    float w = max.x - min.x;
    float h = max.y - min.y;
    ImVec2 ctr = ImVec2(min.x + w * 0.5f, min.y + h * 0.5f);

    if (strcmp(name, "grid") == 0) {
        float cell = w * 0.42f;
        float g = w * 0.16f;
        draw_list->AddRectFilled(ImVec2(min.x, min.y), ImVec2(min.x + cell, min.y + cell), color, 2.5f);
        draw_list->AddRectFilled(ImVec2(max.x - cell, min.y), ImVec2(max.x, min.y + cell), color, 2.5f);
        draw_list->AddRectFilled(ImVec2(min.x, max.y - cell), ImVec2(min.x + cell, max.y), color, 2.5f);
        draw_list->AddRectFilled(ImVec2(max.x - cell, max.y - cell), ImVec2(max.x, max.y), color, 2.5f);
        (void)g;
    }
    else if (strcmp(name, "lightning") == 0) {
        draw_list->PathClear();
        draw_list->PathLineTo(ImVec2(ctr.x + w * 0.15f, min.y));
        draw_list->PathLineTo(ImVec2(min.x, ctr.y + h * 0.05f));
        draw_list->PathLineTo(ImVec2(ctr.x - w * 0.1f, ctr.y + h * 0.05f));
        draw_list->PathLineTo(ImVec2(ctr.x - w * 0.15f, max.y));
        draw_list->PathLineTo(ImVec2(max.x, ctr.y - h * 0.05f));
        draw_list->PathLineTo(ImVec2(ctr.x + w * 0.1f, ctr.y - h * 0.05f));
        draw_list->PathFillConvex(color);
    }
    else if (strcmp(name, "chart_bar") == 0) {
        float bar_w = w / 4.0f;
        float spacing = w / 8.0f;
        draw_list->AddRectFilled(ImVec2(min.x, max.y - h * 0.45f), ImVec2(min.x + bar_w, max.y), color, 1.5f);
        draw_list->AddRectFilled(ImVec2(min.x + bar_w + spacing, max.y - h * 0.85f), ImVec2(min.x + bar_w * 2.0f + spacing, max.y), color, 1.5f);
        draw_list->AddRectFilled(ImVec2(min.x + bar_w * 2.0f + spacing * 2.0f, max.y - h * 0.65f), ImVec2(min.x + bar_w * 3.0f + spacing * 2.0f, max.y), color, 1.5f);
    }
    else if (strcmp(name, "gear") == 0) {
        float r_out = w * 0.35f;
        float r_in = w * 0.15f;
        draw_list->AddCircle(ctr, r_out, color, 16, 2.0f);
        draw_list->AddCircleFilled(ctr, r_in, color);
        int teeth_count = 8;
        for (int i = 0; i < teeth_count; i++) {
            float angle = (float)i * (2.0f * IM_PI / (float)teeth_count);
            ImVec2 p1 = ImVec2(ctr.x + cosf(angle) * (r_out - 1.0f), ctr.y + sinf(angle) * (r_out - 1.0f));
            ImVec2 p2 = ImVec2(ctr.x + cosf(angle) * (r_out + 3.0f), ctr.y + sinf(angle) * (r_out + 3.0f));
            draw_list->AddLine(p1, p2, color, 2.5f);
        }
    }
    else if (strcmp(name, "shield") == 0) {
        draw_list->PathClear();
        draw_list->PathLineTo(ImVec2(ctr.x, min.y));
        draw_list->PathLineTo(ImVec2(max.x, min.y + h * 0.18f));
        draw_list->PathLineTo(ImVec2(max.x, ctr.y));
        draw_list->PathLineTo(ImVec2(ctr.x, max.y));
        draw_list->PathLineTo(ImVec2(min.x, ctr.y));
        draw_list->PathLineTo(ImVec2(min.x, min.y + h * 0.18f));
        draw_list->PathStroke(color, ImDrawFlags_Closed, 1.8f);
    }
    else if (strcmp(name, "monitor") == 0) {
        draw_list->AddRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y - h * 0.22f), color, 2.0f, 0, 1.5f);
        draw_list->AddLine(ImVec2(ctr.x, max.y - h * 0.22f), ImVec2(ctr.x, max.y), color, 2.0f);
        draw_list->AddLine(ImVec2(ctr.x - w * 0.25f, max.y), ImVec2(ctr.x + w * 0.25f, max.y), color, 1.8f);
    }
    else if (strcmp(name, "lock") == 0) {
        draw_list->AddRectFilled(ImVec2(min.x, ctr.y - h * 0.05f), ImVec2(max.x, max.y), color, 2.0f);
        draw_list->PathClear();
        draw_list->PathArcTo(ImVec2(ctr.x, ctr.y - h * 0.05f), w * 0.28f, -IM_PI, 0.0f, 8);
        draw_list->PathStroke(color, 0, 1.6f);
    }
    else if (strcmp(name, "star") == 0) {
        draw_list->PathClear();
        int points_cnt = 5;
        for (int i = 0; i < points_cnt * 2; i++) {
            float r = (i % 2 == 0) ? (w * 0.5f) : (w * 0.22f);
            float angle = (float)i * IM_PI / (float)points_cnt - IM_PI * 0.5f;
            draw_list->PathLineTo(ImVec2(ctr.x + cosf(angle) * r, ctr.y + sinf(angle) * r));
        }
        draw_list->PathFillConvex(color);
    }
    else if (strcmp(name, "user") == 0) {
        draw_list->AddCircleFilled(ImVec2(ctr.x, min.y + h * 0.28f), w * 0.24f, color);
        draw_list->PathClear();
        draw_list->PathArcTo(ImVec2(ctr.x, max.y + h * 0.1f), w * 0.45f, -IM_PI, 0.0f, 8);
        draw_list->PathStroke(color, 0, 1.8f);
    }
    else if (strcmp(name, "cross") == 0) {
        draw_list->AddLine(ImVec2(min.x, min.y), ImVec2(max.x, max.y), color, 2.2f);
        draw_list->AddLine(ImVec2(max.x, min.y), ImVec2(min.x, max.y), color, 2.2f);
    }
    else if (strcmp(name, "wrench") == 0) {
        draw_list->AddLine(ImVec2(min.x + 2.0f, max.y - 2.0f), ImVec2(ctr.x - 1.0f, ctr.y + 1.0f), color, 3.0f);
        draw_list->AddCircle(ImVec2(max.x - 3.0f, min.y + 3.0f), w * 0.28f, color, 12, 1.8f);
    }
    else if (strcmp(name, "check") == 0) {
        draw_list->AddLine(ImVec2(min.x, ctr.y), ImVec2(ctr.x - w * 0.1f, max.y - h * 0.12f), color, 2.2f);
        draw_list->AddLine(ImVec2(ctr.x - w * 0.1f, max.y - h * 0.12f), ImVec2(max.x, min.y + h * 0.08f), color, 2.2f);
    }
    else if (strcmp(name, "minus") == 0) {
        draw_list->AddLine(ImVec2(min.x, ctr.y), ImVec2(max.x, ctr.y), color, 2.0f);
    }
    else if (strcmp(name, "square") == 0) {
        draw_list->AddRect(min, max, color, 0.0f, 0, 1.8f);
    }
    else if (strcmp(name, "rocket") == 0) {
        draw_list->PathClear();
        draw_list->PathLineTo(ImVec2(ctr.x, min.y));
        draw_list->PathLineTo(ImVec2(max.x - w * 0.2f, ctr.y + h * 0.15f));
        draw_list->PathLineTo(ImVec2(ctr.x + w * 0.12f, ctr.y + h * 0.2f));
        draw_list->PathLineTo(ImVec2(ctr.x - w * 0.12f, ctr.y + h * 0.2f));
        draw_list->PathLineTo(ImVec2(min.x + w * 0.2f, ctr.y + h * 0.15f));
        draw_list->PathFillConvex(color);
        draw_list->AddLine(ImVec2(ctr.x - w * 0.1f, ctr.y + h * 0.28f), ImVec2(ctr.x - w * 0.18f, max.y), color, 1.8f);
        draw_list->AddLine(ImVec2(ctr.x + w * 0.1f, ctr.y + h * 0.28f), ImVec2(ctr.x + w * 0.18f, max.y), color, 1.8f);
    }
    else if (strcmp(name, "search") == 0) {
        float r = w * 0.32f;
        ImVec2 c = ImVec2(ctr.x - w * 0.08f, ctr.y - h * 0.08f);
        draw_list->AddCircle(c, r, color, 20, 1.8f);
        ImVec2 handleStart = ImVec2(c.x + r * 0.72f, c.y + r * 0.72f);
        draw_list->AddLine(handleStart, ImVec2(max.x, max.y), color, 2.0f);
    }
    else if (strcmp(name, "bell") == 0) {
        draw_list->PathClear();
        draw_list->PathArcTo(ImVec2(ctr.x, ctr.y - h * 0.06f), w * 0.32f, IM_PI, IM_PI * 2.0f, 14);
        draw_list->PathLineTo(ImVec2(max.x - w * 0.02f, max.y - h * 0.30f));
        draw_list->PathLineTo(ImVec2(min.x + w * 0.02f, max.y - h * 0.30f));
        draw_list->PathStroke(color, ImDrawFlags_Closed, 1.7f);
        draw_list->AddCircleFilled(ImVec2(ctr.x, max.y - h * 0.10f), w * 0.09f, color);
    }
    else if (strcmp(name, "chevron_right") == 0) {
        draw_list->PathClear();
        draw_list->PathLineTo(ImVec2(min.x + w * 0.30f, min.y));
        draw_list->PathLineTo(ImVec2(max.x - w * 0.16f, ctr.y));
        draw_list->PathLineTo(ImVec2(min.x + w * 0.30f, max.y));
        draw_list->PathStroke(color, 0, 2.0f);
    }
    else if (strcmp(name, "logo_mark") == 0) {
        draw_list->PathClear();
        draw_list->PathLineTo(ImVec2(min.x, ctr.y));
        draw_list->PathLineTo(ImVec2(ctr.x - w * 0.30f, ctr.y));
        draw_list->PathLineTo(ImVec2(ctr.x - w * 0.12f, min.y + h * 0.04f));
        draw_list->PathLineTo(ImVec2(ctr.x + w * 0.02f, max.y - h * 0.04f));
        draw_list->PathLineTo(ImVec2(ctr.x + w * 0.16f, ctr.y));
        draw_list->PathLineTo(ImVec2(max.x, ctr.y));
        draw_list->PathStroke(color, 0, 2.2f);
    }
    else if (strcmp(name, "shield_check") == 0) {
        // Современный, легко читаемый значок "Списка исключений" (Checklist / Whitelist)
        float line_y1 = min.y + h * 0.22f;
        float line_y2 = min.y + h * 0.50f;
        float line_y3 = min.y + h * 0.78f;

        // Отрисовка трех аккуратных текстовых линий списка
        draw_list->AddLine(ImVec2(min.x + w * 0.34f, line_y1), ImVec2(max.x, line_y1), color, 1.8f);
        draw_list->AddLine(ImVec2(min.x + w * 0.34f, line_y2), ImVec2(max.x - w * 0.15f, line_y2), color, 1.8f);
        draw_list->AddLine(ImVec2(min.x + w * 0.34f, line_y3), ImVec2(max.x - w * 0.05f, line_y3), color, 1.8f);

        // Отрисовка трех миниатюрных галочек слева от каждой строки
        auto DrawMiniCheck = [&](float cy) {
            ImVec2 p1 = ImVec2(min.x, cy - h * 0.02f);
            ImVec2 p2 = ImVec2(min.x + w * 0.10f, cy + h * 0.08f);
            ImVec2 p3 = ImVec2(min.x + w * 0.24f, cy - h * 0.08f);
            draw_list->AddLine(p1, p2, color, 1.8f);
            draw_list->AddLine(p2, p3, color, 1.8f);
            };

        DrawMiniCheck(line_y1);
        DrawMiniCheck(line_y2);
        DrawMiniCheck(line_y3);
    }
    else if (strcmp(name, "cpu") == 0) {
        draw_list->AddRect(ImVec2(min.x + w * 0.2f, min.y + h * 0.2f), ImVec2(max.x - w * 0.2f, max.y - h * 0.2f), color, 1.5f, 0, 1.5f);
        draw_list->AddRectFilled(ImVec2(min.x + w * 0.35f, min.y + h * 0.35f), ImVec2(max.x - w * 0.35f, max.y - h * 0.35f), color, 1.0f);
        for (int i = 0; i < 3; i++) {
            float offset = w * (0.3f + i * 0.2f);
            draw_list->AddLine(ImVec2(min.x + offset, min.y), ImVec2(min.x + offset, min.y + h * 0.18f), color, 1.2f);
            draw_list->AddLine(ImVec2(min.x + offset, max.y - h * 0.18f), ImVec2(min.x + offset, max.y), color, 1.2f);
            draw_list->AddLine(ImVec2(min.x, min.y + offset), ImVec2(min.x + w * 0.18f, min.y + offset), color, 1.2f);
            draw_list->AddLine(ImVec2(max.x - w * 0.18f, min.y + offset), ImVec2(max.x, min.y + offset), color, 1.2f);
        }
    }
    else if (strcmp(name, "gpu") == 0) {
        draw_list->AddRect(ImVec2(min.x, min.y + h * 0.15f), ImVec2(max.x, max.y - h * 0.15f), color, 1.5f, 0, 1.5f);
        draw_list->AddLine(ImVec2(min.x + w * 0.2f, max.y - h * 0.15f), ImVec2(min.x + w * 0.8f, max.y - h * 0.15f), color, 2.5f);
        draw_list->AddCircle(ctr, w * 0.22f, color, 12, 1.5f);
        for (int i = 0; i < 4; i++) {
            float angle = i * IM_PI * 0.5f;
            draw_list->AddLine(ctr, ImVec2(ctr.x + cosf(angle) * w * 0.2f, ctr.y + sinf(angle) * h * 0.2f), color, 1.2f);
        }
    }
    else if (strcmp(name, "ram") == 0) {
        draw_list->AddRect(ImVec2(min.x, min.y + h * 0.3f), ImVec2(max.x, max.y - h * 0.3f), color, 1.0f, 0, 1.5f);
        for (int i = 0; i < 4; i++) {
            float rx = min.x + w * 0.12f + i * (w * 0.21f);
            draw_list->AddRectFilled(ImVec2(rx, min.y + h * 0.38f), ImVec2(rx + w * 0.14f, max.y - h * 0.38f), color, 0.5f);
        }
        for (int i = 0; i < 8; i++) {
            float px = min.x + w * 0.1f + i * (w * 0.11f);
            draw_list->AddLine(ImVec2(px, max.y - h * 0.3f), ImVec2(px, max.y - h * 0.18f), color, 1.0f);
        }
    }
    else if (strcmp(name, "motherboard") == 0) {
        draw_list->AddRect(min, max, color, 2.0f, 0, 1.5f);
        draw_list->AddRectFilled(ImVec2(min.x + w * 0.18f, min.y + h * 0.18f), ImVec2(min.x + w * 0.48f, min.y + h * 0.48f), color, 1.0f);
        draw_list->AddRect(ImVec2(max.x - w * 0.32f, min.y + h * 0.15f), ImVec2(max.x - w * 0.22f, max.y - h * 0.15f), color, 0.5f, 0, 1.0f);
        draw_list->AddRect(ImVec2(max.x - w * 0.18f, min.y + h * 0.15f), ImVec2(max.x - w * 0.08f, max.y - h * 0.15f), color, 0.5f, 0, 1.0f);
        draw_list->AddRect(ImVec2(min.x + w * 0.15f, max.y - h * 0.32f), ImVec2(max.x - w * 0.4f, max.y - h * 0.22f), color, 0.5f, 0, 1.0f);
    }
    else if (strcmp(name, "opt_standard") == 0) {
        // Простая мишень/точка — символ сбалансированного, "дефолтного" режима
        draw_list->AddCircle(ctr, w * 0.42f, color, 24, 2.2f);
        draw_list->AddCircleFilled(ctr, w * 0.14f, color);
    }
    else if (strcmp(name, "opt_gaming") == 0) {
        // Жирный треугольный "форсаж" — читается чище, чем детализированная ракета на маленьком размере
        draw_list->PathClear();
        draw_list->PathLineTo(ImVec2(ctr.x, min.y));
        draw_list->PathLineTo(ImVec2(max.x - w * 0.12f, max.y - h * 0.15f));
        draw_list->PathLineTo(ImVec2(ctr.x, max.y - h * 0.32f));
        draw_list->PathLineTo(ImVec2(min.x + w * 0.12f, max.y - h * 0.15f));
        draw_list->PathFillConvex(color);
    }
    else if (strcmp(name, "opt_max") == 0) {
        // Жирная закрашенная молния — максимальная мощность
        draw_list->PathClear();
        draw_list->PathLineTo(ImVec2(ctr.x + w * 0.12f, min.y));
        draw_list->PathLineTo(ImVec2(min.x + w * 0.15f, ctr.y + h * 0.05f));
        draw_list->PathLineTo(ImVec2(ctr.x - w * 0.02f, ctr.y + h * 0.05f));
        draw_list->PathLineTo(ImVec2(ctr.x - w * 0.12f, max.y));
        draw_list->PathLineTo(ImVec2(max.x - w * 0.15f, ctr.y - h * 0.05f));
        draw_list->PathLineTo(ImVec2(ctr.x + w * 0.02f, ctr.y - h * 0.05f));
        draw_list->PathFillConvex(color);
    }
}

void RenderAppBackground() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 sz = ImGui::GetIO().DisplaySize;

    dl->AddRectFilled(ImVec2(0, 0), sz, ImColor(Palette::BaseBg));

    float t = (float)ImGui::GetTime();
    float cpuLoad = AppState::currentCPUUsagePercent.load() / 100.0f;
    float ramLoad = AppState::currentRAMUsagePercent.load() / 100.0f;
    float rawLoad = (std::max)(cpuLoad, ramLoad);
    float smoothLoad = AnimTo("bg_load_smooth", rawLoad, 1.6f);

    ImVec4 loadColor = LerpColor(g_ThemeAccent, Palette::Danger, (std::min)(1.0f, smoothLoad * 1.15f));

    float driftX = sinf(t * 0.12f) * 40.0f;
    float driftY = cosf(t * 0.09f) * 24.0f;

    ImVec2 glowTR = ImVec2(sz.x * 0.86f + driftX, sz.y * 0.05f + driftY);
    ImVec2 glowBL = ImVec2(sz.x * 0.10f - driftX * 0.6f, sz.y * 0.92f - driftY * 0.6f);

    float intensity = 0.020f + smoothLoad * 0.018f;
    for (int i = 5; i >= 1; i--) {
        float ft = (float)i / 5.0f;
        dl->AddCircleFilled(glowTR, sz.y * 0.42f * ft, ImColor(loadColor.x, loadColor.y, loadColor.z, intensity * (1.0f - ft) * (1.0f - ft)), 48);
        dl->AddCircleFilled(glowBL, sz.y * 0.34f * ft, ImColor(Palette::Cyan.x, Palette::Cyan.y, Palette::Cyan.z, 0.014f * (1.0f - ft) * (1.0f - ft)), 48);
    }
}

void DrawProcessIcon(const std::wstring& processName, unsigned long pid, ImVec2 pos, float size) {
    ID3D11ShaderResourceView* srv = GetProcessIconSRV(processName, pid);
    if (srv) {
        ImGui::SetCursorScreenPos(pos);
        ImGui::Image((void*)srv, ImVec2(size, size));
    }
    else {
        bool isSys = IsCriticalProcess(processName);
        ImU32 iconCol = isSys ? IM_COL32(100, 220, 140, 220) : IM_COL32(139, 155, 240, 220);
        DrawVectorIconDirect(isSys ? "shield" : "monitor", pos, size, iconCol);
    }
}

// ============================================================================
//  БАЗОВЫЕ UI-КОМПОНЕНТЫ
// ============================================================================
// Направленная "мягкая" тень (как Material elevation): растёт в основном вниз, чуть в
// стороны и почти не растёт вверх. Раньше тень была симметричным контуром-кольцом (AddRect
// с равным отступом на все 4 стороны) — при спреде 14px она дотягивалась до соседней
// карточки, стоящей в 12-24px по горизонтали, и накладывалась на неё уже нарисованным
// пикселем, что выглядело как "чёрный квадрат" на стыке. Направленная заливка тратит спред
// в основном туда, где рядом обычно нет соседних карточек (вниз), и не рисует тень сверху.
static void DrawSoftShadow(ImDrawList* draw_list, ImVec2 min, ImVec2 max, float rounding, float spread, ImU32 color_base) {
    const int steps = 5;
    ImU32 baseAlpha = (color_base >> 24) & 0xFF;
    for (int i = steps; i >= 1; i--) {
        float t = (float)i / (float)steps;
        float grow = spread * t;
        float alphaF = baseAlpha * (1.0f - t) * (1.0f - t);
        ImU32 shadow_color = (color_base & 0x00FFFFFF) | ((ImU32)alphaF << 24);
        ImVec2 lo = ImVec2(min.x - grow * 0.25f, min.y - grow * 0.06f);
        ImVec2 hi = ImVec2(max.x + grow * 0.25f, max.y + grow * 0.85f);
        draw_list->AddRectFilled(lo, hi, shadow_color, rounding + grow * 0.2f);
    }
}

bool DrawRoundIconButton(const char* id, const char* icon_name, ImVec2 pos, float size, ImU32 bg_color, ImU32 hover_color, ImU32 icon_color) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushID(id);
    bool clicked = ImGui::InvisibleButton("##btn", ImVec2(size, size));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    float hoverT = AnimToKey(ImGui::GetID("##hover"), hovered ? 1.0f : 0.0f, 16.0f);

    ImVec4 bgA = ImGui::ColorConvertU32ToFloat4(bg_color);
    ImVec4 bgB = ImGui::ColorConvertU32ToFloat4(hover_color);
    ImU32 bg = ImColor(LerpColor(bgA, bgB, hoverT));

    ImVec2 c = ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f);
    float r = size * 0.5f * (1.0f + hoverT * 0.08f - (active ? 0.06f : 0.0f));

    if (hoverT > 0.02f) {
        dl->AddCircleFilled(c, r + 4.0f, (hover_color & 0x00FFFFFF) | ((ImU32)(hoverT * 55.0f) << 24), 24);
    }
    dl->AddCircleFilled(c, r, bg, 24);
    float iconSize = size * 0.7f * (1.0f + hoverT * 0.06f);
    DrawVectorIconDirect(icon_name, ImVec2(c.x - iconSize * 0.5f, c.y - iconSize * 0.5f), iconSize, icon_color);

    ImGui::PopID();
    return clicked;
}

bool DrawActionButton(const char* label, const char* desc, const char* icon_name, float width, ImU32 color) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    float h = 36.0f; // Adjusted from 34.0f to 36.0f to completely prevent vertical clipping of text descenders
    bool clicked = ImGui::InvisibleButton("##act_row", ImVec2(width, h));
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    float hoverT = AnimTo(label, hovered ? 1.0f : 0.0f, 14.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (hoverT > 0.01f) {
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), ImColor(1.0f, 1.0f, 1.0f, 0.045f * hoverT), Radius::Button);
    }

    float badge = 24.0f; // Компактный аккуратный бейдж под иконку
    ImVec2 badgePos = ImVec2(pos.x + 4.0f, pos.y + (h - badge) * 0.5f);
    ImVec4 colF = ImGui::ColorConvertU32ToFloat4(color);
    dl->AddRectFilled(badgePos, ImVec2(badgePos.x + badge, badgePos.y + badge), ImColor(colF.x, colF.y, colF.z, 0.16f + hoverT * 0.08f), 6.0f);
    DrawVectorIconDirect(icon_name, ImVec2(badgePos.x + badge * 0.22f, badgePos.y + badge * 0.22f), badge * 0.56f, color);

    float textX = pos.x + badge + 14.0f;
    ImGui::PushFont(g_FontBody);
    dl->AddText(ImVec2(textX, pos.y + 2.0f), IM_COL32(245, 246, 250, 255), label); // Adjusted from 1.0f to 2.0f for centering
    ImGui::PopFont();
    ImGui::PushFont(g_FontSmall);
    dl->AddText(ImVec2(textX, pos.y + 19.0f), ImColor(Palette::TextMuted), desc); // Adjusted from 18.0f to 19.0f
    ImGui::PopFont();

    float chevAlpha = 0.30f + hoverT * 0.55f;
    float chevShift = hoverT * 3.0f;
    DrawVectorIconDirect("chevron_right", ImVec2(pos.x + width - 20.0f + chevShift, pos.y + (h - 12.0f) * 0.5f), 12.0f, ImColor(1.0f, 1.0f, 1.0f, chevAlpha));

    return clicked;
}

void BeginFluentCard(const char* id, const ImVec2& size, const char* title, const char* subtitle) {
    // Тень рисуем НА РОДИТЕЛЬСКОМ draw list, ДО BeginChild. Раньше она рисовалась изнутри
    // дочернего окна карточки — ImGui обрезает содержимое child-окна точно по его границе
    // (clip rect), поэтому мягкое затухание тени, которое должно выходить за край карточки,
    // резалось вертикальной/горизонтальной линией и выглядело как "чёрный квадрат"/обрубок
    // на краю. На родительском холсте такой обрезки нет — тень затухает плавно.
    ImVec2 pos_min = ImGui::GetCursorScreenPos();
    ImVec2 cardSize = size;
    if (cardSize.x <= 0.0f) cardSize.x = ImGui::GetContentRegionAvail().x;
    if (cardSize.y <= 0.0f) cardSize.y = ImGui::GetContentRegionAvail().y;
    ImVec2 pos_max = ImVec2(pos_min.x + cardSize.x, pos_min.y + cardSize.y);
    DrawSoftShadow(ImGui::GetWindowDrawList(), pos_min, pos_max, Radius::Card, 16.0f, IM_COL32(0, 0, 0, 110));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::CardBg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Radius::Card);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 13)); // Changed from 18 to 13 to give everything vertical breathing room
    ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    // Лёгкий вертикальный градиент внутри карточки (светлее сверху) поверх плоской заливки —
    // имитация мягкого источника света сверху. AddRectFilledMultiColor не умеет скруглять
    // углы, поэтому раньше квадратный градиент перекрывал скруглённые углы окна (баг с
    // "пропавшим" скруглением) — теперь градиент отступает от углов на радиус карточки,
    // и сами углы остаются нетронутой (скруглённой) заливкой ChildBg.
    float gradH = (std::min)(ImGui::GetWindowHeight(), 90.0f);
    draw_list->AddRectFilledMultiColor(ImVec2(pos_min.x + Radius::Card, pos_min.y), ImVec2(pos_max.x - Radius::Card, pos_min.y + gradH),
        ImColor(Palette::CardBgTop), ImColor(Palette::CardBgTop), ImColor(Palette::CardBg), ImColor(Palette::CardBg));
    draw_list->AddRect(pos_min, pos_max, IM_COL32(255, 255, 255, 14), Radius::Card, 0, 1.0f);
    // Тонкий блик сверху — имитация мягкого верхнего света на "стеклянной" карточке
    draw_list->AddLine(ImVec2(pos_min.x + Radius::Card * 0.4f, pos_min.y + 1.0f),
        ImVec2(pos_max.x - Radius::Card * 0.4f, pos_min.y + 1.0f), IM_COL32(255, 255, 255, 32), 1.0f);

    if (title) {
        ImGui::PushFont(g_FontCardTitle);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.97f), "%s", title);
        ImGui::PopFont();
    }
    if (subtitle) {
        ImGui::PushFont(g_FontSmall);
        ImGui::TextColored(Palette::TextMuted, "%s", subtitle);
        ImGui::PopFont();
    }
    if (title || subtitle) {
        ImGui::Dummy(ImVec2(0, subtitle ? 6.0f : 2.0f));
    }
}

void EndFluentCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void DrawEmptyState(const char* icon_name, const char* title, const char* subtitle) {
    float cw = ImGui::GetContentRegionAvail().x;
    float ch = ImGui::GetContentRegionAvail().y;
    ImVec2 base = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float boxSize = 64.0f;
    ImVec2 boxMin = ImVec2(base.x + (cw - boxSize) * 0.5f, base.y + ch * 0.5f - 70.0f);
    ImVec2 boxMax = ImVec2(boxMin.x + boxSize, boxMin.y + boxSize);

    // Цветной скруглённый контейнер вместо голой мелкой иконки — сразу читается как
    // осознанный пустой экран, а не "тут забыли что-то нарисовать".
    dl->AddRectFilled(boxMin, boxMax, ImColor(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.14f), 18.0f);
    dl->AddRect(boxMin, boxMax, ImColor(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.35f), 18.0f, 0, 1.2f);
    float pulse = (sinf((float)ImGui::GetTime() * 1.4f) * 0.5f + 0.5f);
    dl->AddCircleFilled(ImVec2((boxMin.x + boxMax.x) * 0.5f, (boxMin.y + boxMax.y) * 0.5f), boxSize * 0.62f,
        ImColor(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.05f + pulse * 0.04f), 32);

    float iconSize = 26.0f;
    DrawVectorIconDirect(icon_name, ImVec2((boxMin.x + boxMax.x) * 0.5f - iconSize * 0.5f, (boxMin.y + boxMax.y) * 0.5f - iconSize * 0.5f), iconSize, ImColor(g_ThemeAccent));

    ImGui::PushFont(g_FontCardTitle);
    ImVec2 t1 = ImGui::CalcTextSize(title);
    ImGui::SetCursorScreenPos(ImVec2(base.x + (cw - t1.x) * 0.5f, boxMax.y + 16.0f));
    ImGui::TextColored(Palette::TextMain, "%s", title);
    ImGui::PopFont();

    ImGui::PushFont(g_FontSmall);
    ImVec2 t2 = ImGui::CalcTextSize(subtitle);
    ImGui::SetCursorScreenPos(ImVec2(base.x + (cw - t2.x) * 0.5f, boxMax.y + 40.0f));
    ImGui::TextColored(Palette::TextMuted, "%s", subtitle);
    ImGui::PopFont();
}

// Заливка под наклонным отрезком графика. AddRectFilledMultiColor рисует ТОЛЬКО
// оси-выровненный прямоугольник — если передать ей точки с разной Y (наклонный сегмент
// линии), она молча берёт Y первой точки как плоский "верх" всего прямоугольника вместо
// повторения наклона. На резких перепадах соседние сегменты с разными "плоскими крышками"
// не совпадают друг с другом и с самой линией сверху — это и давало кривые чёрные
// полосы/пилу под графиком. Здесь вместо этого рисуется настоящая трапеция по 4 точкам
// через низкоуровневые примитивы (как это делает сам ImGui внутри AddRectFilledMultiColor).
static void AddGradientTrapezoid(ImDrawList* dl, ImVec2 topLeft, ImVec2 topRight, ImVec2 botRight, ImVec2 botLeft,
    ImU32 colTopLeft, ImU32 colTopRight, ImU32 colBotRight, ImU32 colBotLeft) {
    ImVec2 uv = ImGui::GetIO().Fonts->TexUvWhitePixel;
    dl->PrimReserve(6, 4);
    ImDrawIdx idx = (ImDrawIdx)dl->_VtxCurrentIdx;
    dl->PrimWriteIdx(idx); dl->PrimWriteIdx(idx + 1); dl->PrimWriteIdx(idx + 2);
    dl->PrimWriteIdx(idx); dl->PrimWriteIdx(idx + 2); dl->PrimWriteIdx(idx + 3);
    dl->PrimWriteVtx(topLeft, uv, colTopLeft);
    dl->PrimWriteVtx(topRight, uv, colTopRight);
    dl->PrimWriteVtx(botRight, uv, colBotRight);
    dl->PrimWriteVtx(botLeft, uv, colBotLeft);
}

static void DrawSparkline(ImDrawList* draw_list, ImVec2 start_pos, ImVec2 size, const float* values, int count, ImU32 color) {
    if (count < 2) return;

    ImVector<ImVec2> points;
    points.reserve(count);

    float x_step = size.x / (float)(count - 1);
    float max_val = 100.0f;
    float min_val = 0.0f;

    // Раньше на плоских участках сюда подмешивалась синтетическая синусоида "для живости" —
    // это буквально рисование значения, которого не было в реальных данных. Показываем
    // только то, что реально измерено.
    for (int i = 0; i < count; i++) {
        float val = values[i];
        float x = start_pos.x + i * x_step;

        float finalVal = (std::max)(min_val, (std::min)(max_val, val));
        float y = start_pos.y + size.y - ((finalVal - min_val) / (max_val - min_val)) * size.y;

        y = (std::max)(start_pos.y + 2.0f, (std::min)(start_pos.y + size.y - 2.0f, y));

        points.push_back(ImVec2(x, y));
    }

    ImU32 topCol = (color & 0x00FFFFFF) | 0x22000000;
    ImU32 botCol = (color & 0x00FFFFFF) | 0x00000000;
    float bottomY = start_pos.y + size.y;
    for (int i = 0; i < count - 1; i++) {
        AddGradientTrapezoid(draw_list,
            points[i], points[i + 1], ImVec2(points[i + 1].x, bottomY), ImVec2(points[i].x, bottomY),
            topCol, topCol, botCol, botCol);
    }

    for (int i = 0; i < count - 1; i++) {
        draw_list->AddLine(points[i], points[i + 1], color, 1.8f);
    }
}

void DrawMetricRow(const char* label, float value, ImU32 color, const float* history, int count) {
    ImVec2 rowPos = ImGui::GetCursorScreenPos();
    float rowH = 20.0f;
    float availW = ImGui::GetContentRegionAvail().x;
    const float sparkLeftGap = 64.0f;
    const float sparkRightGap = 8.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::PushFont(g_FontSmall);
    dl->AddText(ImVec2(rowPos.x, rowPos.y - 2.0f), ImColor(Palette::TextMuted), label);
    ImGui::PopFont();

    char valBuf[16];
    sprintf_s(valBuf, "%d%%", (int)value);
    ImGui::PushFont(g_FontCardTitle);
    dl->AddText(ImVec2(rowPos.x, rowPos.y + 10.0f), IM_COL32(255, 255, 255, 255), valBuf);
    ImGui::PopFont();

    ImVec2 sparkPos = ImVec2(rowPos.x + sparkLeftGap, rowPos.y + 2.0f);
    ImVec2 sparkSize = ImVec2((std::max)(20.0f, availW - sparkLeftGap - sparkRightGap), rowH + 6.0f);
    DrawSparkline(dl, sparkPos, sparkSize, history, count, color);

    ImGui::SetCursorScreenPos(ImVec2(rowPos.x, rowPos.y + rowH + 11.0f));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void DrawMultiSeriesChart(ImVec2 frame_pos, ImVec2 frame_size, const float* cpu, const float* ram, const float* disk, const float* net, int sampleCount) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(frame_pos, ImVec2(frame_pos.x + frame_size.x, frame_pos.y + frame_size.y), ImColor(1.0f, 1.0f, 1.0f, 0.015f), 12.0f);
    dl->AddRect(frame_pos, ImVec2(frame_pos.x + frame_size.x, frame_pos.y + frame_size.y), IM_COL32(255, 255, 255, 8), 12.0f, 0, 1.0f);

    // Подписи % — прижаты внутрь рамки (раньше верхняя "100%" центрировалась на самой
    // линии сетки и уходила на 6px ВЫШЕ верхнего края рамки, за границу карточки).
    ImGui::PushFont(g_FontSmall);
    for (int i = 0; i <= 4; i++) {
        float t = (float)i / 4.0f;
        float y = frame_pos.y + frame_size.y * t;
        if (i > 0 && i < 4) {
            dl->AddLine(ImVec2(frame_pos.x + 36.0f, y), ImVec2(frame_pos.x + frame_size.x - 2.0f, y), IM_COL32(255, 255, 255, 5), 1.0f);
        }
        char buf[8];
        sprintf_s(buf, "%d%%", (int)((1.0f - t) * 100.0f));
        float textY = (i == 0) ? y : (i == 4 ? y - 13.0f : y - 6.0f);
        dl->AddText(ImVec2(frame_pos.x + 6.0f, textY), IM_COL32(255, 255, 255, 80), buf);
    }
    ImGui::PopFont();

    if (sampleCount < 2) return;

    // Раньше справа резервировалось ещё 6px "про запас" — линия останавливалась заметно
    // раньше правого края рамки, и там оставался пустой воздух перед подписью "Сейчас".
    // Теперь график доходит почти до самого края (2px на сглаживание/бордер), как и слева.
    float plotX = frame_pos.x + 36.0f;
    float plotW = frame_size.x - 36.0f - 2.0f;
    float xstep = sampleCount > 1 ? plotW / (float)(sampleCount - 1) : 0.0f;

    const float* seriesArr[4] = { cpu, ram, disk, net };
    ImU32 colorsArr[4] = {
        IM_COL32(139, 92, 246, 255),  // CPU
        IM_COL32(59, 130, 246, 255),  // RAM
        IM_COL32(245, 158, 11, 255),  // Disk
        IM_COL32(34, 197, 94, 255)    // Net
    };

    // Жёсткая обрезка по рамке графика — подстраховка от любого 1-2px выхода линии/заливки
    // за границу рамки (толщина обводки, погрешности округления и т.п.).
    dl->PushClipRect(frame_pos, ImVec2(frame_pos.x + frame_size.x, frame_pos.y + frame_size.y), true);

    // Раньше здесь был Catmull-Rom сплайн + синтетический "оживляющий" шум, добавляемый
    // поверх реальных значений на плоских участках. У сплайна есть известное свойство —
    // overshoot: интерполированная кривая может выйти ЗА диапазон соседних точек данных
    // (нырнуть ниже минимума/выше максимума пары точек), а синтетический шум это только
    // усугублял. Итог — график мог "провалиться" ниже 0% или показать горб, которого не
    // было в реальных данных ("стал криво считать"). Здесь строго прямые отрезки между
    // реальными измеренными точками — не может показать значение, которого не было
    // в данных, и физически не может выйти за диапазон между соседними точками.
    for (int s = 0; s < 4; s++) {
        const float* arr = seriesArr[s];
        ImU32 color = colorsArr[s];
        ImU32 color_top = (color & 0x00FFFFFF) | 0x16000000;
        ImU32 color_bottom = (color & 0x00FFFFFF) | 0x00000000;
        float bottomY = frame_pos.y + frame_size.y - 1.0f;

        std::vector<ImVec2> pts;
        pts.reserve(sampleCount);
        for (int i = 0; i < sampleCount; i++) {
            float v = (std::max)(0.0f, (std::min)(100.0f, arr[i]));
            float px = plotX + i * xstep;
            float py = frame_pos.y + frame_size.y - (v / 100.0f) * frame_size.y;
            pts.push_back(ImVec2(px, py));
        }

        for (size_t i = 0; i + 1 < pts.size(); i++) {
            AddGradientTrapezoid(dl,
                pts[i], pts[i + 1], ImVec2(pts[i + 1].x, bottomY), ImVec2(pts[i].x, bottomY),
                color_top, color_top, color_bottom, color_bottom);
        }
        for (size_t i = 0; i + 1 < pts.size(); i++) {
            dl->AddLine(pts[i], pts[i + 1], color, 2.0f);
        }
    }

    dl->PopClipRect();
}

void DrawCircularHardwareGauge(ImDrawList* draw_list, ImVec2 center, float radius, float progress,
    ImU32 color_bg, const char* title, const char* value) {
    (void)value;
    progress = AnimTo("donut_main_val", progress, 5.0f);
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    const float start_angle = -IM_PI - (IM_PI * 0.20f);
    const float total_span = IM_PI * 1.40f;
    const float end_angle = start_angle + (progress * total_span);

    ImVec4 load_color = g_ThemeAccent;
    const char* status_text = "Отлично";
    if (progress < 0.45f) {
        status_text = "Отлично";
    }
    else if (progress < 0.75f) {
        status_text = "Оптимально";
    }
    else {
        load_color = Palette::Warning;
        status_text = "Нагрузка";
    }

    float pulse = (sinf((float)ImGui::GetTime() * 1.6f) * 0.5f) + 0.5f;

    draw_list->AddCircleFilled(center, radius - 10.0f, ImColor(load_color.x, load_color.y, load_color.z, 0.025f + pulse * 0.015f), 40);

    draw_list->PathClear();
    draw_list->PathArcTo(center, radius - 4.0f, start_angle, start_angle + total_span, 64);
    draw_list->PathStroke(color_bg, 0, 3.5f);

    if (progress > 0.005f) {
        const int arcSegs = 40;

        draw_list->PathClear();
        draw_list->PathArcTo(center, radius - 4.0f, start_angle, end_angle, arcSegs);
        draw_list->PathStroke(ImColor(load_color.x, load_color.y, load_color.z, 0.25f), 0, 7.5f);

        draw_list->PathClear();
        draw_list->PathArcTo(center, radius - 4.0f, start_angle, end_angle, arcSegs);
        draw_list->PathStroke(ImColor(load_color), 0, 3.5f);
    }

    if (progress > 0.002f) {
        ImVec2 tip_pos = ImVec2(center.x + cosf(end_angle) * (radius - 4.0f), center.y + sinf(end_angle) * (radius - 4.0f));
        draw_list->AddCircleFilled(tip_pos, 4.5f, ImColor(load_color.x, load_color.y, load_color.z, 0.6f + pulse * 0.4f));
        draw_list->AddCircleFilled(tip_pos, 2.0f, IM_COL32(255, 255, 255, 255));
    }

    char pct_buf[32];
    sprintf_s(pct_buf, "%d%%", (int)(progress * 100.0f));

    ImGui::PushFont(g_FontTitle);
    ImVec2 pct_size = ImGui::CalcTextSize(pct_buf);
    draw_list->AddText(ImVec2(center.x - pct_size.x / 2.0f, center.y - pct_size.y / 2.0f - 8.0f), IM_COL32(255, 255, 255, 255), pct_buf);
    ImGui::PopFont();

    // Статус как цветной чип-бейдж вместо голого цветного текста — держит внимание
    // и явно читается как "состояние", а не просто подпись под цифрой.
    ImGui::PushFont(g_FontBody);
    ImVec2 status_size = ImGui::CalcTextSize(status_text);
    ImVec2 chipPad = ImVec2(12.0f, 5.0f);
    ImVec2 chipMin = ImVec2(center.x - status_size.x / 2.0f - chipPad.x, center.y + radius * 0.28f - chipPad.y);
    ImVec2 chipMax = ImVec2(center.x + status_size.x / 2.0f + chipPad.x, center.y + radius * 0.28f + status_size.y + chipPad.y);
    draw_list->AddRectFilled(chipMin, chipMax, ImColor(load_color.x, load_color.y, load_color.z, 0.16f), (chipMax.y - chipMin.y) * 0.5f);
    draw_list->AddRect(chipMin, chipMax, ImColor(load_color.x, load_color.y, load_color.z, 0.45f), (chipMax.y - chipMin.y) * 0.5f, 0, 1.0f);
    draw_list->AddText(ImVec2(center.x - status_size.x / 2.0f, center.y + radius * 0.28f), ImColor(load_color), status_text);
    ImGui::PopFont();

    if (title && title[0]) {
        ImGui::PushFont(g_FontSmall);
        ImVec2 label_size = ImGui::CalcTextSize(title);
        draw_list->AddText(ImVec2(center.x - label_size.x / 2.0f, center.y - radius * 0.55f), ImColor(Palette::TextMuted), title);
        ImGui::PopFont();
    }
}
std::string TruncateTextToWidth(const std::string& text, float maxWidth) {
    if (maxWidth <= 0.0f || ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
    std::string result = text;
    while (!result.empty() && ImGui::CalcTextSize((result + "...").c_str()).x > maxWidth) {
        result.pop_back();
    }
    return result.empty() ? result : (result + "...");
}

