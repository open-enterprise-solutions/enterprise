# 12. Харденинг (Security Hardening)

> Безопасность C++ desktop-приложения OES: подписание кода, защищённый канал обновлений, предотвращение DLL hijacking, ASLR/DEP, безопасное программирование на C++.

---

## Подписание кода (Code Signing)

### Почему это важно для OES

```
Без подписания кода:
  — Windows SmartScreen блокирует инсталлятор ("Unknown Publisher")
  — Антивирусы срабатывают ложно на неподписанные бинарники
  — Пользователи видят страшное предупреждение UAC
  — Невозможно проверить целостность после загрузки

С подписанием:
  — Инсталлятор устанавливается без предупреждений
  — Пользователи видят имя "Tetracode" в диалоге UAC
  — Антивирусы доверяют подписанным файлам
  — Возможна проверка цепочки доверия
```

### Получение и использование сертификата

```
Поставщики EV Code Signing сертификатов:
  — Sectigo (бывший Comodo)
  — DigiCert
  — GlobalSign

Стоимость: ~$300-500/год (EV) или ~$100-200/год (OV)
EV рекомендуется: сразу получает высокий рейтинг у Microsoft SmartScreen
```

### Подписание в CI/CD (GitHub Actions)

```yaml
# Сохранить сертификат как base64 в GitHub Secrets
# В терминале: certutil -encode codesign.pfx codesign_b64.txt

      - name: Sign binaries
        run: |
          # Восстановить сертификат
          $pfxBytes = [System.Convert]::FromBase64String("${{ secrets.CODE_SIGN_CERT }}")
          [System.IO.File]::WriteAllBytes("$env:TEMP\cert.pfx", $pfxBytes)
          
          # Подписать все .exe и .dll
          $files = Get-ChildItem -Path .\bin\x64\Release -Include "*.exe","*.dll" -Recurse
          foreach ($f in $files) {
              signtool sign `
                  /f "$env:TEMP\cert.pfx" `
                  /p "${{ secrets.CODE_SIGN_PASSWORD }}" `
                  /tr http://timestamp.sectigo.com `
                  /td sha256 `
                  /fd sha256 `
                  $f.FullName
              Write-Host "Signed: $($f.Name)"
          }
          
          # Подписать инсталлятор
          signtool sign `
              /f "$env:TEMP\cert.pfx" `
              /p "${{ secrets.CODE_SIGN_PASSWORD }}" `
              /tr http://timestamp.sectigo.com `
              /td sha256 `
              /fd sha256 `
              .\dist\OES-Setup.exe
          
          # Удалить сертификат из диска
          Remove-Item "$env:TEMP\cert.pfx"
```

### Проверка подписи

```powershell
# Проверить подпись файла
Get-AuthenticodeSignature ".\OES-Setup.exe" | Format-List

# Или через signtool
signtool verify /pa /v ".\OES-Setup.exe"

# Ожидаемый вывод:
# SignerCertificate: Tetracode Dev
# Status: Valid
# TimeStamperCertificate: Sectigo RSA Time Stamping CA
```

---

## Защита от DLL Hijacking

### Что это и почему важно для wxWidgets-приложений

```
DLL Hijacking: злоумышленник подкладывает вредоносную DLL в директорию
рядом с приложением. Windows загружает её вместо системной.

Особенно актуально для OES потому что:
  — wxWidgets и Firebird client поставляются как DLL рядом с .exe
  — Инсталлятор копирует DLL в C:\Program Files\OES\
  — Если директория доступна на запись — уязвимость
```

### Меры защиты

```cpp
// 1. Устанавливать безопасный DLL search order в WinMain
// До любых LoadLibrary() вызовов!

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmd, int nCmdShow) {
    // Отключить текущую директорию из DLL search path
    // (только в Win8+, но всё равно полезно)
    SetDllDirectoryW(L"");
    
    // Загружать только из System32
    // SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    // ОСТОРОЖНО: может сломать загрузку wxWidgets/Firebird DLL
    // Использовать LOAD_LIBRARY_SEARCH_DEFAULT_DIRS если нужны локальные DLL
    SetDefaultDllDirectories(
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
        LOAD_LIBRARY_SEARCH_SYSTEM32
    );
    
    // Дальше инициализация wxWidgets
    // ...
}
```

```nsis
; NSIS: установить правильные права на директорию установки
; Program Files защищены Windows — стандартные пути безопасны
; НЕ устанавливать в %APPDATA% или %TEMP%

Section "Main"
    SetOutPath "$PROGRAMFILES64\OES"
    ; Только SYSTEM и Administrators имеют права записи
    ; Обычные пользователи — только чтение
SectionEnd
```

### Проверка уязвимости к DLL Hijacking

```powershell
# Procmon (Sysinternals) — проверить загрузку DLL
# Фильтр: Process Name = "oes.exe" AND Operation = "CreateFile" AND Result = "NAME NOT FOUND"
# Если DLL ищется в директории, доступной на запись — уязвимость

# Автоматическая проверка через Robber или similar tools:
# https://github.com/MojtabaTajik/Robber
```

---

## ASLR, DEP и другие защитные механизмы

### MSBuild: включить защиту

```xml
<!-- В .vcxproj или через Project Properties -->
<PropertyGroup>
  <!-- DEP (Data Execution Prevention) / NX bit -->
  <EnableDEP>true</EnableDEP>
  
  <!-- ASLR (Address Space Layout Randomization) -->
  <RandomizedBaseAddress>true</RandomizedBaseAddress>
  
  <!-- SafeSEH — защита обработчиков исключений (x86 only) -->
  <SafeSEH>true</SafeSEH>
  
  <!-- Control Flow Guard (CFG) — защита от ROP-атак -->
  <ControlFlowGuard>Guard</ControlFlowGuard>
  
  <!-- Отключить инкрементальную линковку в Release
       (инкрементальная линковка ослабляет ASLR) -->
  <LinkIncremental>false</LinkIncremental>
</PropertyGroup>

<ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
  <Link>
    <!-- /NXCOMPAT — включить DEP -->
    <DataExecutionPrevention>true</DataExecutionPrevention>
    <!-- /DYNAMICBASE — включить ASLR -->
    <RandomizedBaseAddress>true</RandomizedBaseAddress>
    <!-- /HIGHENTROPYVA — High Entropy ASLR (64-bit) -->
    <HighEntropyVA>true</HighEntropyVA>
    <!-- /GUARD:CF — Control Flow Guard -->
    <CfGuard>true</CfGuard>
  </Link>
</ItemDefinitionGroup>
```

### Проверка включённых защит

```powershell
# dumpbin (из Visual Studio)
# Флаги защиты отображаются как строки в разделе DLL characteristics
dumpbin /headers oes.exe | findstr /i "NX compatible"
dumpbin /headers oes.exe | findstr /i "dynamic base"
dumpbin /headers oes.exe | findstr /i "Guard"

# Или PowerShell через Get-PEHeader (утилита):
# https://github.com/mattifestation/PEAnalysis

# Ожидаемый вывод в dumpbin /headers:
#                    NX compatible         - compatible with data execution prevention
#                    Dynamic base          - DLL can move (ASLR)
#                    Guard                 - Control Flow Guard
```

---

## Защищённый канал обновлений

### Принципы безопасных обновлений OES

```
1. Загрузка только по HTTPS
2. Проверка подписи обновления перед установкой
3. Проверка SHA256 хэша
4. Версионирование: не откатываться на старые версии без явного подтверждения
5. Обновления только от официального сервера (pinning)
```

### Реализация проверки обновлений

```cpp
// src/updater.cpp
#include <wx/http.h>
#include <wx/protocol/http.h>
#include <openssl/sha.h>

struct UpdateInfo {
    wxString version;
    wxString downloadUrl;
    wxString sha256Hash;
    wxString signature;       // Подпись метаданных
};

class SecureUpdater {
public:
    // Проверить наличие обновлений
    // URL: https://updates.example.com/oes/latest.json
    static bool CheckForUpdate(UpdateInfo& info) {
        wxHTTP http;
        http.SetHeader(wxT("User-Agent"), wxT("OES/" OES_VERSION_STRING));
        
        // ТОЛЬКО HTTPS
        // В production: использовать WinHTTP или libcurl с проверкой сертификата
        // wxHTTP не проверяет SSL — использовать WinHTTP API напрямую
        
        // ... получить latest.json
        // Пример содержимого:
        // {
        //   "version": "1.5.0",
        //   "url": "https://github.com/org/oes/releases/download/v1.5.0/OES-1.5.0-Setup.exe",
        //   "sha256": "abc123...",
        //   "min_version": "1.0.0"
        // }
        
        return true;
    }
    
    // Проверить SHA256 скачанного файла
    static bool VerifyFileHash(const wxString& filePath, const wxString& expectedHash) {
        wxFile file(filePath);
        if (!file.IsOpened()) return false;
        
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        
        unsigned char buffer[65536];
        ssize_t bytesRead;
        while ((bytesRead = file.Read(buffer, sizeof(buffer))) > 0) {
            SHA256_Update(&sha256, buffer, bytesRead);
        }
        
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_Final(hash, &sha256);
        
        // Преобразовать в hex
        wxString actualHash;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            actualHash += wxString::Format(wxT("%02x"), hash[i]);
        }
        
        return actualHash.IsSameAs(expectedHash, false);  // case-insensitive
    }
    
    // Проверить подпись перед запуском инсталлятора
    static bool VerifyInstallerSignature(const wxString& filePath) {
        // Windows: использовать WinVerifyTrust API
        WINTRUST_FILE_INFO fileInfo = {};
        fileInfo.cbStruct = sizeof(fileInfo);
        fileInfo.pcwszFilePath = filePath.wc_str();
        
        WINTRUST_DATA trustData = {};
        trustData.cbStruct = sizeof(trustData);
        trustData.dwUIChoice = WTD_UI_NONE;
        trustData.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
        trustData.dwUnionChoice = WTD_CHOICE_FILE;
        trustData.pFile = &fileInfo;
        trustData.dwStateAction = WTD_STATEACTION_VERIFY;
        
        GUID guidAction = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        LONG result = WinVerifyTrust(NULL, &guidAction, &trustData);
        
        // Освободить ресурсы
        trustData.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(NULL, &guidAction, &trustData);
        
        return result == ERROR_SUCCESS;
    }
};
```

---

## Безопасное хранение данных

### Лицензионные ключи

```cpp
// Хранить в Windows Credential Manager, НЕ в реестре или файле открытым текстом

#include <wincred.h>
#pragma comment(lib, "Credui.lib")

class LicenseStorage {
public:
    static bool SaveLicenseKey(const wxString& key) {
        std::wstring targetName = L"OES_License_Key";
        std::wstring keyW = key.ToStdWstring();
        
        CREDENTIALW cred = {};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = const_cast<LPWSTR>(targetName.c_str());
        cred.CredentialBlobSize = (DWORD)(keyW.size() * sizeof(wchar_t));
        cred.CredentialBlob = (LPBYTE)keyW.c_str();
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
        
        return CredWriteW(&cred, 0) != FALSE;
    }
    
    static wxString LoadLicenseKey() {
        PCREDENTIALW pCred = nullptr;
        if (CredReadW(L"OES_License_Key", CRED_TYPE_GENERIC, 0, &pCred)) {
            wxString key(
                reinterpret_cast<wchar_t*>(pCred->CredentialBlob),
                pCred->CredentialBlobSize / sizeof(wchar_t)
            );
            CredFree(pCred);
            return key;
        }
        return wxEmptyString;
    }
};
```

### Пароли подключения к БД

```cpp
// Пароли к PostgreSQL/MySQL хранить зашифрованными
// Использовать Windows DPAPI (Data Protection API)

#include <wincrypt.h>
#pragma comment(lib, "Crypt32.lib")

class SecureConfig {
public:
    // Зашифровать строку через DPAPI (привязывается к текущему пользователю)
    static wxString EncryptString(const wxString& plainText) {
        std::string utf8 = plainText.ToUTF8().data();
        
        DATA_BLOB input;
        input.pbData = (BYTE*)utf8.c_str();
        input.cbData = (DWORD)utf8.size();
        
        DATA_BLOB output;
        if (CryptProtectData(&input, L"OES_Config", nullptr, nullptr, nullptr, 0, &output)) {
            // Encode to base64 for storage
            DWORD b64Len = 0;
            CryptBinaryToStringW(output.pbData, output.cbData, CRYPT_STRING_BASE64, nullptr, &b64Len);
            std::wstring b64(b64Len, L'\0');
            CryptBinaryToStringW(output.pbData, output.cbData, CRYPT_STRING_BASE64, &b64[0], &b64Len);
            LocalFree(output.pbData);
            return wxString(b64);
        }
        return wxEmptyString;
    }
    
    static wxString DecryptString(const wxString& encrypted) {
        std::wstring b64 = encrypted.ToStdWstring();
        
        DWORD binLen = 0;
        CryptStringToBinaryW(b64.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &binLen, nullptr, nullptr);
        std::vector<BYTE> bin(binLen);
        CryptStringToBinaryW(b64.c_str(), 0, CRYPT_STRING_BASE64, bin.data(), &binLen, nullptr, nullptr);
        
        DATA_BLOB input = { binLen, bin.data() };
        DATA_BLOB output;
        
        if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output)) {
            wxString result = wxString::FromUTF8((char*)output.pbData, output.cbData);
            LocalFree(output.pbData);
            return result;
        }
        return wxEmptyString;
    }
};
```

---

## Безопасное программирование на C++

### Правила для OES кодовой базы

```cpp
// === 1. НИКОГДА не использовать небезопасные функции ===

// Плохо:
char buf[256];
strcpy(buf, userInput);     // нет проверки размера
sprintf(buf, userInput);    // format string attack
gets(buf);                  // нет проверки размера

// Хорошо:
wxString safe = wxString::FromUTF8(userInput);  // wxWidgets управляет памятью
// Или если нужен char*:
strncpy_s(buf, sizeof(buf), userInput, _TRUNCATE);
snprintf(buf, sizeof(buf), "%s", userInput);

// === 2. SQL Injection — параметризованные запросы ===

// Плохо:
wxString query = "SELECT * FROM users WHERE name = '" + userName + "'";

// Хорошо (Firebird):
wxString query = "SELECT * FROM users WHERE name = ?";
// Передать userName как параметр через IBPP или IDBC

// === 3. Проверка входных данных ===
bool ValidateUserInput(const wxString& input, size_t maxLen = 1024) {
    if (input.IsEmpty() || input.Len() > maxLen) return false;
    // Проверить допустимые символы для конкретного поля
    return true;
}

// === 4. Не хранить чувствительные данные в plain text ===
// Использовать SecureConfig::EncryptString() (см. выше)
// Обнулять строки после использования:
void SecureClear(std::string& s) {
    if (!s.empty()) {
        SecureZeroMemory(&s[0], s.size());
        s.clear();
    }
}
```

### Предотвращение утечек памяти

```cpp
// Использовать RAII и умные указатели
#include <memory>

// Плохо:
Widget* w = new Widget();
// ... если исключение — утечка!

// Хорошо:
auto w = std::make_unique<Widget>();
// Автоматически освобождается

// Для wxWidgets объектов — они управляются фреймворком
// wxWindow::Destroy() вместо delete для окон
```

---

## Харденинг сервера (daemon-режим)

### Запуск daemon с минимальными правами (Linux)

```bash
# Создать отдельного пользователя для OES daemon
sudo useradd --system --no-create-home --shell /bin/false oes

# Права на директории
sudo mkdir -p /var/lib/oes /var/log/oes /etc/oes
sudo chown oes:oes /var/lib/oes /var/log/oes
sudo chmod 750 /var/lib/oes /var/log/oes
sudo chmod 755 /etc/oes  # Конфигурация читается всеми, пишется root

# Firebird БД — только oes может читать/писать
sudo chmod 600 /var/lib/oes/data/oes.fdb
sudo chown oes:oes /var/lib/oes/data/oes.fdb
```

### Windows: запуск сервиса с минимальными правами

```powershell
# Создать учётную запись сервиса (Managed Service Account)
New-ADServiceAccount -Name "OESDaemon" -Enabled $true

# Или использовать встроенный аккаунт "Network Service" / "Local Service"
# вместо LocalSystem (LocalSystem = все права = плохо)

sc.exe config OESDaemon obj= "NT SERVICE\OESDaemon" password= ""

# Дать только необходимые права через Group Policy или secedit
# SeServiceLogonRight — право запускаться как сервис
```

### Файрвол для daemon-порта

```bash
# Linux (ufw): разрешить только необходимые порты
sudo ufw default deny incoming
sudo ufw default allow outgoing

# OES daemon порт (например 4001)
sudo ufw allow from 192.168.0.0/16 to any port 4001  # только LAN
sudo ufw deny 4001  # заблокировать внешний доступ

sudo ufw enable
```

```bash
# macOS (pf): ограничить порт OES daemon
# Добавить в /etc/pf.conf:
# block in on en0 proto tcp to any port 4001
# pass in on en0 proto tcp from 192.168.0.0/16 to any port 4001
sudo pfctl -f /etc/pf.conf
sudo pfctl -e
```

```cmd
REM Windows (netsh): разрешить только из локальной сети
netsh advfirewall firewall add rule ^
  name="OES Daemon" protocol=TCP dir=in localport=4001 ^
  remoteip=192.168.0.0/16 action=allow
netsh advfirewall firewall add rule ^
  name="OES Daemon Block" protocol=TCP dir=in localport=4001 ^
  action=block
```

---

## Сканирование уязвимостей

### cppcheck — статический анализ

```bash
# Установка:
# Windows:  choco install cppcheck
# macOS:    brew install cppcheck
# Linux:    sudo apt install -y cppcheck

# Локальный запуск (все платформы)
cppcheck \
    --enable=all \
    --suppress=missingIncludeSystem \
    --suppress=unmatchedSuppression \
    --error-exitcode=1 \
    --std=c++17 \
    --xml \
    --xml-version=2 \
    src/engine/ 2> cppcheck-report.xml

# HTML отчёт
cppcheck-htmlreport \
    --file=cppcheck-report.xml \
    --report-dir=cppcheck-html \
    --source-dir=.
```

### Проверка зависимостей на уязвимости

```bash
# Если используется vcpkg
vcpkg audit

# Для C++ проектов: osv-scanner — сканирование зависимостей на CVE
# (dotnet list package --vulnerable не применимо к C++ проектам)
# Установить: https://google.github.io/osv-scanner/
osv-scanner --lockfile vcpkg.json .

# OWASP Dependency Check для C++ (через сканирование бинарников)
dependency-check \
    --project "OES" \
    --scan ./bin/x64/Release/ \
    --format HTML \
    --out reports/
```

---

## Чеклист харденинга OES

```
Подписание кода:
  [ ] EV Code Signing сертификат получен
  [ ] Все .exe и .dll подписываются в CI/CD pipeline
  [ ] Инсталлятор NSIS/WiX подписан
  [ ] Timestamp добавлен к подписи (для долгосрочной валидности)
  [ ] Подпись проверяется перед установкой обновлений

Защита бинарников:
  [ ] ASLR включён (/DYNAMICBASE + /HIGHENTROPYVA)
  [ ] DEP включён (/NXCOMPAT)
  [ ] Control Flow Guard включён (/GUARD:CF)
  [ ] SafeSEH включён (для x86 сборок)
  [ ] DLL search order защищён (SetDefaultDllDirectories)

Обновления:
  [ ] Обновления загружаются только по HTTPS
  [ ] SHA256 хэш проверяется перед установкой
  [ ] Подпись инсталлятора проверяется (WinVerifyTrust)

Хранение данных:
  [ ] Лицензионные ключи в Windows Credential Manager
  [ ] Пароли БД зашифрованы через DPAPI
  [ ] Нет паролей в реестре в открытом виде

Безопасное программирование:
  [ ] cppcheck в CI/CD pipeline (без warnings)
  [ ] Нет strcpy/gets/sprintf без проверки размера
  [ ] Параметризованные запросы к БД
  [ ] Входные данные валидируются

Daemon/Server:
  [ ] Запуск от непривилегированного пользователя
  [ ] Минимально необходимые права
  [ ] Файрвол: только необходимые порты
  [ ] Конфигурационные файлы с паролями: chmod 600
```
