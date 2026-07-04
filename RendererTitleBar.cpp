#include "RendererTitleBar.hpp"
#include "RendererStyle.hpp"
#include "Renderer.hpp"
#include "AppState.hpp"
#include "Optimizer.hpp"
#include "imgui.h"
#include <windows.h>
#include <string>
#include <algorithm>

extern ImFont* g_FontSmall;

// ============================================================================
//  ВЕРХНЯЯ ПОЛОСА ПЕРЕТАСКИВАНИЯ ОКНА (безрамочное окно)
// ============================================================================
static RECT g_TitleBarNoDragRects[8];
static int g_TitleBarNoDragRectCount = 0;

bool IsPointOnTitleBarWidget(int clientX, int clientY) {
    POINT pt{ clientX, clientY };
    for (int i = 0; i < g_TitleBarNoDragRectCount; i++) {
        if (PtInRect(&g_TitleBarNoDragRects[i], pt)) return true;
    }
    return false;
}

void RenderTitleBarStrip() {
    g_TitleBarNoDragRectCount = 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;

    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 40.0f), ImColor(Palette::BaseBg));

    ImVec2 logo_pos = ImVec2(pos.x + 16.0f, pos.y + 12.0f);
    DrawVectorIconDirect("logo_mark", logo_pos, 16.0f, ImColor(g_ThemeAccent));
    ImGui::PushFont(g_FontSmall);
    dl->AddText(ImVec2(pos.x + 40.0f, pos.y + 12.0f), IM_COL32(150, 155, 170, 255), "Pulse");
    ImGui::PopFont();

    // Профили оптимизации — по центру заголовка окна. Титлбар рисуется первым и целиком
    // помещается в кадр на любой машине, в отличие от низа сайдбара, где раньше был этот блок
    // и где расхождение реального размера окна с холстом ImGui обрезало плитки по краю.
    {
        const auto& profiles = GetOptimizationProfiles();
        static const char* profIcons[3] = { "opt_standard", "opt_gaming", "opt_max" };
        const int count = (std::min)((int)profiles.size(), 3);

        const float pillW = 30.0f;
        const float pillH = 26.0f;
        const float pillGap = 6.0f;
        const float rowW = pillW * count + pillGap * (count - 1);
        float rowX = pos.x + (width - rowW) * 0.5f;
        float rowY = pos.y + (40.0f - pillH) * 0.5f;

        // Групповая подложка под все три плитки разом — раньше они "висели" тремя
        // отдельными кружками в пустом пространстве титлбара без общего визуального якоря.
        const float groupPad = 5.0f;
        ImVec2 groupMin = ImVec2(rowX - groupPad, rowY - groupPad);
        ImVec2 groupMax = ImVec2(rowX + rowW + groupPad, rowY + pillH + groupPad);
        dl->AddRectFilled(groupMin, groupMax, IM_COL32(255, 255, 255, 7), (pillH + groupPad * 2.0f) * 0.5f);
        dl->AddRect(groupMin, groupMax, IM_COL32(255, 255, 255, 12), (pillH + groupPad * 2.0f) * 0.5f, 0, 1.0f);

        int selected = AppState::selectedProfileIndex.load();
        for (int i = 0; i < count; i++) {
            const OptimizationProfile& prof = profiles[i];
            bool isSelected = (selected == i);
            ImVec2 p = ImVec2(rowX + i * (pillW + pillGap), rowY);

            ImGui::SetCursorScreenPos(p);
            ImGui::PushID(300 + i);
            bool clicked = ImGui::InvisibleButton("##titleProfile", ImVec2(pillW, pillH));
            bool hovered = ImGui::IsItemHovered();
            if (g_TitleBarNoDragRectCount < 8) {
                RECT r{ (LONG)p.x, (LONG)p.y, (LONG)(p.x + pillW), (LONG)(p.y + pillH) };
                g_TitleBarNoDragRects[g_TitleBarNoDragRectCount++] = r;
            }
            ImGui::PopID();
            if (clicked) {
                AppState::selectedProfileIndex = i;
                AppState::SaveSettings();
            }
            if (hovered) {
                ImGui::SetTooltip("%s\n%s", prof.name.c_str(), prof.description.c_str());
            }

            float pressT = AnimToKey(ImGui::GetID((std::string("title_prof_") + std::to_string(i)).c_str()), isSelected ? 1.0f : 0.0f, 16.0f);

            ImVec4 bgOff = hovered ? ImVec4(1.0f, 1.0f, 1.0f, 0.10f) : ImVec4(1.0f, 1.0f, 1.0f, 0.05f);
            ImVec4 bgOn = ImVec4(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.90f);
            ImU32 bg = (ImU32)ImColor(LerpColor(bgOff, bgOn, pressT));
            dl->AddRectFilled(p, ImVec2(p.x + pillW, p.y + pillH), bg, 8.0f);
            if (pressT < 0.5f) {
                dl->AddRect(p, ImVec2(p.x + pillW, p.y + pillH), IM_COL32(255, 255, 255, 12), 8.0f, 0, 1.0f);
            }

            // Иконка выбранной пилюли контрастна к акценту (тёмная на светло-зелёном,
            // белая на тёмно-фиолетовом), а невыбранной — приглушённая.
            ImU32 fgCol = (ImU32)ImColor(LerpColor(Palette::TextMuted, ContrastTextFor(bgOn), pressT));
            float iconSize = 14.0f;
            ImVec2 iconPos = ImVec2(p.x + (pillW - iconSize) * 0.5f, p.y + (pillH - iconSize) * 0.5f);
            DrawVectorIconDirect(profIcons[i], iconPos, iconSize, fgCol);
        }
    }

    ImVec2 control_pos = ImVec2(pos.x + width - 62.0f, pos.y + 9.0f);
    HWND hwnd = GetActiveWindow();

    // Кнопки свернуть/закрыть занимают правый угол титлбара — эта зона и так исключена
    // проверкой "x < width - 100" в WM_LBUTTONDOWN (main.cpp), отдельный флаг им не нужен.
    if (DrawRoundIconButton("win_min", "minus", control_pos, 22.0f,
        IM_COL32(255, 255, 255, 10), IM_COL32(255, 255, 255, 25), IM_COL32(190, 195, 208, 255))) {
        if (hwnd) PostMessage(hwnd, WM_APP_TRAY_MINIMIZE, 0, 0);
    }
    if (DrawRoundIconButton("win_close", "cross", ImVec2(control_pos.x + 28, control_pos.y), 22.0f,
        IM_COL32(51, 26, 26, 200), IM_COL32(190, 45, 45, 255), IM_COL32(245, 105, 105, 255))) {
        if (hwnd) PostMessage(hwnd, WM_CLOSE, 0, 0);
    }

    dl->AddLine(ImVec2(pos.x, pos.y + 40.0f), ImVec2(pos.x + width, pos.y + 40.0f), IM_COL32(255, 255, 255, 6), 1.0f);
    ImGui::Dummy(ImVec2(width, 40.0f));
}
