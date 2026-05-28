# 12. Security Hardening

> Security for the OES C++ desktop application: code signing, secure update channel, DLL hijacking prevention, ASLR/DEP, secure C++ programming.

---

## Code Signing

### Why it matters for OES

```
Without code signing:
  - Windows SmartScreen blocks the installer ("Unknown Publisher")
  - Antiviruses falsely flag unsigned binaries
  - Users see a scary UAC warning
  - It is impossible to verify integrity after download

With signing:
  - The installer runs without warnings
  - Users see the "Tetracode" name in the UAC dialog
  - Antiviruses trust signed files
  - The trust chain can be verified
```

### Obtaining and using a certificate

```
EV Code Signing certificate providers:
  - Sectigo (formerly Comodo)
  - DigiCert
  - GlobalSign

Cost: ~$300-500/year (EV) or ~$100-200/year (OV)
EV is recommended: immediately gets a high Microsoft SmartScreen reputation
```

### Signing in CI/CD (GitHub Actions)

```yaml
# Store the certificate as base64 in GitHub Secrets
# In the terminal: certutil -encode codesign.pfx codesign_b64.txt

      - name: Sign binaries
        run: |
          # Restore the certificate
          $pfxBytes = [System.Convert]::FromBase64String("${{ secrets.CODE_SIGN_CERT }}")
          [System.IO.File]::WriteAllBytes("$env:TEMP\cert.pfx", $pfxBytes)
          
          # Sign all .exe and .dll
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
          
          # Sign the installer
          signtool sign `
              /f "$env:TEMP\cert.pfx" `
              /p "${{ secrets.CODE_SIGN_PASSWORD }}" `
              /tr http://timestamp.sectigo.com `
              /td sha256 `
              /fd sha256 `
              .\dist\OES-Setup.exe
          
          # Remove the certificate from disk
          Remove-Item "$env:TEMP\cert.pfx"
```

### Verifying the signature

```powershell
# Verify the file signature
Get-AuthenticodeSignature ".\OES-Setup.exe" | Format-List

# Or via signtool
signtool verify /pa /v ".\OES-Setup.exe"

# Expected output:
# SignerCertificate: Tetracode Dev
# Status: Valid
# TimeStamperCertificate: Sectigo RSA Time Stamping CA
```

---

## DLL Hijacking protection

### What it is and why it matters for wxWidgets applications

```
DLL hijacking: an attacker plants a malicious DLL in the directory next
to the application. Windows loads it instead of the system one.

Especially relevant for OES because:
  - wxWidgets and the Firebird client ship as DLLs next to the .exe
  - The installer copies DLLs into C:\Program Files\OES\
  - If the directory is writable - a vulnerability exists
```

### Mitigations

```cpp
// 1. Set a secure DLL search order in WinMain
// Before any LoadLibrary() calls!

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmd, int nCmdShow) {
    // Remove the current directory from the DLL search path
    // (Win8+ only, but useful nonetheless)
    SetDllDirectoryW(L"");
    
    // Load only from System32
    // SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    // CAUTION: may break loading of wxWidgets/Firebird DLLs
    // Use LOAD_LIBRARY_SEARCH_DEFAULT_DIRS if local DLLs are needed
    SetDefaultDllDirectories(
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
        LOAD_LIBRARY_SEARCH_SYSTEM32
    );
    
    // Continue with wxWidgets initialization
    // ...
}
```

```nsis
; NSIS: set correct permissions on the install directory
; Program Files is Windows-protected - standard paths are safe
; Do NOT install into %APPDATA% or %TEMP%

Section "Main"
    SetOutPath "$PROGRAMFILES64\OES"
    ; Only SYSTEM and Administrators have write permissions
    ; Regular users - read-only
SectionEnd
```

### Testing for DLL hijacking vulnerability

```powershell
# Procmon (Sysinternals) - inspect DLL loading
# Filter: Process Name = "oes.exe" AND Operation = "CreateFile" AND Result = "NAME NOT FOUND"
# If a DLL is searched in a writable directory - vulnerability

# Automated check via Robber or similar tools:
# https://github.com/MojtabaTajik/Robber
```

---

## ASLR, DEP and other defenses

### MSBuild: enabling protection

```xml
<!-- In .vcxproj or via Project Properties -->
<PropertyGroup>
  <!-- DEP (Data Execution Prevention) / NX bit -->
  <EnableDEP>true</EnableDEP>
  
  <!-- ASLR (Address Space Layout Randomization) -->
  <RandomizedBaseAddress>true</RandomizedBaseAddress>
  
  <!-- SafeSEH - protected exception handlers (x86 only) -->
  <SafeSEH>true</SafeSEH>
  
  <!-- Control Flow Guard (CFG) - protection against ROP attacks -->
  <ControlFlowGuard>Guard</ControlFlowGuard>
  
  <!-- Disable incremental linking in Release
       (incremental linking weakens ASLR) -->
  <LinkIncremental>false</LinkIncremental>
</PropertyGroup>

<ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
  <Link>
    <!-- /NXCOMPAT - enable DEP -->
    <DataExecutionPrevention>true</DataExecutionPrevention>
    <!-- /DYNAMICBASE - enable ASLR -->
    <RandomizedBaseAddress>true</RandomizedBaseAddress>
    <!-- /HIGHENTROPYVA - High Entropy ASLR (64-bit) -->
    <HighEntropyVA>true</HighEntropyVA>
    <!-- /GUARD:CF - Control Flow Guard -->
    <CfGuard>true</CfGuard>
  </Link>
</ItemDefinitionGroup>
```

### Verifying enabled protections

```powershell
# dumpbin (from Visual Studio)
# Protection flags appear as strings in the DLL characteristics section
dumpbin /headers oes.exe | findstr /i "NX compatible"
dumpbin /headers oes.exe | findstr /i "dynamic base"
dumpbin /headers oes.exe | findstr /i "Guard"

# Or via PowerShell Get-PEHeader (utility):
# https://github.com/mattifestation/PEAnalysis

# Expected output in dumpbin /headers:
#                    NX compatible         - compatible with data execution prevention
#                    Dynamic base          - DLL can move (ASLR)
#                    Guard                 - Control Flow Guard
```

---

## Secure update channel

### Principles of secure OES updates

```
1. Download over HTTPS only
2. Verify the update signature before installing
3. Verify SHA256 hash
4. Versioning: do not downgrade without explicit confirmation
5. Updates only from the official server (pinning)
```

### Update check implementation

```cpp
// src/updater.cpp
#include <wx/http.h>
#include <wx/protocol/http.h>
#include <openssl/sha.h>

struct UpdateInfo {
    wxString version;
    wxString downloadUrl;
    wxString sha256Hash;
    wxString signature;       // Metadata signature
};

class SecureUpdater {
public:
    // Check for an update
    // URL: https://updates.example.com/oes/latest.json
    static bool CheckForUpdate(UpdateInfo& info) {
        wxHTTP http;
        http.SetHeader(wxT("User-Agent"), wxT("OES/" OES_VERSION_STRING));
        
        // HTTPS ONLY
        // In production: use WinHTTP or libcurl with certificate verification
        // wxHTTP does not verify SSL - use the WinHTTP API directly
        
        // ... fetch latest.json
        // Example contents:
        // {
        //   "version": "1.5.0",
        //   "url": "https://github.com/org/oes/releases/download/v1.5.0/OES-1.5.0-Setup.exe",
        //   "sha256": "abc123...",
        //   "min_version": "1.0.0"
        // }
        
        return true;
    }
    
    // Verify SHA256 of the downloaded file
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
        
        // Convert to hex
        wxString actualHash;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            actualHash += wxString::Format(wxT("%02x"), hash[i]);
        }
        
        return actualHash.IsSameAs(expectedHash, false);  // case-insensitive
    }
    
    // Verify the signature before launching the installer
    static bool VerifyInstallerSignature(const wxString& filePath) {
        // Windows: use the WinVerifyTrust API
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
        
        // Release resources
        trustData.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(NULL, &guidAction, &trustData);
        
        return result == ERROR_SUCCESS;
    }
};
```

---

## Secure data storage

### License keys

```cpp
// Store in Windows Credential Manager, NOT in the registry or a plaintext file

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

### DB connection passwords

```cpp
// Store PostgreSQL/MySQL passwords encrypted
// Use Windows DPAPI (Data Protection API)

#include <wincrypt.h>
#pragma comment(lib, "Crypt32.lib")

class SecureConfig {
public:
    // Encrypt a string via DPAPI (bound to the current user)
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

## Secure C++ programming

### Rules for the OES codebase

```cpp
// === 1. NEVER use unsafe functions ===

// Bad:
char buf[256];
strcpy(buf, userInput);     // no size check
sprintf(buf, userInput);    // format string attack
gets(buf);                  // no size check

// Good:
wxString safe = wxString::FromUTF8(userInput);  // wxWidgets manages memory
// Or if a char* is needed:
strncpy_s(buf, sizeof(buf), userInput, _TRUNCATE);
snprintf(buf, sizeof(buf), "%s", userInput);

// === 2. SQL Injection - parameterized queries ===

// Bad:
wxString query = "SELECT * FROM users WHERE name = '" + userName + "'";

// Good (Firebird):
wxString query = "SELECT * FROM users WHERE name = ?";
// Pass userName as a parameter via IBPP or IDBC

// === 3. Input validation ===
bool ValidateUserInput(const wxString& input, size_t maxLen = 1024) {
    if (input.IsEmpty() || input.Len() > maxLen) return false;
    // Check allowed characters for the specific field
    return true;
}

// === 4. Do not store sensitive data in plaintext ===
// Use SecureConfig::EncryptString() (see above)
// Zero out strings after use:
void SecureClear(std::string& s) {
    if (!s.empty()) {
        SecureZeroMemory(&s[0], s.size());
        s.clear();
    }
}
```

### Preventing memory leaks

```cpp
// Use RAII and smart pointers
#include <memory>

// Bad:
Widget* w = new Widget();
// ... if an exception is thrown - leak!

// Good:
auto w = std::make_unique<Widget>();
// Released automatically

// For wxWidgets objects - they are managed by the framework
// wxWindow::Destroy() instead of delete for windows
```

---

## Server hardening (daemon mode)

### Run daemon with minimal privileges (Linux)

```bash
# Create a separate user for the OES daemon
sudo useradd --system --no-create-home --shell /bin/false oes

# Directory permissions
sudo mkdir -p /var/lib/oes /var/log/oes /etc/oes
sudo chown oes:oes /var/lib/oes /var/log/oes
sudo chmod 750 /var/lib/oes /var/log/oes
sudo chmod 755 /etc/oes  # Config readable by all, written by root

# Firebird DB - only oes can read/write
sudo chmod 600 /var/lib/oes/data/oes.fdb
sudo chown oes:oes /var/lib/oes/data/oes.fdb
```

### Windows: run service with minimal privileges

```powershell
# Create a service account (Managed Service Account)
New-ADServiceAccount -Name "OESDaemon" -Enabled $true

# Or use the built-in "Network Service" / "Local Service" account
# instead of LocalSystem (LocalSystem = all permissions = bad)

sc.exe config OESDaemon obj= "NT SERVICE\OESDaemon" password= ""

# Grant only required rights via Group Policy or secedit
# SeServiceLogonRight - permission to log on as a service
```

### Firewall for the daemon port

```bash
# Linux (ufw): allow only required ports
sudo ufw default deny incoming
sudo ufw default allow outgoing

# OES daemon port (e.g. 4001)
sudo ufw allow from 192.168.0.0/16 to any port 4001  # LAN only
sudo ufw deny 4001  # block external access

sudo ufw enable
```

```bash
# macOS (pf): restrict the OES daemon port
# Add to /etc/pf.conf:
# block in on en0 proto tcp to any port 4001
# pass in on en0 proto tcp from 192.168.0.0/16 to any port 4001
sudo pfctl -f /etc/pf.conf
sudo pfctl -e
```

```cmd
REM Windows (netsh): allow only from the local network
netsh advfirewall firewall add rule ^
  name="OES Daemon" protocol=TCP dir=in localport=4001 ^
  remoteip=192.168.0.0/16 action=allow
netsh advfirewall firewall add rule ^
  name="OES Daemon Block" protocol=TCP dir=in localport=4001 ^
  action=block
```

---

## Vulnerability scanning

### cppcheck — static analysis

```bash
# Installation:
# Windows:  choco install cppcheck
# macOS:    brew install cppcheck
# Linux:    sudo apt install -y cppcheck

# Local run (all platforms)
cppcheck \
    --enable=all \
    --suppress=missingIncludeSystem \
    --suppress=unmatchedSuppression \
    --error-exitcode=1 \
    --std=c++17 \
    --xml \
    --xml-version=2 \
    src/engine/ 2> cppcheck-report.xml

# HTML report
cppcheck-htmlreport \
    --file=cppcheck-report.xml \
    --report-dir=cppcheck-html \
    --source-dir=.
```

### Checking dependencies for vulnerabilities

```bash
# If vcpkg is used
vcpkg audit

# For C++ projects: osv-scanner - scans dependencies for CVEs
# (dotnet list package --vulnerable does not apply to C++ projects)
# Install: https://google.github.io/osv-scanner/
osv-scanner --lockfile vcpkg.json .

# OWASP Dependency Check for C++ (via binary scanning)
dependency-check \
    --project "OES" \
    --scan ./bin/x64/Release/ \
    --format HTML \
    --out reports/
```

---

## OES hardening checklist

```
Code signing:
  [ ] EV Code Signing certificate obtained
  [ ] All .exe and .dll signed in the CI/CD pipeline
  [ ] NSIS/WiX installer signed
  [ ] Timestamp added to the signature (for long-term validity)
  [ ] Signature verified before installing updates

Binary protection:
  [ ] ASLR enabled (/DYNAMICBASE + /HIGHENTROPYVA)
  [ ] DEP enabled (/NXCOMPAT)
  [ ] Control Flow Guard enabled (/GUARD:CF)
  [ ] SafeSEH enabled (for x86 builds)
  [ ] DLL search order protected (SetDefaultDllDirectories)

Updates:
  [ ] Updates downloaded over HTTPS only
  [ ] SHA256 hash verified before install
  [ ] Installer signature verified (WinVerifyTrust)

Data storage:
  [ ] License keys in Windows Credential Manager
  [ ] DB passwords encrypted via DPAPI
  [ ] No plaintext passwords in the registry

Secure programming:
  [ ] cppcheck in the CI/CD pipeline (no warnings)
  [ ] No strcpy/gets/sprintf without size checks
  [ ] Parameterized DB queries
  [ ] Inputs validated

Daemon/Server:
  [ ] Runs as an unprivileged user
  [ ] Only the minimum required permissions
  [ ] Firewall: only required ports
  [ ] Config files with passwords: chmod 600
```
