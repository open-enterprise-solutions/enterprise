////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : OES metadata → Formily JSON Schema converter (Phase 3)
////////////////////////////////////////////////////////////////////////////

#include "ibWebFormSerializer.h"

#include "metadataConfiguration.h"
#include "metadata.h"

#include "metaCollection/metaObject.h"
#include "metaCollection/metaObjectComposite.h"
#include "metaCollection/attribute/metaAttributeObject.h"
#include "metaCollection/table/metaTableObject.h"
#include "metaCollection/enumeration/metaEnumObject.h"

#include "metaCollection/partial/catalog.h"
#include "metaCollection/partial/document.h"
#include "metaCollection/partial/enumeration.h"
#include "metaCollection/partial/dataProcessor.h"
#include "metaCollection/partial/dataReport.h"

#include "typeDescription.h"
#include "objCtorDefs.h"
#include "objCtor.h"

#include "backend_core.h"
#include "compiler/value.h"

#include <json.hpp>

using json = nlohmann::json;

//***********************************************************************
//*                          Local helpers                              *
//***********************************************************************

namespace {

// Convert wxString → std::string (UTF-8) without pulling in wx headers for
// callers that include only ibWebFormSerializer.h.
inline std::string WxToStd(const wxString& s)
{
	return std::string(s.ToUTF8());
}

// Lower-case a std::string (ASCII-safe; used for resource name generation).
std::string ToLower(std::string s)
{
	for (char& c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

} // anonymous namespace

//***********************************************************************
//*                       Private static helpers                        *
//***********************************************************************

std::string ibWebFormSerializer::ResolveTitle(const wxString& name, const wxString& synonym)
{
	if (!synonym.IsEmpty())
		return WxToStd(synonym);
	return WxToStd(name);
}

// ---------------------------------------------------------------------------
// MetaTypeLabel — map a metadata object CLSID to the REST resource prefix.
// ---------------------------------------------------------------------------
std::string ibWebFormSerializer::MetaTypeLabel(ibClassID metaClsid)
{
	if (metaClsid == g_metaCatalogCLSID)              return "catalog";
	if (metaClsid == g_metaDocumentCLSID)             return "document";
	if (metaClsid == g_metaEnumerationCLSID)          return "enumeration";
	if (metaClsid == g_metaInformationRegisterCLSID)  return "informationRegister";
	if (metaClsid == g_metaAccumulationRegisterCLSID) return "accumulationRegister";
	if (metaClsid == g_metaDataProcessorCLSID)        return "dataProcessor";
	if (metaClsid == g_metaReportCLSID)               return "report";
	return "object";
}

// ---------------------------------------------------------------------------
// ResolveRefResource — given a reference-type CLSID (e.g. "R_<metaID>"),
// return the REST resource string "catalog.products".
// Returns an empty string when the CLSID cannot be resolved.
// ---------------------------------------------------------------------------
std::string ibWebFormSerializer::ResolveRefResource(ibClassID clsid)
{
	if (activeMetaData == nullptr)
		return {};

	// GetTypeCtor resolves the CLSID to the registered constructor wrapper.
	ibCtorMetaValueType* ctor = activeMetaData->GetTypeCtor(clsid);
	if (ctor == nullptr)
		return {};

	ibValueMetaObject* metaObj = ctor->GetMetaObject();
	if (metaObj == nullptr)
		return {};

	const std::string typeLabel = MetaTypeLabel(metaObj->GetClassType());
	const std::string name      = ToLower(WxToStd(metaObj->GetName()));

	return typeLabel + "." + name;
}

// ---------------------------------------------------------------------------
// CollectEnumOptions — return [{value, label}] for an enumeration CLSID.
// ---------------------------------------------------------------------------
json ibWebFormSerializer::CollectEnumOptions(ibClassID clsid)
{
	json jOptions = json::array();

	if (activeMetaData == nullptr)
		return jOptions;

	ibCtorMetaValueType* ctor = activeMetaData->GetTypeCtor(clsid);
	if (ctor == nullptr)
		return jOptions;

	ibValueMetaObject* metaObj = ctor->GetMetaObject();
	if (metaObj == nullptr)
		return jOptions;

	auto* enumObj = wxDynamicCast(metaObj, ibValueMetaObjectEnumeration);
	if (enumObj == nullptr)
		return jOptions;

	for (auto* val : enumObj->GetEnumObjectArray()) {
		if (val->IsDeleted())
			continue;
		json opt;
		opt["value"] = WxToStd(val->GetName());
		const wxString syn = val->GetSynonym();
		opt["label"] = syn.IsEmpty() ? WxToStd(val->GetName()) : WxToStd(syn);
		jOptions.push_back(opt);
	}

	return jOptions;
}

// ---------------------------------------------------------------------------
// MapTypeToJsonType — derive the JSON Schema "type" keyword.
// ---------------------------------------------------------------------------
std::string ibWebFormSerializer::MapTypeToJsonType(const ibTypeDescription& typeDesc)
{
	const auto& clsids = typeDesc.GetClsidList();
	if (clsids.size() == 1) {
		const ibClassID c = clsids.front();
		if (c == g_valueBooleanCLSID) return "boolean";
		if (c == g_valueNumberCLSID)  return "number";
		if (c == g_valueDateCLSID)    return "string";  // ISO-8601 date string
		if (c == g_valueStringCLSID)  return "string";
		// Reference and enum types are represented as strings (the ref GUID/name)
		return "string";
	}
	// Multiple types — use string as the common denominator
	return "string";
}

// ---------------------------------------------------------------------------
// MapTypeToComponent — choose the Formily x-component.
// ---------------------------------------------------------------------------
std::string ibWebFormSerializer::MapTypeToComponent(const ibTypeDescription& typeDesc)
{
	const auto& clsids = typeDesc.GetClsidList();

	if (clsids.empty())
		return "Input";

	if (clsids.size() > 1) {
		// Multiple allowed types → composite widget
		return "CompositeField";
	}

	const ibClassID c = clsids.front();

	if (c == g_valueNumberCLSID)  return "NumberInput";
	if (c == g_valueStringCLSID)  return "Input";
	if (c == g_valueBooleanCLSID) return "Checkbox";

	if (c == g_valueDateCLSID) {
		// ibDateFractions_DateTime → DatePicker with showTime
		// ibDateFractions_Date      → DatePicker
		// ibDateFractions_Time      → TimePicker
		const ibDateFractions frac = typeDesc.GetDateFraction();
		if (frac == ibDateFractions_Time)
			return "TimePicker";
		return "DatePicker";
	}

	// Reference or Enum — resolve via ctor
	if (activeMetaData != nullptr) {
		ibCtorMetaValueType* ctor = activeMetaData->GetTypeCtor(c);
		if (ctor != nullptr) {
			const ibCtorObjectMetaType refType = ctor->GetMetaTypeCtor();

			if (refType == ibCtorObjectMetaType_Reference) {
				// Distinguish enumeration from catalog/document references
				ibValueMetaObject* metaObj = ctor->GetMetaObject();
				if (metaObj != nullptr) {
					if (metaObj->GetClassType() == g_metaEnumerationCLSID)
						return "Select";
					return "RefField";
				}
			}
		}
	}

	return "Input";
}

// ---------------------------------------------------------------------------
// MapTypeToProps — build x-component-props for a field.
// ---------------------------------------------------------------------------
json ibWebFormSerializer::MapTypeToProps(const ibTypeDescription& typeDesc)
{
	json props = json::object();

	const auto& clsids = typeDesc.GetClsidList();

	if (clsids.size() > 1) {
		// Composite: list allowed resource strings
		json allowedTypes = json::array();
		for (const ibClassID c : clsids) {
			const std::string res = ResolveRefResource(c);
			if (!res.empty())
				allowedTypes.push_back(res);
			else if (c == g_valueNumberCLSID)  allowedTypes.push_back("number");
			else if (c == g_valueStringCLSID)  allowedTypes.push_back("string");
			else if (c == g_valueBooleanCLSID) allowedTypes.push_back("boolean");
			else if (c == g_valueDateCLSID)    allowedTypes.push_back("date");
		}
		if (!allowedTypes.empty())
			props["allowedTypes"] = allowedTypes;
		return props;
	}

	if (clsids.empty())
		return props;

	const ibClassID c = clsids.front();

	if (c == g_valueNumberCLSID) {
		props["precision"] = static_cast<int>(typeDesc.GetPrecision());
		props["scale"]     = static_cast<int>(typeDesc.GetScale());
		if (typeDesc.IsNonNegative())
			props["min"] = 0;
		return props;
	}

	if (c == g_valueStringCLSID) {
		if (typeDesc.GetLength() > 0)
			props["maxLength"] = static_cast<int>(typeDesc.GetLength());
		return props;
	}

	if (c == g_valueDateCLSID) {
		const ibDateFractions frac = typeDesc.GetDateFraction();
		if (frac == ibDateFractions_DateTime)
			props["showTime"] = true;
		return props;
	}

	if (c == g_valueBooleanCLSID)
		return props;  // Checkbox needs no extra props

	// Reference or enum
	if (activeMetaData != nullptr) {
		ibCtorMetaValueType* ctor = activeMetaData->GetTypeCtor(c);
		if (ctor != nullptr) {
			ibValueMetaObject* metaObj = ctor->GetMetaObject();
			if (metaObj != nullptr) {
				if (metaObj->GetClassType() == g_metaEnumerationCLSID) {
					// Select: populate static options
					json opts = CollectEnumOptions(c);
					if (!opts.empty())
						props["options"] = opts;
				}
				else {
					// RefField: provide the resource string for the data provider
					const std::string res = ResolveRefResource(c);
					if (!res.empty())
						props["resource"] = res;
				}
			}
		}
	}

	return props;
}

//***********************************************************************
//*                      Public serialization API                       *
//***********************************************************************

// ---------------------------------------------------------------------------
// SerializeFieldSchema — produce a Formily property entry for one attribute.
// ---------------------------------------------------------------------------
json ibWebFormSerializer::SerializeFieldSchema(ibValueMetaObjectAttributeBase* attr)
{
	if (attr == nullptr)
		return json::object();

	const ibTypeDescription& typeDesc = attr->GetTypeDesc();
	const wxString name    = attr->GetName();
	const wxString synonym = attr->GetSynonym();
	const bool required    = attr->FillCheck();

	json field;
	field["type"]        = MapTypeToJsonType(typeDesc);
	field["title"]       = ResolveTitle(name, synonym);
	field["x-component"] = MapTypeToComponent(typeDesc);
	field["x-decorator"] = "FormItem";

	json props = MapTypeToProps(typeDesc);
	if (!props.empty())
		field["x-component-props"] = props;

	if (required)
		field["required"] = true;

	return field;
}

// ---------------------------------------------------------------------------
// SerializeTableSchema — produce a Formily property entry for one tabular
// section (array of objects).
// ---------------------------------------------------------------------------
json ibWebFormSerializer::SerializeTableSchema(ibValueMetaObjectTableData* table)
{
	if (table == nullptr)
		return json::object();

	const wxString name    = table->GetName();
	const wxString synonym = table->GetSynonym();

	json tableField;
	tableField["type"]        = "array";
	tableField["title"]       = ResolveTitle(name, synonym);
	tableField["x-component"] = "EditableTable";
	tableField["x-decorator"] = "FormItem";

	// Build the nested object schema for each row
	json rowProps = json::object();
	for (auto* attr : table->GetAttributeArrayObject()) {
		if (attr->IsDeleted())
			continue;
		const std::string key = ToLower(WxToStd(attr->GetName()));
		rowProps[key] = SerializeFieldSchema(attr);
	}

	json rowSchema;
	rowSchema["type"]       = "object";
	rowSchema["properties"] = rowProps;

	tableField["items"] = rowSchema;

	return tableField;
}

// ---------------------------------------------------------------------------
// SerializeFormSchema — top-level entry point.
// ---------------------------------------------------------------------------
json ibWebFormSerializer::SerializeFormSchema(ibValueMetaObjectRecordData* metaObject)
{
	if (metaObject == nullptr)
		return json::object();

	const ibClassID metaClsid = metaObject->GetClassType();
	const std::string metaType = MetaTypeLabel(metaClsid);

	const wxString name    = metaObject->GetName();
	const wxString synonym = metaObject->GetSynonym();
	const std::string title = ResolveTitle(name, synonym);

	// --- x-oes-meta block ---------------------------------------------------
	json oesMetaBlock;
	oesMetaBlock["metaType"] = metaType;
	oesMetaBlock["name"]     = WxToStd(name);
	if (!synonym.IsEmpty())
		oesMetaBlock["synonym"] = WxToStd(synonym);
	oesMetaBlock["guid"] = WxToStd(metaObject->GetGuid().str());

	// --- x-oes-commands -----------------------------------------------------
	json commands = json::array();
	const bool isDocument = (metaClsid == g_metaDocumentCLSID);
	commands.push_back("save");
	if (isDocument) {
		commands.push_back("post");
		commands.push_back("unpost");
	}
	commands.push_back("delete");
	commands.push_back("copy");

	// --- __header sentinel field --------------------------------------------
	json headerField;
	headerField["type"]        = "void";
	headerField["x-component"] = "FormHeader";
	headerField["x-component-props"]["title"] = title;

	// --- Root properties map ------------------------------------------------
	json properties = json::object();
	properties["__header"] = headerField;

	// For documents, inject the predefined system fields first in a
	// consistent, well-known order so the frontend can position them.
	if (isDocument) {
		auto* docMeta = wxDynamicCast(metaObject, ibValueMetaObjectDocument);
		if (docMeta != nullptr) {

			// number — read-only code field
			if (auto* numberAttr = docMeta->GetDocumentNumber()) {
				json numberField = SerializeFieldSchema(numberAttr);
				numberField["x-component"] = "Input";
				numberField["x-component-props"]["readOnly"] = true;
				properties["number"] = numberField;
			}

			// date — DatePicker with time
			if (auto* dateAttr = docMeta->GetDocumentDate()) {
				json dateField = SerializeFieldSchema(dateAttr);
				dateField["x-component"] = "DatePicker";
				dateField["x-component-props"]["showTime"] = true;
				properties["date"] = dateField;
			}

			// posted — read-only status badge
			if (auto* postedAttr = docMeta->GetDocumentPosted()) {
				json postedField = SerializeFieldSchema(postedAttr);
				postedField["x-component"] = "StatusBadge";
				postedField["x-component-props"]["readOnly"] = true;
				properties["posted"] = postedField;
			}
		}
	}

	// User-defined attributes
	for (auto* attr : metaObject->GetAttributeArrayObject()) {
		if (attr->IsDeleted())
			continue;
		const std::string key = ToLower(WxToStd(attr->GetName()));
		properties[key] = SerializeFieldSchema(attr);
	}

	// Tabular sections
	for (auto* tbl : metaObject->GetTableArrayObject()) {
		if (tbl->IsDeleted())
			continue;
		const std::string key = ToLower(WxToStd(tbl->GetName()));
		properties[key] = SerializeTableSchema(tbl);
	}

	// --- Assemble final schema ----------------------------------------------
	json schema;
	schema["type"]             = "object";
	schema["x-oes-meta"]       = oesMetaBlock;
	schema["x-oes-commands"]   = commands;
	schema["properties"]       = properties;

	return schema;
}
