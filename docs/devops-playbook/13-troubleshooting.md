# 13. Диагностика проблем (Troubleshooting)

> Быстрые чеклисты для C++ desktop приложения OES: crash dumps, отладочные символы, удалённая диагностика, типичные проблемы wxWidgets и Firebird.

---

## Быстрые проверки: первым делом

### Windows (приложение)

```powershell
# Запущен ли OES?
Get-Process -Name "oes" -ErrorAction SilentlyContinue

# Проверить последние ошибки в Event Log
Get-EventLog -LogName Application -Source "OES*" -Newest 20 | Format-List

# Crash dumps — есть ли свежие?
Get-ChildItem "$env:APPDATA\OES\CrashReports\new\" -Filter "*.dmp" | Sort-Object LastWriteTime -Descending | Select-Object -First 5

# Лог приложения
Get-Content "$env:APPDATA\OES\Logs\oes.log" -Tail 50

# Файл базы данных — доступен ли?
Test-Path "$env:APPDATA\OES\data\*.fdb"
```

### macOS (приложение / daemon)

```bash
# Запущен ли OES?
pgrep -l oes

# Статус daemon (launchd)
sudo launchctl list | grep oes

# Логи daemon
tail -50 /var/log/oes/daemon.log
# или через unified logging:
log show --predicate 'process == "oes-daemon"' --last 1h

# Файл базы данных
ls -lh ~/Library/Application\ Support/OES/data/    # desktop
ls -lh /var/lib/oes/data/                           # daemon

# Процесс
ps aux | grep oes

# Открытые файлы процессом
sudo lsof -p $(pgrep oes-daemon) | head -30
```

### Linux (daemon-режим)

```bash
# Статус сервиса
sudo systemctl status oes-daemon

# Последние логи (journald)
sudo journalctl -u oes-daemon --since "1 hour ago" --no-pager

# Логи приложения
tail -50 /var/log/oes/oes-daemon.log

# Файл базы данных
ls -lh /var/lib/oes/data/

# Процесс
ps aux | grep oesd

# Открытые файлы процессом
sudo lsof -p $(pgrep oesd) | head -30

# Использование памяти
free -h
cat /proc/$(pgrep oesd)/status | grep -E "VmRSS|VmPeak|Threads"
```

---

## Анализ crash dump

### Открыть dump в Visual Studio

```
1. Открыть Visual Studio
2. File → Open → File → выбрать .dmp файл
3. VS автоматически определит тип дампа
4. Нажать "Debug with Native Only"
5. VS покажет стек вызовов на момент краша

Для правильного анализа нужны .pdb файлы той же версии:
  — Путь к символам: Tools → Options → Debugging → Symbols
  — Добавить папку с .pdb файлами от нужной версии OES
```

### Открыть dump через WinDbg

```
windbg -z "C:\path\to\crash.dmp"

Основные команды WinDbg:
  .symfix                     — настроить символы Microsoft
  .sympath+ C:\OES\symbols    — добавить свои символы
  .reload                     — перезагрузить символы
  !analyze -v                 — автоматический анализ краша
  k                           — стек вызовов текущего потока
  ~*k                         — стеки всех потоков
  .ecxr; k                    — стек на момент исключения
  lm                          — список загруженных модулей
  !address                    — информация об адресах памяти
```

### Автоматический анализ дампов (скрипт)

```powershell
# scripts\analyze-crashes.ps1
param(
    [string]$DumpDir = "$env:APPDATA\OES\CrashReports",
    [string]$SymbolsDir = "C:\OES\Symbols",
    [string]$OutputDir = "C:\OES\CrashAnalysis"
)

# Требует установки Debugging Tools for Windows (WinDbg)
$cdb = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"

if (-not (Test-Path $cdb)) {
    Write-Error "WinDbg не найден. Установите Windows SDK."
    exit 1
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$dumps = Get-ChildItem -Path $DumpDir -Filter "*.dmp" -Recurse
Write-Host "Найдено crash dumps: $($dumps.Count)"

foreach ($dump in $dumps) {
    $reportFile = Join-Path $OutputDir "$($dump.BaseName)-analysis.txt"
    
    if (Test-Path $reportFile) {
        Write-Host "  Пропуск (уже проанализирован): $($dump.Name)"
        continue
    }
    
    Write-Host "  Анализ: $($dump.Name)"
    
    $symPath = "srv*C:\symbols*https://msdl.microsoft.com/download/symbols;$SymbolsDir"
    
    $script = @"
.sympath $symPath
.reload /f
!analyze -v
.ecxr
k 30
~*k
q
"@
    
    $scriptFile = [System.IO.Path]::GetTempFileName()
    $script | Out-File $scriptFile -Encoding ASCII
    
    & $cdb -z $dump.FullName -c "`$`$<$scriptFile" 2>&1 | Out-File $reportFile -Encoding UTF8
    Remove-Item $scriptFile
    
    # Извлечь сводку
    $summary = Get-Content $reportFile | Select-String "EXCEPTION_CODE|FAILURE_BUCKET_ID|PROBABLE_CAUSE"
    Write-Host "    $($summary -join ' | ')"
}

Write-Host "`nАнализ завершён. Отчёты: $OutputDir"
```

---

## Типичные проблемы и решения

### Приложение не запускается (Windows)

```powershell
# 1. Проверить отсутствие DLL
# Запустить из командной строки (НЕ двойным кликом) чтобы увидеть ошибку:
cd "C:\Program Files\OES"
.\oes.exe

# "The program can't start because VCRUNTIME140.dll is missing"
# Решение: установить Visual C++ Redistributable
# https://aka.ms/vs/17/release/vc_redist.x64.exe

# "The program can't start because fbclient.dll is missing"
# Решение: проверить что Firebird установлен или fbclient.dll в папке с .exe

# 2. Проверить Event Viewer
Get-EventLog -LogName Application -EntryType Error -Newest 5 | Format-List Source, Message

# 3. Procmon (Sysinternals) — посмотреть какой файл не найден
# Фильтр: Process Name = oes.exe, Result = NAME NOT FOUND

# 4. Dependencies (утилита) — анализ зависимостей .exe
# https://github.com/lucasg/Dependencies
```

### Firebird: ошибки подключения

```
"Unable to complete network request to host"
  → Firebird сервер не запущен (если server mode)
  → Проверить: net start FirebirdServerDefaultInstance
  → Или: services.msc → Firebird Guardian

"I/O error during read/write, file: <path>"
  → Файл .fdb повреждён или на диске нет места
  → gfix -validate: проверить целостность
  → df -h (Linux) / dir (Windows): проверить место

"Lock time-out on wait transaction"
  → Приложение не завершило транзакцию
  → gfix -kill: завершить зависшие транзакции (осторожно!)
  → Или подождать таймаут

"database file appears corrupt"
  → КРИТИЧНО: запустить gfix -validate -full
  → Восстановить из бэкапа если повреждение серьёзное
  → gbak может восстановить часть данных

"connection rejected by remote interface"
  → Неверный пользователь/пароль
  → Убедиться что SYSDBA пароль правильный
```

```bash
# Диагностика Firebird (Linux/macOS/Windows)

# Проверить целостность БД
gfix -user SYSDBA -password masterkey -validate -full /path/to/oes.fdb

# Починить транзакции
gfix -user SYSDBA -password masterkey -sweep /path/to/oes.fdb
gfix -user SYSDBA -password masterkey -mend /path/to/oes.fdb  # осторожно!

# Статистика БД
gstat -user SYSDBA -password masterkey -header /path/to/oes.fdb

# Логи Firebird
# Windows: C:\Program Files\Firebird\firebird.log
# macOS:   /usr/local/var/log/firebird.log  (Homebrew)
# Linux:   /var/log/firebird/
```

### wxWidgets: проблемы с UI

```cpp
// === Зависание UI потока ===
// ПРИЗНАК: интерфейс перестаёт реагировать на ввод

// Проблема: долгая операция в UI потоке
void OnButtonClick(wxCommandEvent&) {
    // ПЛОХО: блокирует UI
    LoadHugeDocument();  // занимает 5 секунд
}

// Решение 1: wxThread
void OnButtonClick(wxCommandEvent&) {
    auto thread = new LoadDocumentThread(this, m_filePath);
    thread->Run();
    // Прогресс через wxThreadEvent
}

// Решение 2: wxProgressDialog + wxYield
void OnButtonClick(wxCommandEvent&) {
    wxProgressDialog dlg("Загрузка...", "Обработка файла", 100, this);
    for (int i = 0; i < 100; i++) {
        ProcessChunk(i);
        dlg.Update(i);
        wxYield();  // обработать события UI
    }
}

// === Артефакты отрисовки ===
// ПРИЗНАК: мерцание, некорректная прорисовка при изменении размера

// Решение: двойная буферизация
void OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);  // вместо wxPaintDC
    // ... рисование
}
```

### Утечки памяти: поиск и исправление

```
Инструменты для поиска утечек памяти:

1. Visual Studio Diagnostic Tools (встроено):
   Debug → Windows → Diagnostic Tools → Memory Usage
   → Take Snapshot до и после операции
   → Compare для поиска утечек

2. Application Verifier (Microsoft):
   appverif /enable Heaps /app oes.exe
   Запустить oes.exe — сообщит об утечках при закрытии

3. Deleaker (коммерческий, интеграция с VS):
   Показывает стек вызовов при выделении памяти

4. AddressSanitizer (в Debug-сборке):
   В vcxproj: <EnableASAN>true</EnableASAN>
   Покажет heap overflow, use-after-free, double-free
   
5. valgrind (Linux):
   valgrind --leak-check=full --track-origins=yes ./oesd

Типичные источники утечек в wxWidgets:
  — wxString и wxArrayString в циклах (обычно не утечка, wxWidgets управляет)
  — wxBitmap не освобождается при замене
  — Дочерние окна созданные с new без родителя
  — Обработчики событий не отписываются (wxEvtHandler::Unbind)
```

---

## Удалённая диагностика

### Сбор диагностической информации от пользователя

```powershell
# scripts\collect-diagnostics.ps1
# Запускать от имени пользователя с проблемой

$outputDir = "$env:DESKTOP\OES-Diagnostics-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

Write-Host "Сбор диагностики OES..."

# Версия OES
$oesExe = "C:\Program Files\OES\oes.exe"
if (Test-Path $oesExe) {
    (Get-Item $oesExe).VersionInfo | Out-File "$outputDir\oes-version.txt"
}

# Логи приложения
Copy-Item "$env:APPDATA\OES\Logs\oes.log" "$outputDir\" -ErrorAction SilentlyContinue
Copy-Item "$env:APPDATA\OES\Logs\oes.log.1" "$outputDir\" -ErrorAction SilentlyContinue

# Crash dumps (только последние 3)
$crashDir = "$env:APPDATA\OES\CrashReports"
if (Test-Path $crashDir) {
    Get-ChildItem $crashDir -Filter "*.dmp" -Recurse |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 3 |
        ForEach-Object { Copy-Item $_.FullName $outputDir\ }
}

# Системная информация
systeminfo | Out-File "$outputDir\systeminfo.txt"
Get-EventLog -LogName Application -Source "OES*" -Newest 50 2>/dev/null |
    Format-List | Out-File "$outputDir\eventlog-oes.txt"

# Событие из Application Log за последние 24ч
Get-EventLog -LogName Application -EntryType Error,Warning -After (Get-Date).AddHours(-24) 2>/dev/null |
    Format-List | Out-File "$outputDir\eventlog-errors.txt"

# Версии DLL
Get-ChildItem "C:\Program Files\OES\" -Filter "*.dll" |
    ForEach-Object { $_.VersionInfo } |
    Select-Object FileName, FileVersion, ProductVersion |
    Out-File "$outputDir\dll-versions.txt"

# Архивировать
$zipFile = "$outputDir.zip"
Compress-Archive -Path $outputDir -DestinationPath $zipFile
Remove-Item $outputDir -Recurse

Write-Host "Готово! Отправьте файл в поддержку: $zipFile"
explorer.exe (Split-Path $zipFile)
```

### Включить расширенное логирование

```cpp
// В коде OES: поддержка уровней логирования через аргументы командной строки
// oes.exe --log-level=debug --log-file=C:\temp\oes-verbose.log

// Или через конфигурационный файл:
// [Logging]
// Level=debug       ; error, warning, info, debug, trace
// File=oes.log
// MaxSizeMb=50
```

---

## OES daemon не отвечает

```bash
# 1. Daemon работает?
sudo systemctl status oes-daemon
sudo ps aux | grep oesd

# 2. Если не работает — последние логи перед падением
sudo journalctl -u oes-daemon --since "2 hours ago" --no-pager | tail -100

# 3. Если завис (не отвечает, но process есть) — получить stack trace
sudo kill -SIGUSR1 $(pgrep oesd)    # Если реализован в OES: вывести stack
# или
sudo gdb -p $(pgrep oesd) -ex "thread apply all bt" -ex "detach" -ex "quit"

# 4. Нехватка файловых дескрипторов?
cat /proc/$(pgrep oesd)/limits | grep "open files"
ls /proc/$(pgrep oesd)/fd | wc -l

# 5. Нехватка памяти?
cat /proc/$(pgrep oesd)/status | grep VmRSS
dmesg | grep -i "oom\|out of memory" | tail -10

# 6. Принудительный дамп для анализа
sudo kill -SIGABRT $(pgrep oesd)    # Создаст core dump (если ulimit настроен)
# или настроить core dumps:
ulimit -c unlimited
echo '/var/log/oes/core.%p' | sudo tee /proc/sys/kernel/core_pattern

# 7. Перезапуск (последний вариант)
sudo systemctl restart oes-daemon
```

---

## Диагностика производительности

```powershell
# Windows: производительность OES

# CPU и память процесса в реальном времени
while ($true) {
    $p = Get-Process -Name "oes" -ErrorAction SilentlyContinue
    if ($p) {
        Write-Host "$(Get-Date -Format 'HH:mm:ss') CPU: $($p.CPU.ToString('F1'))s | RAM: $([math]::Round($p.WorkingSet64/1MB))MB | Threads: $($p.Threads.Count)"
    }
    Start-Sleep -Seconds 2
}

# Или через Perfmon (System Monitor):
# Add counters: Process\% Processor Time\oes
#               Process\Working Set\oes
#               .NET CLR Memory\# Gen 2 Collections (если есть managed код)
```

```bash
# Linux: производительность daemon

# perf — CPU profiling (если доступен)
sudo perf top -p $(pgrep oesd)

# strace — системные вызовы (слишком медленно для production)
sudo strace -p $(pgrep oesd) -e trace=file,network -f 2>&1 | tail -100

# Мониторинг в реальном времени
watch -n 1 "cat /proc/$(pgrep oesd)/status | grep -E 'VmRSS|Threads|voluntary'"
```

---

## Чеклист: что проверить первым

```
При любой проблеме с OES — проверить в этом порядке:

Desktop — Windows:
  1. □ Свежие crash dumps? (%APPDATA%\OES\CrashReports\)
  2. □ Лог приложения? (%APPDATA%\OES\Logs\oes.log)
  3. □ Event Viewer (Application)? Ошибки с Source="OES*"
  4. □ Место на диске? (Disk Management)
  5. □ Антивирус не блокирует? (временно отключить для теста)
  6. □ Файл .fdb доступен и не повреждён?
  7. □ Нужные DLL присутствуют? (Dependencies утилита)
  8. □ Версия приложения актуальна? (Help → About)

Desktop — macOS:
  1. □ Логи: ~/Library/Logs/OES/ или Console.app
  2. □ Данные: ~/Library/Application Support/OES/data/*.fdb
  3. □ Место на диске: df -h
  4. □ Процесс: pgrep -la oes
  5. □ Crash reports: ~/Library/Logs/DiagnosticReports/

Desktop — Linux:
  1. □ Логи: ~/.local/share/oes/logs/oes.log
  2. □ Данные: ~/.local/share/oes/data/*.fdb
  3. □ Место: df -h

Daemon/Server (Linux):
  1. □ sudo systemctl status oes-daemon
  2. □ sudo journalctl -u oes-daemon --since "1h ago"
  3. □ Диск не полный? (df -h)
  4. □ Память не исчерпана? (free -h)
  5. □ gfix -validate: целостность БД
  6. □ Права на файлы БД? (ls -la /var/lib/oes/)
  7. □ Firebird запущен? (sudo systemctl status firebird3.0-guardian)
  8. □ Файрвол не блокирует порт? (sudo ufw status)

Daemon/Server (macOS):
  1. □ sudo launchctl list | grep oes
  2. □ tail -50 /var/log/oes/daemon.log
  3. □ df -h
  4. □ Firebird: brew services list | grep firebird
```

---

## Полезные команды для OES

```powershell
# Windows

# Открыть папку логов
explorer "$env:APPDATA\OES\Logs"

# Открыть папку с crash dumps
explorer "$env:APPDATA\OES\CrashReports"

# Хвост лога в реальном времени
Get-Content "$env:APPDATA\OES\Logs\oes.log" -Wait -Tail 20

# Размер БД пользователя
Get-ChildItem "$env:APPDATA\OES" -Filter "*.fdb" -Recurse |
    Select-Object Name, @{N='Size MB';E={[math]::Round($_.Length/1MB,1)}}

# Проверить подпись установленных файлов
Get-ChildItem "C:\Program Files\OES" -Filter "*.exe" |
    ForEach-Object { Get-AuthenticodeSignature $_.FullName } |
    Select-Object Path, Status, SignerCertificate

# Версия Firebird client
(Get-Item "C:\Program Files\OES\fbclient.dll").VersionInfo | Select-Object FileVersion
```

```bash
# Linux

# Хвост лога daemon
sudo journalctl -u oes-daemon -f

# Размер БД
du -sh /var/lib/oes/data/

# Статистика Firebird БД
gstat -user SYSDBA -password "$FB_SYSDBA_PASSWORD" \
    -header /var/lib/oes/data/oes.fdb

# Количество активных транзакций
gstat -user SYSDBA -password "$FB_SYSDBA_PASSWORD" \
    -record /var/lib/oes/data/oes.fdb | grep "transactions"

# Открытые соединения к Firebird
netstat -an | grep 3050  # Firebird default port

# Количество потоков daemon
cat /proc/$(pgrep oesd)/status | grep Threads
```
