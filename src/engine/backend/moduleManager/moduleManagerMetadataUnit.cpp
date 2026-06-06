////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-access 
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "backend/metaData.h"

#include "backend/system/value/valueMap.h"


ibValueModuleManager::ibValueMetadataUnit::ibValueMetadataUnit(ibMetaData* metaData) :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true), m_metaData(metaData)
{
	m_members.Bind(this, &ibValueMetadataUnit::FillMembers);
}

ibValueModuleManager::ibValueMetadataUnit::~ibValueMetadataUnit()
{
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
	enChartsOfCharacteristicTypes,
	enChartsOfAccounts,
	enAccountingRegisters,
};

void ibValueModuleManager::ibValueMetadataUnit::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp("CommonModules", true, false, g_metaCommonModuleCLSID);
	helper.AppendProp("CommonForms", true, false, g_metaCommonFormCLSID);
	helper.AppendProp("CommonTemplates", true, false, g_metaCommonTemplateCLSID);
	helper.AppendProp("Constants", true, false, g_metaConstantCLSID);
	helper.AppendProp("Catalogs", true, false, g_metaCatalogCLSID);
	helper.AppendProp("Documents", true, false, g_metaDocumentCLSID);
	helper.AppendProp("Enumerations", true, false, g_metaEnumerationCLSID);
	helper.AppendProp("DataProcessors", true, false, g_metaDataProcessorCLSID);
	helper.AppendProp("Reports", true, false, g_metaReportCLSID);
	helper.AppendProp("InformationRegisters", true, false, g_metaInformationRegisterCLSID);
	helper.AppendProp("AccumulationRegisters", true, false, g_metaAccumulationRegisterCLSID);
	helper.AppendProp("ChartsOfCharacteristicTypes", true, false, g_metaChartOfCharacteristicTypesCLSID);
	helper.AppendProp("ChartsOfAccounts", true, false, g_metaChartOfAccountsCLSID);
	helper.AppendProp("AccountingRegisters", true, false, g_metaAccountingRegisterCLSID);
}

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

bool ibValueModuleManager::ibValueMetadataUnit::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueModuleManager::ibValueMetadataUnit::GetPropVal(const long lPropNum, ibValue& pvarPropVal)//attribute value
{
	ibValueStructure* valStruct = new ibValueStructure();
	switch (lPropNum)
	{
	case enCommonModules: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaCommonModuleCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
	} break;
	case enCommonForms: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaCommonFormCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
	} break;
	case enCommonTemplates: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaCommonTemplateCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
	} break;
	case enConstants: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaConstantCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
	} break;
	case enCatalogs: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaCatalogCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
	} break;
	case enDocuments: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaDocumentCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
	} break;
	case enEnumerations: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaEnumerationCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
	} break;
	case enDataProcessors: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaDataProcessorCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
	} break;
	case enReports: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaReportCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
	} break;
	case enInformationRegisters: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaInformationRegisterCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
	} break;
	case enAccumulationRegisters: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaAccumulationRegisterCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
		break;
	}
	case enChartsOfCharacteristicTypes: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaChartOfCharacteristicTypesCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
		break;
	}
	case enChartsOfAccounts: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaChartOfAccountsCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
		break;
	}
	case enAccountingRegisters: {
		for (const auto object : m_metaData->GetAnyArrayObject(g_metaAccountingRegisterCLSID)) {
			valStruct->Insert(object->GetName(), object);
		}
		break;
	}
	}
	pvarPropVal = valStruct;
	return true;
}