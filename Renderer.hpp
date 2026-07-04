#pragma once
#include <windows.h>

// Отправляется из титлбара (кнопка "Свернуть") в WndProc (main.cpp) — просим
// свернуть окно в системный трей вместо обычного сворачивания на панель задач.
#define WM_APP_TRAY_MINIMIZE (WM_APP + 1)

void SetCustomImGuiStyle();
void RenderUI();
void PruneProcessIconCache();

// Точный хит-тест по клиентским координатам клика на интерактивные виджеты титлбара
// (плитки профилей и т.п.). Главное окно ImGui растянуто на весь клиент, поэтому
// io.WantCaptureMouse истинен везде внутри окна и не годится для решения "перехватывать ли
// клик под перетаскивание окна" (WM_LBUTTONDOWN в main.cpp). Хит-тест по геометрии, а не по
// io.WantCaptureMouse/hover-флагу, устраняет гонку: клик может прийти в WndProc раньше, чем
// ImGui успеет отрендерить кадр и пересчитать hover для текущей позиции мыши.
bool IsPointOnTitleBarWidget(int clientX, int clientY);
