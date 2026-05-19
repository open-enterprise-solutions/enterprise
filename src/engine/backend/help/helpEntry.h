/////////////////////////////////////////////////////////////////////////////
// Single help-corpus topic.
//
// Backing record for one identifier in the syntax helper: keyword,
// built-in function, system enum, metadata class / attribute / method,
// primitive type, collection, form event, operator. One ibHelpEntry per
// locale per id; ids are canonical and locale-independent.
//
// See docs/syntax-helper-design.md §2 for the binding contract:
// - Id grammar  ……………………… §2.2  (e.g. "fn.Message", "attr.Document.Invoice.Code")
// - JSON schema  ……………………… §2.3
// - Category dictionary  ……… §2.4  (locale-stable category_keys + per-locale display)
// - Lifetime contract  …………… §3.4  (snapshot-via-shared_ptr; never holds raw refs)
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_ENTRY_H_
#define _IB_HELP_ENTRY_H_

#include "backend/backend.h"

#include <vector>

// Stable, locale-independent classification of help topics. The string
// prefix in `ibHelpEntry::id` mirrors this enum (see §2.2) — keep them in
// sync. New kinds: append, never reorder (id strings already in JSON
// corpora reference these positions implicitly via the prefix table).
enum class ibHelpKind {
	kKeyword,            // "kw.<Word>"          — language keywords
	kSystemFunction,     // "fn.<Name>"          — built-in functions / procs
	kSystemConstant,     // "const.<Name>"       — built-in constants (PageBreak, LineBreak, …)
	kSystemEnum,         // "enum.<Type>.<Value>" — system enum literal values
	kMetaObjectType,     // "mo.<Kind>.<Name>"   — concrete metadata object (e.g. mo.Catalog.Invoice)
	kMetaObjectAttribute,// "attr.<Kind>.<Name>.<Attr>"
	kMetaObjectMethod,   // "meth.<Kind>.<Name>.<Method>"
	kPrimitiveType,      // "type.<Name>"        — Date, String, Number, Boolean, …
	kCollection,         // "cls.<Name>"         — ValueList, Map, Array, …
	kEvent,              // "ev.<Scope>.<Name>"  — form / object events
	kOperator,           // "op.<Symbol>"        — language operators
};

// One help topic in a single locale. Locales are stored in side-by-side
// directories (data/help/<locale>/), one ibHelpEntry per locale per id —
// the corpus held by appData reflects one locale at a time.
struct BACKEND_API ibHelpEntry {
	// Canonical, locale-independent. Loader rejects duplicates within a
	// single source corpus (platform OR per-config); cross-source
	// collisions become overlays (§3.6).
	wxString id;

	// Localised identifier — the user types this in the editor and
	// search bar; resolver matches against it. May coincide with
	// nameEn for English locales.
	wxString nameLocal;

	// English identifier ("Date"). Stable join key across locales — used
	// by the resolver when `preferLocalName == false`, and by tooling
	// that needs locale-independent reference (id grammar, alias maps).
	wxString nameEn;

	// One-line call form, e.g. "Date(<Year>, <Month>, <Day>)". Used in
	// autocomplete tooltips and as the entry's subtitle in the detail
	// pane.
	wxString signature;

	// Long-form prose. Authored in markdown (subset documented in §8 —
	// headings, lists, fenced code, no tables). Rendered to HTML by the
	// detail-view at display time; not pre-rendered on disk.
	wxString description;

	// Multi-line formatted declaration / call form for the "Syntax"
	// section of the detail pane.
	wxString syntaxBlock;

	// Markdown <dl>-style key:value list — one entry per formal parameter.
	wxString parameters;

	// Return-value semantics. Empty for procedures and statement-form
	// keywords.
	wxString returnDescr;

	// Worked example (markdown fenced code block). Multiple examples
	// concatenated with blank lines between blocks.
	wxString example;

	// Free-form tier list — OES runtime targets the entry is valid on
	// (e.g. "Designer, codeRunner, daemon, wenterprise-server").
	// Localised string, no parsing required by readers.
	wxString availability;

	ibHelpKind kind = ibHelpKind::kKeyword;

	// Locale-stable category keys (NOT display strings). The detail pane
	// looks up the display name in `data/help/<locale>/_categories.json`
	// at render time. Adding a locale = adding one dictionary + one
	// buckets directory; no entry schema change.
	//   e.g. {"applied_objects","documents","invoice","properties","code"}
	std::vector<wxString> categoryKeys;

	// Opaque ids of related entries — rendered as "See
	// also" links in the detail pane. Loader validates each at LoadAll
	// time; dangling ids demote to load warnings (kWarning) and the
	// entry still loads.
	std::vector<wxString> seeAlso;

	// Set after human review (Phase 7 help-editor). LLM-filled drafts
	// ship with `reviewed=false`; the UI tags drafts with a "draft" badge,
	// and the CI cutoff PR (Phase 7) starts refusing ship-builds with any
	// `reviewed=false` entries in the platform corpus. Per-config
	// corpora are exempt from the gate (user content).
	bool reviewed = false;

	// True when this entry comes from the per-configuration corpus
	// (configCacheDir/help/<locale>/) rather than the platform corpus
	// (installDataDir/help/<locale>/). The merging constructor in
	// §3.6 sets this flag. UI uses it to tag config-derived entries
	// visually so authors can distinguish "OES platform docs" from
	// "this configuration's docs".
	bool fromConfiguration = false;
};

#endif // _IB_HELP_ENTRY_H_
