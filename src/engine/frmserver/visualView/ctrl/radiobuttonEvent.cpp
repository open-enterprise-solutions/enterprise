#include "widgets.h"
#include "form.h"
#include "sizer.h"                        // ibValueSizerItem — what a control stands in among its siblings
#include "core/serialize/dataBuilder.h"   // ibDataNode — an event's arguments

//*******************************************************************
//*                             Events                              *
//*******************************************************************

bool ibValueRadioButton::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	if (event != ibProtocol::ibEvent::ibKind::Toggle)
		return ibValueWindow::OnClientEvent(event, args);
	OnClickedRadioButton();
	return true;
}

void ibValueRadioButton::OnClickedRadioButton()
{
	// A read-only binding takes no click: nothing is written and no event is raised — the next Update shows the
	// selection unchanged.
	if (IsReadOnly())
		return;

	if (!m_propertySource->IsEmptyProperty()) {
		// Its value into the source — the buttons bound beside it read it back as not theirs. A button standing for
		// no value has nothing to write.
		if (m_propertyChoiceValue->IsEmptyProperty() || m_formOwner == nullptr)
			return;
		m_formOwner->SetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), GetChoiceValue());
	}
	else {
		// Selected among the unbound radio buttons beside it — the ones in the same group, each in its sizer item.
		ibValueFrame* item = GetParent();
		ibValueFrame* const group = item != nullptr && item->GetComponentType() == COMPONENT_TYPE_SIZERITEM
			? item->GetParent() : item;
		for (unsigned int idx = 0; group != nullptr && idx < group->GetChildCount(); idx++) {
			ibValueFrame* sibling = group->GetChild(idx);
			if (sibling != nullptr && sibling->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
				sibling = sibling->GetChildCount() > 0 ? sibling->GetChild(0) : nullptr;
			ibValueRadioButton* const radio = dynamic_cast<ibValueRadioButton*>(sibling);
			if (radio != nullptr && radio != this && radio->m_propertySource->IsEmptyProperty())
				radio->m_propertySelected->SetValue(false);
		}
		m_propertySelected->SetValue(true);
	}

	if (m_formOwner != nullptr)
		m_formOwner->RefreshForm();

	CallAsEvent(m_onChange);
}
