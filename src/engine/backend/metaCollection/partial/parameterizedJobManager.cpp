////////////////////////////////////////////////////////////////////////////
//	Description : parameterized scheduled job — manager
////////////////////////////////////////////////////////////////////////////

#include "parameterizedJobManager.h"

#include "backend/appData.h"
#include "backend/metaData.h"
#include "commonObject.h"
#include "reference/reference.h"
#include "selector/objectSelector.h"
#include "backend/system/value/valueGuid.h"
#include "backend/system/value/valueBackgroundJob.h"   // LastJob
#include "backend/job/jobManager.h"                    // LastJob

const ibValueMetaObjectCommonModule* ibValueManagerDataObjectJob::GetManagerModule() const
{
	return m_metaObject->GetManagerModule();
}

ibValueReferenceDataObject* ibValueManagerDataObjectJob::EmptyRef() const
{
	return ibValueReferenceDataObject::Create(m_metaObject);
}

// (FindByCode / FindByDescription are the base's — ibValueManagerDataObjectPredefined, in
//  commonObjectManagerQuery.cpp, which says why this copy had to go.)

enum Func {
	eCreateItem = 0,
	eCreateFolder,
	eSelect,
	eFindByCode,
	eFindByDescription,
	eExecute,
	eLastJob,
	eGetForm,
	eGetListForm,
	eGetSelectForm,
	eGetTemplate,
	eEmptyRef
};

void ibValueManagerDataObjectJob::FillManagerMethods(ibMemberTable& helper) const
{
	// Order is load-bearing — CallAsFunc switches on the method index (eCreateItem = 0 …).
	helper.AppendFunc(ibT("CreateItem"), ibT("CreateItem()"));
	helper.AppendFunc(ibT("CreateFolder"), ibT("CreateFolder()"));
	helper.AppendFunc(ibT("Select"), ibT("Select()"));
	helper.AppendFunc(ibT("FindByCode"), 1, ibT("FindByCode(code : string)"));
	helper.AppendFunc(ibT("FindByDescription"), 2, ibT("FindByDescription(descr : string, exact = true : boolean)"));
	// RUN ONE ROW by reference, ignoring its schedule — the script-side twin of the list command,
	// and the same single entry the tick uses. Without it a job could only be exercised by waiting
	// out its interval, which makes "is the job wrong or is the manager wrong?" unanswerable.
	helper.AppendFunc(ibT("Execute"), 1, ibT("Execute(job : reference)"));
	// …and what its last pass did, as the BackgroundJob that pass is — the same answer a predefined job's
	// row gives as LastJob. Undefined when no pass of the row has run in this process.
	helper.AppendFunc(ibT("LastJob"), 1, ibT("LastJob(job : reference)"));
	helper.AppendFunc(ibT("GetForm"), 3, ibT("GetForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(ibT("GetListForm"), 3, ibT("GetListForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(ibT("GetSelectForm"), 3, ibT("GetSelectForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(ibT("GetTemplate"), 1, ibT("GetTemplate(name : string)"));
	helper.AppendFunc(ibT("EmptyRef"), ibT("EmptyRef()"));
}

bool ibValueManagerDataObjectJob::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	// Our own ordinal, not the table index — ibValueManagerDataObject::BuiltinMethodNum says why.
	switch (BuiltinMethodNum(lMethodNum))
	{
	case eCreateItem:
		pvarRetValue = m_metaObject->CreateObjectValue(ibObjectMode::OBJECT_ITEM);
		return true;
	case eCreateFolder:
		pvarRetValue = m_metaObject->CreateObjectValue(ibObjectMode::OBJECT_FOLDER);
		return true;
	case eSelect:
		pvarRetValue = new ibValueSelectorRecordDataObject(m_metaObject);
		return true;
	case eFindByCode:
		pvarRetValue = FindByCode(*paParams[0]);
		return true;
	case eFindByDescription:
		pvarRetValue = FindByDescription(*paParams[0], lSizeArray > 1 ? *paParams[1] : ibValue());
		return true;
	case eExecute: {
		// The argument is a reference to one of THIS job's rows — the one value that crosses a
		// session boundary, and the only thing a job is ever started with.
		ibValueReferenceDataObject* reference = lSizeArray > 0 ? paParams[0]->ConvertToType<ibValueReferenceDataObject>() : nullptr;
		if (reference == nullptr)
			ibBackendCoreException::Error(_("a job reference is required"));
		if (!m_metaObject->AccessRight_Execute())
			ibBackendCoreException::Error(_("insufficient rights to execute the job"));
		pvarRetValue = m_metaObject->RunJobByReference(reference->GetGuid());
		return true;
	}
	case eLastJob: {
		// The row's guid IS its job's key (parameterizedJobRegistration.cpp), so the manager is asked by it.
		ibValueReferenceDataObject* reference = lSizeArray > 0 ? paParams[0]->ConvertToType<ibValueReferenceDataObject>() : nullptr;
		if (reference == nullptr)
			ibBackendCoreException::Error(_("a job reference is required"));
		if (!m_metaObject->AccessRight_Read())
			ibBackendAccessException::Error(ibString::Format(_("reading '%s'"), m_metaObject->GetSynonym()));
		ibJobManager* const manager = ibApplicationInstance::GetJobManager();
		std::shared_ptr<ibBackgroundRun> run = manager != nullptr ? manager->LastJob(reference->GetGuid()) : nullptr;
		pvarRetValue = run != nullptr ? ibValue(new ibValueBackgroundJob(std::move(run))) : ibValue();
		return true;
	}
	case eGetForm: {
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetGenericForm(ibFormRequest(lSizeArray > 0 ? paParams[0]->GetString() : ibString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	case eGetListForm: {
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(ibFormRequest(lSizeArray > 0 ? paParams[0]->GetString() : ibString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	case eGetSelectForm: {
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetSelectForm(ibFormRequest(lSizeArray > 0 ? paParams[0]->GetString() : ibString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	case eGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	case eEmptyRef:
		pvarRetValue = EmptyRef();
		return true;
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}
