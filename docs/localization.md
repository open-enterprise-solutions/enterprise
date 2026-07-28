# Localization — two translation surfaces

> **Scope:** how text gets translated in OES. There are **two independent surfaces** — do
> not confuse them:
> 1. **UI language** (the platform's own strings: menus, dialogs, error text) — process-wide
>    **gettext** catalogs.
> 2. **Configuration language** (metadata synonyms, form labels, enum presentations authored
>    *inside* a configuration) — per-session **raw-loc** strings resolved through
>    `ibBackendLocalization`.
>
> Files: `backend/backend_localization.{h,cpp}`, `backend/appData.cpp` (`InitLocale`),
> `locale/`, `lang/`. Companions: [main-frame.md](main-frame.md) (startup / `InitLocale`
> slot), [descriptions.md](descriptions.md) (where synonyms live on a metaobject).

---

## 1. The split in one line

| | UI language | Configuration language |
|---|---|---|
| **Translates** | platform's own `_("...")` strings | text authored inside a configuration (synonyms, labels) |
| **Scope** | process-wide (`wxLocale`) | per-session (`ibSession::GetLanguageCode()`) |
| **Storage** | `.po`/`.mo` gettext catalogs on disk | inline `code = 'text';` blobs in the serialized config |
| **Resolver** | `wxGetTranslation` via the `_()` macro | `ibBackendLocalization::GetTranslateGetRawLocText` |
| **Add a language** | new `.po` → `.mo` in `lang/<code>/` | new `code = '…';` entry on each translated field |

A single web deployment can therefore serve two users their forms in **different**
configuration languages (per-session) while both see the **same** platform UI language
(process-wide) — the two surfaces are orthogonal.

---

## 2. UI language — gettext catalogs

### 2.1 How a string becomes translatable

Wrap it in the `_()` macro anywhere in `src/engine/**`:

```cpp
wxLogError(_("The database could not be opened."));
throw ibBackendCoreException(_("Undefined identifier: %s"), name);
```

`_()` is `wxGetTranslation` — at runtime it looks the source (English) string up in the
loaded catalog and returns the active language's `msgstr`, or the English source if there is
no translation.

### 2.2 Where the catalogs live — `lang/` vs `locale/`

Two directories, two roles — this trips people up:

- **`locale/`** is the **source-of-truth workflow** directory: the extracted template
  `open_es.pot` and the per-language `ru.po` / `uk.po` (editable) plus their compiled
  `ru.mo` / `uk.mo`.
- **`lang/`** is the **runtime lookup** directory. `InitLocale` calls
  `wxLocale::AddCatalogLookupPathPrefix("lang")` and `m_locale.AddCatalog("open_es")`
  (`appData.cpp`), so at runtime wx reads **`lang/<langcode>/open_es.mo`** (e.g.
  `lang/ru/open_es.mo`).

The deploy step copies `locale/<code>.mo` → `lang/<code>/open_es.mo`. Edit under `locale/`;
ship under `lang/`.

### 2.3 The workflow (adding / updating strings)

Uses the standard gettext tools (Poedit bundles them):

```bash
# 1. Re-extract the template from current sources.
xgettext --from-code=UTF-8 --keyword=_ --keyword=wxTRANSLATE \
         --keyword=wxPLURAL:1,2 --language=C++ --no-wrap \
         --output=locale/open_es.pot --files-from=<list-of-cpp-files>

# 2. Merge new entries into each language (keeps existing translations).
msgmerge --no-wrap --update --backup=none locale/ru.po locale/open_es.pot
msgmerge --no-wrap --update --backup=none locale/uk.po locale/open_es.pot

# 3. Fill empty msgstr entries (by hand or in Poedit).

# 4. Compile to .mo.
msgfmt --check-format --output-file=locale/ru.mo locale/ru.po
msgfmt --check-format --output-file=locale/uk.mo locale/uk.po

# 5. Deploy: copy locale/<code>.mo -> lang/<code>/open_es.mo
```

The Poedit GUI (`File → Open` on the `.po`) does steps 1 + 3 in one pass; the CLI route is
for batch/CI.

### 2.4 Adding a whole new UI language

1. `msginit --locale=<code> --input=locale/open_es.pot --output=locale/<code>.po`, translate,
   `msgfmt` to `locale/<code>.mo`.
2. Ship `lang/<code>/open_es.mo`.
3. The language becomes selectable through the platform locale that `InitLocale` resolves
   (`wxLocale::FindLanguageInfo`); an unknown code falls back to the system language.

---

## 3. Configuration language — inline raw-loc strings

Metadata text that a *developer of a configuration* authors — a Catalog's Synonym, a form
control's Title, an enum value's Presentation — is **not** gettext. It is stored **inside the
configuration**, translated per user language, in a compact inline format.

### 3.1 The raw-loc string format

One string carries every language, keyed by short language code:

```
ru = 'Справочник'; en = 'Catalog'; uk = 'Довідник';
```

Grammar (from `ibBackendLocalization::CreateLocalizationArray`):
`code = 'text';` repeated. `code` is the language short code; the value is single-quoted;
entries are `;`-terminated; whitespace outside quotes is ignored. The parser is a small state
machine over `open_text` / `open_symbol` flags — no regex, no allocation per call beyond a
`thread_local` reuse buffer.

### 3.2 The API (`ibBackendLocalization`, static-only)

| Call | Does |
|---|---|
| `IsLocalizationString(raw)` | is this a `code='…';` blob (vs a plain string)? |
| `CreateLocalizationArray(raw, out)` | parse → `[{m_code, m_data}, …]` |
| `GetTranslateFromArray(langCode, array)` | pick one language's text |
| `GetTranslateGetRawLocText(raw)` | parse **and** resolve to the **active** language in one call |
| `CreateLocalizationRawLocText(text)` | wrap a plain string as `activeLang = 'text';` (authoring) |

`GetTranslateGetRawLocText(raw)` is the everyday read path: given a stored blob, hand back the
string for the language this session should see. A blob that has no entry for the active
language falls back per `GetTranslateFromArray`'s resolution order.

### 3.3 The active language — `GetUserLanguage()` (HOT PATH)

`GetTranslateGetRawLocText(raw)` calls `GetUserLanguage()` to know which entry to return.
This is a **hot path**: one report line or form synonym hits it once per translatable field ×
row count — millions of calls on a 10k-row report. So it does the minimum:

```
GetUserLanguage() =
    ibSession::Current()->GetLanguageCode()   // per-session, pre-resolved m_resolvedLanguageCode
    || ms_strUserLanguage                      // process-wide default (SetUserLanguage), "en" if unset
```

The per-session code (`m_resolvedLanguageCode`) is computed once on authentication
(`SetUserInfo`) as `override || user-default`, so the hot path is a single field load, no
logic. Two concurrent web sessions each render their own user's language because the code
lives on the session, not on a global.

The process-wide default (`SetUserLanguage`) is pinned at boot to the configuration's main
language code before metadata loads; sessionless callers (codeRunner, bootstrap) resolve
through it.

### 3.4 Adding a language to a configuration

There is no catalog to compile — a configuration language is added *field by field* in the
stored blobs: give each translated field a new `code = '…';` entry. The Designer's advprop
string editor routes through `ibBackendLocalization`, so authoring a translation there writes
the blob for you. A field with no entry for a requested language falls back to whatever the
blob does have (see `GetTranslateFromArray`).

---

## 4. What is *not* here

- **Number / date formatting** is not part of either surface — it rides the platform locale
  (`wxLocale`) for the UI side and the value types (`ibNumber`, date) for data.
- **Keywords are English-only.** The script language has no localized keywords (see
  [compiler-pipeline.md](compiler-pipeline.md)); localization touches user-facing text, never
  syntax.
