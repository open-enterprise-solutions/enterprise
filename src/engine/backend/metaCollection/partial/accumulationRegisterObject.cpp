#include "accumulationRegister.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/system/systemManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

// WriteRecordSet / DeleteRecordSet inherited from ibValueRecordSetObject
// (Phase B template-method) — the scaffold is in commonObject.cpp; the
// Begin/Commit + LockByKeys helpers it calls live in commonObjectRecordSetQuery.cpp.
// Phase A bug-fix bundle (Delete-path BeforeWrite/OnWrite + error text
// mismatches) folded into the base scaffold; the per-type override that
// used to carry the bugs is removed.

// 🛑 THIS ORDER IS THE CALL NUMBER, AND IT MUST MATCH FillMembers EXACTLY. A method is invoked by
// its INDEX in the member table, so an enumerator out of step silently runs a different verb:
// `Write` landed on Load, `Load` on Unload and `Unload` on Write. Posting any document crashed
// (Write handed its bool to Load, which casts it to a table) and an Unload would have WRITTEN the
// set. Found 2026-09-03 by posting a goods receipt from the sandbox.
enum func
{
	eAdd = 0,
	eCount,
	eClear,
	eWriteRecordSet,
	eLoad,
	eUnload,
	eModifiedRecordSet,
	eReadRecordSet,
	eSelectedRecordSet,
	eGetMetadataRecordSet,
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueRecordSetObjectAccumulationRegister::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Add"), wxT("Add()"));
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
	helper.AppendFunc(wxT("Write"), 1, wxT("Write(replace : boolean)"));
	helper.AppendFunc(wxT("Load"), 1, wxT("Load(value: table)"));
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

bool ibValueRecordSetObjectAccumulationRegister::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordSetObjectAccumulationRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	// The set's own properties (Filter) live on the base — asked here first because a register may add
	// its own later, and answered by the base when it has none of its own. Returning false outright is
	// what kept `Filter` unreachable from outside while it was declared in this very file's member
	// table (2026-09-05).
	return ibValueRecordSetObject::GetPropVal(lPropNum, pvarPropVal);
}

bool ibValueRecordSetObjectAccumulationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case func::eAdd:
		pvarRetValue = new ibValueRecordSetObjectRegisterReturnLine(this, GetItem(AppendRow()));
		return true;
	case func::eCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case func::eClear:
		ibValueModelStorage::Clear();
		// ⭐ CLEARING IS A CHANGE. Emptying the set is how a handler says "no movements" - and the
		// document's final write skips a set that is not modified, so an unmarked Clear would leave
		// yesterday's movements standing.
		Modify(true);
		return true;
	case func::eLoad:
		LoadDataFromTable(paParams[0]->ConvertToType<ibValueModel>());
		return true;
	case func::eUnload:
		pvarRetValue = SaveDataToTable();
		return true;
	case func::eWriteRecordSet:
		WriteRecordSet(
			lSizeArray > 0 ?
			paParams[0]->GetBoolean() : true
		);
		return true;
	case func::eModifiedRecordSet:
		pvarRetValue = m_objModified;
		return true;
	case func::eReadRecordSet:
		Read();
		return true;
	case func::eSelectedRecordSet:
		pvarRetValue = Selected();
		return true;
	case func::eGetMetadataRecordSet:
		pvarRetValue = GetMetaObject();
		return true;
	}

	return false;
}