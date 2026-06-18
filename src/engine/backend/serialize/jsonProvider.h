#ifndef __SERIALIZE_JSON_PROVIDER_H__
#define __SERIALIZE_JSON_PROVIDER_H__

////////////////////////////////////////////////////////////////////////////
//	Description : ibJsonProvider — a READABLE, AI-facing view of the node tree.
//
//	A second ibFormatProvider next to ibBinaryProvider: it renders the SAME
//	ibDataNode tree as JSON text (scalar -> value, Bool -> true/false, Number ->
//	number, String -> "text", Binary -> "base64:...", Child -> { object },
//	Array -> [ list ]). Structural keys are underscore-prefixed (_clsid, _metaId,
//	_children, _raw) so they read apart from the data. Write-only for now (export
//	/ inspection); Read (JSON -> tree) is a later step.
////////////////////////////////////////////////////////////////////////////

#include "backend/serialize/dataBuilder.h"
#include <string>
#include <functional>

class BACKEND_API ibJsonProvider : public ibFormatProvider {
public:
	bool Write(const ibDataNode& root, ibWriter& writer) const override;
	bool Read(ibReader& reader, ibDataNode& root) const override; // TODO: JSON -> tree

	// clsid -> readable type name. REFERENCE types (CatalogRef.X, DocumentRef.Y,
	// DataProcessor.Object) are metadata-specific, so the caller injects a resolver
	// backed by ibMetaData::GetTypeCtor; a static built-in fallback covers the rest.
	// Without a resolver only built-in types resolve (refs stay numeric).
	void SetTypeResolver(std::function<wxString(ibClassID)> fn) { m_resolveType = std::move(fn); }

private:
	void EmitNode(const ibDataNode& node, std::string& out, int depth) const;
	void EmitValue(const ibDataValue& value, std::string& out, int depth) const;
	wxString ResolveType(ibClassID clsid) const;

	std::function<wxString(ibClassID)> m_resolveType;
};

#endif // !__SERIALIZE_JSON_PROVIDER_H__
