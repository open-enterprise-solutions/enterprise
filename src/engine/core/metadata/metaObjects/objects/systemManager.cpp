////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base manager for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "systemManager.h"
#include "core/compiler/valueMap.h"

#include "core/metadata/metadata.h"
#include "core/metadata/metaObjects/metaObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CSystemManager, CValue);

CSystemManager::CSystemManager(IMetadata* metaData) : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper()), m_metaData(metaData)
{
}

CSystemManager::~CSystemManager() {
	wxDELETE(m_methodHelper);
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
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendProp("constants");
	m_methodHelper->AppendProp("catalogs");
	m_methodHelper->AppendProp("documents");
	m_methodHelper->AppendProp("enumerations");
	m_methodHelper->AppendProp("dataProcessors");
	m_methodHelper->AppendProp("externalDataProcessors");
	m_methodHelper->AppendProp("reports");
	m_methodHelper->AppendProp("externalReports");
	m_methodHelper->AppendProp("informationRegisters");
	m_methodHelper->AppendProp("accumulationRegisters");
}

#include "catalogManager.h"
#include "documentManager.h"
#include "enumerationManager.h"
#include "dataProcessorManager.h"
#include "dataReportManager.h"
#include "informationRegisterManager.h"
#include "accumulationRegisterManager.h"

#include "core/metadata/metaObjects/objects/constantManager.h"

bool CSystemManager::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	switch (lPropNum)
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
		pvarPropVal = new CValueStructure(constMananager);
		return true; 
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
		pvarPropVal = new CValueStructure(catMananager);
		return true;
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
		pvarPropVal = new CValueStructure(docMananager);
		return true;
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
		pvarPropVal = new CValueStructure(enumMananager);
		return true;
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
		pvarPropVal = new CValueStructure(dataProcessoMananager);
		return true;
	}
	case enExternalDataProcessors:
		pvarPropVal = new CManagerExternalDataProcessorValue();
		return true;
	case enReports:
	{
		std::map<wxString, CValue> reportMananager;
		for (auto obj : m_metaData->GetMetaObjects(g_metaReportCLSID)) {
			CMetaObjectReport* dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				reportMananager.insert_or_assign(obj->GetName(), new CReportManager(dataRef));
			}
		}
		pvarPropVal = new CValueStructure(reportMananager);
		return true;
	}
	case enExternalReports:
		pvarPropVal = new CManagerExternalReport();
		return true;
	case enInformationRegisters:
	{
		std::map<wxString, CValue> informationRegister;
		for (auto obj : m_metaData->GetMetaObjects(g_metaInformationRegisterCLSID)) {
			CMetaObjectInformationRegister* dataRef = NULL;
			if (obj->ConvertToValue(dataRef)) {
				informationRegister.insert_or_assign(obj->GetName(), new CInformationRegisterManager(dataRef));
			}
		}
		pvarPropVal = new CValueStructure(informationRegister);
		return true;
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
		pvarPropVal = new CValueStructure(accumulationRegister);
		return true;
	}
	}

	return false;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SO_VALUE_REGISTER(CSystemManager, "systemManager", TEXT2CLSID("MG_SYSM"));