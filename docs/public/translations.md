# The engine's own translations

**What.** The engine translates its own words (refusals, captions, the lines of its windows) itself:
`core/localization/dictionary.h`. One language is one `ibDictionary`, the set of that language's
gettext catalogs (`ibMsgCatalog`, one `.mo` each). It is loaded once and kept for the process. `_()`,
`ibPLURAL` and `ibGETTEXT_IN_CONTEXT` are the engine's macros over it. A string made at run time is
translated with `ibGetTranslation`. The language is the THREAD's.

**Why.** wx's `wxTranslations` is one object per process. A process that holds several bases (the
application server) could speak only one language: the first base's. Now every thread that works
for a base speaks that base's language.

## The choice per thread

Asked on every call, in this order:

1. **The base the thread works for** (`ibDictionary::SetSource`, installed in `appHost.cpp`). That
   is the session's base, the base the thread is bound to, or the base whose service runs on it.
   The base answers with its dictionary (`ibApplicationInstance::GetDictionary`), a pointer read
   without a lock.
2. **The process's language** (`SetProcessLanguage`). On the server it is what `InitLocale` settled
   from the request or `backend.conf`, else US English — never the machine's own language. On a thin
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

- The path is `<prefix>/<language>/<domain>.mo` or `<prefix>/<language>/LC_MESSAGES/<domain>.mo`.
  The language is tried as named, then as the language alone (`uk_UA` → `uk`).
- Prefixes, in order: those from `AddCatalogLookupPathPrefix`, then the working folder's `lang`, the
  program folder's `lang`, and the first `lang` found walking up from the executable.
- The engine's domain is `open_es` (`kEngineDomain`). Every dictionary has it, with no one declaring it.
  Another domain is declared (`AddDomain`) before any language is asked for: a dictionary does not
  change once made.
- The `.mo` reader and the plural-forms calculator come from wxWidgets, under the wxWindows Library
  Licence ([NOTICE.md](../../NOTICE.md)). A charset other than UTF-8 is journalled and read as UTF-8.

## Where wx stays

- `wxLocale` remains for the C locale and wx's own windows. `InitLocale` still adds `open_es` to it.
- wx's own words - the stock labels of New, Open..., Save As..., Undo, Paste and the rest, and its dialogs -
  come from its own catalog, `wxstd`, which `wxLocale::Init` loads by itself from the same `lang/<code>/`.
  `lang/<code>/wxstd.po` is wx's `locale/<code>.po`; in `ru` the three stock labels it left fuzzy
  (`&Find...`, `Rep&lace...`, `&CD-ROM`) are settled, since a fuzzy entry is not compiled.
- `WXINTL_NO_GETTEXT_MACRO` is defined for every target that links core: wx's `_()` is off. `core/core.h`
  brings the engine's.
- wx's `wxGetTranslation`, `wxPLURAL`, `wxGETTEXT_IN_CONTEXT` and `wxTRANSLATE` are not used. Their
  replacements are `ibGetTranslation`, `ibPLURAL`, `ibGETTEXT_IN_CONTEXT` and `ibTRANSLATE`.
