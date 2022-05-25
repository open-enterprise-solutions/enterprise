////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base manager for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "systemManager.h"
#include "compiler/methods.h"
#include "compiler/valueMap.h"

#include "metadata/metadata.h"
#include "metadata/metaObjects/metaObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CSystemManager, CValue);

CSystemManager::CSystemManager(IMetadata* metaData) : CValue(eValueTypes::TYPE_VALUE, true),
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
	enInformationRegisters,
	enAccumulationRegisters
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
		{"informationRegisters","informationRegisters"},
		{"accumulationRegisters","accumulationRegisters"},
	};

	int nCountA = sizeof(aAttributes) / sizeof(aAttributes[0]);
	m_methods->PrepareAttributes(aAttributes, nCountA);
}

#include "catalogManager.h"
#include "documentManager.h"
#include "enumerationManager.h"
#include "dataProcessorManager.h"
#include "dataReportManager.h"
#include "informationRegisterManager.h"
#include "accumulationRegisterManager.h"

#include "metadata/metaObjects/objects/constantManager.h"

CValue CSystemManager::GetAttribute(attributeArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enConstants:
	{
		std::map<wxString, CValue> constMananager;
		for (auto obj : m_metaData->GetMetaObjects(g_metaConstantCLSID)) {
			CMetaConstantObject* dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				constMananager.insert_or_assign(obj->GetName(), new CConstantManager(dataRef));
			}
		}

		return new CValueStructure(constMananager);
	}
	case enCatalogs:
	{
		std::map<wxString, CValue> catMananager;
		for (auto obj : m_metaData->GetMetaObjects(g_metaCatalogCLSID)) {
			CMetaObjectCatalog* dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				catMananager.insert_or_assign(obj->GetName(), new CCatalogManager(dataRef));
			}
		}

		return new CValueStructure(catMananager);
	}
	case enDocuments:
	{
		std::map<wxString, CValue> docMananager;
		for (auto obj : m_metaData->GetMetaObjects(g_metaDocumentCLSID)) {
			CMetaObjectDocument* dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				docMananager.insert_or_assign(obj->GetName(), new CDocumentManager(dataRef));
			}
		}

		return new CValueStructure(docMananager);

	}
	case enEnumerations:
	{
		std::map<wxString, CValue> enumMananager;
		for (auto obj : m_metaData->GetMetaObjects(g_metaEnumerationCLSID)) {
			CMetaObjectEnumeration* dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				enumMananager.insert_or_assign(obj->GetName(), new CEnumerationManager(dataRef));

			}
		}

		return new CValueStructure(enumMananager);
	}
	case enDataProcessors:
	{
		std::map<wxString, CValue> dataProcessoMananager;
		for (auto obj : m_metaData->GetMetaObjects(g_metaDataProcessorCLSID)) {
			CMetaObjectDataProcessor* dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				dataProcessoMananager.insert_or_assign(obj->GetName(), new CDataProcessorManager(dataRef));
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
			CMetaObjectReport* dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				reportMananager.insert_or_assign(obj->GetName(), new CReportManager(dataRef));
			}
		}

		return new CValueStructure(reportMananager);
	}
	case enExternalReports:
		return new CManagerExternalReport();
	case enInformationRegisters:
	{
		std::map<wxString, CValue> informationRegister;
		for (auto obj : m_metaData->GetMetaObjects(g_metaInformationRegisterCLSID)) {
			CMetaObjectInformationRegister* dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				informationRegister.insert_or_assign(obj->GetName(), new CInformationRegisterManager(dataRef));
			}
		}

		return new CValueStructure(informationRegister);
	}
	case enAccumulationRegisters:
	{
		std::map<wxString, CValue> accumulationRegister;
		for (auto obj : m_metaData->GetMetaObjects(g_metaAccumulationRegisterCLSID)) {
			CMetaObjectAccumulationRegister* dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				accumulationRegister.insert_or_assign(obj->GetName(), new CAccumulationRegisterManager(dataRef));
			}
		}

		return new CValueStructure(accumulationRegister);
	}
	}

	return CValue();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SO_VALUE_REGISTER(CSystemManager, "systemManager", CSystemManager, TEXT2CLSID("MG_SYSM"));