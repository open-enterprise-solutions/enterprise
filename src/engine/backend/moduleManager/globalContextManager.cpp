////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base manager for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "globalContextManager.h"
#include "backend/system/value/valueMap.h"


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
			// Name surface builds lazily on first GetPMethods() — no eager populate.
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

};


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

void ibValueGlobalContextManager::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Constants"));
	helper.AppendProp(wxT("Catalogs"));
	helper.AppendProp(wxT("Documents"));
	helper.AppendProp(wxT("Enumerations"));
	helper.AppendProp(wxT("DataProcessors"));
	helper.AppendProp(wxT("ExternalDataProcessors"));
	helper.AppendProp(wxT("Reports"));
	helper.AppendProp(wxT("ExternalReports"));
	helper.AppendProp(wxT("InformationRegisters"));
	helper.AppendProp(wxT("AccumulationRegisters"));
	helper.AppendProp(wxT("ChartsOfCharacteristicTypes"));
	helper.AppendProp(wxT("ChartsOfAccounts"));
	helper.AppendProp(wxT("AccountingRegisters"));
}

#include "backend/metaCollection/partial/dataProcessorManager.h"
#include "backend/metaCollection/partial/dataReportManager.h"

bool ibValueGlobalContextManager::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enConstants:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaConstantCLSID, m_metaData);
		return true;
	case enCatalogs:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaCatalogCLSID, m_metaData);
		return true;
	case enDocuments:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaDocumentCLSID, m_metaData);
		return true;
	case enEnumerations:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaEnumerationCLSID, m_metaData);
		return true;
	case enDataProcessors:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaDataProcessorCLSID, m_metaData);
		return true;
	case enExternalDataProcessors:
		pvarPropVal = new ibValueManagerDataObjectExternalDataProcessor();
		return true;
	case enReports:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaReportCLSID, m_metaData);
		return true;
	case enExternalReports:
		pvarPropVal = new ibValueManagerDataObjectExternalReport();
		return true;
	case enInformationRegisters:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaInformationRegisterCLSID, m_metaData);
		return true;
	case enAccumulationRegisters:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaAccumulationRegisterCLSID, m_metaData);
		return true;
	case enChartsOfCharacteristicTypes:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaChartOfCharacteristicTypesCLSID, m_metaData);
		return true;
	case enChartsOfAccounts:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaChartOfAccountsCLSID, m_metaData);
		return true;
	case enAccountingRegisters:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaAccountingRegisterCLSID, m_metaData);
		return true;
	}

	return false;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SYSTEM_TYPE_REGISTER(ibValueGlobalContextManager, "GlobalContextManager", string_to_clsid("MG_SYSM"));
SYSTEM_TYPE_REGISTER(ibValueGlobalContextStructureManager, "GlobalContextStructureManager", string_to_clsid("MG_SYAM"));