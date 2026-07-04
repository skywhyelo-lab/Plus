#include "RendererDashboard.hpp"
#include "RendererStyle.hpp"
#include "AppState.hpp"
#include "Optimizer.hpp"
#include "imgui.h"
#include <string>
#include <algorithm>
#include <mutex>

using AppState::WStringToString;

extern ImFont* g_FontCardTitle;
extern ImFont* g_FontSmall;

// ============================================================================
//  ГЛАВНАЯ (ОБЗОР)
// ============================================================================
void RenderMainTab() {
    float avail_w = ImGui::GetContentRegionAvail().x;
    const float gap = 16.0f;
    const float rowGap = 16.0f;

    float liveCPU[12], liveRAM[12], liveDisk[12], liveNet[12];
    {
        std::lock_guard<std::mutex> lock(AppState::telemetryHistoryMutex);
        int offset = AppState::historyOffset;
        for (int i = 0; i < 12; i++) {
            int idx = (offset - 12 + i + 100) % 100;
            liveRAM[i] = AppState::ramUsageHistory[idx];
            liveCPU[i] = AppState::cpuUsageHistory[idx];
            liveDisk[i] = AppState::diskUsageHistory[idx];
            liveNet[i] = AppState::netUsageHistory[idx];
        }
    }

    // --- РЯД 1: общая производительность + список процессов ---
    float row1H = 236.0f;
    float col1AW = (avail_w - gap) * 0.62f;
    float col1BW = avail_w - gap - col1AW;

    BeginFluentCard("GeneralPerformance", ImVec2(col1AW, row1H), "Общая производительность", nullptr);
    if (ImGui::BeginTable("GenPerfLayout", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("DonutCol", ImGuiTableColumnFlags_WidthFixed, 164.0f);
        ImGui::TableSetupColumn("SparkCol", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImVec2 cur_pos = ImGui::GetCursorScreenPos();
        ImVec2 donut_center = ImVec2(cur_pos.x + 76.0f, cur_pos.y + 80.0f);

        DrawCircularHardwareGauge(ImGui::GetWindowDrawList(), donut_center, 72.0f, AppState::currentRAMUsagePercent.load() / 100.0f,
            IM_COL32(255, 255, 255, 12), nullptr, nullptr);

        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(cur_pos.x + 152.0f, cur_pos.y + 8.0f),
            ImVec2(cur_pos.x + 152.0f, cur_pos.y + 152.0f),
            IM_COL32(255, 255, 255, 14)
        );

        ImGui::Dummy(ImVec2(164, 164));

        ImGui::TableSetColumnIndex(1);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4.0f);

        // Плавное сглаживание отображаемых чисел (counter tween) — без него проценты скачут
        // рывками на каждое обновление телеметрии (раз в 500мс), что выглядит дёшево.
        float tweenCPU = AnimToKey(ImGui::GetID("tween_cpu_main"), AppState::currentCPUUsagePercent.load(), 10.0f);
        float tweenRAM = AnimToKey(ImGui::GetID("tween_ram_main"), AppState::currentRAMUsagePercent.load(), 10.0f);
        float tweenDisk = AnimToKey(ImGui::GetID("tween_disk_main"), AppState::currentDiskUsagePercent.load(), 10.0f);
        float tweenNet = AnimToKey(ImGui::GetID("tween_net_main"), AppState::currentNetUsagePercent.load(), 10.0f);

        DrawMetricRow("CPU", tweenCPU, IM_COL32(139, 92, 246, 255), liveCPU, 12);
        DrawMetricRow("RAM", tweenRAM, IM_COL32(150, 105, 250, 255), liveRAM, 12);
        DrawMetricRow("Диск", tweenDisk, IM_COL32(165, 125, 255, 255), liveDisk, 12);
        DrawMetricRow("Сеть", tweenNet, IM_COL32(180, 145, 255, 255), liveNet, 12);

        ImGui::EndTable();
    }
    EndFluentCard();

    ImGui::SameLine(0, gap);

    BeginFluentCard("MiniProcesses", ImVec2(col1BW, row1H), "Активные процессы");
    if (ImGui::BeginTable("MiniProcGrid", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("Процесс", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("Память", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("Статус", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::PushFont(g_FontSmall);
        ImGui::TableHeadersRow();
        ImGui::PopFont();

        std::vector<ProcessInfo> tempProcs;
        {
            std::lock_guard<std::mutex> lk(AppState::processesMutex);
            tempProcs = AppState::runningProcesses;
        }

        int rows_rendered = 0;
        for (const auto& proc : tempProcs) {
            if (rows_rendered >= 4) break;
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImVec2 cp = ImGui::GetCursorScreenPos();
            // Подобранный вместе с пользователем сдвиг вниз (было +10 для иконки при тексте на
            // естественной высоте, потом -3 => +7). Теперь сдвигаем сам текст названия на ту же
            // величину, а иконка центрируется просто по его новому прямоугольнику — так оба
            // элемента гарантированно оказываются на одной высоте, без рассинхрона.
            const float miniRowVOffset = 7.0f;
            ImGui::SetCursorScreenPos(ImVec2(cp.x + 20.0f, cp.y + miniRowVOffset));
            std::string shortName = WStringToString(proc.name);
            if (shortName.size() > 13) shortName = shortName.substr(0, 11) + "...";
            ImGui::Text("%s", shortName.c_str());
            ImVec2 nameTextMin = ImGui::GetItemRectMin();
            ImVec2 nameTextMax = ImGui::GetItemRectMax();
            float miniIconY = (nameTextMin.y + nameTextMax.y) * 0.5f - 7.0f;
            DrawProcessIcon(proc.name, proc.pid, ImVec2(cp.x, miniIconY), 14.0f);

            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%.1f%%", proc.cpuPercent);

            ImGui::TableSetColumnIndex(2);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%.1f GB", (float)proc.memorySizeMB / 1024.0f);

            ImGui::TableSetColumnIndex(3);
            ImVec2 pipPos = ImGui::GetCursorScreenPos();
            bool liveCritical = IsCriticalProcess(proc.name, GetSelectedCleanupLevel());
            ImU32 dotCol = liveCritical ? IM_COL32(34, 197, 94, 255) : IM_COL32(59, 130, 246, 255);
            float dotPulse = GetHeartbeatPulse();
            ImGui::SetCursorScreenPos(ImVec2(pipPos.x + 11, pipPos.y));
            ImGui::PushFont(g_FontSmall);
            if (liveCritical) ImGui::TextColored(Palette::Success, "Систем.");
            else ImGui::TextColored(Palette::Info, "Активен");
            ImGui::PopFont();
            // Точка-статус центрируется по реальному прямоугольнику текста рядом с ней —
            // тем же приёмом, что и иконка процесса выше.
            ImVec2 statusTextMin = ImGui::GetItemRectMin();
            ImVec2 statusTextMax = ImGui::GetItemRectMax();
            float pipCy = (statusTextMin.y + statusTextMax.y) * 0.5f + 2.0f;
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pipPos.x + 4, pipCy), 5.0f + dotPulse * 2.5f, (dotCol & 0x00FFFFFF) | ((ImU32)(25.0f + dotPulse * 45.0f) << 24));
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pipPos.x + 4, pipCy), 3.0f, dotCol);

            rows_rendered++;
        }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushStyleColor(ImGuiCol_Text, g_ThemeAccent);
    if (ImGui::Selectable("Показать все процессы  ->", false, 0, ImVec2(180, 20))) activeTab = 1;
    ImGui::PopStyleColor();
    EndFluentCard();

    ImGui::Dummy(ImVec2(0, rowGap));

    // --- РЯД 2: мониторинг системы + быстрые действия + аппаратная информация ---
    float row2H = 300.0f; // Конечная фиксированная высота для идеальной симметрии обеих колонок
    float col2AW = (avail_w - gap) * 0.62f;
    float col2BW = avail_w - gap - col2AW;

    BeginFluentCard("LiveSysMonitoring", ImVec2(col2AW, row2H), "Мониторинг системы");
    {
        ImDrawList* mdl_dl = ImGui::GetWindowDrawList();
        ImVec2 headerPos = ImGui::GetCursorScreenPos();
        headerPos.y -= 26.0f;

        auto Legend = [&](float& x, const char* lbl, ImVec4 col) {
            mdl_dl->AddCircleFilled(ImVec2(x, headerPos.y + 8.0f), 3.5f, ImColor(col));
            ImGui::SetCursorScreenPos(ImVec2(x + 10.0f, headerPos.y));
            ImGui::PushFont(g_FontSmall);
            ImGui::TextColored(Palette::TextMuted, "%s", lbl);
            ImGui::PopFont();
            x += ImGui::CalcTextSize(lbl).x + 24.0f;
            };
        float legendX = headerPos.x + ImGui::GetContentRegionAvail().x - 200.0f;
        Legend(legendX, "CPU", ImColor(139, 92, 246, 255));
        Legend(legendX, "RAM", ImColor(59, 130, 246, 255));
        Legend(legendX, "Диск", ImColor(245, 158, 11, 255));
        Legend(legendX, "Сеть", ImColor(34, 197, 94, 255));

        ImGui::SetCursorScreenPos(ImVec2(headerPos.x, headerPos.y + 26.0f));
        ImVec2 chartFrame = ImGui::GetCursorScreenPos();
        ImVec2 chartSize = ImVec2(ImGui::GetContentRegionAvail().x, 130.0f);

        static float orderedCPU[100], orderedRAM[100], orderedDisk[100], orderedNet[100];
        {
            std::lock_guard<std::mutex> lock(AppState::telemetryHistoryMutex);
            int chartOffset = AppState::historyOffset;
            for (int i = 0; i < 100; i++) {
                int idx = (chartOffset + i) % 100; // oldest -> newest, chronological order
                orderedCPU[i] = AppState::cpuUsageHistory[idx];
                orderedRAM[i] = AppState::ramUsageHistory[idx];
                orderedDisk[i] = AppState::diskUsageHistory[idx];
                orderedNet[i] = AppState::netUsageHistory[idx];
            }
        }
        DrawMultiSeriesChart(chartFrame, chartSize, orderedCPU, orderedRAM, orderedDisk, orderedNet, 100);
        ImGui::Dummy(chartSize);

        ImGui::Dummy(ImVec2(0, 1));
        ImGui::PushFont(g_FontSmall);
        float plotLeft = chartFrame.x + 36.0f;
        float plotW = chartSize.x - 36.0f - 6.0f;
        float labelY = ImGui::GetCursorScreenPos().y;
        const char* timeLabels[5] = { "-50с", "-37с", "-25с", "-12с", "Сейчас" };
        for (int i = 0; i < 5; i++) {
            ImVec2 ts = ImGui::CalcTextSize(timeLabels[i]);
            float centerX = plotLeft + plotW * ((float)i / 4.0f);
            float x = centerX - ts.x * 0.5f;
            if (i == 0) x = plotLeft;
            if (i == 4) x = plotLeft + plotW - ts.x;
            ImGui::GetWindowDrawList()->AddText(ImVec2(x, labelY), ImColor(Palette::TextMuted), timeLabels[i]);
        }
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 10));

        if (ImGui::BeginTable("MonFooterCards", 4, ImGuiTableFlags_None)) {
            ImGui::TableSetupColumn("Col0", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Col1", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Col2", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Col3", ImGuiTableColumnFlags_WidthStretch);

            float curUsage = AppState::currentCPUUsagePercent.load();
            float simulatedTemp = 36.0f + (curUsage * 0.32f) + (float)(sin(ImGui::GetTime() * 0.8f) * 0.5f);
            char tempBuf[32];
            sprintf_s(tempBuf, "Темп.: %d°C", (int)simulatedTemp);

            auto DrawFooterCard = [](const char* label, float value, const char* subtext, ImU32 color) {
                ImGui::TableNextColumn();
                ImVec2 start = ImGui::GetCursorScreenPos();
                float w = ImGui::GetContentRegionAvail().x;
                float h = 54.0f;

                // Тот же counter tween, что и в основных метриках выше — отдельный ключ
                // (суффикс "_footer"), чтобы не путать с анимацией главных виджетов.
                std::string tweenKey = std::string("tween_") + label + "_footer";
                value = AnimToKey(ImGui::GetID(tweenKey.c_str()), value, 10.0f);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(start, ImVec2(start.x + w, start.y + h), IM_COL32(255, 255, 255, 6), 10.0f);
                dl->AddRect(start, ImVec2(start.x + w, start.y + h), IM_COL32(255, 255, 255, 10), 10.0f, 0, 1.0f);

                // Радиус скругления не может быть больше половины ширины полоски (3px) — раньше
                // здесь стоял Radius::Card (10px), из-за чего ImGui рисовал искажённую, будто
                // "обрезанную" сверху и снизу форму. Плавающая тонкая табличка вместо привязки
                // к угловому радиусу карточки — с небольшим отступом от краёв, без артефактов.
                dl->AddRectFilled(ImVec2(start.x + 6.0f, start.y + 6.0f), ImVec2(start.x + 9.0f, start.y + h - 6.0f), color, 1.5f);

                ImGui::SetCursorScreenPos(ImVec2(start.x + 16.0f, start.y + 4.0f));
                ImGui::PushFont(g_FontSmall);
                ImGui::TextColored(Palette::TextMuted, "%s", label);
                ImGui::PopFont();

                ImGui::SetCursorScreenPos(ImVec2(start.x + 16.0f, start.y + 16.0f));
                ImGui::PushFont(g_FontCardTitle);
                char valBuf[32];
                if (strcmp(label, "Сеть") == 0) {
                    float tweenMbps = AnimToKey(ImGui::GetID("tween_net_mbps_footer"), AppState::currentNetworkMbps.load(), 10.0f);
                    sprintf_s(valBuf, "%.1f Mbs", tweenMbps);
                }
                else {
                    sprintf_s(valBuf, "%d%%", (int)value);
                }
                ImGui::TextColored(Palette::TextMain, "%s", valBuf);
                ImGui::PopFont();

                ImGui::SetCursorScreenPos(ImVec2(start.x + 16.0f, start.y + 36.0f));
                ImGui::PushFont(g_FontSmall);
                ImGui::TextColored(Palette::TextMuted, "%s", subtext);
                ImGui::PopFont();

                ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + h));
                ImGui::Dummy(ImVec2(w, 0.0f));
                };

            DrawFooterCard("CPU", AppState::currentCPUUsagePercent.load(), tempBuf, IM_COL32(139, 92, 246, 255));
            DrawFooterCard("RAM", AppState::currentRAMUsagePercent.load(), "Использовано", IM_COL32(59, 130, 246, 255));
            DrawFooterCard("Диск", AppState::currentDiskUsagePercent.load(), "Активность", IM_COL32(245, 158, 11, 255));
            DrawFooterCard("Сеть", AppState::currentNetUsagePercent.load(), "Подключено", IM_COL32(34, 197, 94, 255));
            ImGui::EndTable();
        }
    }
    EndFluentCard();

    ImGui::SameLine(0, gap);

    ImGui::BeginGroup();

    // 1. Быстрые действия (Изменена высота со 128.0f на 120.0f)
    BeginFluentCard("QuickActions", ImVec2(col2BW, 120.0f), "Быстрые действия", nullptr);
    {
        float act_w = ImGui::GetContentRegionAvail().x;

        // Поместили кнопки чуть повыше (-11px) и уменьшили интервал (3px) для идеального центрирования
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 11.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 3.0f));
        if (DrawActionButton("Оптимизация системы", "Ускорить работу ОС", "wrench", act_w, ImColor(Palette::Cyan)) && !AppState::isOptimizing.load()) {
            // Гвард от повторного клика: без него два параллельных PerformOptimizationTask
            // одновременно пишут в общие атомики отчёта и дерутся за снапшот процессов —
            // отсюда была гонка данных и краш при нескольких быстрых кликах подряд.
            OptimizationProfile chosenProfile = GetOptimizationProfiles()[AppState::selectedProfileIndex.load()];
            std::thread opt([chosenProfile]() { PerformOptimizationTask(chosenProfile); });
            opt.detach();
        }
        if (DrawActionButton("Управление списком", "Настройка листа", "gear", act_w, ImColor(Palette::Warning))) {
            activeTab = 2;
        }
        ImGui::PopStyleVar();
    }
    EndFluentCard();

    // Убран ручной Dummy-интервал. Вместо него используется нативный ItemSpacing.y (12.0px) из стиля ImGui.
    // Это предотвращает лишнее смещение элементов вниз и делает вертикальные зазоры идеально сбалансированными.

    // 2. Аппаратная информация (Высота скорректирована до 168.0f для идеального выравнивания нижних границ обеих колонок)
    BeginFluentCard("HardwareInfo", ImVec2(col2BW, 168.0f), "Аппаратная информация", nullptr);
    if (ImGui::BeginTable("HWVerticalTable", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("LabelCol", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("ValueCol", ImGuiTableColumnFlags_WidthStretch);

        auto DrawHWRow = [](const char* icon, ImU32 iconColor, const char* label, const std::string& value) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 19.0f); // Высота строк увеличена с 16.0f до 19.0f для идеальной читаемости
            ImGui::TableSetColumnIndex(0);
            ImVec2 cp = ImGui::GetCursorScreenPos();
            DrawVectorIconDirect(icon, ImVec2(cp.x, cp.y + 2.0f), 13.0f, iconColor);
            ImGui::SetCursorScreenPos(ImVec2(cp.x + 18.0f, cp.y));
            ImGui::PushFont(g_FontSmall);
            ImGui::TextColored(Palette::TextMuted, "%s", label);
            ImGui::PopFont();

            ImGui::TableSetColumnIndex(1);
            ImGui::PushFont(g_FontSmall);
            float maxW = ImGui::GetContentRegionAvail().x;
            std::string val = TruncateTextToWidth(value, maxW);
            float valW = ImGui::CalcTextSize(val.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, maxW - valW - 4.0f));
            ImGui::TextColored(Palette::TextMain, "%s", val.c_str());
            ImGui::PopFont();
            };

        DrawHWRow("cpu", ImColor(g_ThemeAccent), "Процессор", AppState::cpuName);
        DrawHWRow("gpu", ImColor(Palette::Cyan), "Видеокарта", AppState::gpuName);
        DrawHWRow("ram", ImColor(Palette::Warning), "Оперативная память", AppState::ramSizeGB);
        DrawHWRow("motherboard", ImColor(Palette::Success), "Материнская плата", AppState::motherboardName);

        ImGui::EndTable();
    }

    // Кликабельный вызов нативных свойств Windows
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f); // Слегка уменьшено с 8.0f для ровного баланса границ
    ImGui::PushStyleColor(ImGuiCol_Text, g_ThemeAccent);
    ImGui::PushFont(g_FontSmall);
    ImVec2 linkPos = ImGui::GetCursorScreenPos();
    float wlAvailW = ImGui::GetContentRegionAvail().x;
    if (ImGui::Selectable("Подробнее о системе", false, 0, ImVec2(wlAvailW, 14.0f))) {
        ShellExecuteW(NULL, L"open", L"control", L"/name Microsoft.System", NULL, SW_SHOWNORMAL);
    }
    DrawVectorIconDirect("chevron_right", ImVec2(linkPos.x + wlAvailW - 12.0f, linkPos.y + 1.0f), 10.0f, ImColor(g_ThemeAccent));
    ImGui::PopFont();
    ImGui::PopStyleColor();
    EndFluentCard();

    ImGui::EndGroup();
}
