#ifndef __CALLBACK_DROP_TARGET_H__
#define __CALLBACK_DROP_TARGET_H__

////////////////////////////////////////////////////////////////////////////
// A DROP TARGET THAT IS A CALLBACK — the same-process drag, named once.
////////////////////////////////////////////////////////////////////////////
//
// wx wants a wxDropTarget SUBCLASS for every place something can be dropped, which is a class per
// gesture — and inside one product the gesture is nearly always the same one: a control drags,
// another control accepts, both are ours, and the payload exists only because wx will not start a
// drag without one. What actually moves is the SELECTION of the source control, which the handler
// already knows how to read.
//
// Two constructors, because there are two honest cases and forcing one shape on both serves
// neither:
//
//   * WHAT was dropped is already known (the source's selected row) — the handler takes nothing.
//     The query constructor's join diagram and the list settings' composition panels.
//   * WHERE it was dropped matters (a text editor: a field belongs under the mouse, not at
//     whatever the caret was doing) — the handler takes the point and the text, and says whether
//     it took it.
//
////////////////////////////////////////////////////////////////////////////

#include <wx/dnd.h>

#include <functional>
#include <utility>

class ibCallbackDropTarget : public wxTextDropTarget
{
public:
	// The whole answer: where, what, and whether it was accepted.
	using Handler = std::function<bool(wxCoord x, wxCoord y, const wxString& text)>;

	explicit ibCallbackDropTarget(Handler onDrop) : m_onDrop(std::move(onDrop)) {}

	// A SAME-PROCESS DRAG whose payload carries nothing: the thing being moved is what the source
	// control has selected, so the drop is a notification and nothing else.
	explicit ibCallbackDropTarget(std::function<void()> onDrop)
		: m_onDrop([handler = std::move(onDrop)](wxCoord, wxCoord, const wxString&) {
			if (handler) handler();
			return true;
		}) {}

	bool OnDropText(wxCoord x, wxCoord y, const wxString& text) override
	{
		return m_onDrop ? m_onDrop(x, y, text) : false;
	}

private:
	Handler m_onDrop;
};

#endif
