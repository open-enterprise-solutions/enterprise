////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : configuration XML export/import
////////////////////////////////////////////////////////////////////////////

#include "metadataConfiguration.h"
#include "metaCollection/metaObject.h"
#include "metaCollection/metaObjectMetadata.h"
#include "metaCollection/metaModuleObject.h"
#include "metaCollection/attribute/metaAttributeObject.h"
#include "metaCollection/table/metaTableObject.h"

#include <wx/xml/xml.h>
#include <wx/wfstream.h>
#include <wx/base64.h>

//***********************************************************************
//*                    XML Serialization Helpers                        *
//***********************************************************************

namespace {

wxString TypeToString(const ibValueTypes& type)
{
	switch (type) {
	case ibValueTypes::TYPE_BOOLEAN: return wxT("Boolean");
	case ibValueTypes::TYPE_NUMBER: return wxT("Number");
	case ibValueTypes::TYPE_DATE: return wxT("Date");
	case ibValueTypes::TYPE_STRING: return wxT("String");
	case ibValueTypes::TYPE_REFFER: return wxT("Reference");
	case ibValueTypes::TYPE_ENUM: return wxT("Enum");
	default: return wxT("Empty");
	}
}

void AddTextNode(wxXmlNode* parent, const wxString& name, const wxString& value)
{
	wxXmlNode* node = new wxXmlNode(wxXML_ELEMENT_NODE, name);
	node->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, value));
	parent->AddChild(node);
}

void SaveAttributeToXML(wxXmlNode* parent, ibValueMetaObjectAttributeBase* attr)
{
	wxXmlNode* xmlAttr = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("Attribute"));
	xmlAttr->AddAttribute(wxT("guid"), attr->GetGuid().str());
	xmlAttr->AddAttribute(wxT("id"), wxString::Format(wxT("%d"), attr->GetMetaID()));

	AddTextNode(xmlAttr, wxT("Name"), attr->GetName());
	AddTextNode(xmlAttr, wxT("Synonym"), attr->GetSynonym());

	const ibTypeDescription& typeDesc = attr->GetTypeDesc();
	const auto& clsidList = typeDesc.GetClsidList();
	if (!clsidList.empty()) {
		wxXmlNode* xmlType = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("Type"));
		for (const auto& clsid : clsidList) {
			AddTextNode(xmlType, wxT("TypeId"), clsid_to_string(clsid));
		}
		xmlAttr->AddChild(xmlType);
	}

	parent->AddChild(xmlAttr);
}

void SaveTableToXML(wxXmlNode* parent, ibValueMetaObjectTableData* table)
{
	wxXmlNode* xmlTable = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("Table"));
	xmlTable->AddAttribute(wxT("guid"), table->GetGuid().str());
	xmlTable->AddAttribute(wxT("id"), wxString::Format(wxT("%d"), table->GetMetaID()));

	AddTextNode(xmlTable, wxT("Name"), table->GetName());
	AddTextNode(xmlTable, wxT("Synonym"), table->GetSynonym());

	// Table attributes (columns)
	wxXmlNode* xmlAttrs = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("Attributes"));
	for (auto attr : table->GetAttributeArrayObject()) {
		if (attr->IsDeleted()) continue;
		SaveAttributeToXML(xmlAttrs, attr);
	}
	xmlTable->AddChild(xmlAttrs);

	parent->AddChild(xmlTable);
}

void SaveMetaObjectToXML(wxXmlNode* parent, ibValueMetaObject* metaObject, const wxString& elementName)
{
	wxXmlNode* xmlObj = new wxXmlNode(wxXML_ELEMENT_NODE, elementName);
	xmlObj->AddAttribute(wxT("guid"), metaObject->GetGuid().str());
	xmlObj->AddAttribute(wxT("id"), wxString::Format(wxT("%d"), metaObject->GetMetaID()));
	xmlObj->AddAttribute(wxT("clsid"), clsid_to_string(metaObject->GetClassType()));

	AddTextNode(xmlObj, wxT("Name"), metaObject->GetName());
	AddTextNode(xmlObj, wxT("Synonym"), metaObject->GetSynonym());

	if (!metaObject->GetComment().IsEmpty())
		AddTextNode(xmlObj, wxT("Comment"), metaObject->GetComment());

	// Save child attributes
	ibValueMetaObjectRecordData* recordData = nullptr;
	if (metaObject->ConvertToValue(recordData)) {
		// User-defined attributes
		wxXmlNode* xmlAttrs = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("Attributes"));
		for (auto attr : recordData->GetAttributeArrayObject()) {
			if (attr->IsDeleted()) continue;
			SaveAttributeToXML(xmlAttrs, attr);
		}
		xmlObj->AddChild(xmlAttrs);

		// Tables
		wxXmlNode* xmlTables = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("Tables"));
		for (auto table : recordData->GetTableArrayObject()) {
			if (table->IsDeleted()) continue;
			SaveTableToXML(xmlTables, table);
		}
		xmlObj->AddChild(xmlTables);
	}

	// Save module code
	ibValueMetaObjectModule* moduleObj = nullptr;
	if (metaObject->ConvertToValue(moduleObj)) {
		// Module source available — save as CDATA
	}

	parent->AddChild(xmlObj);
}

} // anonymous namespace

//***********************************************************************
//*                    Save Configuration to XML                       *
//***********************************************************************

bool ibMetaDataConfigurationBase::SaveConfigToXML(const wxString& strFileName)
{
	ibValueMetaObjectConfiguration* commonMetaObject = GetCommonMetaObject();
	if (commonMetaObject == nullptr)
		return false;

	wxXmlDocument doc;
	wxXmlNode* root = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("Configuration"));
	root->AddAttribute(wxT("guid"), GetConfigGuid().str());
	root->AddAttribute(wxT("version"), wxT("1"));

	AddTextNode(root, wxT("Name"), commonMetaObject->GetName());
	AddTextNode(root, wxT("Synonym"), commonMetaObject->GetSynonym());

	// Group metadata objects by type
	struct TypeGroup {
		ibClassID clsid;
		wxString groupName;
		wxString itemName;
	};

	const TypeGroup groups[] = {
		{ g_metaConstantCLSID, wxT("Constants"), wxT("Constant") },
		{ g_metaCatalogCLSID, wxT("Catalogs"), wxT("Catalog") },
		{ g_metaDocumentCLSID, wxT("Documents"), wxT("Document") },
		{ g_metaEnumerationCLSID, wxT("Enumerations"), wxT("Enumeration") },
		{ g_metaDataProcessorCLSID, wxT("DataProcessors"), wxT("DataProcessor") },
		{ g_metaReportCLSID, wxT("Reports"), wxT("Report") },
		{ g_metaInformationRegisterCLSID, wxT("InformationRegisters"), wxT("InformationRegister") },
		{ g_metaAccumulationRegisterCLSID, wxT("AccumulationRegisters"), wxT("AccumulationRegister") },
		{ g_metaChartOfCharacteristicTypesCLSID, wxT("ChartsOfCharacteristicTypes"), wxT("ChartOfCharacteristicTypes") },
		{ g_metaChartOfAccountsCLSID, wxT("ChartsOfAccounts"), wxT("ChartOfAccounts") },
		{ g_metaAccountingRegisterCLSID, wxT("AccountingRegisters"), wxT("AccountingRegister") },
	};

	for (const auto& group : groups) {
		auto objects = GetAnyArrayObject(group.clsid);
		if (objects.empty())
			continue;

		wxXmlNode* xmlGroup = new wxXmlNode(wxXML_ELEMENT_NODE, group.groupName);
		for (auto obj : objects) {
			if (obj->IsDeleted()) continue;
			SaveMetaObjectToXML(xmlGroup, obj, group.itemName);
		}
		root->AddChild(xmlGroup);
	}

	// Common modules
	auto commonModules = GetAnyArrayObject(g_metaCommonModuleCLSID);
	if (!commonModules.empty()) {
		wxXmlNode* xmlModules = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("CommonModules"));
		for (auto obj : commonModules) {
			if (obj->IsDeleted()) continue;
			SaveMetaObjectToXML(xmlModules, obj, wxT("CommonModule"));
		}
		root->AddChild(xmlModules);
	}

	doc.SetRoot(root);

	wxFileOutputStream stream(strFileName);
	if (!stream.IsOk())
		return false;

	return doc.Save(stream);
}

//***********************************************************************
//*                    Load Configuration from XML                     *
//***********************************************************************

bool ibMetaDataConfigurationBase::LoadConfigFromXML(const wxString& strFileName)
{
	//TODO: Implement XML import — parse XML, create metadata objects
	// This requires creating metadata objects from XML elements,
	// which involves the metadata factory system.
	// For now, only export is implemented.
	return false;
}
