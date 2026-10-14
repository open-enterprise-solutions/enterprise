# The engine's own translations

**What.** The engine's words - refusals, captions, the lines of its windows - are translated by the
engine itself: `core/localization/translations.h`. One language is one set of catalogs (`ibMsgCatalog`,
a gettext `.mo` each), loaded once and kept for the process; `_()` and its kin (`wxPLURAL`,
`wxGETTEXT_IN_CONTEXT`) are the engine's macros over it, and the language a call speaks in is the
THREAD's.

**Why.** wxWidgets' `wxTranslations` is one object for the whole process, and wx's `_()` asks it. A
process that holds several bases - the application server - could therefore speak one language only,
the first base's. The base names its locale ([date-model.md](date-model.md), *Regional settings*); with the
engine translating its own words, every thread that works for a base speaks that base's language, and
two bases in one server answer their clients each in its own. It is also a step towards the server side
standing without wx's widgets: the reader of a catalog is small, and it is now the engine's own (still on
`wxString` and wx's files, as the rest of core is).

## The choice per thread

The language of a translation is asked on every call, in this order:

1. **The source the backend installs** (`ibTranslations::SetSource`, `appHost.cpp`): the locale of
   the base the thread works for - its session's base, the base it is bound to, or the base whose own
   service runs on it - the way `ibDateTime::Now()` takes its distance and the journal its context. A base
   that names no locale answers nothing here.
2. **The process's language** (`SetProcessLanguage`): what `InitLocale` settled from the first base, or
   what a thin client's `--locale` said, by any of a language's names (`CanonicalLanguage`: `uk`, `uk-UA`,
   `Ukrainian` are all `uk_UA`), or the machine's own language when it said nothing. The thin client has
   no `wxLocale`; its windows speak this.
3. Nothing: the text itself.

Core knows no base. The source is a callback that answers a set, not a name; the base's locale is read
through a per-thread copy keyed by the base and its locale's generation (`ibRegionalSettings::
LocaleGeneration`, unique for the process), so a translation copies no string and takes no lock on its
way - `_()` is said a dozen times per control made, on every thread at once. A set does not change
once made: the domains are declared before any language is asked for, and the pointers a thread keeps
stay valid for the process.

## The catalogs

- `lang/<language>/<domain>.mo` (or `…/LC_MESSAGES/<domain>.mo`) under what `AddCatalogLookupPathPrefix`
  adds (first), then the working folder's `lang`, the program folder's `lang`, and the first `lang` found
  walking up from the executable (a run from the build tree). A language named with its region (`uk_UA`)
  is looked for as named, then as its language alone (`uk`).
- The engine's domain is `open_es` (`AddDomain`, declared by the backend when the host comes up, before any
  base opens); every language loads it when its set is made. The catalogs themselves
  (`lang/ru/open_es.po` and the `.mo` built from it) do not change.
- The reader of a `.mo` and the plural-forms calculator are wxWidgets' own (`src/common/translation.cpp`),
  renamed and kept under the wxWindows Library Licence - [NOTICE.md](../../NOTICE.md). One reading differs:
  a catalog that names no charset is read as UTF-8 here, where wx reads it in the process's locale.

## Where it stops

- `wxLocale` stays: the C locale, `wxUILocale`, and wx's own windows (`wxstd`) are wx's. `InitLocale` still
  adds `open_es` to it, for wx's own `_()` inside the library; the engine's `_()` never reaches wx -
  `WXINTL_NO_GETTEXT_MACRO` is said for every target that links core, so a unit that says `_()` without
  `core/core.h` does not compile rather than translating through wx unseen. wx keeps `wxPLURAL` and
  `wxGETTEXT_IN_CONTEXT` on regardless: a unit must have the header before it says them, and every unit
  that says them today does.
- The sets are kept for the process and never freed, so the CRT's leak report at a test process's exit
  lists them beside the string pool's blocks.
- A `.mo` with a charset other than UTF-8 is read through `wxCSConv`, as wx reads it.
- The web server and the thin client over an application server: the thin client's windows speak its own
  `--locale`; the server's words to it speak the base's.
