////////////////////////////////////////////////////////////////////////////
//	Description : document journal manager
////////////////////////////////////////////////////////////////////////////

#include "documentJournalManager.h"

#include "backend/system/value/valueGuid.h"

enum {
	eSelect,
	eGetListForm,
};

void ibValueManagerDataObjectDocumentJournal::FillManagerMethods(ibMemberTable& helper) const
{
	// The references of the documents listed, by date: `Select(From, To)`, either bound left out.
	helper.AppendFunc(ibT("Select"), 2, ibT("Select(From : date, To : date)"));
	helper.AppendFunc(ibT("GetListForm"), 3, ibT("GetListForm(string, owner, guid)"));
}

bool ibValueManagerDataObjectDocumentJournal::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (BuiltinMethodNum(lMethodNum))
	{
	case eSelect:
		pvarRetValue = m_metaObject->Select(lSizeArray > 0 ? *paParams[0] : ibValue(), lSizeArray > 1 ? *paParams[1] : ibValue());
		return true;
	case eGetListForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(ibFormRequest(lSizeArray > 0 ? paParams[0]->GetString() : ibString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}
