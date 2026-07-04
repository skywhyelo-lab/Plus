#pragma once

// Кастомный безрамочный титлбар: лого, плитки профилей, кнопки свернуть/закрыть.
void RenderTitleBarStrip();

// Точный хит-тест по клиентским координатам клика на интерактивные виджеты титлбара —
// см. подробное объяснение гонки состояний в Renderer.hpp (WM_LBUTTONDOWN в main.cpp).
bool IsPointOnTitleBarWidget(int clientX, int clientY);
