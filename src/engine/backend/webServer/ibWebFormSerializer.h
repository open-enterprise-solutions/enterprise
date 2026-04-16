////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : OES metadata → Formily JSON Schema converter (Phase 3)
//	              Converts ibValueMetaObjectRecordData to a Formily-compatible
//	              JSON Schema object ready for the GET /api/form/:resource/schema
//	              endpoint.
////////////////////////////////////////////////////////////////////////////

#ifndef _IB_WEB_FORM_SERIALIZER_H__
#define _IB_WEB_FORM_SERIALIZER_H__

#include "backend/backend.h"
#include "backend/clsid.h"

#include <json.hpp>

class ibValueMetaObjectRecordData;
class ibValueMetaObjectAttributeBase;
class ibValueMetaObjectTableData;
struct ibTypeDescription;

class BACKEND_API ibWebFormSerializer {
public:

	// Produce the complete Formily JSON Schema for an OES metadata object.
	// Handles catalogs, documents, data processors, and reports.
	// Returns an empty json object if metaObject is nullptr.
	static nlohmann::json SerializeFormSchema(ibValueMetaObjectRecordData* metaObject);

	// Produce the Formily field schema for a single attribute.
	static nlohmann::json SerializeFieldSchema(ibValueMetaObjectAttributeBase* attr);

	// Produce the Formily array + nested object schema for a tabular section.
	static nlohmann::json SerializeTableSchema(ibValueMetaObjectTableData* table);

private:

	// Resolve the best display title: synonym if present, otherwise name.
	static std::string ResolveTitle(const wxString& name, const wxString& synonym);

	// Map an ibTypeDescription to the Formily x-component string.
	// Returns "Input" when the type is unknown or composite.
	static std::string MapTypeToComponent(const ibTypeDescription& typeDesc);

	// Build the x-component-props object for a given type description.
	static nlohmann::json MapTypeToProps(const ibTypeDescription& typeDesc);

	// Derive the JSON Schema primitive type keyword ("string", "number",
	// "boolean", "array", "object") from an OES type description.
	static std::string MapTypeToJsonType(const ibTypeDescription& typeDesc);

	// Resolve a reference CLSID to its API resource string, e.g.
	// "catalog.counterparties" or "enumeration.vat_rates".
	// Returns an empty string if the CLSID cannot be resolved.
	static std::string ResolveRefResource(ibClassID clsid);

	// Collect the enum values (name + synonym) for a reference CLSID that
	// points to an ibValueMetaObjectEnumeration.
	static nlohmann::json CollectEnumOptions(ibClassID clsid);

	// Determine the meta type string ("catalog", "document", …) for the
	// given meta object CLSID.
	static std::string MetaTypeLabel(ibClassID metaClsid);
};

#endif // _IB_WEB_FORM_SERIALIZER_H__
