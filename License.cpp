#include "License.hpp"
#include "AppState.hpp"
#include "resource.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// ============================================================================
// Firebase-проект Pulse (pulse-50386). Публичный Web API key — не секрет, он
// защищён только Firestore Security Rules (см. переписку/документацию проекта):
// чтение любого ключа разрешено всем, а обновление hwid — только если он ещё
// null и меняются исключительно поля hwid/activatedAt. Так что даже если этот
// ключ вытащат из exe, создать новый лицензионный ключ или угнать чужой им
// нельзя — только "занять" ещё не активированный ключ, что и есть штатная
// активация.
// ============================================================================
static const wchar_t* kFirestoreHost = L"firestore.googleapis.com";
static const char* kProjectId = "pulse-50386";
static const char* kApiKey = "AIzaSyD5SJOd8xIKBXnf47nuCPzPLMzPBXwdckg";

namespace {

std::string Sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::string result;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return result;
    if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) == 0) {
        BCryptHashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0);
        BYTE digest[32];
        if (BCryptFinishHash(hHash, digest, sizeof(digest), 0) == 0) {
            char buf[65];
            for (int i = 0; i < 32; i++) sprintf_s(buf + i * 2, 3, "%02x", digest[i]);
            result.assign(buf, 64);
        }
        BCryptDestroyHash(hHash);
    }
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return result;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

// Простейший HTTPS-запрос к firestore.googleapis.com. Возвращает HTTP-статус,
// тело ответа кладёт в outBody. Firestore REST — плоский JSON, поэтому не
// тащим сюда JSON-библиотеку, парсим руками ниже по месту использования.
int HttpsRequest(const wchar_t* method, const std::wstring& path, const std::string& body, std::string& outBody) {
    outBody.clear();
    HINTERNET hSession = WinHttpOpen(L"Pulse-License/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return 0;
    // Таймауты — чтобы при отсутствии сети приложение не зависало надолго.
    WinHttpSetTimeouts(hSession, 5000, 5000, 8000, 8000);

    HINTERNET hConnect = WinHttpConnect(hSession, kFirestoreHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return 0; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method, path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return 0; }

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    BOOL sent = WinHttpSendRequest(hRequest, headers, (DWORD)-1,
        body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);

    int status = 0;
    if (sent && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0, size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        status = (int)statusCode;

        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
            std::vector<char> chunk(avail);
            DWORD read = 0;
            if (WinHttpReadData(hRequest, chunk.data(), avail, &read)) {
                outBody.append(chunk.data(), read);
            } else break;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return status;
}

// Достаёт значение "stringValue" поля fieldName из плоского Firestore JSON-документа.
// Работает только для наших простых документов (без вложенных объектов/массивов).
bool ExtractStringField(const std::string& json, const char* fieldName, std::string& out) {
    std::string needle = "\"" + std::string(fieldName) + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    size_t svPos = json.find("\"stringValue\"", pos);
    size_t nullPos = json.find("\"nullValue\"", pos);
    size_t nextField = json.find("\"fields\"", pos + needle.size()); // не должно попадаться раньше
    if (nullPos != std::string::npos && (svPos == std::string::npos || nullPos < svPos)) return false; // hwid: null
    if (svPos == std::string::npos) return false;
    size_t colon = json.find(':', svPos);
    size_t q1 = json.find('"', colon + 1);
    size_t q2 = json.find('"', q1 + 1);
    if (q1 == std::string::npos || q2 == std::string::npos) return false;
    out = json.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

std::string GetLicenseCachePath() {
    wchar_t appData[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData) != S_OK) return "";
    std::wstring dir = std::wstring(appData) + L"\\Pulse";
    CreateDirectoryW(dir.c_str(), nullptr);
    return WideToUtf8(dir + L"\\license.dat");
}

} // namespace

namespace License {

std::string GetHardwareId() {
    // MachineGuid — стабильный идентификатор установки Windows, переживает
    // переустановку приложения (но не переустановку самой ОС — этого достаточно
    // для привязки "на этом ПК навсегда" в рамках задачи).
    std::string machineGuid;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0,
        KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[64] = {};
        DWORD size = sizeof(buf);
        DWORD type = 0;
        if (RegQueryValueExW(hKey, L"MachineGuid", nullptr, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS) {
            machineGuid = WideToUtf8(buf);
        }
        RegCloseKey(hKey);
    }

    DWORD volSerial = 0;
    GetVolumeInformationW(L"C:\\", nullptr, 0, &volSerial, nullptr, nullptr, nullptr, 0);
    char volBuf[16];
    sprintf_s(volBuf, "%08X", volSerial);

    return Sha256Hex(machineGuid + "|" + std::string(volBuf));
}

// Ключ, сохранённый при прошлой успешной активации на этом ПК (пустая строка,
// если кэша нет). Нужен, чтобы перепроверять его на сервере при каждом запуске.
static std::string GetCachedKey() {
    std::string path = GetLicenseCachePath();
    if (path.empty()) return "";
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::string hwid, key;
    std::getline(f, hwid);
    std::getline(f, key);
    if (hwid != GetHardwareId()) return "";
    return key;
}

static void ClearCachedLicense() {
    std::string path = GetLicenseCachePath();
    if (!path.empty()) DeleteFileW(Utf8ToWide(path).c_str());
}

static void SaveCachedLicense(const std::string& key) {
    std::string path = GetLicenseCachePath();
    if (path.empty()) return;
    std::ofstream f(path, std::ios::trunc);
    if (f.is_open()) {
        f << GetHardwareId() << "\n" << key << "\n";
    }
}

static Status ActivateInternal(const std::string& licenseKeyRaw, std::string& outMessage, bool allowClaim) {
    std::string licenseKey = licenseKeyRaw;
    // Небольшая нормализация ввода: пробелы по краям, верхний регистр.
    while (!licenseKey.empty() && isspace((unsigned char)licenseKey.front())) licenseKey.erase(licenseKey.begin());
    while (!licenseKey.empty() && isspace((unsigned char)licenseKey.back())) licenseKey.pop_back();
    for (auto& c : licenseKey) c = (char)toupper((unsigned char)c);

    if (licenseKey.size() < 8) {
        outMessage = "Неверный формат ключа.";
        return Status::InvalidFormat;
    }

    std::string hwid = GetHardwareId();
    std::wstring docPath = L"/v1/projects/" + Utf8ToWide(kProjectId) + L"/databases/(default)/documents/keys/" + Utf8ToWide(licenseKey);
    std::wstring getPath = docPath + L"?key=" + Utf8ToWide(kApiKey);

    std::string body;
    int status = HttpsRequest(L"GET", getPath, "", body);

    if (status == 0) {
        outMessage = "Нет подключения к серверу активации. Проверьте интернет.";
        return Status::NetworkError;
    }
    if (status == 404) {
        outMessage = "Такой ключ не найден.";
        return Status::NotFound;
    }
    if (status != 200) {
        outMessage = "Сервер активации вернул ошибку (" + std::to_string(status) + ").";
        return Status::NetworkError;
    }

    std::string existingHwid;
    bool hasHwid = ExtractStringField(body, "hwid", existingHwid);

    if (hasHwid) {
        if (existingHwid == hwid) {
            // Уже активирован именно на этом ПК — просто подтверждаем локально.
            SaveCachedLicense(licenseKey);
            outMessage = "Лицензия подтверждена.";
            return Status::Activated;
        }
        outMessage = "Этот ключ уже активирован на другом компьютере.";
        return Status::AlreadyBoundElsewhere;
    }

    // hwid == null означает "ключ сейчас свободен" — либо ещё ни разу не
    // активирован, либо был деактивирован администратором. Фоновая проверка
    // при старте (allowClaim=false) не должна САМА себе его занимать — иначе
    // деактивация ключа никогда не подействует, пока приложение уже когда-то
    // было активировано на этом ПК (баг, пойманный на реальном тесте).
    if (!allowClaim) {
        outMessage = "Ключ деактивирован. Введите ключ заново.";
        return Status::NotFound;
    }

    // hwid ещё null — пытаемся застолбить его за собой через PATCH с updateMask.
    // Правила Firestore разрешают этот PATCH только пока hwid == null, так что при
    // гонке (два ПК одновременно) выиграет только один — второй получит 200, но
    // при повторном GET увидит чужой hwid и остановится на ветке выше при retry.
    std::string nowIso = "2024-01-01T00:00:00Z"; // серверная валидация не требует точного времени
    std::string patchBody = "{\"fields\":{\"hwid\":{\"stringValue\":\"" + hwid + "\"},\"activatedAt\":{\"timestampValue\":\"" + nowIso + "\"}}}";
    std::wstring patchPath = docPath + L"?updateMask.fieldPaths=hwid&updateMask.fieldPaths=activatedAt&key=" + Utf8ToWide(kApiKey);

    std::string patchResp;
    int patchStatus = HttpsRequest(L"PATCH", patchPath, patchBody, patchResp);

    if (patchStatus == 200) {
        SaveCachedLicense(licenseKey);
        outMessage = "Лицензия активирована!";
        return Status::Activated;
    }

    // PATCH мог не пройти из-за гонки (кто-то успел раньше) — перепроверим текущее состояние.
    std::string recheckBody;
    if (HttpsRequest(L"GET", getPath, "", recheckBody) == 200) {
        std::string h2;
        if (ExtractStringField(recheckBody, "hwid", h2)) {
            if (h2 == hwid) {
                SaveCachedLicense(licenseKey);
                outMessage = "Лицензия активирована!";
                return Status::Activated;
            }
            outMessage = "Этот ключ уже активирован на другом компьютере.";
            return Status::AlreadyBoundElsewhere;
        }
    }

    outMessage = "Не удалось активировать ключ (ошибка сервера).";
    return Status::NetworkError;
}

Status Activate(const std::string& licenseKeyRaw, std::string& outMessage) {
    return ActivateInternal(licenseKeyRaw, outMessage, /*allowClaim=*/true);
}

// Только проверяет текущий статус ключа на сервере, не занимая его заново,
// если он вдруг свободен (использует EnsureLicensed при каждом запуске).
static Status VerifyOnly(const std::string& licenseKeyRaw, std::string& outMessage) {
    return ActivateInternal(licenseKeyRaw, outMessage, /*allowClaim=*/false);
}

// ============================================================================
// Простое модальное окно ввода ключа (чистый Win32, без ImGui — нужно ещё до
// инициализации DX11/ImGui бэкенда).
// ============================================================================
namespace {
    std::string g_dialogResultKey;
    bool g_dialogActivated = false;

    constexpr COLORREF kBgColor = RGB(15, 15, 20);
    constexpr COLORREF kFieldBgColor = RGB(26, 26, 34);
    constexpr COLORREF kTextColor = RGB(230, 230, 236);
    constexpr COLORREF kMutedColor = RGB(150, 150, 160);
    constexpr COLORREF kErrorColor = RGB(240, 90, 90);
    constexpr COLORREF kAccentColor = RGB(90, 170, 255);

    HBRUSH g_bgBrush = nullptr;
    HBRUSH g_fieldBrush = nullptr;
    HFONT g_fontTitle = nullptr;
    HFONT g_fontBody = nullptr;
    HFONT g_fontSmall = nullptr;
    bool g_lastStatusIsError = false;

    void CreateDialogAssets() {
        if (!g_bgBrush) g_bgBrush = CreateSolidBrush(kBgColor);
        if (!g_fieldBrush) g_fieldBrush = CreateSolidBrush(kFieldBgColor);
        if (!g_fontTitle) g_fontTitle = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        if (!g_fontBody) g_fontBody = CreateFontW(20, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        if (!g_fontSmall) g_fontSmall = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    }

    INT_PTR CALLBACK LicenseDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
        static HWND hEdit, hStatus, hOk;
        switch (msg) {
        case WM_INITDIALOG: {
            CreateDialogAssets();
            BOOL dark = TRUE;
            DwmSetWindowAttribute(hDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

            HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON),
                IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
            if (hIcon) SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

            // Центрируем окно по экрану — по умолчанию Windows ставит его в (0,0).
            RECT rc; GetWindowRect(hDlg, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
            SetWindowPos(hDlg, nullptr, (sw - w) / 2, (sh - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

            HWND hTitle = CreateWindowExW(0, L"STATIC", L"Активация Pulse", WS_CHILD | WS_VISIBLE,
                28, 22, 340, 28, hDlg, nullptr, nullptr, nullptr);
            SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

            HWND hSub = CreateWindowExW(0, L"STATIC", L"Введите лицензионный ключ, чтобы продолжить", WS_CHILD | WS_VISIBLE,
                28, 54, 340, 20, hDlg, nullptr, nullptr, nullptr);
            SendMessageW(hSub, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
            SetWindowLongPtrW(hSub, GWLP_USERDATA, (LONG_PTR)1); // помечаем как "приглушённый" текст для WM_CTLCOLORSTATIC

            hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_UPPERCASE | ES_CENTER,
                28, 88, 340, 34, hDlg, (HMENU)101, nullptr, nullptr);
            SendMessageW(hEdit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            hStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                28, 128, 340, 34, hDlg, (HMENU)102, nullptr, nullptr);
            SendMessageW(hStatus, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

            hOk = CreateWindowExW(0, L"BUTTON", L"Активировать", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
                28, 172, 165, 36, hDlg, (HMENU)IDOK, nullptr, nullptr);
            HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Выход", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                203, 172, 165, 36, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);
            SendMessageW(hCancel, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            SetFocus(hEdit);
            return FALSE;
        }
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND hCtl = (HWND)lParam;
            bool muted = GetWindowLongPtrW(hCtl, GWLP_USERDATA) == 1;
            SetTextColor(hdc, hCtl == hStatus && g_lastStatusIsError ? kErrorColor : (muted ? kMutedColor : kTextColor));
            SetBkColor(hdc, kBgColor);
            return (INT_PTR)g_bgBrush;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, kTextColor);
            SetBkColor(hdc, kFieldBgColor);
            return (INT_PTR)g_fieldBrush;
        }
        case WM_DRAWITEM: {
            // Кастомная отрисовка кнопки "Активировать" акцентным цветом — обычные
            // BS_PUSHBUTTON в Win32 нельзя перекрасить иначе.
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlID == IDOK) {
                bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                HBRUSH brush = CreateSolidBrush(pressed ? RGB(70, 135, 210) : kAccentColor);
                FillRect(dis->hDC, &dis->rcItem, brush);
                DeleteObject(brush);
                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, RGB(10, 10, 14));
                SelectObject(dis->hDC, g_fontBody);
                DrawTextW(dis->hDC, L"Активировать", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                wchar_t buf[128] = {};
                GetDlgItemTextW(hDlg, 101, buf, 128);
                std::string key = WideToUtf8(buf);
                SetDlgItemTextW(hDlg, 102, L"Проверяем ключ...");
                g_lastStatusIsError = false;
                InvalidateRect(hStatus, nullptr, TRUE);
                UpdateWindow(hStatus);

                std::string msg2;
                Status st = Activate(key, msg2);
                if (st == Status::Activated) {
                    g_dialogResultKey = key;
                    g_dialogActivated = true;
                    EndDialog(hDlg, IDOK);
                } else {
                    g_lastStatusIsError = true;
                    SetDlgItemTextW(hDlg, 102, Utf8ToWide(msg2).c_str());
                    InvalidateRect(hStatus, nullptr, TRUE);
                }
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
        return FALSE;
    }
}

bool EnsureLicensed(HINSTANCE hInstance) {
    // Перепроверяем ключ на сервере при КАЖДОМ запуске, а не просто доверяем
    // локальному кэшу — иначе деактивация ключа админом не действует, пока
    // пользователь сам не удалит файл кэша (баг, на который наткнулись при
    // тестировании). Локальный кэш служит только для офлайн-режима: если
    // сети нет прямо сейчас, доверяем прошлой успешной активации, чтобы не
    // блокировать пользователя из-за временных проблем с интернетом.
    std::string cachedKey = GetCachedKey();
    if (!cachedKey.empty()) {
        std::string msg;
        Status st = VerifyOnly(cachedKey, msg);
        if (st == Status::Activated) return true;
        if (st == Status::NetworkError) return true; // офлайн — доверяем кэшу
        // AlreadyBoundElsewhere / NotFound / InvalidFormat — ключ реально
        // деактивирован или переиспользован в другом месте, кэш недействителен.
        ClearCachedLicense();
    }

    // Простой шаблон диалога, собранный руками (без .rc), чтобы не плодить лишние ресурсы.
    struct {
        DLGTEMPLATE t;
        WORD menu = 0;
        WORD windowClass = 0;
        wchar_t title[16] = L"Pulse";
    } dlg{};
    dlg.t.style = DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dlg.t.dwExtendedStyle = 0;
    dlg.t.cdit = 0;
    dlg.t.x = 0; dlg.t.y = 0; dlg.t.cx = 200; dlg.t.cy = 140;

    INT_PTR result = DialogBoxIndirectParamW(hInstance, (LPCDLGTEMPLATEW)&dlg, nullptr, LicenseDlgProc, 0);
    return result == IDOK && g_dialogActivated;
}

} // namespace License
