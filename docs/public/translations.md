# The engine's own translations

**What.** The engine translates its own words (refusals, captions, the lines of its windows) itself:
`core/localization/dictionary.h`. One language is one `ibDictionary`, the set of that language's
gettext catalogs (`ibMsgCatalog`, one `.po` each, read as written). It is loaded once and kept for the process. `_()`,
`ibPLURAL` and `ibGETTEXT_IN_CONTEXT` are the engine's macros over it. A string made at run time is
translated with `ibGetTranslation`. The language is the THREAD's.

**Why.** wx's `wxTranslations` is one object per process. A process that holds several bases (the
web server, a file base per publication) could speak only one language: the first base's. Now every thread that works
for a base speaks that base's language.

## The choice per thread

Asked on every call, in this order:

1. **The base the thread works for** (`ibDictionary::SetSource`, installed in `appHost.cpp`). That
   is the session's base, the base the thread is bound to, or the base whose service runs on it.
   The base answers with its dictionary (`ibApplicationInstance::GetDictionary`), a pointer read
   without a lock.
2. **The process's language** (`SetProcessLanguage`). On the server it is what `InitLocale` settled
   from the request or `backend.conf` (the shipped one names `en`), else the machine's own language,
   named as wxWidgets names it (`ibSystemLanguage`), and US English where even that cannot be set. On a thin
   client it is the `Locale` of the login's answer: the base's locale, else the server's language.
3. Otherwise the text itself.

## The base's locale

- A shared row of `sys_settings`: category `Regional`, a fixed object, no user. It is read once, when
  the base opens (`ReadLocale`, `appData.cpp`).
- No row: the base speaks the process's language.
- A child process the base starts gets `--locale` from `ibApplicationInstance::GetLocale()`: the
  base's locale, else the process's.
- There is no page to set it yet. The old setter was in the thick designer.

## A language's name

`CanonicalLanguage` gives one name for any spelling. `uk_UA`, `uk-UA`, `UK_ua` and `uk_UA.UTF-8` all
become `uk_UA`, and `uk` stays `uk`. The language is two or three letters and the region, if any, is
two. Anything else, including English names such as `Ukrainian`, names no language. There is no
table of languages and no machine default. `ForLanguage` and `SetProcessLanguage` take any spelling.

## The catalogs

- The path is `<prefix>/<language>/<domain>.po` or `<prefix>/<language>/LC_MESSAGES/<domain>.po`.
  The language is tried as named, then as the language alone (`uk_UA` → `uk`).
- Prefixes, in order: those from `AddCatalogLookupPathPrefix`, then the working folder's `lang`, the
  program folder's `lang`, and the first `lang` found walking up from the executable.
- The engine's domain is `open_es` (`kEngineDomain`). Every dictionary has it, with no one declaring it.
  Another domain is declared (`AddDomain`) before any language is asked for: a dictionary does not
  change once made.
- The `.po` is read as the translators write it; nothing compiles it (no `.mo`, no `msgfmt`). The reader takes
  what `msgfmt --check-format` compiled:
  - not a translation: an entry still `fuzzy`, an empty `msgstr`, an obsolete entry (`#~`), the header;
  - not taken, and journalled: a `c-format` translation that names other conversions than its text;
  - the whole catalog refused, journalled with the line: a line no `.po` has.
- A charset other than UTF-8 is journalled and read as UTF-8.
- The plural-forms calculator comes from wxWidgets, under the wxWindows Library Licence
  ([NOTICE.md](../../NOTICE.md)); the reader is the engine's.

## Where wx stays

- The C library's locale is set by `core/localization/locale.h` (`ibSetLocale`, wx's locale code ported); `wxLocale`
  is not used.
- wx's own words in the forked code - the stock labels of New, Open..., Save As..., Undo, Paste and the rest, the
  cursor names, the Yes and No of a message box - are the engine's words: one catalog, `open_es`. A stock label
  comes from `ibGetStockLabel` (`deskgui/univ/stockItem.h`, wx's table) through `_()`. Their translations were
  taken from wx's `locale/<code>.po` into `open_es.po` (`msgmerge --compendium`), with the entries wx left fuzzy
  settled, since a fuzzy entry is not read. One localization - wx's translations object is not made, and there
  is no `wxstd` catalog. The code of the wxWidgets library itself speaks English.
- `WXINTL_NO_GETTEXT_MACRO` is defined for every target that links core: wx's `_()` is off. `core/core.h`
  brings the engine's.
- wx's `wxGetTranslation`, `wxPLURAL`, `wxGETTEXT_IN_CONTEXT` and `wxTRANSLATE` are not used. Their
  replacements are `ibGetTranslation`, `ibPLURAL`, `ibGETTEXT_IN_CONTEXT` and `ibTRANSLATE`.

## The browser page

- The page says its own words (menus of its own, windows, refusals) from the same catalog. The web server serves
  the program folder's `lang/` beside the page, at `lang/` and `<publication>/lang/`.
- `www/src/localization.js` reads `<language>/open_es.po`, the language as named and then alone, by the engine's
  rules (`readCatalogue`, `unquote`, `conversions` mirror `dictionary.cpp`).
  `_(text)` and `_(text, context)` are the engine's `_()` and `ibGETTEXT_IN_CONTEXT`.
- The language is the browser's before the login, then the login's `Locale`. No catalog for it: English.
- What the server sends (its menus, forms, messages) comes translated already.
