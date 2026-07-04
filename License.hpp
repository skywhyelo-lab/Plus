#pragma once
#include <windows.h>
#include <string>

// Простая система лицензий на Firestore (REST, без Cloud Functions — работает
// целиком на бесплатном плане Spark). Ключ привязывается к железу (HWID) один
// раз и навсегда: см. License.cpp за подробностями протокола.
namespace License {
    enum class Status {
        Activated,           // ключ успешно активирован/подтверждён на этом ПК
        AlreadyBoundElsewhere, // ключ уже привязан к другому железу
        NotFound,            // такого ключа нет в базе
        NetworkError,         // не достучались до Firestore
        InvalidFormat
    };

    // Уникальный отпечаток этого ПК (MachineGuid + серийник системного диска, SHA-256, hex).
    std::string GetHardwareId();

    // Пытается активировать/проверить ключ через Firestore. При успехе кэширует результат локально.
    Status Activate(const std::string& licenseKey, std::string& outMessage);

    // Показывает блокирующее Win32-окно ввода ключа, пока не будет активирован валидный
    // ключ (или пользователь не закроет приложение). Возвращает false, если пользователь
    // отменил ввод (приложение должно завершиться).
    bool EnsureLicensed(HINSTANCE hInstance);
}
