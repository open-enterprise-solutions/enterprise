#include "toolbar.h"
#include "backend/metaCollection/partial/object.h"
#include "form.h"

OptionList* CValueToolBarItem::GetToolAction(EventAction* action)
{
	OptionList* optionlist = new OptionList;
	CValueToolbar* toolbar = dynamic_cast<CValueToolbar*> (m_parent);
	if (toolbar == nullptr)
		return optionlist;
	IValueFrame* sourceElement = toolbar->GetActionSrc() != wxNOT_FOUND ?
		FindControlByID(toolbar->GetActionSrc()) : nullptr;
	if (sourceElement != nullptr) {
		const actionData_t& data = sourceElement->GetActions(sourceElement->GetTypeForm());
		for (unsigned int i = 0; i < data.GetCount(); i++) {
			const action_identifier_t& id = data.GetID(i);
			optionlist->AddOption(
				data.GetNameByID(id), id
			);
		}
	}
	return optionlist;
}