#include "Renderer.hpp"
#include "RendererStyle.hpp"
#include "RendererTitleBar.hpp"
#include "RendererDashboard.hpp"
#include "RendererProcesses.hpp"
#include "RendererReport.hpp"
#include "RendererOptimization.hpp"
#include "AppState.hpp"
#include "Optimizer.hpp"
#include "imgui.h"
#include <string>
#include <cmath>
#include <mutex>

extern ImFont* g_FontBody;
extern ImFont* g_FontSmall;
extern ImFont* g_FontCardTitle;
extern ImFont* g_FontTitle;

// ============================================================================
//  ТОСТ "ДО / ПОСЛЕ" — краткий оверлей результата оптимизации в правом нижнем углу.
// ============================================================================
static void RenderOptimizationToast() {
    static size_t lastSeenCount = 0;
    static OptimizationReport toastReport;
    static float toastTimer = 0.0f;

    {
        std::lock_guard<std::mutex> lock(AppState::reportHistoryMutex);
        if (AppState::reportHistory.size() > lastSeenCount) {
            lastSeenCount = AppState::reportHistory.size();
            toastReport = AppState::reportHistory.back();
            toastTimer = 6.0f;
        }
    }

    if (toastTimer <= 0.0f) return;
    toastTimer -= ImGui::GetIO().DeltaTime;
    if (toastTimer <= 0.0f) return;

    float age = 6.0f - toastTimer;
    float fadeIn = (std::min)(1.0f, age / 0.35f);
    float fadeOut = (std::min)(1.0f, toastTimer / 1.0f);
    float alpha = fadeIn * fadeOut;
    float slideY = (1.0f - fadeIn) * 16.0f; // лёгкий подъём снизу при появлении

    ImVec2 sz = ImGui::GetIO().DisplaySize;
    float w = 260.0f, h = 84.0f;
    ImVec2 boxMax = ImVec2(sz.x - 20.0f, sz.y - 20.0f + slideY);
    ImVec2 boxMin = ImVec2(boxMax.x - w, boxMax.y - h);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(boxMin, boxMax, ImColor(0.094f, 0.094f, 0.125f, 0.94f * alpha), Radius::Card);
    dl->AddRect(boxMin, boxMax, ImColor(1.0f, 1.0f, 1.0f, 0.10f * alpha), Radius::Card, 0, 1.0f);

    ImGui::PushFont(g_FontSmall);
    dl->AddText(ImVec2(boxMin.x + 16.0f, boxMin.y + 12.0f), ImColor(Palette::TextMuted.x, Palette::TextMuted.y, Palette::TextMuted.z, alpha), "ОПТИМИЗАЦИЯ ЗАВЕРШЕНА");
    ImGui::PopFont();

    char beforeBuf[16], afterBuf[16];
    sprintf_s(beforeBuf, "%d%%", (int)toastReport.ramPercentBefore);
    sprintf_s(afterBuf, "%d%%", (int)toastReport.ramPercentAfter);

    ImGui::PushFont(g_FontCardTitle);
    ImVec2 beforeSz = ImGui::CalcTextSize(beforeBuf);
    float rowY = boxMin.y + 34.0f;
    dl->AddText(ImVec2(boxMin.x + 16.0f, rowY), ImColor(Palette::TextMuted.x, Palette::TextMuted.y, Palette::TextMuted.z, alpha), beforeBuf);

    // Стрелка между "было" и "стало": вниз и зелёная, если нагрузка на RAM снизилась,
    // вверх и красная, если выросла (профиль без закрытия процессов может её не снижать).
    bool improved = toastReport.ramPercentAfter <= toastReport.ramPercentBefore;
    float arrowT = (float)ImGui::GetTime();
    float arrowShift = sinf(arrowT * 4.0f) * 2.0f;
    ImVec2 arrowCenter = ImVec2(boxMin.x + 16.0f + beforeSz.x + 22.0f + arrowShift, rowY + beforeSz.y * 0.5f);
    ImU32 arrowCol = improved ? ImColor(0.20f, 0.78f, 0.35f, alpha) : ImColor(0.94f, 0.27f, 0.27f, alpha);
    float dir = improved ? 1.0f : -1.0f; // 1 = наконечник смотрит вниз, -1 = вверх
    dl->AddLine(ImVec2(arrowCenter.x, arrowCenter.y - 5.0f * dir), ImVec2(arrowCenter.x, arrowCenter.y + 5.0f * dir), arrowCol, 2.0f);
    dl->AddLine(ImVec2(arrowCenter.x - 5.0f, arrowCenter.y + 1.0f * dir), ImVec2(arrowCenter.x, arrowCenter.y + 5.0f * dir), arrowCol, 2.0f);
    dl->AddLine(ImVec2(arrowCenter.x + 5.0f, arrowCenter.y + 1.0f * dir), ImVec2(arrowCenter.x, arrowCenter.y + 5.0f * dir), arrowCol, 2.0f);

    dl->AddText(ImVec2(arrowCenter.x + 14.0f, rowY), ImColor(1.0f, 1.0f, 1.0f, alpha), afterBuf);
    ImGui::PopFont();

    char statsBuf[96];
    sprintf_s(statsBuf, "Освобождено %zu MB · закрыто %d процессов", toastReport.freedRAM, toastReport.killedProcs);
    ImGui::PushFont(g_FontSmall);
    dl->AddText(ImVec2(boxMin.x + 16.0f, boxMax.y - 22.0f), ImColor(Palette::TextMuted.x, Palette::TextMuted.y, Palette::TextMuted.z, alpha), statsBuf);
    ImGui::PopFont();
}

// RenderUI() — единственный публичный вход в этот файл (см. Renderer.hpp): диспетчер,
// вызывающий RenderTitleBarStrip/RenderSidebar и Render{Main,TaskManager,Whitelist,History}Tab
// в зависимости от activeTab. Сами вкладки вынесены в отдельные модули (RendererDashboard.cpp,
// RendererProcesses.cpp, RendererReport.cpp, RendererTitleBar.cpp), общие стили/примитивы —
// в RendererStyle.hpp/.cpp, чтобы не дублировать их в каждом модуле.

// ============================================================================
//  БОКОВАЯ НАВИГАЦИЯ
// ============================================================================
static void NavButton(const char* icon_name, const char* label, int tabIndex) {
    bool active = (activeTab == tabIndex);
    float width = ImGui::GetContentRegionAvail().x;
    ImVec2 size(width, 44);

    ImVec2 cursorBefore = ImGui::GetCursorScreenPos();

    ImGui::PushID(tabIndex);
    bool clicked = ImGui::InvisibleButton("##nav", size);
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    if (clicked) activeTab = tabIndex;

    float activeT = AnimToKey(ImGui::GetID((std::string("nav_a_") + label).c_str()), active ? 1.0f : 0.0f, 14.0f);
    float hoverT = AnimToKey(ImGui::GetID((std::string("nav_h_") + label).c_str()), (hovered && !active) ? 1.0f : 0.0f, 16.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (activeT > 0.01f) {
        // Мягкая цветная тень под активной плиткой — несколько слоёв с падающей прозрачностью
        // и растущим отступом, вместо плоской заливки без глубины.
        ImVec2 pillMin = cursorBefore;
        ImVec2 pillMax = ImVec2(cursorBefore.x + width, cursorBefore.y + size.y);
        for (int i = 5; i >= 1; i--) {
            float grow = (float)i * 3.0f;
            ImU32 shadowCol = ImColor(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, activeT * 0.10f);
            dl->AddRectFilled(ImVec2(pillMin.x - grow * 0.3f, pillMin.y + grow), ImVec2(pillMax.x + grow * 0.3f, pillMax.y + grow), shadowCol, Radius::Button + grow * 0.3f);
        }
        ImU32 pillCol = ImColor(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.90f * activeT);
        dl->AddRectFilled(pillMin, pillMax, pillCol, Radius::Button);
        // Тонкий внутренний блик сверху — как на карточках, добавляет ощущение объёма/стекла
        dl->AddLine(ImVec2(pillMin.x + Radius::Button * 0.5f, pillMin.y + 1.5f),
            ImVec2(pillMax.x - Radius::Button * 0.5f, pillMin.y + 1.5f), IM_COL32(255, 255, 255, (int)(40.0f * activeT)), 1.0f);
    }
    else if (hoverT > 0.01f) {
        dl->AddRectFilled(cursorBefore, ImVec2(cursorBefore.x + width, cursorBefore.y + size.y),
            ImColor(1.0f, 1.0f, 1.0f, 0.05f * hoverT), Radius::Button);
    }

    ImVec4 inactiveCol = Palette::TextMuted;
    ImVec4 txtCol = LerpColor(inactiveCol, Palette::TextMain, activeT);

    ImU32 iconColor = ImColor(txtCol);
    ImVec2 iconPos = ImVec2(cursorBefore.x + 14, cursorBefore.y + (size.y - 17.0f) * 0.5f);
    DrawVectorIconDirect(icon_name, iconPos, 17.0f, iconColor);

    ImGui::PushFont(activeT > 0.5f ? g_FontCardTitle : g_FontBody);
    ImVec2 txtSize = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(cursorBefore.x + 42, cursorBefore.y + (size.y - txtSize.y) * 0.5f), ImColor(txtCol), label);
    ImGui::PopFont();
}

static void RenderSidebar() {
    const float sidebarW = 180.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 20));
    ImGui::BeginChild("Sidebar", ImVec2(sidebarW, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 sbPos = ImGui::GetWindowPos();
    ImVec2 winMax = ImVec2(sbPos.x + ImGui::GetWindowWidth(), sbPos.y + ImGui::GetWindowHeight());
    dl->AddLine(ImVec2(winMax.x, sbPos.y), ImVec2(winMax.x, winMax.y), IM_COL32(255, 255, 255, 8), 1.0f);

    ImGui::SetCursorPosX(14.0f);
    ImVec2 logoPos = ImGui::GetCursorScreenPos();
    float logoSize = 32.0f;

    dl->AddRectFilled(logoPos, ImVec2(logoPos.x + logoSize, logoPos.y + logoSize), ImColor(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.16f), 8.0f);
    dl->AddRect(logoPos, ImVec2(logoPos.x + logoSize, logoPos.y + logoSize), ImColor(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.70f), 8.0f, 0, 1.5f);

    float padX = logoSize * 0.15f;
    float padY = logoSize * 0.25f;
    ImVec2 lMin = ImVec2(logoPos.x + padX, logoPos.y + padY);
    ImVec2 lMax = ImVec2(logoPos.x + logoSize - padX, logoPos.y + logoSize - padY);
    float lW = lMax.x - lMin.x;
    float lH = lMax.y - lMin.y;
    ImVec2 lCtr = ImVec2(lMin.x + lW * 0.5f, lMin.y + lH * 0.5f);

    dl->PathClear();
    dl->PathLineTo(ImVec2(lMin.x, lCtr.y));
    dl->PathLineTo(ImVec2(lMin.x + lW * 0.22f, lCtr.y));
    dl->PathLineTo(ImVec2(lMin.x + lW * 0.35f, lCtr.y + lH * 0.15f));
    dl->PathLineTo(ImVec2(lMin.x + lW * 0.45f, lMin.y));
    dl->PathLineTo(ImVec2(lMin.x + lW * 0.55f, lMax.y));
    dl->PathLineTo(ImVec2(lMin.x + lW * 0.65f, lCtr.y - lH * 0.1f));
    dl->PathLineTo(ImVec2(lMin.x + lW * 0.78f, lCtr.y));
    dl->PathLineTo(ImVec2(lMax.x, lCtr.y));

    dl->PathStroke(ImColor(g_ThemeAccent.x, g_ThemeAccent.y, g_ThemeAccent.z, 0.39f), 0, 3.5f);
    dl->PathStroke(IM_COL32(255, 255, 255, 255), 0, 1.8f);

    ImGui::SetCursorScreenPos(ImVec2(logoPos.x + logoSize + 10.0f, logoPos.y - 3.0f));
    ImGui::PushFont(g_FontCardTitle);
    ImGui::TextColored(Palette::TextMain, "Pulse");
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(logoPos.x + logoSize + 10.0f, logoPos.y + 15.0f));
    ImGui::PushFont(g_FontSmall);
    ImGui::TextColored(Palette::TextMuted, "Dashboard");
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(logoPos.x, logoPos.y + logoSize + 24.0f));

    ImGui::SetCursorPosX(6);
    NavButton("grid", "Обзор", 0);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::SetCursorPosX(6);
    NavButton("monitor", "Процессы", 1);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::SetCursorPosX(6);
    NavButton("shield_check", "Исключения", 2);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::SetCursorPosX(6);
    NavButton("chart_bar", "История", 3);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::SetCursorPosX(6);
    NavButton("gear", "Оптимизация", 4);

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}
// ============================================================================
//  ЗАГОЛОВОК СТРАНИЦЫ
// ============================================================================
static void RenderPageHeader() {
    if (activeTab == 0) {
        return;
    }
    struct TabInfo { const char* title; const char* subtitle; };
    static const TabInfo tabs[5] = {
        { "", "" },
        { "Процессы", "Пнямой мониторинг запущенных системных задач" },
        { "Исключения", "Процессы, защищённые от завершения оптимизатором" },
        { "История", "Журнал выполненных оптимизаций системы" },
        { "Оптимизация", "Постоянные системные твики: диск, сеть, службы, CPU" },
    };
    const TabInfo& info = tabs[activeTab];

    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushFont(g_FontTitle);
    ImGui::TextColored(Palette::TextMain, "%s", info.title);
    ImGui::PopFont();
    ImGui::PushFont(g_FontSmall);
    ImGui::TextColored(Palette::TextMuted, "%s", info.subtitle);
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + 54.0f));
}

// ============================================================================
//  ГЛАВНАЯ ФУНКЦИЯ РЕНДЕРИНГА UI
// ============================================================================
void RenderUI() {
    UpdateThemeAccent();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##MainCanvas", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    RenderAppBackground();
    RenderTitleBarStrip();
    RenderOptimizationToast();

    if (AppState::hasStartupWarning.load()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.45f, 0.32f, 0.05f, 1.0f));
        ImGui::BeginChild("StartupWarningBanner", ImVec2(0, 32), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetCursorPos(ImVec2(16, 8));
        ImGui::TextColored(ImVec4(1.0f, 0.93f, 0.75f, 1.0f), "%s", AppState::startupWarningText.c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - 60.0f);
        if (ImGui::SmallButton("Закрыть##startupWarn")) {
            AppState::hasStartupWarning = false;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("BodyArea", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    RenderSidebar();
    ImGui::SameLine(0, 0);

    float availX = ImGui::GetContentRegionAvail().x - 36.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::BeginChild("ContentArea", ImVec2(availX, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    RenderPageHeader();

    static int lastTab = activeTab;
    static float tabFade = 1.0f;
    if (lastTab != activeTab) {
        lastTab = activeTab;
        tabFade = 0.0f;
    }
    tabFade += (1.0f - tabFade) * (1.0f - expf(-10.0f * io.DeltaTime));
    if (tabFade > 0.999f) tabFade = 1.0f;

    ImVec2 preTabPos = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(preTabPos.x, preTabPos.y + (1.0f - tabFade) * 10.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.15f + tabFade * 0.85f);
    switch (activeTab) {
    case 0: RenderMainTab(); break;
    case 1: RenderTaskManagerTab(); break;
    case 2: RenderWhitelistTab(); break;
    case 3: RenderHistoryTab(); break;
    case 4: RenderOptimizationTab(); break;
    }
    ImGui::PopStyleVar();

    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar();
}
