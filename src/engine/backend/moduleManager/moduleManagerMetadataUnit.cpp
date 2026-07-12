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
	// The clsid rides in the (long) property-tag slot only to distinguish these members; GetPropVal
	// dispatches by ordinal and re-supplies the clsid itself, so the narrowing is intentional and
	// lossless-in-practice. static_cast makes it explicit (silences C4305/C4309 on the u64 clsid).
	helper.AppendProp("CommonModules", true, false, static_cast<long>(g_metaCommonModuleCLSID));
	helper.AppendProp("CommonForms", true, false, static_cast<long>(g_metaCommonFormCLSID));
	helper.AppendProp("CommonTemplates", true, false, static_cast<long>(g_metaCommonTemplateCLSID));
	helper.AppendProp("Constants", true, false, static_cast<long>(g_metaConstantCLSID));
	helper.AppendProp("Catalogs", true, false, static_cast<long>(g_metaCatalogCLSID));
	helper.AppendProp("Documents", true, false, static_cast<long>(g_metaDocumentCLSID));
	helper.AppendProp("Enumerations", true, false, static_cast<long>(g_metaEnumerationCLSID));
	helper.AppendProp("DataProcessors", true, false, static_cast<long>(g_metaDataProcessorCLSID));
	helper.AppendProp("Reports", true, false, static_cast<long>(g_metaReportCLSID));
	helper.AppendProp("InformationRegisters", true, false, static_cast<long>(g_metaInformationRegisterCLSID));
	helper.AppendProp("AccumulationRegisters", true, false, static_cast<long>(g_metaAccumulationRegisterCLSID));
	helper.AppendProp("ChartsOfCharacteristicTypes", true, false, static_cast<long>(g_metaChartOfCharacteristicTypesCLSID));
	helper.AppendProp("ChartsOfAccounts", true, false, static_cast<long>(g_metaChartOfAccountsCLSID));
	helper.AppendProp("AccountingRegisters", true, false, static_cast<long>(g_metaAccountingRegisterCLSID));
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