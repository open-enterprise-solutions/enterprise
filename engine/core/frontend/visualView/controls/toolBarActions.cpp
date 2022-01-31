#include "toolbar.h"
#include "form.h"

void ParseElements(IValueFrame *element, OptionList *optionlist)
{
	if (element->GetClassName() == wxT("tablebox")) {
		optionlist->AddOption(element->GetPropertyAsString("name"), element->GetControlID());
	}

	for (unsigned int i = 0; i < element->GetChildCount(); i++) {
		ParseElements(element->GetChild(i), optionlist);
	}
}

OptionList *CValueToolbar::GetActionSource(Property *)
{
	OptionList *optionlist = new OptionList;

	optionlist->AddOption("<not selected>", 0);
	optionlist->AddOption("form", FORM_ACTION);

	ParseElements(GetOwnerForm(), optionlist);
	return optionlist;
}