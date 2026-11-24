////////////////////////////////////////////////////////////////////////////
//	Description : document journal manager
////////////////////////////////////////////////////////////////////////////

#include "documentJournalManager.h"
#include "selector/objectSelector.h"

#include "backend/system/value/valueGuid.h"

enum {
	eSelect,
	eGetForm,
	eGetListForm,
	eGetTemplate,
};

void ibValueManagerDataObjectDocumentJournal::FillManagerMethods(ibMemberTable& helper) const
{
	// A period, both bounds the date's and inclusive: `Select(From, To)`, either left out.
	helper.AppendFunc(ibT("Select"), 2, ibT("Select(From : date, To : date)"));
	helper.AppendFunc(ibT("GetForm"), 3, ibT("GetForm(string, owner, guid)"));
	helper.AppendFunc(ibT("GetListForm"), 3, ibT("GetListForm(string, owner, guid)"));
	helper.AppendFunc(ibT("GetTemplate"), 1, ibT("GetTemplate(string)"));
}

bool ibValueManagerDataObjectDocumentJournal::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (m_members.GetMethodData(lMethodNum, this, &ibValueManagerDataObjectDocumentJournal::FillManagerMethods))
	{
	case eSelect:
		pvarRetValue = new ibValueSelectorDocumentJournalDataObject(m_metaObject,
			lSizeArray > 0 ? *paParams[0] : ibValue(), lSizeArray > 1 ? *paParams[1] : ibValue());
		return true;
	// A journal's forms are its lists, as a register's are: GetForm is the list form named.
	case eGetForm:
	case eGetListForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(ibFormRequest(lSizeArray > 0 ? paParams[0]->GetString() : ibString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	case eGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}
