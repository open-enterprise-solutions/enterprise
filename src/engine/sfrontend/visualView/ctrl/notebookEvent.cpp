#include "notebook.h"
#include "form.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — an event's arguments
#include "backend/appData.h"

bool ibValueNotebook::OnClientEvent(ibClientEvent event, const ibDataNode& args)
{
	switch (event) {
	case ibClientEvent::Page: OnPageChanged((ibFormID)args.GetValue<s32>(wxT("Page"))); return true;
	case ibClientEvent::Move: OnEndDrag((unsigned int)args.GetValue<s32>(wxT("Position"))); return true;
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
