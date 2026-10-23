#include "notebook.h"
#include "form.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — an event's arguments
#include "backend/appData.h"

bool ibValueNotebook::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	switch (event) {
	case ibProtocol::ibEvent::ibKind::Page: OnPageChanged((ibFormID)args.GetValue<std::int32_t>(ibProtocol::ibEvent::Page)); return true;
	case ibProtocol::ibEvent::ibKind::Move: OnEndDrag((unsigned int)args.GetValue<std::int32_t>(ibProtocol::ibEvent::Position)); return true;
	default:                  return ibValueWindow::OnClientEvent(event, args);
	}
}

void ibValueNotebook::OnPageChanged(const ibFormID& pageId)
{
	ibValueNotebookPage* activePage = nullptr;
	for (unsigned int i = 0; i < GetChildCount(); i++) {
		ibValueNotebookPage* child = dynamic_cast<ibValueNotebookPage*>(GetChild(i));
		wxASSERT(child);
		if (child != nullptr && child->GetControlID() == pageId)
			activePage = child;
	}

	if (activePage == nullptr || m_activePage == activePage)
		return;

	m_activePage = activePage;

	CallAsEvent(
		m_eventOnPageChanged, ibValue(m_activePage)
	);

	m_formOwner->RefreshForm();
}

void ibValueNotebook::OnEndDrag(unsigned int position)
{
	// A form open in the designer keeps the page order it was designed with.
	if (!appData->DesignerMode())
		ChangeChildPosition(m_activePage, position);
}
