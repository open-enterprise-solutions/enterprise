#include "webChildFrame.h"

#include "visualView/visualHostClient.h"

ibWebChildFrame::ibWebChildFrame(ibWebWindow* parent, const wxString& title)
{
	SetLabel(title);
	if (parent != nullptr)
		SetParent(parent);
}

// ibWebDocChildFrame ctor + dtor are inline in webChildFrame.h now — they
// just forward to ibDocChildFrameAny<> base / zero base's dangling refs.

ibVisualHostClient* ibWebDocChildFrame::GetHost() const
{
	// Route through the ibMetaView subclass that holds the per-tab host.
	// m_childView lives on ibDocChildFrameAnyBase (template base); pre-
	// migration this used the hand-rolled m_view, same dynamic_cast logic.
	auto* view = dynamic_cast<ibFormVisualEditView*>(m_childView);
	return view != nullptr ? view->GetVisualHost() : nullptr;
}
