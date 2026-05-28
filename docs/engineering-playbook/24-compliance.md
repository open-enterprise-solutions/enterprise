# 24. Compliance: LGPL, Licensing, Data Handling

## When it applies

| Requirement | When needed |
|------------|------------|
| **LGPL 2.1** | Always — OES is distributed under LGPL 2.1 |
| **Dependency licenses** | When adding any third-party library |
| **GDPR** | When OES collects, transmits, or stores EU citizen data (crash reports, telemetry, cloud features) |
| **Customer corporate policies** | When shipping to enterprise customers (SOC 2, ISO 27001, internal policy) |
| **Export control** | When shipping to certain countries (EAR, ITAR — consult lawyers) |

---

## LGPL 2.1 — our license

### What LGPL 2.1 means for OES

LGPL 2.1 (Lesser General Public License) allows:
- Commercial products to **use** OES as a library without being required to open their source
- Embedding OES into proprietary systems under its terms

Obligations when distributing:
- Provide the OES source code (or a link to it)
- Don't restrict users from replacing the OES library with a modified version
- Preserve copyright notices

### What you cannot do with OES code

```
NOT ALLOWED:
- Change the OES license from LGPL 2.1 to proprietary
- Statically embed OES into a closed-source DLL so the user
  cannot replace OES with their version
- Remove copyright notices from the source code
- "tivoization" — preventing the user from installing
  a modified OES on the same device

ALLOWED:
- Use OES in commercial products (dynamic linking)
- Create plugins under any license (they are not part of OES)
- Modify OES for internal use
- Sell products that use OES
```

### Source code notices

Every new `.cpp` / `.h` file must start with:

```cpp
/*
 * Open Enterprise Solutions (OES)
 * Copyright (C) 2024-2025 OES Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
```

---

## Dependency licensing

### License compatibility with LGPL 2.1

| Dependency license | Compatible? | Conditions |
|---------------------|------------|---------|
| MIT | Yes | Preserve the notice |
| BSD 2/3-Clause | Yes | Preserve the notice |
| Apache 2.0 | Yes | Acknowledge patents |
| LGPL 2.1 | Yes | Our case (wxWidgets) |
| LGPL 3.0 | Caution | May tighten requirements (see note below) |
| GPL 2.0 | No | Makes all of OES GPL |
| GPL 3.0 | No | Same |
| AGPL | No | Most restrictive |
| Proprietary | Consult lawyers | Depends on the terms |

> **Note on LGPL 3.0:** It includes anti-tivoization requirements (§6 GPL 3.0 inherited through LGPL 3.0). That means if OES or an LGPL 3.0 dependency ships on a device with hardware restrictions (e.g. signed firmware that prevents replacing the library), the user must be able to install a modified version. For a desktop Windows/Linux application this is usually satisfied automatically (the DLL can be replaced), but it must be checked for embedded or OEM shipments. Consult lawyers when shipping to closed-platform devices.

> **MySQL Connector/C warning:** The GPL 2.0 version is **incompatible with LGPL 2.1**. Dynamically or statically linking a GPL library with OES code spreads GPL across the entire distribution, breaking the OES license. If MySQL support is needed, use the **commercial MySQL Connector license** (Oracle OEM license) or switch to MariaDB Connector/C (LGPL 2.1). Verify the current version in THIRD_PARTY_LICENSES.md before every release.

### License audit when adding a dependency

```bash
# Before adding a new library — check its license
# Automated audit tools (examples):

# FOSSA (SaaS, automated analysis)
# licensee (Ruby gem)
# liccheck (Python)

# Manual check: always read the LICENSE file in the repo

# Add an entry to THIRD_PARTY_LICENSES.md:
# | Library | Version | License | URL |
```

### Required entries in THIRD_PARTY_LICENSES.md

```markdown
# OES third-party components

| Component | Version | License | URL |
|-----------|--------|----------|-----|
| wxWidgets | 3.3.2 | LGPL 2.1 with exceptions | https://wxwidgets.org |
| Firebird | 4.0 | IDPL / LGPL | https://firebirdsql.org |
| libpq (PostgreSQL) | 16 | PostgreSQL License | https://postgresql.org |
| SQLite | 3.45 | Public Domain | https://sqlite.org |
| MySQL Connector/C | 8.0 | GPL 2.0 / Commercial | https://mysql.com |
| zlib | 1.3 | zlib License | https://zlib.net |
| libcurl | 8.6 | curl License (MIT-like) | https://curl.se |
```

> When adding a new dependency — update this file.

---

## Crash reports and telemetry — GDPR

### What ends up in a crash report

A Windows minidump may contain:
- Call stack and memory addresses
- Register contents and parts of the process memory
- **Potentially**: chunks of data from app buffers — file names, paths, document fragments

This can be personal data under GDPR.

### Requirements for collecting crash reports

#### 1. Explicit user consent

```cpp
// On first run — show the consent dialog
class FirstRunDialog : public wxDialog {
    void ShowCrashReportingConsent() {
        // The text must clearly explain:
        // - What is collected (call stack, system info)
        // - Where it is sent (crash.oes-platform.com)
        // - How it is used (only to fix bugs)
        // - You can opt out in Settings → Privacy
    }
};

// Save the user's choice
void AppSettings::SetCrashReportingEnabled(bool enabled) {
    m_config->Write("/Privacy/CrashReporting", enabled);
}
```

#### 2. What you must NOT send in a crash report

```cpp
// When sending a crash report — filter out sensitive data
struct CrashReportPayload {
    std::string oesVersion;
    std::string osVersion;      // "Windows 11 x64"
    std::string cpuArch;        // "x86_64"
    uint64_t    ramMb;
    std::string stackTrace;     // Addresses + symbols only, no data
    std::string exceptionCode;

    // DO NOT INCLUDE:
    // - Project file names (may carry customer PII)
    // - DB connection strings
    // - User paths (%USERPROFILE%\...)
    // - Document/report content
};
```

#### 3. Anonymize before sending

```cpp
std::string AnonymizePath(const std::string& path) {
    // Replace %USERPROFILE%\... with <USER_HOME>\...
    // Replace %COMPUTERNAME% with <HOSTNAME>
    // Keep only the last path component
    std::regex userPath(R"(C:\\Users\\[^\\]+\\)");
    return std::regex_replace(path, userPath, "<USER_HOME>\\");
}
```

### GDPR checklist for crash reports and telemetry

- [ ] User has explicitly consented (opt-in, not opt-out)
- [ ] Can be disabled in settings without functionality loss
- [ ] Crash report doesn't contain user document content
- [ ] File paths anonymized
- [ ] DB connection strings don't end up in the dump
- [ ] Retention: no longer than 90 days
- [ ] Privacy policy includes a section on crash reports
- [ ] Crash report server is in the EU or has a Data Processing Agreement (if in the US — SCCs)

---

## Licensing OES itself (commercial customers)

### OES licensing models

| Type | Description | Restrictions |
|-----|---------|------------|
| LGPL Open Source | Free, source open | Must follow LGPL |
| OEM / Commercial | Commercial license for embedding | Per agreement |
| SaaS / Cloud | Hosting OES as a service | Per agreement |
| Enterprise Support | Support + updates | Per agreement |

### Protecting license keys

```cpp
// License keys — never store in code, never log
// Proper application-side storage:

// Windows: DPAPI (CryptProtectData) to encrypt the key on disk
// Linux/macOS: keyring / Keychain Services

class LicenseStorage {
public:
    bool StoreLicenseKey(const std::string& key);
    std::string LoadLicenseKey();

private:
#ifdef _WIN32
    // Windows DPAPI
    bool EncryptWithDpapi(const std::string& data,
                           std::vector<uint8_t>& encrypted);
#endif
};

// In app code — NEVER:
// wxLogMessage("License key: %s", licenseKey); // forbidden
// printf("Activating with key: %s\n", key);    // forbidden
```

### Usage audit (for enterprise)

```cpp
// For enterprise licenses with a user count cap:
// - Activation log stored locally (not in the cloud without consent)
// - Log data: machine ID (hash), activation date, version
// - DO NOT store: user name, email, IP without explicit consent
```

---

## AI tools and compliance

### Rules for using AI during OES development

1. **Do NOT send to AI**: code with real customer DB connection strings, license keys, data from production dumps
2. **Do NOT use** real customer data in prompts — mask or replace with test data
3. **AI-generated code** must always be reviewed for:
   - Hardcoded secrets
   - Wrong NULL / error handling (a common C++ issue)
   - LGPL violation (if AI suggests copying GPL code)
   - Buffer overflow and other C++ memory safety issues
4. **GitHub Copilot / similar**: verify that the generated code is not a verbatim copy of GPL-licensed code (risk of license contamination)

```
# Bad prompt
"Here is the Firebird connection string for customer Acme Corp:
 firebird://SYSDBA:masterkey@192.168.1.50/C:\db\acme.fdb
 Help me debug a query..."

# Good prompt
"There is a Firebird DSQL query that misbehaves with NULL:
 SELECT * FROM ORDERS WHERE CUSTOMER_ID = ? AND STATUS = ?
 The second parameter is sometimes NULL — how do we handle it correctly?"
```

---

## Compliance documents

| Document | Contents | Where to store |
|----------|-------------|-------------|
| LICENSE | Full LGPL 2.1 text | Repository root |
| THIRD_PARTY_LICENSES.md | All third-party components and their licenses | Repository root |
| Privacy Policy | Privacy policy (crash reports, telemetry) | Website + `docs/legal/` |
| End User License Agreement (EULA) | Terms for commercial customers | `docs/legal/` |
| Data Processing Agreement (DPA) | With customers processing EU data | Legal department |
| Incident Response Plan | What to do on a crash report data leak | `docs/security/` |

---

## Compliance checklist when shipping a release

- [ ] `THIRD_PARTY_LICENSES.md` updated (every new dependency listed)
- [ ] Every new `.cpp` / `.h` file carries the LGPL header
- [ ] No GPL/AGPL-licensed dependencies in the public distribution
- [ ] License keys don't get logged into the Application Event Log / log files
- [ ] Crash reports: DB connection strings don't end up in the dump
- [ ] First-run crash report consent dialog present
- [ ] `LICENSE` file shipped with the installer (Inno Setup: `Source: "LICENSE"; DestDir: "{app}"`)
- [ ] Source code of the version is tagged in git and available (LGPL requirement)
- [ ] Code signing certificate not expired
- [ ] EULA / Privacy Policy updated if new data collection functionality was added
