#include "toolbar.h"
#include "metadata/objects/baseObject.h"
#include "form.h"

OptionList *CValueToolBarItem::GetActions(Property *)
{
	OptionList *optionlist = new OptionList;
	CValueToolbar *toolbar = dynamic_cast<CValueToolbar *> (GetParent());

	if (!toolbar) {
		return optionlist;
	}

	IValueFrame *sourceElement = toolbar->m_actionSource != wxNOT_FOUND ?
		FindControlByID(toolbar->m_actionSource) : NULL;

	if (sourceElement) {
		actionData_t actions = sourceElement->GetActions(sourceElement->GetTypeForm());
		for (unsigned int i = 0; i < actions.GetCount(); i++) {
			action_identifier_t action_id = actions.GetID(i);
			optionlist->AddOption(actions.GetNameByID(action_id), action_id);
		}
	}

	return optionlist;
}