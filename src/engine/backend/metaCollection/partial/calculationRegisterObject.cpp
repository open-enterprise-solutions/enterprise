#include "calculationRegister.h"

// The record set is written as every register's is (ibValueRecordSetObject::SaveData): the document forms its
// movements — a correction of a past month included, as records of the current one — and holds them, and what
// is derived from them is a reading of them (calculationRegister.h, "The fact — a reading of the records"). The write
// adds one thing of its own: the marks of what its records lead (calculationRegister.h, "The recalculation's marks").

////////////////////////////////////////////////////////////////////////////////////////////////////

// 🛑⭐⭐ THE ORDER HERE IS THE ORDER OF FillMembers BELOW, and nothing checks it. A method is called by
// its INDEX in the member table, so a case label that sits one row off answers the wrong method. The
// import had Load and Unload ahead of Write here while FillMembers declares Write first — so a
// script's `rs.Write()` ran Load (casting its argument to a table: "Variable type does not support this
// operation"), `Load(t)` ran Unload, and `Unload()` WROTE THE SET. MEASURED 2026-09-10 with a
// first-chance exception stack: ThrowErrorTypeOperation <- ConvertToType<ibValueModel> <- this CallAsFunc.
//
// The accumulation register had this exact misordering at the snapshot the import was taken from
// (2026-08-16) and was corrected on 2026-09-04 (0ace1d0c); this copy predates the correction.
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

void ibValueRecordSetObjectCalculationRegister::FillMembers(ibMemberTable& helper) const
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

	// `Filter` is NOT declared here — see informationRegisterObject.cpp: it is an export variable of
	// the set, bound in InitializeObject, and that reaches this table on its own.
}

//****************************************************************************
//*                       A line of a calculation register                   *
//****************************************************************************

void ibValueRecordSetObjectCalculationRegister::DescribeReturnLine(ibMemberTable& helper) const
{
	ibValueRecordSetObject::DescribeReturnLine(helper);
	helper.AppendFunc(wxT("GetBase"), 3, wxT("GetBase(Resources, Dimensions, Sections)"),
		ibValueCalculationLine::eMethodGetBase, wxNOT_FOUND);
}

// The line asks as it stands in the set — its type, its base period (or its registration), its values of the paired dimensions — so a
// posting takes the base of a movement it has just formed, and nothing of the recorder's is read back for it.
bool ibValueRecordSetObjectCalculationRegister::ibValueCalculationLine::CallAsFunc(const long lMethodNum,
	ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	if (m_ownerSet->m_methodHelperReturnLine.GetMethodData(lMethodNum) != eMethodGetBase)
		return ibValueRecordSetObjectRegisterReturnLine::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);

	const ibValueMetaObjectCalculationRegister* reg = m_ownerSet->GetCalculationMetaObject();
	const ibCalcBaseAsked asked = ibCalcBaseAskedOf(reg, lSizeArray > 0 ? *paParams[0] : ibValue(),
		lSizeArray > 1 ? *paParams[1] : ibValue(), lSizeArray > 2 ? *paParams[2] : ibValue());

	ibCalcBaseRecord record;
	ibValue from, to, registered;
	GetValueByMetaID(reg->GetRegisterLineNumber()->GetMetaID(), record.m_line);
	GetValueByMetaID(reg->GetCalculationType()->GetMetaID(), record.m_type);
	GetValueByMetaID(reg->GetBasePeriodStart()->GetMetaID(), from);
	GetValueByMetaID(reg->GetBasePeriodEnd()->GetMetaID(), to);
	GetValueByMetaID(reg->GetRegistrationPeriod()->GetMetaID(), registered);
	record.m_from = from.GetDateTime();
	record.m_to = to.GetDateTime();
	record.m_registration = registered.GetDateTime();
	for (const ibCalcBaseAsked::ibPairing& pairing : asked.m_dimensions) {
		ibValue value;
		GetValueByMetaID(pairing.m_own->GetMetaID(), value);
		record.m_dimensions.push_back(value);
	}
	pvarRetValue = ibCalcReadBase(reg, asked, { record });
	return true;
}

//****************************************************************************
//*                    The write, and the marks it leaves                    *
//****************************************************************************

namespace {

// The recorder this set is addressed by, or empty when it is addressed by nothing (a set with no key is the whole
// register, and there is no one recorder to mark by).
ibValue RecorderOfSet(const ibValueMetaObjectCalculationRegister* meta, const ibRowMetaValues& keys)
{
	if (meta == nullptr || meta->GetRegisterRecorder() == nullptr)
		return ibValue();
	const auto found = keys.find(meta->GetRegisterRecorder()->GetMetaID());
	return found != keys.end() ? found->second : ibValue();
}

} // namespace

// ⭐ A WRITE IS A CLEARING AND A WRITING, NOT A DIFFERENCE (Max, 2026-09-14: "deleting and posting again is clearing the
// movements"; "tracking every sneeze over millions is too much"). What the recorder held leads before it goes — it is
// gone; what it holds leads after it is in. Nothing compares the two: a set written again as it was marks what its
// lines lead, as the reference does, and the run that meets those marks finds its figures as they were. A comparison
// was built for a day and taken out — the posting's own clearing (a document posted again starts from a base without
// its own movements, ibValueRecordDataObjectRecorderRef::WriteObject) left it nothing to compare with anyway.
// Before: the positions its stornos reversed are marked again; after: its own marks are answered, and its stornos
// answer theirs — last, so a mark its own records have just made is answered by its own correction beside them
// (calculationRegister.h, "The recalculation's marks").
bool ibValueRecordSetObjectCalculationRegister::SaveData(bool replace, bool clearTable)
{
	const ibValueMetaObjectCalculationRegister* meta = GetCalculationMetaObject();
	const ibValue recorder = RecorderOfSet(meta, m_keyValues);
	if (recorder.IsEmpty())
		return ibValueRecordSetObject::SaveData(replace, clearTable);

	if (replace) {
		ibRecalculationMarkReversed(meta, recorder);
		ibRecalculationMarkLedBy(meta, recorder);
	}
	if (!ibValueRecordSetObject::SaveData(replace, clearTable))
		return false;
	ibRecalculationAnswerOwn(meta, recorder);
	ibRecalculationMarkLedBy(meta, recorder);
	ibRecalculationAnswerBy(meta, recorder);
	return true;
}

bool ibValueRecordSetObjectCalculationRegister::DeleteData()
{
	const ibValueMetaObjectCalculationRegister* meta = GetCalculationMetaObject();
	const ibValue recorder = RecorderOfSet(meta, m_keyValues);
	if (!recorder.IsEmpty()) {
		ibRecalculationMarkReversed(meta, recorder);
		ibRecalculationMarkLedBy(meta, recorder);
	}
	if (!ibValueRecordSetObject::DeleteData())
		return false;
	if (!recorder.IsEmpty())
		ibRecalculationAnswerOwn(meta, recorder);
	return true;
}

//////////////////////////////////////////////////////////////////////////

bool ibValueRecordSetObjectCalculationRegister::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordSetObjectCalculationRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	// 🛑⭐⭐ THE SET'S OWN PROPERTIES (Filter) LIVE ON THE BASE — delegate, never answer false outright.
	// This exact defect was fixed in the accumulation register on 2026-09-05; the calculation register was
	// imported from a snapshot of 2026-08-16, BEFORE that fix, and carried the old `return false` with it.
	// MEASURED 2026-09-10 through the registration journal: `TypeOf(rs.Filter)` answered Undefined here
	// and RecordSetRegisterKey on an accumulation register — so no record set of this register could be
	// pointed at its recorder, which is the one thing a register ALWAYS subordinate to one must do.
	return ibValueRecordSetObject::GetPropVal(lPropNum, pvarPropVal);
}

bool ibValueRecordSetObjectCalculationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);

	switch (lMethodNum)
	{
	case recordSet::enAdd:
		pvarRetValue = new ibValueCalculationLine(this, GetItem(AppendRow()));
		return true;
	case recordSet::enCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case recordSet::enClear:
		ibValueModelStorage::Clear();
		// ⭐ CLEARING IS A CHANGE. Emptying the set is how a handler says "no movements" - and the
		// document's final write skips a set that is not modified, so an unmarked Clear would leave
		// yesterday's movements standing. For a calculation register this is the RECALCULATION path
		// itself - clear and rewrite - so without it a recalculated payroll kept the old figures.
		// (The accumulation register got this in 0ace1d0c, 2026-09-04; the import predates it.)
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
