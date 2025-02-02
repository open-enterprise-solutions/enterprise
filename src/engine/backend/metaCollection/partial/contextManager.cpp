////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base manager for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "contextManager.h"

#include "backend/compiler/value/valueMap.h"
#include "backend/metaCollection/metaObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CContextSystemManager, CValue);

CContextSystemManager::CContextSystemManager(IMetaData* metaData) : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper()), m_metaData(metaData)
{
}

CContextSystemManager::~CContextSystemManager() {
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

void CContextSystemManager::PrepareNames() const
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

#include "constantManager.h"
#include "catalogManager.h"
#include "documentManager.h"
#include "enumerationManager.h"
#include "dataProcessorManager.h"
#include "dataReportManager.h"
#include "informationRegisterManager.h"
#include "accumulationRegisterManager.h"

bool CContextSystemManager::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enConstants:
	{
		CValueStructure* valStruct = CValue::CreateAndConvertObjectValueRef<CValueStructure>();
		for (auto& obj : m_metaData->GetMetaObject(g_metaConstantCLSID)) {
			CMetaObjectConstant* dataRef = nullptr;
			if (obj->ConvertToValue(dataRef)) {
				valStruct->Insert(obj->GetName(), m_metaData->CreateAndConvertObjectValueRef<CConstantManager>(dataRef));
			}
		}
		pvarPropVal = valStruct;
		return true;
	}
	case enCatalogs:
	{
		CValueStructure* valStruct = CValue::CreateAndConvertObjectValueRef<CValueStructure>();
		for (auto& obj : m_metaData->GetMetaObject(g_metaCatalogCLSID)) {
			CMetaObjectCatalog* dataRef = nullptr;
			if (obj->ConvertToValue(dataRef)) {
				valStruct->Insert(obj->GetName(), m_metaData->CreateAndConvertObjectValueRef<CCatalogManager>(dataRef));
			}
		}
		pvarPropVal = valStruct;
		return true;
	}
	case enDocuments:
	{
		CValueStructure* valStruct = CValue::CreateAndConvertObjectValueRef<CValueStructure>();
		for (auto& obj : m_metaData->GetMetaObject(g_metaDocumentCLSID)) {
			CMetaObjectDocument* dataRef = nullptr;
			if (obj->ConvertToValue(dataRef)) {
				valStruct->Insert(obj->GetName(), m_metaData->CreateAndConvertObjectValueRef<CDocumentManager>(dataRef));
			}
		}
		pvarPropVal = valStruct;
		return true;
	}
	case enEnumerations:
	{
		CValueStructure* valStruct = CValue::CreateAndConvertObjectValueRef<CValueStructure>();
		for (auto& obj : m_metaData->GetMetaObject(g_metaEnumerationCLSID)) {
			CMetaObjectEnumeration* dataRef = nullptr;
			if (obj->ConvertToValue(dataRef)) {
				valStruct->Insert(obj->GetName(), m_metaData->CreateAndConvertObjectValueRef<CEnumerationManager>(dataRef));

			}
		}
		pvarPropVal = valStruct;
		return true;
	}
	case enDataProcessors:
	{
		CValueStructure* valStruct = CValue::CreateAndConvertObjectValueRef<CValueStructure>();
		for (auto& obj : m_metaData->GetMetaObject(g_metaDataProcessorCLSID)) {
			CMetaObjectDataProcessor* dataRef = nullptr;
			if (obj->ConvertToValue(dataRef)) {
				valStruct->Insert(obj->GetName(), m_metaData->CreateAndConvertObjectValueRef<CDataProcessorManager>(dataRef));
			}
		}
		pvarPropVal = valStruct;
		return true;
	}
	case enExternalDataProcessors:
		pvarPropVal = CValue::CreateAndConvertObjectValueRef<CManagerExternalDataProcessorValue>();
		return true;
	case enReports:
	{
		CValueStructure* valStruct = CValue::CreateAndConvertObjectValueRef<CValueStructure>();
		for (auto& obj : m_metaData->GetMetaObject(g_metaReportCLSID)) {
			CMetaObjectReport* dataRef = nullptr;
			if (obj->ConvertToValue(dataRef)) {
				valStruct->Insert(obj->GetName(), m_metaData->CreateAndConvertObjectValueRef<CReportManager>(dataRef));
			}
		}
		pvarPropVal = valStruct;
		return true;
	}
	case enExternalReports:
		pvarPropVal = CValue::CreateAndConvertObjectValueRef<CManagerExternalReport>();
		return true;
	case enInformationRegisters:
	{
		CValueStructure* valStruct = CValue::CreateAndConvertObjectValueRef<CValueStructure>();
		for (auto& obj : m_metaData->GetMetaObject(g_metaInformationRegisterCLSID)) {
			CMetaObjectInformationRegister* dataRef = nullptr;
			if (obj->ConvertToValue(dataRef)) {
				valStruct->Insert(obj->GetName(), m_metaData->CreateAndConvertObjectValueRef<CInformationRegisterManager>(dataRef));
			}
		}
		pvarPropVal = valStruct;
		return true;
	}
	case enAccumulationRegisters:
	{
		CValueStructure* valStruct = CValue::CreateAndConvertObjectValueRef<CValueStructure>();
		for (auto& obj : m_metaData->GetMetaObject(g_metaAccumulationRegisterCLSID)) {
			CMetaObjectAccumulationRegister* dataRef = nullptr;
			if (obj->ConvertToValue(dataRef)) {
				valStruct->Insert(obj->GetName(), m_metaData->CreateAndConvertObjectValueRef<CAccumulationRegisterManager>(dataRef));
			}
		}
		pvarPropVal = valStruct;
		return true;
	}
	}

	return false;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SYSTEM_TYPE_REGISTER(CContextSystemManager, "systemManager", string_to_clsid("MG_SYSM"));