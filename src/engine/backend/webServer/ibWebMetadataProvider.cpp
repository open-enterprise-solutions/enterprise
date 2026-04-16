////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : Serialise OES metadata tree to JSON for the REST API
//	              Patterns reuse those established in metadataConfigurationJSON.cpp
////////////////////////////////////////////////////////////////////////////

#include "ibWebMetadataProvider.h"

#include "metadataConfiguration.h"
#include "metaCollection/metaObject.h"
#include "metaCollection/metaObjectMetadata.h"
#include "metaCollection/metaObjectComposite.h"
#include "metaCollection/attribute/metaAttributeObject.h"
#include "metaCollection/enumeration/metaEnumObject.h"
#include "metaCollection/dimension/metaDimensionObject.h"
#include "metaCollection/resource/metaResourceObject.h"
#include "metaCollection/table/metaTableObject.h"

#include "metaCollection/partial/catalog.h"
#include "metaCollection/partial/document.h"
#include "metaCollection/partial/enumeration.h"
#include "metaCollection/partial/constant.h"
#include "metaCollection/partial/informationRegister.h"
#include "metaCollection/partial/accumulationRegister.h"
#include "metaCollection/partial/dataProcessor.h"
#include "metaCollection/partial/dataReport.h"

#include <json.hpp>

using json = nlohmann::json;

//***********************************************************************
//*                       Local helpers                                *
//***********************************************************************

namespace {

inline std::string WxToStd(const wxString& s)
{
	return std::string(s.ToUTF8());
}

// Serialise a type description (qualifiers only — no raw CLSIDs exposed)
json SerializeTypeDesc(const ibTypeDescription& td)
{
	json j = json::object();

	const auto& clsids = td.GetClsidList();
	if (clsids.empty())
		return j;

	// Map CLSIDs to human-readable type names
	json jTypes = json::array();
	for (const auto& clsid : clsids) {
		if      (clsid == string_to_clsid("VL_NUMB")) jTypes.push_back("number");
		else if (clsid == string_to_clsid("VL_STRI")) jTypes.push_back("string");
		else if (clsid == string_to_clsid("VL_DATE")) jTypes.push_back("date");
		else if (clsid == string_to_clsid("VL_BOOL")) jTypes.push_back("boolean");
		else                                           jTypes.push_back("reference");
	}
	j["types"] = jTypes;

	// Number qualifiers
	if (td.GetPrecision() > 0) {
		j["precision"]   = td.GetPrecision();
		j["scale"]       = td.GetScale();
		j["nonNegative"] = td.IsNonNegative();
	}

	// String qualifiers
	if (td.GetLength() > 0) {
		j["length"]        = td.GetLength();
		j["allowedLength"] = static_cast<int>(td.GetAllowedLength());
	}

	// Date qualifiers
	if (td.GetDateFraction() != ibDateFractions::ibDateFractions_DateTime)
		j["dateFraction"] = static_cast<int>(td.GetDateFraction());

	return j;
}

// Serialise a single attribute (user-defined or predefined)
json SerializeAttribute(ibValueMetaObjectAttributeBase* attr)
{
	json j = json::object();
	j["guid"]     = WxToStd(attr->GetGuid().str());
	j["id"]       = attr->GetMetaID();
	j["name"]     = WxToStd(attr->GetName());

	const wxString synonym = attr->GetSynonym();
	if (!synonym.IsEmpty())
		j["synonym"] = WxToStd(synonym);

	j["fillCheck"] = attr->FillCheck();

	json jType = SerializeTypeDesc(attr->GetTypeDesc());
	if (!jType.empty())
		j["type"] = jType;

	return j;
}

// Serialise a tabular section with its columns
json SerializeTable(ibValueMetaObjectTableData* table)
{
	json j = json::object();
	j["guid"]   = WxToStd(table->GetGuid().str());
	j["id"]     = table->GetMetaID();
	j["name"]   = WxToStd(table->GetName());

	const wxString synonym = table->GetSynonym();
	if (!synonym.IsEmpty())
		j["synonym"] = WxToStd(synonym);

	json jAttrs = json::array();
	for (auto attr : table->GetAttributeArrayObject()) {
		if (attr->IsDeleted()) continue;
		jAttrs.push_back(SerializeAttribute(attr));
	}
	j["attributes"] = jAttrs;

	return j;
}

// Common record-data fields: user attributes + tabular sections
void AppendRecordDataFields(json& j, ibValueMetaObjectRecordData* obj)
{
	json jAttrs = json::array();
	for (auto attr : obj->GetAttributeArrayObject()) {
		if (attr->IsDeleted()) continue;
		jAttrs.push_back(SerializeAttribute(attr));
	}
	j["attributes"] = jAttrs;

	json jTables = json::array();
	for (auto tbl : obj->GetTableArrayObject()) {
		if (tbl->IsDeleted()) continue;
		jTables.push_back(SerializeTable(tbl));
	}
	j["tabularSections"] = jTables;
}

// Serialise register dimensions and resources
void AppendRegisterFields(json& j, ibValueMetaObjectRegisterData* reg)
{
	json jDims = json::array();
	for (auto dim : reg->GetDimentionArrayObject()) {
		if (dim->IsDeleted()) continue;
		jDims.push_back(SerializeAttribute(dim));
	}
	j["dimensions"] = jDims;

	json jRes = json::array();
	for (auto res : reg->GetResourceArrayObject()) {
		if (res->IsDeleted()) continue;
		jRes.push_back(SerializeAttribute(res));
	}
	j["resources"] = jRes;

	json jAttrs = json::array();
	for (auto attr : reg->GetAttributeArrayObject()) {
		if (attr->IsDeleted()) continue;
		jAttrs.push_back(SerializeAttribute(attr));
	}
	j["attributes"] = jAttrs;
}

// Base header fields common to every metadata object
json MakeObjectHeader(ibValueMetaObject* obj, const std::string& metaType)
{
	json j = json::object();
	j["guid"]     = WxToStd(obj->GetGuid().str());
	j["id"]       = obj->GetMetaID();
	j["name"]     = WxToStd(obj->GetName());
	j["metaType"] = metaType;

	const wxString synonym = obj->GetSynonym();
	if (!synonym.IsEmpty())
		j["synonym"] = WxToStd(synonym);

	return j;
}

} // anonymous namespace

//***********************************************************************
//*                   ibWebMetadataProvider                            *
//***********************************************************************

json ibWebMetadataProvider::SerializeMetadataTree()
{
	json tree = json::object();

	if (activeMetaData == nullptr)
		return tree;

	tree["catalogs"]              = SerializeCatalogs();
	tree["documents"]             = SerializeDocuments();
	tree["enumerations"]          = SerializeEnumerations();
	tree["constants"]             = SerializeConstants();
	tree["informationRegisters"]  = SerializeInformationRegisters();
	tree["accumulationRegisters"] = SerializeAccumulationRegisters();
	tree["dataProcessors"]        = SerializeDataProcessors();
	tree["reports"]               = SerializeReports();

	return tree;
}

// ---- Catalogs ----

json ibWebMetadataProvider::SerializeCatalogs()
{
	json jArray = json::array();

	if (activeMetaData == nullptr)
		return jArray;

	auto* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr)
		return jArray;

	for (auto* catalog : root->GetAnyArrayObject<ibValueMetaObjectCatalog>()) {
		if (catalog->IsDeleted()) continue;

		json j = MakeObjectHeader(catalog, "catalog");
		AppendRecordDataFields(j, catalog);

		// Predefined system attributes exposed as a flat map
		json jSys = json::object();
		if (auto* attr = catalog->GetAttributeForCode())
			jSys["code"] = SerializeAttribute(attr);
		if (auto* attr = catalog->GetDataDescription())
			jSys["description"] = SerializeAttribute(attr);
		if (auto* attr = catalog->GetDataReference())
			jSys["reference"] = SerializeAttribute(attr);
		j["systemAttributes"] = jSys;

		jArray.push_back(j);
	}

	return jArray;
}

// ---- Documents ----

json ibWebMetadataProvider::SerializeDocuments()
{
	json jArray = json::array();

	if (activeMetaData == nullptr)
		return jArray;

	auto* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr)
		return jArray;

	for (auto* doc : root->GetAnyArrayObject<ibValueMetaObjectDocument>()) {
		if (doc->IsDeleted()) continue;

		json j = MakeObjectHeader(doc, "document");
		AppendRecordDataFields(j, doc);

		// Expose key predefined attributes
		json jSys = json::object();
		if (auto* attr = doc->GetDocumentNumber())
			jSys["number"] = SerializeAttribute(attr);
		if (auto* attr = doc->GetDocumentDate())
			jSys["date"] = SerializeAttribute(attr);
		if (auto* attr = doc->GetDocumentPosted())
			jSys["posted"] = SerializeAttribute(attr);
		if (auto* attr = doc->GetDataReference())
			jSys["reference"] = SerializeAttribute(attr);
		j["systemAttributes"] = jSys;

		jArray.push_back(j);
	}

	return jArray;
}

// ---- Enumerations ----

json ibWebMetadataProvider::SerializeEnumerations()
{
	json jArray = json::array();

	if (activeMetaData == nullptr)
		return jArray;

	auto* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr)
		return jArray;

	for (auto* enm : root->GetAnyArrayObject<ibValueMetaObjectEnumeration>()) {
		if (enm->IsDeleted()) continue;

		json j = MakeObjectHeader(enm, "enumeration");

		json jValues = json::array();
		for (auto* val : enm->GetEnumObjectArray()) {
			if (val->IsDeleted()) continue;
			json jVal = json::object();
			jVal["guid"] = WxToStd(val->GetGuid().str());
			jVal["id"]   = val->GetMetaID();
			jVal["name"] = WxToStd(val->GetName());
			const wxString syn = val->GetSynonym();
			if (!syn.IsEmpty()) jVal["synonym"] = WxToStd(syn);
			jValues.push_back(jVal);
		}
		j["values"] = jValues;

		jArray.push_back(j);
	}

	return jArray;
}

// ---- Constants ----

json ibWebMetadataProvider::SerializeConstants()
{
	json jArray = json::array();

	if (activeMetaData == nullptr)
		return jArray;

	auto* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr)
		return jArray;

	for (auto* con : root->GetAnyArrayObject<ibValueMetaObjectConstant>()) {
		if (con->IsDeleted()) continue;

		json j = MakeObjectHeader(con, "constant");

		json jType = SerializeTypeDesc(con->GetTypeDesc());
		if (!jType.empty())
			j["type"] = jType;

		jArray.push_back(j);
	}

	return jArray;
}

// ---- Information registers ----

json ibWebMetadataProvider::SerializeInformationRegisters()
{
	json jArray = json::array();

	if (activeMetaData == nullptr)
		return jArray;

	auto* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr)
		return jArray;

	for (auto* reg : root->GetAnyArrayObject<ibValueMetaObjectInformationRegister>()) {
		if (reg->IsDeleted()) continue;

		json j = MakeObjectHeader(reg, "informationRegister");
		j["writeMode"]   = static_cast<int>(reg->GetWriteRegisterMode());
		j["periodicity"] = static_cast<int>(reg->GetPeriodicity());
		AppendRegisterFields(j, reg);

		jArray.push_back(j);
	}

	return jArray;
}

// ---- Accumulation registers ----

json ibWebMetadataProvider::SerializeAccumulationRegisters()
{
	json jArray = json::array();

	if (activeMetaData == nullptr)
		return jArray;

	auto* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr)
		return jArray;

	for (auto* reg : root->GetAnyArrayObject<ibValueMetaObjectAccumulationRegister>()) {
		if (reg->IsDeleted()) continue;

		json j = MakeObjectHeader(reg, "accumulationRegister");
		j["registerType"] = static_cast<int>(reg->GetRegisterType());
		AppendRegisterFields(j, reg);

		jArray.push_back(j);
	}

	return jArray;
}

// ---- Data processors ----

json ibWebMetadataProvider::SerializeDataProcessors()
{
	json jArray = json::array();

	if (activeMetaData == nullptr)
		return jArray;

	auto* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr)
		return jArray;

	for (auto* dp : root->GetAnyArrayObject<ibValueMetaObjectDataProcessor>()) {
		if (dp->IsDeleted()) continue;

		json j = MakeObjectHeader(dp, "dataProcessor");
		AppendRecordDataFields(j, dp);
		jArray.push_back(j);
	}

	return jArray;
}

// ---- Reports ----

json ibWebMetadataProvider::SerializeReports()
{
	json jArray = json::array();

	if (activeMetaData == nullptr)
		return jArray;

	auto* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr)
		return jArray;

	for (auto* rpt : root->GetAnyArrayObject<ibValueMetaObjectReport>()) {
		if (rpt->IsDeleted()) continue;

		json j = MakeObjectHeader(rpt, "report");
		AppendRecordDataFields(j, rpt);
		jArray.push_back(j);
	}

	return jArray;
}
