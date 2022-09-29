#include "toolbar.h"
#include "form.h"

inline void ParseElements(IValueFrame* element, OptionList* optionlist)
{
	if (element->GetClassName() == wxT("tablebox")) {
		optionlist->AddOption(element->GetControlName(), element->GetControlID());
	}

	for (unsigned int i = 0; i < element->GetChildCount(); i++) {
		ParseElements(element->GetChild(i), optionlist);
	}
}

OptionList* CValueToolbar::GetActionSource(PropertyOption*)
{
	OptionList* optionlist = new OptionList;

	optionlist->AddOption(_("<not selected>"), 0);
	optionlist->AddOption(_("form"), FORM_ACTION);

	ParseElements(GetOwnerForm(), optionlist);
	return optionlist;
}