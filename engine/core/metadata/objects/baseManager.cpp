////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base manager for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "baseManager.h"
#include "compiler/methods.h"
#include "compiler/valueMap.h"

#include "metadata/metadata.h"
#include "metadata/metaObjects/metaObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CSystemManager, CValue);

CSystemManager::CSystemManager(IMetadata *metaData) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_metaData(metaData)
{
}

CSystemManager::~CSystemManager() {
	wxDELETE(m_methods);
}

enum
{
	enConstants = 0,
	enCatalogs,
	enDocuments,
	enEnumerations,
	enDataProcessors,
	enExternalDataProcessors,
	enReports,
	enExternalReports,
};

void CSystemManager::PrepareNames() const
{
	SEng aAttributes[] =
	{
		{"constants","constants"},
		{"catalogs","catalogs"},
		{"documents","documents"},
		{"enumerations","enumerations"},
		{"dataProcessors","dataProcessors"},
		{"externalDataProcessors","extenalDataProcessors"},
		{"reports","reports"},
		{"externalReports","externalReports"},
	};

	int nCountA = sizeof(aAttributes) / sizeof(aAttributes[0]);
	m_methods->PrepareAttributes(aAttributes, nCountA);
}

#include "catalogManager.h"
#include "documentManager.h"
#include "enumerationManager.h"
#include "dataProcessorManager.h"
#include "dataReportManager.h"

#include "metadata/objects/constantManager.h"

CValue CSystemManager::GetAttribute(attributeArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case enConstants:
	{
		std::map<wxString, CValue> constMananager;
		for (auto obj : m_metaData->GetMetaObjects(g_metaConstantCLSID)) {
			CMetaConstantObject *dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				constMananager.insert_or_assign(obj->GetName(), new CManagerConstantValue(dataRef));
			}
		}

		return new CValueStructure(constMananager);
	}
	case enCatalogs:
	{
		std::map<wxString, CValue> catMananager;
		for (auto obj : m_metaData->GetMetaObjects(g_metaCatalogCLSID)) {
			CMetaObjectCatalogValue *dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				catMananager.insert_or_assign(obj->GetName(), new CManagerCatalogValue(dataRef));
			}
		}

		return new CValueStructure(catMananager);
	}
	case enDocuments:
	{
		std::map<wxString, CValue> docMananager;
		for (auto obj : m_metaData->GetMetaObjects(g_metaDocumentCLSID)) {
			CMetaObjectDocumentValue *dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				docMananager.insert_or_assign(obj->GetName(), new CManagerDocumentValue(dataRef));
			}
		}

		return new CValueStructure(docMananager);

	}
	case enEnumerations:
	{
		std::map<wxString, CValue> enumMananager;
		for (auto obj : m_metaData->GetMetaObjects(g_metaEnumerationCLSID)) {
			CMetaObjectEnumerationValue *dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				enumMananager.insert_or_assign(obj->GetName(), new CManagerEnumerationValue(dataRef));

			}
		}

		return new CValueStructure(enumMananager);
	}
	case enDataProcessors:
	{
		std::map<wxString, CValue> dataProcessoMananager;
		for (auto obj : m_metaData->GetMetaObjects(g_metaDataProcessorCLSID)) {
			CMetaObjectDataProcessorValue *dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				dataProcessoMananager.insert_or_assign(obj->GetName(), new CManagerDataProcessorValue(dataRef));
			}
		}

		return new CValueStructure(dataProcessoMananager);
	}
	case enExternalDataProcessors:
		return new CManagerExternalDataProcessorValue();
	case enReports:
	{
		std::map<wxString, CValue> reportMananager;
		for (auto obj : m_metaData->GetMetaObjects(g_metaReportCLSID)) {
			CMetaObjectReportValue *dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				reportMananager.insert_or_assign(obj->GetName(), new CManagerReportValue(dataRef));
			}
		}

		return new CValueStructure(reportMananager);
	}
	case enExternalReports:
		return new CManagerExternalReport();
	}

	return CValue();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SO_VALUE_REGISTER(CSystemManager, "systemManager", CSystemManager, TEXT2CLSID("MG_SYSM"));