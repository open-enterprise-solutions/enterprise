/////////////////////////////////////////////////////////////////////////////
// Identifier → help entry resolution.
//
// Pure free function — same inputs always produce same outputs. Lives
// outside ibHelpCorpus so the storage class stays storage-only and the
// resolver is swappable / unit-testable independently.
//
// `ibHelpResolveHint` is pure data: strings + enum. The desktop editor
// builds it from its precompile context; the web client builds the same
// struct from URL query params (?parent=Documents.Invoice&role=member_access).
// Same struct, two producers.
//
// See docs/syntax-helper-design.md §3.2 for the contract.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_RESOLVER_H_
#define _IB_HELP_RESOLVER_H_

#include "backend/backend.h"

#include <vector>

class ibHelpCorpus;
struct ibHelpEntry;

// Surrounding-token context for disambiguating identifiers. Optional —
// a default-constructed hint produces a kind-agnostic name match across
// every locale-equivalent identifier in the corpus.
struct BACKEND_API ibHelpResolveHint {
	enum class Role {
		kUnknown,         // no context — match any kind
		kTypeName,        // identifier appears as a type annotation
		kCallExpression,  // identifier is followed by '(' — function/proc
		kMemberAccess,    // identifier follows a '.' — attribute/method
		kAssignmentTarget // left of '=' — variable / settable property
	};

	// Left of '.' when role == kMemberAccess. Empty otherwise.
	wxString parentIdentifier;

	// Closest enclosing metadata object id (e.g. "mo.Document.Invoice").
	// Empty when the lookup is outside an object module.
	wxString containingMetaObjectId;

	Role expectedRole = Role::kUnknown;

	// When true (default), the resolver matches against the localised
	// name first (`nameLocal`); when false it matches against `nameEn`.
	// Web clients with explicit English-only intent pass false.
	bool preferLocalName = true;
};

// Resolve an identifier to one or more candidate help entries. Returns
// an empty vector when nothing matches. Lookup contract:
//
//   1. Exact match on the preferred-locale name → all entries with that
//      name (potentially many — e.g. a primitive type, a function,
//      and a metadata attribute may all share the same identifier).
//   2. If the hint pins kind (e.g. kCallExpression → function-like
//      kinds), filter candidates by kind before returning.
//   3. parentIdentifier / containingMetaObjectId tighten metadata-class
//      matches — an attribute called Code under Document.Invoice wins
//      over an attribute called Code under Catalog.Item when the hint
//      contains the matching parent.
//
// Pointers returned point into the corpus's entry storage and are
// stable for the corpus's lifetime — the caller must keep its
// shared_ptr<const ibHelpCorpus> snapshot through any operation that
// uses these pointers (§3.4 lifetime contract).
BACKEND_API std::vector<const ibHelpEntry*>
ResolveByName(const ibHelpCorpus&      corpus,
              const wxString&          identifier,
              const ibHelpResolveHint& hint = {});

#endif // _IB_HELP_RESOLVER_H_
