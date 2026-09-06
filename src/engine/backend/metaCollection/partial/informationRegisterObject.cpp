#include "informationRegister.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/system/systemManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

// WriteRecordSet / DeleteRecordSet inherited from ibValueRecordSetObject
// (Phase B template-method) — the scaffold is in commonObject.cpp; the
// Begin/Commit + LockByKeys helpers it calls live in commonObjectRecordSetQuery.cpp.

////////////////////////////////////////////////////////////////////////////////////////////////////

const ibSourceExplorer* ibValueRecordManagerObjectInformationRegister::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(
		wxT("Ref"), _("Ref"), m_metaObject->GetMetaID(), GetClassType(),
		false, false
	);

	ibValueMetaObjectInformationRegister* metaRef = nullptr;

	if (m_metaObject->ConvertToValue(metaRef)) {
		if (metaRef->GetPeriodicity() != ibPeriodicity::eNonPeriodic) {
			m_sourceExplorer.AppendColumn(metaRef->GetRegisterPeriod()->GetQueryColumn());
		}
	}

	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		m_sourceExplorer.AppendColumn(object->GetQueryColumn());
	}

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		m_sourceExplorer.AppendColumn(object->GetQueryColumn());
	}

	for (const auto object : m_metaObject->GetAttributeArrayObject()) {
		m_sourceExplorer.AppendColumn(object->GetQueryColumn());
	}

	return &m_sourceExplorer;
}

#pragma region _form_builder_h_
void ibValueRecordManagerObjectInformationRegister::ShowFormValue(const wxString& strFormName, ibBackendControlFrame* ownerControl)
{
	ibBackendValueForm* const foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialized then generate  
	ibBackendValueForm* const valueForm =
		GetFormValue(strFormName, ownerControl);

	if (valueForm != nullptr) {
		valueForm->Modify(m_recordSet->IsModified());
		valueForm->ShowForm();
	}
}

ibBackendValueForm* ibValueRecordManagerObjectInformationRegister::GetFormValue(const wxString& strFormName, ibBackendControlFrame* ownerControl)
{
	ibBackendValueForm* const foundedForm = GetForm();

	if (foundedForm == nullptr) {

		ibBackendValueForm* createdForm = m_metaObject->CreateAndBuildForm(
			strFormName,
			ibValueMetaObjectInformationRegister::eFormRecord,
			ownerControl,
			this,
			m_objGuid
		);

		if (createdForm != nullptr)
			createdForm->CloseOnOwnerClose(false);

		return createdForm;
	}

	return foundedForm;
}
#pragma endregion

bool ibValueRecordManagerObjectInformationRegister::WriteRegister(bool replace)
{
	if (!appData->DesignerMode())
	{
		ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();

		if (!scope || !scope->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!ibBackendException::IsEvalMode())
		{
			{
				scope.SafeBeginTransaction();

				bool newObject = ibValueRecordManagerObjectInformationRegister::IsNewObject();

				// A REGISTER'S KEY FLOATS OVER ITS DIMENSIONS, so editing one does not modify a record — it
				// REPLACES it: the old key is gone from the table and a row under a new key is what remains.
				// SaveData rewrites m_objGuid's composite in place (SetKeyValues), so the previous one has to be
				// taken now, before the write.
				const ibRowMetaValues keyBefore = m_objGuid.GetKeyValues();

				if (!SaveData()) {
					scope.SafeRollBackTransaction();
					ibBackendCoreException::Error(_("Register '%s': failed to store the record"),
						m_metaObject != nullptr ? m_metaObject->GetSynonym() : wxString());
					return false;
				}

				ibBackendValueForm::UpdateFormUniqueKey(m_objGuid);

				scope.SafeCommitTransaction();

				// Told afterwards if there is anybody to tell — ibFormToNotify (backend_form.h).
				ibBackendValueForm* const valueForm = ibFormToNotify([this] { return GetForm(); });

				// WHICH NEWS THE LIST NEEDS is not "was this new" but "can the list still find the row it knows".
				// It identifies rows by their key, so only an identity that APPEARED or MOVED is worth an anchor;
				// a plain re-write leaves every key where it was and must not drag the user's cursor to it (an
				// object form sits open while the list is browsed elsewhere). A key change is, for the list,
				// exactly a create: the row it held is gone, this one is new.
				const bool keyMoved = !newObject && m_objGuid.GetKeyValues() != keyBefore;

				if ((newObject || keyMoved) && valueForm != nullptr) valueForm->NotifyCreate(GetValue());
				else if (valueForm != nullptr) valueForm->NotifyChange(GetValue());
			}

			m_recordSet->Modify(false);
		}
	}

	return true;
}

bool ibValueRecordManagerObjectInformationRegister::DeleteRegister()
{
	if (!appData->DesignerMode())
	{
		ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();

		if (!scope || !scope->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!ibBackendException::IsEvalMode())
		{
			{
				// Told afterwards if there is anybody to tell — ibFormToNotify (backend_form.h).
				ibBackendValueForm* const valueForm = ibFormToNotify([this] { return GetForm(); });
				{
					scope.SafeBeginTransaction();

					if (!DeleteData()) {
						scope.SafeRollBackTransaction();
						ibBackendCoreException::Error(_("Register '%s': failed to delete the record"),
							m_metaObject != nullptr ? m_metaObject->GetSynonym() : wxString());
						return false;
					}

					scope.SafeCommitTransaction();

					if (valueForm != nullptr) valueForm->NotifyDelete(GetValue());
				}
				m_recordSet->Modify(false);
			}
		}
	}

	return true;
}

enum recordManager
{
	enCopyRecordManager,
	enWriteRecordManager,
	enDeleteRecordManager,
	enModifiedRecordManager,
	enReadRecordManager,
	enSelectedRecordManager,
	enGetFormRecord,
	enGetTemplate,
	enGetMetadataRecordManager
};

// 🛑 THIS ORDER IS THE CALL NUMBER, AND IT MUST MATCH FillMembers EXACTLY. A method is invoked by
// its INDEX in the member table, so an enumerator out of step silently runs a different verb:
// `Write` landed on Load, `Load` on Unload and `Unload` on Write. Posting any document crashed
// (Write handed its bool to Load, which casts it to a table) and an Unload would have WRITTEN the
// set. Found 2026-09-03 by posting a goods receipt from the sandbox.
enum recordSet
{
	enAdd = 0,
	enCount,
	enClear,
	enWriteRecordSet,
	enLoad,
	enUnload,
	enModifiedRecordSet,
	enReadRecordSet,
	enSelectedRecordSet,
	enGetMetadataRecordSet,
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueRecordSetObjectInformationRegister::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Add"), wxT("Add()"));
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
	helper.AppendFunc(wxT("Write"), 1, wxT("Write(replace : boolean)"));
	helper.AppendFunc(wxT("Load"), 1, wxT("Load(value : any table)"));
	helper.AppendFunc(wxT("Unload"), wxT("Unload()"));
	helper.AppendFunc(wxT("Modified"), wxT("Modified()"));
	helper.AppendFunc(wxT("Read"), wxT("Read()"));
	helper.AppendFunc(wxT("Selected"), wxT("Selected()"));
	helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));

	// ⭐ THE FILTER, AS A PROPERTY. It is also bound as a module export (InitializeObject), which is
	// what lets the set's own module say `Filter.X` — but an export is invisible from outside and to
	// the editor's completion. Declared here, a caller can address the set before writing it, which
	// is the difference between "replace these records" and "replace the table".
	helper.AppendProp(wxT("Filter"), ibValueRecordSetObject::enPropFilter);
}

void ibValueRecordManagerObjectInformationRegister::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Copy"), wxT("Copy()"));
	helper.AppendFunc(wxT("Write"), 1, wxT("Write(replace : boolean)"));
	helper.AppendFunc(wxT("Delete"), wxT("Delete()"));
	helper.AppendFunc(wxT("Modified"), wxT("Modified()"));
	helper.AppendFunc(wxT("Read"), wxT("Read()"));
	helper.AppendFunc(wxT("Selected"), wxT("Selected()"));
	helper.AppendFunc(wxT("GetFormRecord"), 3, wxT("GetFormRecord(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	helper.AppendFunc(wxT("GetMetadata"), wxT("getMetadata()"));

	//set object name
	wxString objectName;

	//fill custom object
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		helper.AppendProp(
			objectName,
			object->GetMetaID()
		);
	}
}

bool ibValueRecordManagerObjectInformationRegister::SetPropVal(const long lPropNum, const ibValue& varPropVal)       //setting attribute
{
	return SetValueByMetaID(
		m_members.GetPropData(lPropNum), varPropVal
	);
}

bool ibValueRecordManagerObjectInformationRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return GetValueByMetaID(
		m_members.GetPropData(lPropNum), pvarPropVal
	);
}

//////////////////////////////////////////////////////////////////////////

bool ibValueRecordSetObjectInformationRegister::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordSetObjectInformationRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	// The set's own properties (Filter) live on the base — asked here first because a register may add
	// its own later, and answered by the base when it has none of its own. Returning false outright is
	// what kept `Filter` unreachable from outside while it was declared in this very file's member
	// table (2026-09-05).
	return ibValueRecordSetObject::GetPropVal(lPropNum, pvarPropVal);
}

bool ibValueRecordSetObjectInformationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);

	switch (lMethodNum)
	{
	case recordSet::enAdd:
		pvarRetValue = new ibValueRecordSetObjectRegisterReturnLine(this, GetItem(AppendRow()));
		return true;
	case recordSet::enCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case recordSet::enClear:
		ibValueModelStorage::Clear();
		// ⭐ CLEARING IS A CHANGE. Emptying the set is how a handler says "no movements" - and the
		// document's final write skips a set that is not modified, so an unmarked Clear would leave
		// yesterday's movements standing.
		Modify(true);
		return true;
	case recordSet::enLoad:
		LoadDataFromTable(paParams[0]->ConvertToType<ibValueModel>());
		return true;
	case recordSet::enUnload:
		pvarRetValue = SaveDataToTable();
		return true;
	case recordSet::enWriteRecordSet:
		WriteRecordSet(
			lSizeArray > 0 ?
			paParams[0]->GetBoolean() : true
		);
		return true;
	case recordSet::enModifiedRecordSet:
		pvarRetValue = m_objModified;
		return true;
	case recordSet::enReadRecordSet:
		Read();
		return true;
	case recordSet::enSelectedRecordSet:
		pvarRetValue = Selected();
		return true;
	case recordSet::enGetMetadataRecordSet:
		pvarRetValue = GetMetaObject();
		return true;
	}

	return false;
}

bool ibValueRecordManagerObjectInformationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case recordManager::enCopyRecordManager:
		pvarRetValue = CopyRegister();
		return true;
	case recordManager::enWriteRecordManager:
		pvarRetValue = WriteRegister(
			lSizeArray > 0 ?
			paParams[0]->GetBoolean() : true
		);
		return true;
	case recordManager::enDeleteRecordManager:
		pvarRetValue = DeleteRegister();
		return true;
	case recordManager::enModifiedRecordManager:
		pvarRetValue = m_recordSet->IsModified();
		return true;
	case recordManager::enReadRecordManager:
		m_recordSet->Read();
		return true;
	case recordManager::enSelectedRecordManager:
		pvarRetValue = m_recordSet->Selected();
		return true;
	case recordManager::enGetFormRecord:
		pvarRetValue = GetFormValue(
			lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr
		);
		return true;
	case enGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	case recordManager::enGetMetadataRecordManager:
		pvarRetValue = m_metaObject;
		return true;
	}

	return false;
}