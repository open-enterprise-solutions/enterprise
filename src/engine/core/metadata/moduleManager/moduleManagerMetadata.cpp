////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-access 
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "core/metadata/metadata.h"

#include "core/compiler/valueMap.h"

wxIMPLEMENT_DYNAMIC_CLASS(IModuleManager::CMetadataValue, CValue);

IModuleManager::CMetadataValue::CMetadataValue(IMetadata* metaData) :
	CValue(eValueTypes::TYPE_VALUE, true),
	m_methodHelper(new CMethodHelper()), m_metaData(metaData)
{
}

IModuleManager::CMetadataValue::~CMetadataValue()
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

void IModuleManager::CMetadataValue::PrepareNames() const
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

bool IModuleManager::CMetadataValue::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false; 
}

bool IModuleManager::CMetadataValue::GetPropVal(const long lPropNum, CValue& pvarPropVal)//значение атрибута
{
	std::map<wxString, CValue> m_aMetaObjects;

	switch (lPropNum)
	{
	case enCommonModules:
	{
		for (auto obj : m_metaData->GetMetaObjects(g_metaCommonModuleCLSID)) {
			m_aMetaObjects.insert_or_assign(obj->GetName(), obj);
		}
	} break;
	case enCommonForms:
	{
		for (auto obj : m_metaData->GetMetaObjects(g_metaCommonFormCLSID))
		{
			m_aMetaObjects.insert_or_assign(obj->GetName(), obj);
		}
	} break;
	case enCommonTemplates:
	{
		for (auto obj : m_metaData->GetMetaObjects(g_metaTemplateCLSID))
		{
			m_aMetaObjects.insert_or_assign(obj->GetName(), obj);
		}
	} break;
	case enConstants:
	{
		for (auto obj : m_metaData->GetMetaObjects(g_metaConstantCLSID))
		{
			m_aMetaObjects.insert_or_assign(obj->GetName(), obj);
		}
	} break;
	case enCatalogs:
	{
		for (auto obj : m_metaData->GetMetaObjects(g_metaCatalogCLSID))
		{
			m_aMetaObjects.insert_or_assign(obj->GetName(), obj);
		}
	} break;
	case enDocuments:
	{
		for (auto obj : m_metaData->GetMetaObjects(g_metaDocumentCLSID))
		{
			m_aMetaObjects.insert_or_assign(obj->GetName(), obj);
		}
	} break;
	case enEnumerations:
	{
		for (auto obj : m_metaData->GetMetaObjects(g_metaEnumerationCLSID))
		{
			m_aMetaObjects.insert_or_assign(obj->GetName(), obj);
		}
	} break;
	case enDataProcessors:
	{
		for (auto obj : m_metaData->GetMetaObjects(g_metaDataProcessorCLSID))
		{
			m_aMetaObjects.insert_or_assign(obj->GetName(), obj);
		}
	} break;
	case enReports:
	{
		for (auto obj : m_metaData->GetMetaObjects(g_metaReportCLSID))
		{
			m_aMetaObjects.insert_or_assign(obj->GetName(), obj);
		}
	} break;
	case enInformationRegisters:
	{
		for (auto obj : m_metaData->GetMetaObjects(g_metaInformationRegisterCLSID))
		{
			m_aMetaObjects.insert_or_assign(obj->GetName(), obj);
		}
	} break;
	case enAccumulationRegisters:
	{
		for (auto obj : m_metaData->GetMetaObjects(g_metaAccumulationRegisterCLSID))
		{
			m_aMetaObjects.insert_or_assign(obj->GetName(), obj);
		}
	} break;
	}

	pvarPropVal = new CValueStructure(m_aMetaObjects);
	return true; 
}