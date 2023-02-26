#include "toolbar.h"
#include "core/metadata/metaObjects/objects/object.h"
#include "form.h"

OptionList* CValueToolBarItem::GetToolAction(EventAction* action)
{
	OptionList* optionlist = new OptionList;
	CValueToolbar* toolbar = dynamic_cast<CValueToolbar*> (m_parent);
	if (toolbar == NULL)
		return optionlist;
	IValueFrame* sourceElement = toolbar->GetActionSrc() != wxNOT_FOUND ?
		FindControlByID(toolbar->GetActionSrc()) : NULL;
	if (sourceElement != NULL) {
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