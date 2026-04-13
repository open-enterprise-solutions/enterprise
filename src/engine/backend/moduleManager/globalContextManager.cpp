////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base manager for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "globalContextManager.h"
#include "backend/system/value/valueMap.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueGlobalContextManager, ibValue);

#include "backend/objCtor.h"

class ibValueGlobalContextStructureManager : public ibValueStructure {
public:

	ibValueGlobalContextStructureManager() : m_clsid(0), m_metaData(nullptr) {}
	ibValueGlobalContextStructureManager(const ibClassID& clsid, ibMetaData* metaData)
		: ibValueStructure(true), m_clsid(clsid), m_metaData(metaData) {

		for (const auto object : m_metaData->GetAnyArrayObject(clsid)) {
			ibCtorMetaValueType* so = m_metaData->GetTypeCtor(object, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
			if (so == nullptr)
				continue;
			ibValuePtr<ibValue> createdValue(so->CreateObject());
			if (createdValue != nullptr)
				createdValue->PrepareNames();
			ibValueStructure::Insert(object->GetName(), createdValue);
		}
	}

	virtual wxString GetClassName() const {
		ibCtorAbstractType* so = m_metaData->GetAvailableCtor(m_clsid);
		if (so != nullptr)
			return so->GetClassName() + wxT("Manager");
		return ibValueStructure::GetClassName();
	}

	virtual wxString GetString() const {
		ibCtorAbstractType* so = m_metaData->GetAvailableCtor(m_clsid);
		if (so != nullptr)
			return so->GetClassName() + wxT("Manager");
		return ibValueStructure::GetString();
	}

private:

	ibMetaData* m_metaData;

	ibClassID m_clsid;

	wxDECLARE_DYNAMIC_CLASS(ibValueGlobalContextStructureManager);
};

wxIMPLEMENT_DYNAMIC_CLASS(ibValueGlobalContextStructureManager, ibValue);

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
	enAccumulationRegisters,
	enChartsOfCharacteristicTypes,
	enChartsOfAccounts,
	enAccountingRegisters
};

void ibValueGlobalContextManager::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendProp(wxT("Constants"));
	m_methodHelper->AppendProp(wxT("Catalogs"));
	m_methodHelper->AppendProp(wxT("Documents"));
	m_methodHelper->AppendProp(wxT("Enumerations"));
	m_methodHelper->AppendProp(wxT("DataProcessors"));
	m_methodHelper->AppendProp(wxT("ExternalDataProcessors"));
	m_methodHelper->AppendProp(wxT("Reports"));
	m_methodHelper->AppendProp(wxT("ExternalReports"));
	m_methodHelper->AppendProp(wxT("InformationRegisters"));
	m_methodHelper->AppendProp(wxT("AccumulationRegisters"));
	m_methodHelper->AppendProp(wxT("ChartsOfCharacteristicTypes"));
	m_methodHelper->AppendProp(wxT("ChartsOfAccounts"));
	m_methodHelper->AppendProp(wxT("AccountingRegisters"));
}

#include "backend/metaCollection/partial/dataProcessorManager.h"
#include "backend/metaCollection/partial/dataReportManager.h"

bool ibValueGlobalContextManager::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enConstants:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueGlobalContextStructureManager>(g_metaConstantCLSID, m_metaData);
		return true;
	case enCatalogs:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueGlobalContextStructureManager>(g_metaCatalogCLSID, m_metaData);
		return true;
	case enDocuments:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueGlobalContextStructureManager>(g_metaDocumentCLSID, m_metaData);
		return true;
	case enEnumerations:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueGlobalContextStructureManager>(g_metaEnumerationCLSID, m_metaData);
		return true;
	case enDataProcessors:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueGlobalContextStructureManager>(g_metaDataProcessorCLSID, m_metaData);
		return true;
	case enExternalDataProcessors:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueManagerDataObjectExternalDataProcessor>();
		return true;
	case enReports:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueGlobalContextStructureManager>(g_metaReportCLSID, m_metaData);
		return true;
	case enExternalReports:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueManagerDataObjectExternalReport>();
		return true;
	case enInformationRegisters:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueGlobalContextStructureManager>(g_metaInformationRegisterCLSID, m_metaData);
		return true;
	case enAccumulationRegisters:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueGlobalContextStructureManager>(g_metaAccumulationRegisterCLSID, m_metaData);
		return true;
	case enChartsOfCharacteristicTypes:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueGlobalContextStructureManager>(g_metaChartOfCharacteristicTypesCLSID, m_metaData);
		return true;
	case enChartsOfAccounts:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueGlobalContextStructureManager>(g_metaChartOfAccountsCLSID, m_metaData);
		return true;
	case enAccountingRegisters:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueGlobalContextStructureManager>(g_metaAccountingRegisterCLSID, m_metaData);
		return true;
	}

	return false;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SYSTEM_TYPE_REGISTER(ibValueGlobalContextManager, "GlobalContextManager", string_to_clsid("MG_SYSM"));
SYSTEM_TYPE_REGISTER(ibValueGlobalContextStructureManager, "GlobalContextStructureManager", string_to_clsid("MG_SYAM"));