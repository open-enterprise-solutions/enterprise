////////////////////////////////////////////////////////////////////////////
//	Description : a set of registrations — and the border it moves
////////////////////////////////////////////////////////////////////////////

#include "sequence.h"

#include "backend/metaData.h"

// 🛑 THIS ORDER IS THE CALL NUMBER, AND IT MUST MATCH FillMembers EXACTLY — a method is invoked by its
// INDEX in the member table, so an enumerator out of step silently runs a different verb (the register's
// set learned that in 2026-09-03: `Write` landed on `Load`).
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

// What a set of registrations answers to: the same verbs a register's set does — it is filled, read,
// written and unloaded in the same words, and a configuration should not have to learn a second set
// of them for this kind of row.
void ibValueRecordSetObjectSequence::FillMembers(ibMemberTable& helper) const
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
}

// 🛑 …AND DECLARING THEM IS HALF THE WORK. The member table says a set HAS `Add`; this says what
// `Add` DOES, and without it the call answered an empty value — `Sequences.<Name>.Add()` handed the
// handler nothing and the next line died on "a variable is not an aggregate object" (2026-09-18).
// Written exactly as a register's set writes it (accumulationRegisterObject.cpp), because a
// registration is filled, counted, cleared and unloaded in the same words as a movement.
bool ibValueRecordSetObjectSequence::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
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
		// Clearing IS a change: emptying the set is how a handler says "no registrations", and the
		// document's final write skips a set that is not modified.
		Modify(true);
		return true;
	case func::eLoad:
		LoadDataFromTable(paParams[0]->ConvertToType<ibValueModel>());
		return true;
	case func::eUnload:
		pvarRetValue = SaveDataToTable();
		return true;
	case func::eWriteRecordSet:
		WriteRecordSet(lSizeArray > 0 ? paParams[0]->GetBoolean() : true);
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

// The recorder this set is addressed by — empty for a set nobody keyed, which is the answer the
// rules take as "there is no document here to move a border for".
ibValue ibValueRecordSetObjectSequence::Recorder() const
{
	const ibValueMetaObjectSequence* seq = GetSequenceMetaObject();
	const ibMetaID id = seq != nullptr ? seq->GetRegisterRecorder()->GetMetaID() : 0;
	return id != 0 && FindKeyValue(id) ? GetKeyValue(id) : ibValue();
}

// ⭐⭐ THE SET MOVES ITS OWN BORDER — FORWARD AFTER THE ROWS ARE WRITTEN. They are what the border
// steps onto: the rules read the keys off them and move exactly those keys (sequenceBorderRules.cpp).
bool ibValueRecordSetObjectSequence::WriteRecordSet(bool replace, bool clearTable)
{
	if (!ibValueRecordSetObject::WriteRecordSet(replace, clearTable))
		return false;
	ibSequenceBorderWritten(GetSequenceMetaObject(), Recorder());
	return true;
}

// …AND BACK BEFORE THEY ARE CLEARED. After the delete the rows are gone and nothing could name the
// keys this document belonged to — this is the last point at which they are known.
bool ibValueRecordSetObjectSequence::DeleteRecordSet()
{
	ibSequenceBorderCleared(GetSequenceMetaObject(), Recorder());
	return ibValueRecordSetObject::DeleteRecordSet();
}
