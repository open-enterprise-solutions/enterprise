#include "toolBar.h"
#include "form.h"

// Populates the designer's property-grid drop-down with candidate
// action sources (the owner form plus any Tablebox under it). Only
// exercised from the designer's property editor; web doesn't render
// a property grid, so the web build collapses this to a false-return
// stub (keeps the virtual on the vtable without pulling the
// designer-only ibPropertyList code path).
bool ibValueToolbar::GetActionSource(ibPropertyList* property)
{
#ifndef OES_USE_WEB
	property->AppendItem(
		wxT("form"),
		_("Form"),
		FORM_ACTION,
		m_formOwner->GetIcon(),
		(ibValueFrame*)m_formOwner
	);

	class ibValueToolbarActionParser {
	public:
		static inline void FillActionSource(ibValueFrame* element, ibPropertyList* property) {
			if (element->GetClassName() == wxT("Tablebox")) {
				property->AppendItem(
					element->GetControlName(),
					stringUtils::GenerateSynonym(element->GetControlName()),
					element->GetControlID(),
					element->GetIcon(),
					element);
			}

			for (unsigned int i = 0; i < element->GetChildCount(); i++) {
				FillActionSource(element->GetChild(i), property);
			}
		}
	};

	ibValueToolbarActionParser::FillActionSource(m_formOwner, property);
	return true;
#else
	(void)property;
	return false;
#endif
}