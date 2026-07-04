#include "RendererProcesses.hpp"
#include "RendererStyle.hpp"
#include "Renderer.hpp"
#include "AppState.hpp"
#include "Optimizer.hpp"
#include "imgui.h"
#include <string>
#include <algorithm>
#include <mutex>
#include <unordered_map>

using AppState::WStringToString;
using AppState::StringToWString;

extern ImFont* g_FontCardTitle;
extern ImFont* g_FontSmall;

static char procSearch[128] = "";
static char wlInput[128] = "";

void RenderTaskManagerTab() {
    ImGui::BeginChild("FilterBar", ImVec2(0, 46), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPos(ImVec2(14, 9));
    ImVec2 sPos = ImGui::GetCursorScreenPos();
    DrawVectorIconDirect("search", ImVec2(sPos.x, sPos.y), 14.0f, ImColor(Palette::TextMuted));
    ImGui::SetCursorScreenPos(ImVec2(sPos.x + 20.0f, sPos.y - 2.0f));
    ImGui::PushItemWidth(280);
    ImGui::InputTextWithHint("##ProcFilter", "Поиск по процессам...", procSearch, IM_ARRAYSIZE(procSearch));
    ImGui::PopItemWidth();

    ImGui::SameLine(ImGui::GetWindowWidth() - 140);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Radius::Button);
    if (ImGui::Button("Обновить список", ImVec2(124, 26))) RefreshSystemProcesses();
    ImGui::PopStyleVar();
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 10));

    float gridH = (std::max)(80.0f, ImGui::GetContentRegionAvail().y - 20.0f);
    BeginFluentCard("GridContainer", ImVec2(0, gridH));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(12.0f, 3.0f));

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX
        | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_BordersInnerV;

    if (ImGui::BeginTable("ProcessGrid", 5, tableFlags)) {
        float tableRowWidth = ImGui::GetContentRegionAvail().x;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Процесс", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 0.0f, 0);
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 80.0f, 1);
        ImGui::TableSetupColumn("Память", ImGuiTableColumnFlags_WidthFixed, 110.0f, 2);
        ImGui::TableSetupColumn("Категория", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 130.0f, 3);
        ImGui::TableSetupColumn("Действия", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 168.0f, 4);
        ImGui::PushFont(g_FontSmall);
        ImGui::TableHeadersRow();
        ImGui::PopFont();

        std::wstring filterWS = StringToWString(procSearch);
        std::vector<ProcessInfo> tempProcs;
        {
            std::lock_guard<std::mutex> lk(AppState::processesMutex);
            tempProcs = AppState::runningProcesses;
        }

        std::vector<ProcessInfo> filtered;
        for (const auto& proc : tempProcs) {
            if (!filterWS.empty() && proc.name.find(filterWS) == std::wstring::npos) continue;
            filtered.push_back(proc);
        }

        ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
        if (sortSpecs && sortSpecs->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
            bool asc = spec.SortDirection == ImGuiSortDirection_Ascending;
            switch (spec.ColumnUserID) {
            case 0:
                std::sort(filtered.begin(), filtered.end(), [asc](const ProcessInfo& a, const ProcessInfo& b) {
                    int c = _wcsicmp(a.name.c_str(), b.name.c_str());
                    return asc ? c < 0 : c > 0;
                    });
                break;
            case 1:
                std::sort(filtered.begin(), filtered.end(), [asc](const ProcessInfo& a, const ProcessInfo& b) {
                    return asc ? a.pid < b.pid : a.pid > b.pid;
                    });
                break;
            case 2:
                std::sort(filtered.begin(), filtered.end(), [asc](const ProcessInfo& a, const ProcessInfo& b) {
                    return asc ? a.memorySizeMB < b.memorySizeMB : a.memorySizeMB > b.memorySizeMB;
                    });
                break;
            }
        }
        else {
            std::sort(filtered.begin(), filtered.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
                return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
                });
        }

        std::wstring pendingWhitelist;
        DWORD pendingKillPid = 0;

        static std::unordered_map<DWORD, double> s_procFirstSeen;
        double nowTime = ImGui::GetTime();

        for (const auto& proc : filtered) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 30.0f);

            std::string row_id = "p_row_" + std::to_string(proc.pid);
            ImGui::PushID(row_id.c_str());

            ImVec2 rowScreenPos = ImGui::GetCursorScreenPos();
            bool rowHovered = ImGui::IsMouseHoveringRect(rowScreenPos, ImVec2(rowScreenPos.x + tableRowWidth, rowScreenPos.y + 30.0f));
            if (rowHovered) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(255, 255, 255, 22));
            }

            auto seenIt = s_procFirstSeen.find(proc.pid);
            if (seenIt == s_procFirstSeen.end()) {
                s_procFirstSeen[proc.pid] = nowTime;
                seenIt = s_procFirstSeen.find(proc.pid);
            }
            float rowAge = (float)(nowTime - seenIt->second);
            float rowAppearT = (std::min)(1.0f, rowAge / 0.30f);
            rowAppearT = 1.0f - powf(1.0f - rowAppearT, 3.0f); // easeOutCubic
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (0.2f + rowAppearT * 0.8f));

            ImGui::TableSetColumnIndex(0);
            ImVec2 cell_pos = ImGui::GetCursorScreenPos();
            // Раньше иконка и текст ставились с фиксированными отступами (+1 / без отступа),
            // подобранными на глаз под другой размер строки — из-за этого содержимое стабильно
            // сидело выше геометрического центра 30px-й строки. Центрируем оба элемента явно,
            // по их реальному измеренному размеру, а не по случайно угаданным числам.
            const float rowH = 30.0f;
            const float iconSize = 14.0f;
            float iconY = cell_pos.y + (rowH - iconSize) * 0.5f;
            DrawProcessIcon(proc.name, proc.pid, ImVec2(cell_pos.x + 4, iconY), iconSize);

            float textLineH = ImGui::GetTextLineHeight();
            float textY = cell_pos.y + (rowH - textLineH) * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(cell_pos.x + 24, textY));
            float nameMaxW = ImGui::GetContentRegionAvail().x - 24.0f;
            std::string procName = TruncateTextToWidth(WStringToString(proc.name), nameMaxW);
            ImGui::Text("%s", procName.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, textY));
            ImGui::TextDisabled("%lu", proc.pid);

            ImGui::TableSetColumnIndex(2);
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, textY));
            float heat = (std::min)(1.0f, (float)proc.memorySizeMB / 800.0f);
            ImVec4 heatCol = heat < 0.45f
                ? LerpColor(Palette::TextMain, Palette::Warning, heat * 2.2f)
                : LerpColor(Palette::Warning, Palette::Danger, (heat - 0.45f) * 1.8f);
            ImGui::TextColored(heatCol, "%d MB", (int)proc.memorySizeMB);

            ImGui::TableSetColumnIndex(3);
            ImVec2 scr_pos = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float statusPulse = GetHeartbeatPulse();
            float dotCy = scr_pos.y + rowH * 2.5f;

            // Статус в этой колонке живой — реагирует на выбранный профиль, чтобы сразу было
            // видно, что попадёт под очистку прямо сейчас. Доступность кнопок ниже (колонка
            // "Действия") завязана отдельно на жёсткую системную защиту (Максимум-уровень),
            // не зависящую от профиля — ручное управление процессом всегда доступно.
            bool liveCritical = IsCriticalProcess(proc.name, GetSelectedCleanupLevel());
            bool hardProtected = IsCriticalProcess(proc.name, ProcessCleanupLevel::MaximumPurge);

            if (liveCritical) {
                dl->AddCircleFilled(ImVec2(scr_pos.x + 6.0f, dotCy), 5.5f + statusPulse * 2.5f, (IM_COL32(34, 197, 94, 255) & 0x00FFFFFF) | ((ImU32)(20.0f + statusPulse * 40.0f) << 24));
                dl->AddCircleFilled(ImVec2(scr_pos.x + 6.0f, dotCy), 3.5f, IM_COL32(34, 197, 94, 255));
                ImGui::SetCursorScreenPos(ImVec2(scr_pos.x + 16.0f, textY));
                ImGui::TextColored(Palette::Success, "Системный");
            }
            else {
                dl->AddCircleFilled(ImVec2(scr_pos.x + 6.0f, dotCy), 5.5f + statusPulse * 2.5f, (IM_COL32(59, 130, 246, 255) & 0x00FFFFFF) | ((ImU32)(20.0f + statusPulse * 40.0f) << 24));
                dl->AddCircleFilled(ImVec2(scr_pos.x + 6.0f, dotCy), 3.5f, IM_COL32(59, 130, 246, 255));
                ImGui::SetCursorScreenPos(ImVec2(scr_pos.x + 16.0f, textY));
                ImGui::TextColored(Palette::Info, "Активен");
            }

            ImGui::TableSetColumnIndex(4);
            if (hardProtected) {
                ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, textY));
                ImGui::TextDisabled("Защищён");
            }
            else {
                ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, scr_pos.y + (rowH - 24.0f) * 0.5f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 3.0f));
                const ImVec2 btnSize(74.0f, 24.0f);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(Palette::Warning.x, Palette::Warning.y, Palette::Warning.z, 0.16f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(Palette::Warning.x, Palette::Warning.y, Palette::Warning.z, 0.30f));
                ImGui::PushStyleColor(ImGuiCol_Text, Palette::Warning);
                if (ImGui::Button("В список", btnSize)) {
                    pendingWhitelist = proc.name;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Добавить в исключения — процесс никогда не будет закрыт оптимизатором");
                ImGui::PopStyleColor(3);

                ImGui::SameLine(0, 6);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(Palette::Danger.x, Palette::Danger.y, Palette::Danger.z, 0.16f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(Palette::Danger.x, Palette::Danger.y, Palette::Danger.z, 0.32f));
                ImGui::PushStyleColor(ImGuiCol_Text, Palette::Danger);
                if (ImGui::Button("Закрыть", btnSize)) {
                    pendingKillPid = proc.pid;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Немедленно завершить процесс");
                ImGui::PopStyleColor(3);

                ImGui::PopStyleVar(2);
            }
            ImGui::PopStyleVar(); // Alpha (появление строки)
            ImGui::PopID();
        }

        if (s_procFirstSeen.size() > tempProcs.size() + 32) {
            for (auto it = s_procFirstSeen.begin(); it != s_procFirstSeen.end(); ) {
                bool stillAlive = std::any_of(tempProcs.begin(), tempProcs.end(), [&](const ProcessInfo& p) { return p.pid == it->first; });
                it = stillAlive ? std::next(it) : s_procFirstSeen.erase(it);
            }
        }

        ImGui::EndTable();

        if (pendingKillPid != 0) {
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pendingKillPid);
            if (h) { TerminateProcess(h, 0); CloseHandle(h); }
            RefreshSystemProcesses();
        }
        if (!pendingWhitelist.empty()) {
            bool added = false;
            {
                std::lock_guard<std::mutex> lock(AppState::whitelistMutex);
                if (std::find(AppState::userWhitelist.begin(), AppState::userWhitelist.end(), pendingWhitelist) == AppState::userWhitelist.end()) {
                    AppState::userWhitelist.push_back(pendingWhitelist);
                    added = true;
                }
            }
            if (added) {
                AppState::LogToBoth("[WHITELIST] Добавлено: " + WStringToString(pendingWhitelist));
                RefreshSystemProcesses();
                AppState::SaveSettings();
            }
        }
    }
    ImGui::PopStyleVar();
    EndFluentCard();
}

// ============================================================================
//  БЕЛЫЙ СПИСОК
// ============================================================================
void RenderWhitelistTab() {
    ImGui::BeginChild("AddWlPanel", ImVec2(0, 46), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPos(ImVec2(14, 9));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Palette::TextMuted, "Процесс (.exe):");
    ImGui::SameLine();
    ImGui::PushItemWidth(280);
    ImGui::InputTextWithHint("##WlInputString", "например: game.exe", wlInput, IM_ARRAYSIZE(wlInput));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Radius::Button);
    if (ImGui::Button("Добавить в исключения", ImVec2(196, 26))) {
        std::wstring wName = StringToWString(wlInput);
        if (!wName.empty()) {
            bool added = false;
            {
                std::lock_guard<std::mutex> lock(AppState::whitelistMutex);
                if (std::find(AppState::userWhitelist.begin(), AppState::userWhitelist.end(), wName) == AppState::userWhitelist.end()) {
                    AppState::userWhitelist.push_back(wName);
                    added = true;
                }
            }
            if (added) {
                AppState::LogToBoth(L"[WHITELIST] Добавлено вручную: " + wName);
                RefreshSystemProcesses();
                AppState::SaveSettings();
            }
            wlInput[0] = '\0';
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 14));

    float avail_w = ImGui::GetContentRegionAvail().x;
    const float gap = 16.0f;

    char sysTitle[64], userTitle[64];
    size_t sysCount = 0, userCount = 0;
    {
        std::lock_guard<std::mutex> lock(AppState::whitelistMutex);
        sysCount = AppState::systemCriticalWhitelist.size();
        userCount = AppState::userWhitelist.size();
    }
    sprintf_s(sysTitle, "Системные зависимости · %zu", sysCount);
    sprintf_s(userTitle, "Ваши исключения · %zu", userCount);

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(12.0f, 4.0f));

    float wlPanelH = (std::max)(80.0f, ImGui::GetContentRegionAvail().y - 20.0f);

    BeginFluentCard("SystemWlPanel", ImVec2((avail_w - gap) * 0.5f, wlPanelH), sysTitle, "Защищены от завершения на любом профиле");
    if (ImGui::BeginTable("SysWlTable", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        std::lock_guard<std::mutex> lock(AppState::whitelistMutex);
        for (const auto& item : AppState::systemCriticalWhitelist) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 26.0f);
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();

            ImVec2 lockPos = ImGui::GetCursorScreenPos();
            DrawVectorIconDirect("lock", ImVec2(lockPos.x, lockPos.y + 1), 12.0f, ImColor(Palette::TextMuted));
            ImGui::SetCursorScreenPos(ImVec2(lockPos.x + 20.0f, lockPos.y));
            ImGui::TextColored(Palette::TextMuted, "%s", WStringToString(item).c_str());
        }
        ImGui::EndTable();
    }
    EndFluentCard();

    ImGui::SameLine(0, gap);

    BeginFluentCard("UserWlPanel", ImVec2((avail_w - gap) * 0.5f, wlPanelH), userTitle, "Персональный белый список");

    std::wstring toDelete;
    std::vector<std::wstring> tempUser;
    {
        std::lock_guard<std::mutex> lock(AppState::whitelistMutex);
        tempUser = AppState::userWhitelist;
    }

    if (tempUser.empty()) {
        DrawEmptyState("star", "Список пуст", "Добавьте процесс выше или через вкладку «Процессы»");
    }
    else if (ImGui::BeginTable("UserWlTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Имя", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Действие", ImGuiTableColumnFlags_WidthFixed, 96.0f);

        for (const auto& item : tempUser) {
            std::string itemStr = WStringToString(item);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 30.0f);
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();

            ImVec2 starPos = ImGui::GetCursorScreenPos();
            DrawVectorIconDirect("star", ImVec2(starPos.x, starPos.y + 1), 12.0f, ImColor(Palette::Warning));
            ImGui::SetCursorScreenPos(ImVec2(starPos.x + 20.0f, starPos.y));
            ImGui::Text("%s", itemStr.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(Palette::Danger.x, Palette::Danger.y, Palette::Danger.z, 0.14f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(Palette::Danger.x, Palette::Danger.y, Palette::Danger.z, 0.32f));
            ImGui::PushStyleColor(ImGuiCol_Text, Palette::Danger);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 3.0f));
            std::string delLabel = "Убрать##" + itemStr;
            if (ImGui::Button(delLabel.c_str(), ImVec2(88, 24))) {
                toDelete = item;
            }
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
        }
        ImGui::EndTable();
    }
    EndFluentCard();

    ImGui::PopStyleVar();

    if (!toDelete.empty()) {
        {
            std::lock_guard<std::mutex> lock(AppState::whitelistMutex);
            AppState::userWhitelist.erase(
                std::remove(AppState::userWhitelist.begin(), AppState::userWhitelist.end(), toDelete),
                AppState::userWhitelist.end());
        }
        AppState::LogToBoth("[WHITELIST] Удалено исключение: " + WStringToString(toDelete));
        RefreshSystemProcesses();
        AppState::SaveSettings();
    }
}
