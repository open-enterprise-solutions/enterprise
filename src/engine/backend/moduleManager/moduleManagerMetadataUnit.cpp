////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-access 
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "backend/metaData.h"

#include "backend/compiler/value/valueMap.h"

wxIMPLEMENT_DYNAMIC_CLASS(IModuleManager::CMetadataUnit, CValue);

IModuleManager::CMetadataUnit::CMetadataUnit(IMetaData* metaData) :
	CValue(eValueTypes::TYPE_VALUE, true),
	m_methodHelper(new CMethodHelper()), m_metaData(metaData)
{
}

IModuleManager::CMetadataUnit::~CMetadataUnit()
{
	wxDELETE(m_methodHelper);
}

enum
{
	enCommonModules = 0,
	enCommonForms,
	enCommonTemplates,
	enConstants,
	enCatalogs,
	enDocuments,
	enEnumerations,
	enDataProcessors,
	enReports,
	enInformationRegisters,
	enAccumulationRegisters,
};

void IModuleManager::CMetadataUnit::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendProp("commonModules", true, false, g_metaCommonModuleCLSID);
	m_methodHelper->AppendProp("commonForms", true, false, g_metaCommonFormCLSID);
	m_methodHelper->AppendProp("commonTemplates", true, false, g_metaTemplateCLSID);
	m_methodHelper->AppendProp("constants", true, false, g_metaConstantCLSID);
	m_methodHelper->AppendProp("catalogs", true, false, g_metaCatalogCLSID);
	m_methodHelper->AppendProp("documents", true, false, g_metaDocumentCLSID);
	m_methodHelper->AppendProp("enumerations", true, false, g_metaEnumerationCLSID);
	m_methodHelper->AppendProp("dataProcessors", true, false, g_metaDataProcessorCLSID);
	m_methodHelper->AppendProp("reports", true, false, g_metaReportCLSID);
	m_methodHelper->AppendProp("informationRegisters", true, false, g_metaInformationRegisterCLSID);
	m_methodHelper->AppendProp("accumulationRegisters", true, false, g_metaAccumulationRegisterCLSID);
}

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

bool IModuleManager::CMetadataUnit::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool IModuleManager::CMetadataUnit::GetPropVal(const long lPropNum, CValue& pvarPropVal)//значение атрибута
{
	CValueStructure* valStruct = CValue::CreateAndConvertObjectValueRef<CValueStructure>();
	switch (lPropNum)
	{
	case enCommonModules: {
		for (auto& obj : m_metaData->GetMetaObject(g_metaCommonModuleCLSID)) {
			valStruct->Insert(obj->GetName(), obj);
		}
	} break;
	case enCommonForms: {
		for (auto& obj : m_metaData->GetMetaObject(g_metaCommonFormCLSID)) {
			valStruct->Insert(obj->GetName(), obj);
		}
	} break;
	case enCommonTemplates: {
		for (auto& obj : m_metaData->GetMetaObject(g_metaTemplateCLSID)) {
			valStruct->Insert(obj->GetName(), obj);
		}
	} break;
	case enConstants: {
		for (auto& obj : m_metaData->GetMetaObject(g_metaConstantCLSID)) {
			valStruct->Insert(obj->GetName(), obj);
		}
	} break;
	case enCatalogs: {
		for (auto& obj : m_metaData->GetMetaObject(g_metaCatalogCLSID)) {
			valStruct->Insert(obj->GetName(), obj);
		}
	} break;
	case enDocuments: {
		for (auto& obj : m_metaData->GetMetaObject(g_metaDocumentCLSID)) {
			valStruct->Insert(obj->GetName(), obj);
		}
	} break;
	case enEnumerations: {
		for (auto& obj : m_metaData->GetMetaObject(g_metaEnumerationCLSID)) {
			valStruct->Insert(obj->GetName(), obj);
		}
	} break;
	case enDataProcessors: {
		for (auto& obj : m_metaData->GetMetaObject(g_metaDataProcessorCLSID)) {
			valStruct->Insert(obj->GetName(), obj);
		}
	} break;
	case enReports: {
		for (auto& obj : m_metaData->GetMetaObject(g_metaReportCLSID)) {
			valStruct->Insert(obj->GetName(), obj);
		}
	} break;
	case enInformationRegisters: {
		for (auto& obj : m_metaData->GetMetaObject(g_metaInformationRegisterCLSID)) {
			valStruct->Insert(obj->GetName(), obj);
		}
	} break;
	case enAccumulationRegisters: {
		for (auto& obj : m_metaData->GetMetaObject(g_metaAccumulationRegisterCLSID)) {
			valStruct->Insert(obj->GetName(), obj);
		}
		break;
	}
	}
	pvarPropVal = valStruct;
	return true;
}