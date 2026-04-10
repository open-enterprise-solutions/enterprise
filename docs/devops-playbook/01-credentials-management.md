# 01. Управление учётными данными — OES

> Самый важный документ. Утечка секрета = компрометация установок у клиентов и лицензионной инфраструктуры.

---

## Типы секретов в OES

### Ключи подключения к базам данных
- **Firebird embedded** — мастер-пароль базы (`SYSDBA`), пароль базы приложения
- **Firebird server** — хост, порт, пользователь, пароль (для серверного режима)
- **PostgreSQL** — хост, порт, пользователь, пароль приложения
- **SQLite** — путь к файлу (не пароль, но файл должен быть защищён правами)
- **MySQL / ODBC** — строки подключения, пароли пользователей

### Лицензионные ключи
- **Серийный номер OES** — лицензионный ключ конечного пользователя
- **Приватный ключ подписи лицензий** — RSA/ECDSA ключ для генерации лицензий на стороне вендора
- **Публичный ключ проверки лицензий** — встроен в приложение (не секрет, но важен для целостности)
- **Токен License Server API** — для валидации лицензий в онлайн-режиме

### Ключи сборки и дистрибуции
- **Code Signing Certificate** — сертификат Authenticode для подписи EXE/MSI/NSIS-инсталлятора
- **Приватный ключ Code Signing** — хранить в HSM или зашифрованном хранилище
- **Пароль PFX/P12** — для экспорта/импорта Code Signing сертификата
- **GitHub Deploy Keys** — доступ к репозиторию с build-сервера (read-only)
- **GitHub Actions Secrets** — CI/CD секреты для сборки и публикации

### Ключи серверной инфраструктуры (daemon-режим)
- **SSH ключи** — доступ к build-серверу и серверам развёртывания OES daemon
- **Cloudflare API Token** — управление DNS для update/license серверов
- **Telegram Bot Token** — уведомления о сборках и ошибках (опционально)

### Конфигурационные секреты приложения
- **Мастер-ключ шифрования конфигурации** — для шифрования файла `oes.conf` или реестра
- **SMTP пароли** — если OES отправляет email-уведомления из daemon-режима
- **S3/MinIO ключи** — для хранения бэкапов баз данных

---

## Инструменты хранения

| Инструмент | Для чего | Бесплатный |
|-----------|---------|-----------|
| 1Password Teams | Командное хранение паролей, ключей, сертификатов | Нет ($4/мес/чел) |
| Bitwarden | Альтернатива 1Password, self-hosted | Да (self-host) |
| HashiCorp Vault | Программное хранение, API доступ (build-сервер) | Да (open-source) |
| GitHub Secrets | CI/CD секреты (Code Signing, API токены) | Да |
| SOPS + age | Шифрование файлов конфигурации в git | Да |
| Windows Credential Manager | Локальное хранение на machine пользователя | Да (встроен) |
| DPAPI (Windows) | Шифрование секретов приложением на уровне ОС | Да (встроен) |
| pass (GPG) | Личные пароли девопса | Да |

### Когда что использовать

```
Командные пароли, DB-пароли, лицензионные ключи  → 1Password / Bitwarden
CI/CD секреты (Code Signing, API токены)          → GitHub Secrets
Конфигурация build-сервера в репозитории          → SOPS + age
Секреты runtime на сервере (daemon-режим)         → HashiCorp Vault / переменные окружения
Хранение паролей на машине пользователя           → Windows Credential Manager / DPAPI
Личные пароли девопса                             → pass (GPG)
```

---

## Процессы

### Как выдать доступ новому сотруднику

```
1. Создать аккаунт в 1Password / Bitwarden
2. Добавить в нужные vault/коллекции (dev — НЕ production, НЕ лицензионные ключи)
3. Сгенерировать персональный SSH ключ:
   ssh-keygen -t ed25519 -C "имя@oes-team"
4. Добавить публичный ключ на build-сервер:
   ssh-copy-id -i ~/.ssh/id_ed25519.pub build-user@build-server
5. Добавить в GitHub организацию с нужными правами
6. Выдать доступ к Cloudflare (если нужно) с минимальными правами
7. НЕ выдавать доступ к приватному ключу подписи лицензий (только CI/CD)
```

**НИКОГДА:**
- Не передавать лицензионные ключи клиентов через мессенджеры
- Не хранить Code Signing сертификат на локальных машинах разработчиков
- Не коммитить строки подключения к БД в исходный код
- Не использовать общий SSH ключ на всю команду

### Как передать SSH ключ

```bash
# Публичный ключ — можно передать открыто (Slack, email, GitHub)
cat ~/.ssh/id_ed25519.pub
# ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAA... user@machine

# Приватный ключ — НИКОГДА НЕ ПЕРЕДАВАТЬ
# Каждый генерирует свой. Точка.
```

Если нужен deploy key для build-сервера:
```bash
# На build-сервере
ssh-keygen -t ed25519 -C "deploy@oes-build" -f ~/.ssh/deploy_key -N ""
# Добавить публичный ключ в GitHub → Settings → Deploy Keys (read-only)
cat ~/.ssh/deploy_key.pub
```

### Как ротировать пароль базы данных

#### Firebird (embedded / server)

```bash
# 1. Сгенерировать новый пароль
NEW_PASS=$(openssl rand -base64 24 | tr -dc 'a-zA-Z0-9' | head -c 24)
echo "Новый пароль: $NEW_PASS"
# Сохранить в 1Password/Bitwarden

# 2. Сменить пароль пользователя Firebird (server-режим)
# Через isql-fb или утилиту gsec:
gsec -user SYSDBA -pass masterkey -mo APP_USER -pw "$NEW_PASS"

# 3. Для embedded Firebird — пароль хранится в зашифрованном конфиге OES
# Обновить в файле конфигурации (см. шаблон ниже)

# 4. Обновить секрет в 1Password и GitHub Secrets
# 5. Перезапустить OES daemon (если запущен как сервис)
sc stop OESDaemon && sc start OESDaemon
# или на Linux:
sudo systemctl restart oes-daemon
```

#### PostgreSQL (если используется как backend)

```bash
# 1. Сгенерировать новый пароль
NEW_PASS=$(openssl rand -base64 32)

# 2. Сменить пароль в PostgreSQL
sudo -u postgres psql -c "ALTER USER oes_user WITH PASSWORD '$NEW_PASS';"

# 3. Обновить строку подключения в конфигурации OES
# C:\ProgramData\OES\oes.conf (Windows)
# /etc/oes/oes.conf (Linux daemon)

# 4. Перезапустить OES daemon
sc stop OESDaemon && sc start OESDaemon

# 5. Обновить зашифрованный конфиг в репозитории (если используете SOPS)
sops --encrypt oes.conf.production > oes.conf.production.enc
git add oes.conf.production.enc && git commit -m "rotate db password"
```

### Чеклист при увольнении сотрудника

```
[ ] Удалить SSH ключ с build-сервера и всех серверов daemon
    grep -r "user@machine" /home/*/.ssh/authorized_keys
    sed -i '/user@machine/d' /home/build-user/.ssh/authorized_keys

[ ] Удалить из GitHub организации
[ ] Удалить из 1Password / Bitwarden
[ ] Удалить из Cloudflare
[ ] Ротировать ВСЕ секреты, к которым был доступ
    — Пароли БД (Firebird, PostgreSQL, MySQL)
    — API токены (License Server, Cloudflare)
    — Пароль Code Signing контейнера (если был доступ)
[ ] Если был доступ к лицензионному приватному ключу — немедленно перевыпустить
[ ] Удалить из Telegram-групп (мониторинг, алерты)
[ ] Удалить с VPN (если есть)
[ ] Проверить логи за последний день
[ ] Задокументировать дату и что было отозвано
```

### Где хранить конфигурацию OES на рабочих машинах

```
Windows (рекомендуется):
  C:\ProgramData\OES\oes.conf      — конфиг сервиса/daemon (права SYSTEM)
  C:\ProgramData\OES\license.key   — лицензионный ключ (права Administrators)
  HKLM\Software\OES\               — реестр (для некритичных настроек)

macOS (Desktop):
  ~/Library/Application Support/OES/oes.conf   — конфиг десктопного приложения
  ~/Library/Application Support/OES/license.key
  /etc/oes/oes.conf                             — конфиг daemon (chmod 600)
  /var/lib/oes/                                 — данные daemon
  /var/log/oes/                                 — логи daemon

Linux daemon:
  /etc/oes/oes.conf                — конфиг daemon (chmod 600, владелец oes)
  /var/lib/oes/                    — данные приложения
  /var/log/oes/                    — логи

Права доступа:
  — oes.conf должен быть доступен ТОЛЬКО процессу OES
  — НЕ в публичных директориях
  — НЕ в git
```

```cmd
REM Windows — установить права только для SYSTEM и Administrators
icacls "C:\ProgramData\OES\oes.conf" /inheritance:r
icacls "C:\ProgramData\OES\oes.conf" /grant SYSTEM:(F)
icacls "C:\ProgramData\OES\oes.conf" /grant Administrators:(F)
```

```bash
# Linux daemon
sudo chown oes:oes /etc/oes/oes.conf
sudo chmod 600 /etc/oes/oes.conf
```

```bash
# macOS daemon
sudo chown oes:oes /etc/oes/oes.conf
sudo chmod 600 /etc/oes/oes.conf

# macOS desktop (пользовательский конфиг)
chmod 600 ~/Library/Application\ Support/OES/oes.conf
```

### Шаблон oes.conf.example (конфигурация без значений)

```ini
; === OES Configuration Template ===
; Скопировать в oes.conf и заполнить значениями
; НИКОГДА не коммитить oes.conf с реальными данными

[Application]
Mode=                       ; desktop | daemon | service
LicenseKey=                 ; Лицензионный ключ (для встроенной проверки)
LicenseServerURL=           ; https://license.oes-vendor.com/api/v1/validate

[Database.Primary]
Engine=                     ; firebird | postgresql | sqlite | mysql
Host=                       ; localhost (или IP для сервера)
Port=                       ; 3050 (Firebird) | 5432 (PostgreSQL) | 3306 (MySQL)
Database=                   ; Путь к файлу .fdb или имя БД
User=                       ; Пользователь БД
Password=                   ; СГЕНЕРИРОВАННЫЙ_ПАРОЛЬ — хранить в 1Password

[Database.Firebird]
EmbeddedMode=               ; true | false
MasterPassword=             ; Мастер-пароль Firebird SYSDBA

[Daemon]
ListenHost=                 ; 127.0.0.1 (только локально) | 0.0.0.0
ListenPort=                 ; 8765 (порт HTTP API daemon-режима)
TLSCert=                    ; Путь к TLS сертификату
TLSKey=                     ; Путь к TLS приватному ключу

[Updates]
UpdateServerURL=            ; https://updates.oes-vendor.com/
UpdateCheckToken=           ; Токен для аутентифицированных обновлений

[Notifications]
SMTPHost=                   ; smtp.example.com
SMTPPort=                   ; 587
SMTPUser=                   ;
SMTPPassword=               ;
TelegramBotToken=           ; Для уведомлений в daemon-режиме
TelegramChatID=             ;

[Backup]
S3Endpoint=                 ; https://s3.amazonaws.com (опционально)
S3Bucket=                   ;
S3AccessKey=                ;
S3SecretKey=                ;
```

### Emergency: утечка секрета

**Если секрет попал в публичный доступ (git, скриншот, лог):**

```
ШАГ 1: НЕМЕДЛЕННО ротировать скомпрометированный секрет
        — Сменить пароль БД / отозвать API токен / пересоздать ключ
        — Если скомпрометирован Code Signing сертификат — НЕМЕДЛЕННО
          связаться с CA для отзыва и перевыпуска
        — Если скомпрометирован приватный ключ лицензий — пересоздать ключевую
          пару и выпустить обновление для всех клиентов

ШАГ 2: Обновить секрет везде где он используется
        — oes.conf на всех серверах
        — GitHub Secrets
        — 1Password / Bitwarden
        — CI/CD пайплайны

ШАГ 3: Проверить логи на несанкционированный доступ
        — Логи Firebird / PostgreSQL — необычные подключения
        — Логи License Server — массовые активации
        — Логи OES daemon: /var/log/oes/daemon.log

ШАГ 4: Удалить секрет из истории git (если попал в коммит)
        # Использовать BFG Repo-Cleaner:
        bfg --delete-files oes.conf
        git reflog expire --expire=now --all && git gc --prune=now --aggressive
        git push --force --all
        # Уведомить всех членов команды о принудительном push

ШАГ 5: Задокументировать инцидент
        — Что утекло (пароль БД, лицензионный ключ, Code Signing?)
        — Когда обнаружено
        — Возможные затронутые клиенты
        — Какие действия предприняты
        — Меры для предотвращения повторения
```

---

## SOPS + age (шифрование конфигурации в git)

### Установка

```bash
# macOS
brew install sops age

# Ubuntu (build-сервер)
sudo apt install age
SOPS_VERSION=3.8.1
curl -LO https://github.com/getsops/sops/releases/download/v${SOPS_VERSION}/sops-v${SOPS_VERSION}.linux.amd64
sudo mv sops-v${SOPS_VERSION}.linux.amd64 /usr/local/bin/sops
sudo chmod +x /usr/local/bin/sops

# Windows (build-машина)
choco install sops
# или скачать бинарник с GitHub Releases
```

### Генерация ключа

```bash
# Создать ключ age
age-keygen -o key.txt
# Public key: age1ql3z7hjy54pw3hyww5ayyfg7zqgvc7w3j2elw8zmrj2kg5sfn9aqmcac8p

# Сохранить ключ в 1Password!
cat key.txt

# На build-сервере — положить ключ:
mkdir -p /root/.config/sops/age/
nano /root/.config/sops/age/keys.txt
chmod 600 /root/.config/sops/age/keys.txt
```

### Конфигурация .sops.yaml

```yaml
# .sops.yaml в корне репозитория
creation_rules:
  - path_regex: oes\.conf\.production\.enc$
    age: >-
      age1ql3z7hjy54pw3hyww5ayyfg7zqgvc7w3j2elw8zmrj2kg5sfn9aqmcac8p
  - path_regex: oes\.conf\.staging\.enc$
    age: >-
      age1ql3z7hjy54pw3hyww5ayyfg7zqgvc7w3j2elw8zmrj2kg5sfn9aqmcac8p
```

### Шифрование / расшифровка

```bash
# Шифрование конфигурационного файла
sops --encrypt oes.conf.production > oes.conf.production.enc

# Расшифровка
export SOPS_AGE_KEY_FILE=/root/.config/sops/age/keys.txt
sops --decrypt oes.conf.production.enc > oes.conf.production

# Редактирование зашифрованного файла (откроет в $EDITOR)
sops oes.conf.production.enc

# Коммитим зашифрованный файл (безопасно хранить в git)
git add oes.conf.production.enc .sops.yaml
git commit -m "update encrypted production config"
```

### Использование в CI/CD (GitHub Actions)

```yaml
# В GitHub Secrets: SOPS_AGE_KEY (содержимое key.txt)
- name: Decrypt OES config
  run: |
    mkdir -p /root/.config/sops/age/
    echo "${{ secrets.SOPS_AGE_KEY }}" > /root/.config/sops/age/keys.txt
    sops --decrypt oes.conf.production.enc > oes.conf.production
```

---

## Code Signing (подпись инсталлятора)

### Безопасное хранение сертификата в CI/CD

```bash
# Никогда не хранить PFX-файл в репозитории!
# GitHub Secrets: CODE_SIGN_PFX (base64 от .pfx файла)
# GitHub Secrets: CODE_SIGN_PASSWORD (пароль от .pfx)
```

```yaml
# .github/workflows/release.yml
- name: Sign installer
  env:
    CODE_SIGN_PFX: ${{ secrets.CODE_SIGN_PFX }}
    CODE_SIGN_PASSWORD: ${{ secrets.CODE_SIGN_PASSWORD }}
  run: |
    # Декодировать PFX
    echo "$CODE_SIGN_PFX" | base64 -d > signing.pfx

    # Подписать инсталлятор (signtool.exe)
    "C:\Program Files (x86)\Windows Kits\10\bin\x64\signtool.exe" sign `
      /f signing.pfx `
      /p "$CODE_SIGN_PASSWORD" `
      /tr http://timestamp.digicert.com `
      /td sha256 /fd sha256 `
      OES-Setup.exe

    # Удалить PFX после подписи
    Remove-Item signing.pfx
```

---

## Платформенное хранение секретов

### macOS: Keychain

```bash
# Сохранить пароль Firebird в macOS Keychain
security add-generic-password \
  -a "oes-app" \
  -s "OES-Firebird-SYSDBA" \
  -w "СГЕНЕРИРОВАННЫЙ_ПАРОЛЬ"

# Получить пароль из Keychain
security find-generic-password -s "OES-Firebird-SYSDBA" -w
```

```cpp
// В коде OES (macOS): чтение из Keychain через Security framework
#include <Security/Security.h>

wxString LoadFirebirdPasswordFromKeychain() {
    SecKeychainItemRef item = nullptr;
    UInt32 passwordLen = 0;
    void* passwordData = nullptr;
    
    OSStatus status = SecKeychainFindGenericPassword(
        nullptr, 15, "OES-Firebird-DB",
        7, "oes-app",
        &passwordLen, &passwordData, &item
    );
    
    if (status == errSecSuccess) {
        wxString pwd = wxString::FromUTF8(static_cast<char*>(passwordData), passwordLen);
        SecKeychainItemFreeContent(nullptr, passwordData);
        return pwd;
    }
    return wxEmptyString;
}
```

### Linux: libsecret / gnome-keyring

```bash
# Через secret-tool (libsecret)
secret-tool store --label='OES Firebird Password' service oes-firebird username sysdba

# Получить
secret-tool lookup service oes-firebird username sysdba
```

## Windows DPAPI (шифрование на уровне ОС)

Для хранения паролей БД в desktop-режиме OES использует DPAPI — шифрование, привязанное к учётной записи Windows:

```cpp
// Пример использования в C++ коде OES (Windows)
// Шифрование строки подключения через CryptProtectData
#include <windows.h>
#include <wincrypt.h>

// Зашифровать секрет (только текущий пользователь может расшифровать)
bool EncryptSecret(const std::wstring& plaintext, std::vector<BYTE>& encrypted) {
    DATA_BLOB input, output;
    input.pbData = (BYTE*)plaintext.data();
    input.cbData = plaintext.size() * sizeof(wchar_t);
    if (!CryptProtectData(&input, L"OES Config", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return false;
    }
    encrypted.assign(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return true;
}
```

---

## Генерация безопасных паролей

```bash
# Случайный пароль 32 символа
openssl rand -base64 32

# Только буквы и цифры (совместимо с Firebird)
openssl rand -base64 48 | tr -dc 'a-zA-Z0-9' | head -c 24

# UUID (для License Key генерации)
uuidgen

# PowerShell (Windows)
[System.Web.Security.Membership]::GeneratePassword(24, 4)
# или
-join ((65..90) + (97..122) + (48..57) | Get-Random -Count 24 | ForEach-Object {[char]$_})
```

---

## Правила безопасности (закрепить в команде)

1. **Никогда** не коммитить oes.conf / строки подключения в git
2. **Никогда** не передавать лицензионные ключи клиентов через мессенджеры
3. **Никогда** не логировать пароли и строки подключения (ни в debug, ни в релизе)
4. **Никогда** не хранить Code Signing сертификат на машинах разработчиков
5. **Всегда** использовать oes.conf.example без значений в репозитории
6. **Всегда** ротировать секреты при увольнении сотрудника
7. **Всегда** использовать разные пароли для dev/staging/production БД
8. **Всегда** шифровать бэкапы баз данных
9. **Всегда** минимальные права пользователей БД (только нужные таблицы/схемы)
10. **Регулярно** (раз в квартал) ротировать критичные секреты
11. **Проверять** git diff перед коммитом — не попала ли строка подключения
12. **Встраивать** только публичный ключ проверки лицензий, НИКОГДА не приватный
