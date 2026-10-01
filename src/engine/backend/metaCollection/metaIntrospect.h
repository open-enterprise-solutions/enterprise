#ifndef _IB_META_INTROSPECT_H_
#define _IB_META_INTROSPECT_H_

////////////////////////////////////////////////////////////////////////////
//	Description : reading the SHAPE of a configuration — kinds, names, one
//	              object as a node
////////////////////////////////////////////////////////////////////////////
//
// The questions a tool asks before it can write anything: what kinds are
// there, what is called what, and what does THIS object look like.
//
// ⭐ THE ANSWER IS A NODE, not text. A caller that renders JSON and a caller
// that inspects the answer want the same thing at different depths, and a door
// that only produced text forced the second one to parse back what we had just
// written. The string form below is a RENDERING of the node, kept because the
// plugin capability promises a string.
//
// ⭐ AN OBJECT IS FOUND BY ITS ID. A name is what a person types and what a
// rename changes; the identity is the metaID, and the rendered node carries it
// (`NodeId`) — so an answer already holds the handle its own follow-up needs.
// Finding by name stays, because a caller that has only read a list has only
// names.
//
// WHY IT LIVES HERE. The bodies used to sit in an anonymous namespace inside
// plugin/pluginHost.cpp — the plugin boundary owning a mechanism instead of
// being a window onto one. They have two consumers now (the plugin capability
// and the MCP server in the core), so they moved next to the metadata they
// read, and the plugin host delegates.
//
// A KIND NAME IS THE ONE A SCRIPT WRITES ("Catalog", "Document",
// "InformationRegister"), resolved through the registry every metatype puts
// itself into. Nothing here keeps a list: a new metatype is answerable the day
// it registers.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_core.h"
#include "backend/clsid.h"
#include "backend/serialize/dataBuilder.h"

#include <functional>
#include <vector>

#include <wx/string.h>

class ibMetaData;
class ibValueMetaObject;

// KIND NAME → CLASS ID. Answers 0 when there is no configuration open, the
// name is unknown, or the name belongs to something that is not a metatype (a
// value class, a control) — asking for "Array" must not return every array in
// the tree.
BACKEND_API ibClassID ibResolveMetaKind(const ibMetaData* metaData, const wxString& kind);

// Every metaobject of a kind, in tree order. Empty when the kind does not resolve.
//
// ⭐ THE OBJECTS, because the name alone is rarely the end of it: a caller that lists a kind goes
// on to ADDRESS one, and every verb downstream — metadata_get, metadata_delete, section_*,
// predefined_* — is addressed by NodeId. This used to answer names only, and the id had to be
// fetched back one FindAnyObjectByFilter at a time from names this walk had just thrown away.
BACKEND_API std::vector<const ibValueMetaObject*> ibListMetaObjects(const ibMetaData* metaData,
	const wxString& kind);

// The same walk, projected to names — which is all four of its other callers want.
BACKEND_API std::vector<wxString> ibListMetaObjectNames(const ibMetaData* metaData, const wxString& kind);

// FINDING. Null when the kind does not resolve or nothing carries that
// name / id. The id form searches the whole tree, so it finds an attribute or a
// tabular section as readily as the object that owns it.
BACKEND_API ibValueMetaObject* ibFindMetaObject(ibMetaData* metaData,
	const wxString& kind, const wxString& name);
BACKEND_API ibValueMetaObject* ibFindMetaObjectById(ibMetaData* metaData, const ibMetaID& id);

// THE OBJECT AS A NODE — its properties, its attributes with their types, its
// tabular sections, its children. False when the object cannot describe itself.
BACKEND_API bool ibBuildMetaObjectNode(ibValueMetaObject* object, ibDataNode& node);

// WHICH SYNTAX FORM THIS CONFIGURATION IS WRITTEN IN — true for the word-fenced
// dialect (`If … Then … EndIf`), false for the C-style one (`if (…) { … }`).
//
// It is a question about a configuration, not a preference: the two are
// different languages to the compiler, and handing anyone the other one's
// spelling produces code that reads correctly and does not compile. Answers the
// C-style form when nothing is open — that is what a new configuration is
// created with, so it is the honest default rather than a coin toss.
BACKEND_API bool ibConfigurationWritesInWords(const ibMetaData* metaData);

// clsid → the portable type name a configuration writes ("CatalogRef.Goods").
// Handed to a JSON provider so a rendered answer says what a type IS instead of
// printing a number that means nothing outside this process. Bound to the
// configuration it was asked about: the same id names different types in two
// open trees.
BACKEND_API std::function<wxString(ibClassID)> ibMetaTypeResolver(const ibMetaData* metaData);

// The node, rendered. ⚠ READ ONLY: the JSON view is lossy BY DESIGN (see
// serialize/jsonProvider.h) — it is a readable rendering, not a way back in.
// Writing goes property by property, through the gate that knows what may be
// written; it never goes by handing a whole document back.
BACKEND_API wxString ibDescribeMetaObject(ibMetaData* metaData,
	const wxString& kind, const wxString& name);

#endif // _IB_META_INTROSPECT_H_
