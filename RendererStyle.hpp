#pragma once
#include "imgui.h"
#include "Optimizer.hpp"
#include <d3d11.h>
#include <string>

// Общие стили/цвета/константы приложения — используются всеми Render*Tab модулями,
// чтобы не дублировать палитру и низкоуровневые примитивы отрисовки в каждом файле.

namespace Palette {
    extern const ImVec4 BaseBg;
    extern const ImVec4 SurfaceBg;
    extern const ImVec4 CardBg;
    extern const ImVec4 CardBgSoft;
    extern const ImVec4 CardBgTop;
    extern const ImVec4 Accent;
    extern const ImVec4 AccentHover;
    extern const ImVec4 Cyan;
    extern const ImVec4 Violet;
    extern const ImVec4 TextMain;
    extern const ImVec4 TextMuted;
    extern const ImVec4 Success;
    extern const ImVec4 Warning;
    extern const ImVec4 Danger;
    extern const ImVec4 Info;
    extern const ImU32  Bg;
}

namespace Radius {
    constexpr float Card = 16.0f;
    constexpr float Button = 12.0f;
    constexpr float Input = 10.0f;
}

// Индекс активной вкладки (0 Обзор, 1 Процессы, 2 Исключения, 3 История) — общее состояние
// навигации, читается/пишется из Renderer.cpp (сайдбар) и из RendererDashboard.cpp (переходы
// по ссылкам "Показать все процессы", "Управление списком").
extern int activeTab;

// Акцентный цвет интерфейса, зависящий от выбранного профиля оптимизации (см. UpdateThemeAccent).
extern ImVec4 g_ThemeAccent;
extern ImVec4 g_ThemeAccentHover;

float AnimToKey(ImGuiID key, float target, float speed = 12.0f);
ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t);

// Тёмный или белый текст в зависимости от яркости фоновой заливки — чтобы подписи
// на акцентных кнопках оставались читаемыми при любой теме профиля (напр. светло-
// зелёный "Стандартный" требует тёмного текста, тёмно-фиолетовый "Игровой" — белого).
ImVec4 ContrastTextFor(const ImVec4& bg);

void UpdateThemeAccent();
ProcessCleanupLevel GetSelectedCleanupLevel();
float GetHeartbeatPulse();

void DrawVectorIconDirect(const char* name, ImVec2 pos, float size, ImU32 color);
void RenderAppBackground();
void DrawProcessIcon(const std::wstring& processName, unsigned long pid, ImVec2 pos, float size);
bool DrawRoundIconButton(const char* id, const char* icon_name, ImVec2 pos, float size, ImU32 bg_color, ImU32 hover_color, ImU32 icon_color);
bool DrawActionButton(const char* label, const char* desc, const char* icon_name, float width, ImU32 color);
void BeginFluentCard(const char* id, const ImVec2& size, const char* title = nullptr, const char* subtitle = nullptr);
void EndFluentCard();
// Единое пустое состояние (иконка в цветном скруглённом контейнере + заголовок + подпись),
// вместо голой мелкой иконки с двумя строками серого текста в центре карточки.
void DrawEmptyState(const char* icon_name, const char* title, const char* subtitle);
void DrawMetricRow(const char* label, float value, ImU32 color, const float* history, int count);
void DrawMultiSeriesChart(ImVec2 frame_pos, ImVec2 frame_size, const float* cpu, const float* ram, const float* disk, const float* net, int sampleCount);
void DrawCircularHardwareGauge(ImDrawList* draw_list, ImVec2 center, float radius, float progress, ImU32 color_bg, const char* title, const char* value);
std::string TruncateTextToWidth(const std::string& text, float maxWidth);
