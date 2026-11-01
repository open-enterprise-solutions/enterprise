////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base manager for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "globalContextManager.h"
#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueJob.h"   // ScheduledJobs.Predefined — the collection, its row and the schedule
#include "backend/system/value/valueScheduledJob.h"    // ScheduledJobs' verbs — one job, either kind
#include "backend/system/value/valueBackgroundJob.h"   // BackgroundJobs


#include "backend/objCtor.h"

constexpr ibClassID g_valueGlobalContextStructureManagerCLSID = system_to_clsid("MG_SYAM");

class ibValueGlobalContextStructureManager : public ibValueStructure {
	public:

	// Its own id — the structure it derives from answers with the structure's (valueMap.h). Not
	// m_clsid below: that is the kind of metadata it manages.
	virtual ibClassID GetClassType() const override { return g_valueGlobalContextStructureManagerCLSID; }

	ibValueGlobalContextStructureManager() : m_metaData(nullptr), m_clsid(0) {}
	ibValueGlobalContextStructureManager(const ibClassID& clsid, ibMetaData* metaData)
		: ibValueStructure(true), m_metaData(metaData), m_clsid(clsid) {

		for (const auto object : m_metaData->GetAnyArrayObject(clsid)) {
			ibCtorMetaValueType* so = m_metaData->GetTypeCtor(object, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
			if (so == nullptr)
				continue;
			const ibValue createdValue = so->CreateObject();
			// TWO OBJECTS MAY SHARE A NAME while a configuration is being edited (it is refused only when
			// saved), so a repeated name is put, not inserted: one of them holds it, and which does not matter.
			ibValueStructure::SetAt(object->GetName(), createdValue);
		}
	}

	virtual ibString GetClassName() const {
		ibCtorAbstractType* so = m_metaData->GetAvailableCtor(m_clsid);
		if (so != nullptr)
			return so->GetClassName() + ibT("Manager");
		return ibValueStructure::GetClassName();
	}

	virtual ibString GetString() const {
		ibCtorAbstractType* so = m_metaData->GetAvailableCtor(m_clsid);
		if (so != nullptr)
			return so->GetClassName() + ibT("Manager");
		return ibValueStructure::GetString();
	}

private:

	ibMetaData* m_metaData;

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
// …AND THE VERBS THAT ASK BOTH AT ONCE — GetScheduledJobs, FindPredefined, FindByUUID,
// CreateScheduledJob. Each answers with a ScheduledJob (valueScheduledJob.h), the one value both kinds
// are read and written through, so an administration list walks one array whatever declared its rows.
void ibValueScheduledJobsManager_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(ibT("Predefined"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Parameterized"), true, false, wxNOT_FOUND);

	helper.AppendFunc(ibT("GetScheduledJobs"), 1, ibT("GetScheduledJobs(filter : Structure)"));
	helper.AppendFunc(ibT("FindPredefined"), 1, ibT("FindPredefined(metadata : MetaObject)"));
	helper.AppendFunc(ibT("FindByUUID"), 1, ibT("FindByUUID(uuid : UUID)"));
	helper.AppendFunc(ibT("CreateScheduledJob"), 1, ibT("CreateScheduledJob(metadata : MetaObject)"));
}

class ibValueScheduledJobsManager : public ibValueStaticMembers<&ibValueScheduledJobsManager_BindNames> {
	enum Prop { enPredefined = 0, enParameterized };
	enum Func { enGetScheduledJobs = 0, enFindPredefined, enFindByUUID, enCreateScheduledJob };
public:
	ibValueScheduledJobsManager(ibMetaData* metaData)
		: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_metaData(metaData)
	{
		m_predefined = ibValuePtr<ibValuePredefinedJobs>(new ibValuePredefinedJobs(metaData));

		// The parameterized fork is the SAME parameterised manager every other metatype gets —
		// handed this metatype's clsid. Nothing about jobs is special on this side, and that is
		// the point: a job's rows select exactly like a catalog's.
		m_parameterized = ibValuePtr<ibValueGlobalContextStructureManager>(
			new ibValueGlobalContextStructureManager(g_metaParameterizedJobCLSID, metaData));
	}

	virtual ibString GetClassName() const { return ibT("ScheduledJobsManager"); }
	virtual ibString GetString() const { return ibT("ScheduledJobsManager"); }

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override {
		switch (lPropNum) {
		case enPredefined: pvarPropVal = m_predefined; return true;
		case enParameterized: pvarPropVal = m_parameterized; return true;
		}
		return false;
	}

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override {
		const ibValue argument = lSizeArray > 0 ? *paParams[0] : ibValue();
		switch (lMethodNum) {
		case enGetScheduledJobs: pvarRetValue = ibValueScheduledJob::GetScheduledJobs(m_metaData, argument); return true;
		case enFindPredefined: pvarRetValue = ibValueScheduledJob::FindPredefined(m_metaData, argument); return true;
		case enFindByUUID: pvarRetValue = ibValueScheduledJob::FindByUUID(m_metaData, argument); return true;
		case enCreateScheduledJob: pvarRetValue = ibValueScheduledJob::CreateScheduledJob(m_metaData, argument); return true;
		}
		return false;
	}

private:
	ibMetaData* m_metaData;
	ibValuePtr<ibValuePredefinedJobs> m_predefined;
	ibValuePtr<ibValueGlobalContextStructureManager> m_parameterized;
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
	// Work started to run on its own — `BackgroundJobs.Execute(…)`, listed and found again by its UUID.
	// Not metadata: the jobs are the base's, whatever the configuration declares.
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
	case enChartsOfCalculationTypes:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaChartOfCalculationTypesCLSID, m_metaData);
		return true;
	case enCalculationRegisters:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaCalculationRegisterCLSID, m_metaData);
		return true;
	case enSequences:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaSequenceCLSID, m_metaData);
		return true;
	case enScheduledJobs:
		pvarPropVal = new ibValueScheduledJobsManager(m_metaData);
		return true;
	case enSessionParameters:
		pvarPropVal = new ibValueSessionParameters(m_metaData);
		return true;
	case enFunctionalOptions:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaFunctionalOptionCLSID, m_metaData);
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
SYSTEM_TYPE_REGISTER(ibValueGlobalContextStructureManager, "GlobalContextStructureManager", g_valueGlobalContextStructureManagerCLSID);
