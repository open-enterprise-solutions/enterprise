#ifndef __SERIALIZE_JSON_PROVIDER_H__
#define __SERIALIZE_JSON_PROVIDER_H__

////////////////////////////////////////////////////////////////////////////
//	Description : ibJsonProvider — a READABLE, AI-facing view of the node tree.
//
//	A second ibFormatProvider next to ibBinaryProvider: it renders the SAME
//	ibDataNode tree as JSON text (scalar -> value, Bool -> true/false, Number ->
//	number, String -> "text", Binary -> "base64:...", Child -> { object },
//	Array -> [ list ]). Structural keys are underscore-prefixed (_clsid, _metaId,
//	_children, _raw) so they read apart from the data.
//
//	Read (JSON -> tree) is IMPLEMENTED and complete — a self-contained recursive-descent
//	parser covering everything Write emits: nested children, arrays, base64 binaries, the
//	structural keys, the synthetic ones. It is deliberately NOT wired into any load path:
//	nothing calls it, and ibBinaryProvider stays the round-trip format.
//
//	Why it is not a round trip. The VIEW is lossy by design — readability is its whole
//	point — so three things do not survive Write -> Read, and no parser can undo them:
//	  1. Fields and Properties are emitted as ONE flat key set, so which area a key came
//	     from is gone (EmitNode: "areas don't collide in practice"). On the way back a
//	     Child value goes to Properties (where ibDataNode::Child puts it by construction)
//	     and every other key goes to Fields.
//	  2. Date becomes an ISO STRING — indistinguishable from a String that happens to look
//	     like a date, so it reads back as a String.
//	  3. NodeType emits the resolved type NAME (numeric clsid only as a fallback), and
//	     TypeDesc is injected beside typeId with no node entry behind it. TypeDesc is
//	     parsed and dropped; a name-form NodeType needs SetTypeLookup (below) to become a
//	     clsid again, and without one the node keeps clsid 0 rather than inventing a value.
//	Closing 1 and 2 means changing what WRITE emits — a separate lossless emitter, not more
//	parser. Malformed input throws with the byte offset rather than yielding a partial tree.
////////////////////////////////////////////////////////////////////////////

#include "backend/serialize/dataBuilder.h"
#include <string>
#include <functional>

class BACKEND_API ibJsonProvider : public ibFormatProvider {
public:
	bool Write(const ibDataNode& root, ibWriter& writer) const override;
	bool Read(ibReader& reader, ibDataNode& root) const override;  // implemented; nothing calls it

	// clsid -> readable type name. REFERENCE types (CatalogRef.X, DocumentRef.Y,
	// DataProcessor.Object) are metadata-specific, so the caller injects a resolver
	// backed by ibMetaData::GetTypeCtor; a static built-in fallback covers the rest.
	// Without a resolver only built-in types resolve (refs stay numeric).
	void SetTypeResolver(std::function<wxString(ibClassID)> fn) { m_resolveType = std::move(fn); }

	// The INVERSE, for Read: type name -> clsid. Same asymmetry as above — a reference
	// type's name only means something against the metadata that defined it, so the
	// caller supplies the lookup. Unset (the default) leaves a name-form NodeType at
	// clsid 0; a numeric NodeType needs no lookup and always resolves.
	void SetTypeLookup(std::function<ibClassID(const wxString&)> fn) { m_lookupType = std::move(fn); }

private:
	void EmitNode(const ibDataNode& node, std::string& out, int depth) const;
	void EmitValue(const ibDataValue& value, std::string& out, int depth) const;
	wxString ResolveType(ibClassID clsid) const;

	std::function<wxString(ibClassID)>       m_resolveType;
	std::function<ibClassID(const wxString&)> m_lookupType;
};

#endif // !__SERIALIZE_JSON_PROVIDER_H__
