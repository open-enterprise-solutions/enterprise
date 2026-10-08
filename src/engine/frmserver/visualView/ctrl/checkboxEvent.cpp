#include "widgets.h"
#include "form.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — an event's arguments
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaData.h"

//*******************************************************************
//*                             Events                              *
//*******************************************************************

bool ibValueCheckbox::OnClientEvent(ibProtocolEvent event, const ibDataNode& args)
{
	if (event != ibProtocolEvent::Toggle)
		return ibValueWindow::OnClientEvent(event, args);
	OnClickedCheckbox(args.GetValue<bool>(wxT("Checked")));
	return true;
}

void ibValueCheckbox::OnClickedCheckbox(bool checked)
{
	// A read-only binding takes no toggle: the box keeps its value, nothing is written and no event is
	// raised — the next Update shows the value unchanged.
	if (IsReadOnly())
		return;

	m_selValue = checked;

	if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr) {
		// Form writes only a direct-field binding (head selects the attribute); a
		// dotted reference path is read-only → no-op.
		m_formOwner->SetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), m_selValue);
	}

	m_formOwner->RefreshForm();

	CallAsEvent(m_onCheckboxClicked);
}
