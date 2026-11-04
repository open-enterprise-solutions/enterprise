////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base manager for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "globalContextManager.h"
#include "backend/system/value/valueJob.h"   // ScheduledJobs.Predefined — the collection, its row and the schedule
#include "backend/system/value/valueBackgroundJob.h"   // BackgroundJobs


#include "backend/objCtor.h"
#include "metaObjectCollection.h"

constexpr ibClassID g_valueManagerCollectionCLSID = system_to_clsid("MG_SYAM");

// Catalogs, Documents, … — the MANAGERS of one metatype, one per object that has one (metaObjectCollection.h).
class ibValueManagerCollection : public ibValueMetaObjectCollection {
public:

	ibValueManagerCollection() : ibValueMetaObjectCollection(nullptr), m_clsid(0) {}
	ibValueManagerCollection(const ibClassID& clsid, ibMetaData* metaData)
		: ibValueMetaObjectCollection(metaData), m_clsid(clsid) {
		for (const auto object : m_metaData->GetAnyArrayObject(clsid))
			if (m_metaData->GetTypeCtor(object, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager) != nullptr)
				m_listValue.push_back(object);
	}

	// Its own id, not the registry's. Not m_clsid below: that is the kind of metadata it manages.
	virtual ibClassID GetClassType() const override { return g_valueManagerCollectionCLSID; }

	virtual ibString GetClassName() const override {
		ibCtorAbstractType* so = m_metaData != nullptr ? m_metaData->GetAvailableCtor(m_clsid) : nullptr;
		if (so != nullptr)
			return so->GetClassName() + ibT("Manager");
		return ibValueMetaObjectCollection::GetClassName();
	}

	virtual ibString GetString() const override {
		ibCtorAbstractType* so = m_metaData != nullptr ? m_metaData->GetAvailableCtor(m_clsid) : nullptr;
		if (so != nullptr)
			return so->GetClassName() + ibT("Manager");
		return ibValueMetaObjectCollection::GetString();
	}

protected:

	// The object's manager, made for the asking; an empty element — the editor's sample of a walk — has none.
	virtual ibValue ElementValue(ibValueMetaObject* const& object) const override {
		ibCtorMetaValueType* const so = object != nullptr
			? m_metaData->GetTypeCtor(object, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager) : nullptr;
		return so != nullptr ? so->CreateObject() : ibValue();
	}

private:

	ibClassID m_clsid;
};


// ScheduledJobs — ONE door, TWO forks. Both kinds of scheduled job live behind the same name,
// because they are one mechanism and not two that happen to sound alike:
//
//   ScheduledJobs.Predefined.…      — the predefined jobs: no rows, no manager of their own, so
//                                     the fork IS the collection — every one of them, with its
//                                     live settings, walked with For Each.
//   ScheduledJobs.Parameterized.<Name>
//                                   — a parameterized job's manager, the ordinary one a reference
//                                     object gets: Select(), FindByDescription(), Execute(ref).
//
// Two forks rather than "predefined here, job names directly there": a name at the top level
// would put the platform's own member and a configuration's job in one namespace, where the first
// job called Predefined shadows the way to reach every other one. Naming both sides also says
// which kind one is looking at, which is the question a reader of the script actually has.
//
// Two forks and nothing more, so the door is a value with two properties, not a Structure: a structure is a
// map somebody fills, and counting or walking this one answered about the platform's own members. A fork is
// made when it is read, as every manager the global context hands out is — Predefined reads the base afresh.
void ibValueScheduledJobsManager_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(ibT("Predefined"));
	helper.AppendProp(ibT("Parameterized"));
}

class ibValueScheduledJobsManager : public ibValueStaticMembers<&ibValueScheduledJobsManager_BindNames> {
public:
	ibValueScheduledJobsManager() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_metaData(nullptr) {}
	explicit ibValueScheduledJobsManager(ibMetaData* metaData)
		: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_metaData(metaData) {}

	virtual ibString GetString() const override { return ibT("ScheduledJobsManager"); }

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override {
		switch (lPropNum) {
		case enPredefined:
			pvarPropVal = new ibValuePredefinedJobs(m_metaData);
			return true;
		case enParameterized:
			// The SAME parameterised manager every other metatype gets — handed this metatype's clsid.
			// Nothing about jobs is special on this side, and that is the point: a job's rows select
			// exactly like a catalog's.
			pvarPropVal = new ibValueManagerCollection(g_metaParameterizedJobCLSID, m_metaData);
			return true;
		}
		return false;
	}

private:

	// In the order the binder appends them.
	enum Prop {
		enPredefined,
		enParameterized,
	};

	ibMetaData* m_metaData;
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
	enAccountingRegisters,
	enChartsOfCalculationTypes,
	enCalculationRegisters,
	enSequences,
	enScheduledJobs,
	enSessionParameters,
	enFunctionalOptions,
	enBackgroundJobs
};

void ibValueGlobalContextManager::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp(ibT("Constants"));
	helper.AppendProp(ibT("Catalogs"));
	helper.AppendProp(ibT("Documents"));
	helper.AppendProp(ibT("Enumerations"));
	helper.AppendProp(ibT("DataProcessors"));
	helper.AppendProp(ibT("ExternalDataProcessors"));
	helper.AppendProp(ibT("Reports"));
	helper.AppendProp(ibT("ExternalReports"));
	helper.AppendProp(ibT("InformationRegisters"));
	helper.AppendProp(ibT("AccumulationRegisters"));
	helper.AppendProp(ibT("ChartsOfCharacteristicTypes"));
	helper.AppendProp(ibT("ChartsOfAccounts"));
	helper.AppendProp(ibT("AccountingRegisters"));
	helper.AppendProp(ibT("ChartsOfCalculationTypes"));
	helper.AppendProp(ibT("CalculationRegisters"));
	// Where a border is read and said — `Sequences.<Name>.GetBorder(…)` / `SetBorder(…)`.
	helper.AppendProp(ibT("Sequences"));
	// ONE entry point for scheduled work, two forks inside it — Predefined and Parameterized. See
	// ibValueScheduledJobsManager above for why both live behind one name.
	helper.AppendProp(ibT("ScheduledJobs"));
	// The configuration's own declared parameters of this session. Read anywhere,
	// written only by the session module (metaSessionParameterObject.h).
	helper.AppendProp(ibT("SessionParameters"));
	// Whether a part of the configuration is used in this base — `FunctionalOptions.<Name>.Get()`. Each
	// option IS a constant, so the manager a name yields is the constant's own (metaFunctionalOptionObject.h).
	helper.AppendProp(ibT("FunctionalOptions"));
	// Work running on its own, found again — `BackgroundJobs.GetBackgroundJobs(…)`, `FindByUUID(…)`; started by
	// RunBackground. Not metadata: the jobs are the base's, whatever the configuration declares.
	helper.AppendProp(ibT("BackgroundJobs"));
}

#include "backend/metaCollection/metaSessionParameterObject.h"   // the metatype AND the value it yields
#include "backend/metaCollection/partial/dataProcessorManager.h"
#include "backend/metaCollection/partial/dataReportManager.h"

bool ibValueGlobalContextManager::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enConstants:
		pvarPropVal = new ibValueManagerCollection(g_metaConstantCLSID, m_metaData);
		return true;
	case enCatalogs:
		pvarPropVal = new ibValueManagerCollection(g_metaCatalogCLSID, m_metaData);
		return true;
	case enDocuments:
		pvarPropVal = new ibValueManagerCollection(g_metaDocumentCLSID, m_metaData);
		return true;
	case enEnumerations:
		pvarPropVal = new ibValueManagerCollection(g_metaEnumerationCLSID, m_metaData);
		return true;
	case enDataProcessors:
		pvarPropVal = new ibValueManagerCollection(g_metaDataProcessorCLSID, m_metaData);
		return true;
	case enExternalDataProcessors:
		pvarPropVal = new ibValueManagerDataObjectExternalDataProcessor();
		return true;
	case enReports:
		pvarPropVal = new ibValueManagerCollection(g_metaReportCLSID, m_metaData);
		return true;
	case enExternalReports:
		pvarPropVal = new ibValueManagerDataObjectExternalReport();
		return true;
	case enInformationRegisters:
		pvarPropVal = new ibValueManagerCollection(g_metaInformationRegisterCLSID, m_metaData);
		return true;
	case enAccumulationRegisters:
		pvarPropVal = new ibValueManagerCollection(g_metaAccumulationRegisterCLSID, m_metaData);
		return true;
	case enChartsOfCharacteristicTypes:
		pvarPropVal = new ibValueManagerCollection(g_metaChartOfCharacteristicTypesCLSID, m_metaData);
		return true;
	case enChartsOfAccounts:
		pvarPropVal = new ibValueManagerCollection(g_metaChartOfAccountsCLSID, m_metaData);
		return true;
	case enAccountingRegisters:
		pvarPropVal = new ibValueManagerCollection(g_metaAccountingRegisterCLSID, m_metaData);
		return true;
	case enChartsOfCalculationTypes:
		pvarPropVal = new ibValueManagerCollection(g_metaChartOfCalculationTypesCLSID, m_metaData);
		return true;
	case enCalculationRegisters:
		pvarPropVal = new ibValueManagerCollection(g_metaCalculationRegisterCLSID, m_metaData);
		return true;
	case enSequences:
		pvarPropVal = new ibValueManagerCollection(g_metaSequenceCLSID, m_metaData);
		return true;
	case enScheduledJobs:
		pvarPropVal = new ibValueScheduledJobsManager(m_metaData);
		return true;
	case enSessionParameters:
		pvarPropVal = new ibValueSessionParameters(m_metaData);
		return true;
	case enFunctionalOptions:
		pvarPropVal = new ibValueManagerCollection(g_metaFunctionalOptionCLSID, m_metaData);
		return true;
	case enBackgroundJobs:
		pvarPropVal = new ibValueBackgroundJobsManager();
		return true;
	}

	return false;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SYSTEM_TYPE_REGISTER(ibValueGlobalContextManager, "GlobalContextManager", system_to_clsid("MG_SYSM"));
SYSTEM_TYPE_REGISTER(ibValueManagerCollection, "ManagerCollection", g_valueManagerCollectionCLSID);
SYSTEM_TYPE_REGISTER(ibValueScheduledJobsManager, "ScheduledJobsManager", system_to_clsid("MG_SCJOB"));
