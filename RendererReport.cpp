#include "RendererReport.hpp"
#include "RendererStyle.hpp"
#include "AppState.hpp"
#include "imgui.h"
#include <string>
#include <vector>
#include <algorithm>
#include <mutex>

extern ImFont* g_FontCardTitle;
extern ImFont* g_FontSmall;

// ============================================================================
//  ИСТОРИЯ ОТЧЁТОВ
// ============================================================================
void RenderHistoryTab() {
    std::vector<OptimizationReport> history;
    {
        std::lock_guard<std::mutex> lock(AppState::reportHistoryMutex);
        history = AppState::reportHistory;
    }
    std::reverse(history.begin(), history.end()); // новые сверху

    char title[64];
    sprintf_s(title, "Отчёты оптимизации · %zu", history.size());

    float historyH = (std::max)(80.0f, ImGui::GetContentRegionAvail().y - 20.0f);
    BeginFluentCard("HistoryPanel", ImVec2(0, historyH), title, "Журнал всех выполненных оптимизаций системы");

    if (history.empty()) {
        DrawEmptyState("chart_bar", "Список пуст", "Запустите оптимизацию, чтобы увидеть отчёт здесь");
    }
    else if (ImGui::BeginTable("HistoryTable", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("Время", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Профиль", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Освобождено RAM", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Закрыто процессов", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Остановлено служб", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Temp-файлов", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::PushFont(g_FontSmall);
        ImGui::TableHeadersRow();
        ImGui::PopFont();

        for (const auto& rep : history) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 30.0f);

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImVec2 cp = ImGui::GetCursorScreenPos();
            DrawVectorIconDirect("chart_bar", ImVec2(cp.x, cp.y + 2.0f), 12.0f, ImColor(g_ThemeAccent));
            ImGui::SetCursorScreenPos(ImVec2(cp.x + 20.0f, cp.y));
            ImGui::TextColored(Palette::TextMain, "%s", rep.time.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(Palette::TextMuted, "%s", rep.profileName.empty() ? "-" : rep.profileName.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::AlignTextToFramePadding();
            if (rep.freedRAM > 0) {
                ImGui::TextColored(Palette::Success, "%zu MB", rep.freedRAM);
            }
            else {
                ImGui::TextDisabled("0 MB");
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::AlignTextToFramePadding();
            if (rep.killedProcs > 0) ImGui::Text("%d", rep.killedProcs);
            else ImGui::TextDisabled("0");

            ImGui::TableSetColumnIndex(4);
            ImGui::AlignTextToFramePadding();
            if (rep.stoppedServices > 0) ImGui::Text("%d", rep.stoppedServices);
            else ImGui::TextDisabled("0");

            ImGui::TableSetColumnIndex(5);
            ImGui::AlignTextToFramePadding();
            if (rep.tempFilesDeleted > 0) ImGui::Text("%d", rep.tempFilesDeleted);
            else ImGui::TextDisabled("0");
        }
        ImGui::EndTable();
    }
    EndFluentCard();
}
