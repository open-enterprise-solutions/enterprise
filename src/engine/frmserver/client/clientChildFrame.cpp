#include "clientChildFrame.h"
#include "clientFrame.h"

ibClientChildFrame::ibClientChildFrame(ibClientFrame* frame, s32 id, ibDocument* doc, ibView* view, const wxString& title)
	: ibDocChildFrameAnyBase(doc, view), m_frame(frame), m_id(id), m_title(title)
{
}

ibClientChildFrame::~ibClientChildFrame()
{
	// The view and the document are gone by now (ibDocument::DeleteAllViews deletes both, and the frame
	// erases the tab only after it): forget them, so the base does not tell a freed view it lost its frame.
	m_childView = nullptr;
	m_childDocument = nullptr;
}

wxString ibClientChildFrame::GetTitle() const
{
	const ibDocument* const document = GetDocument();
	wxString title = document != nullptr && !document->GetTitle().IsEmpty() ? document->GetTitle() : m_title;
	// …and "*" while its document is modified — what the desktop's view put into its frame's label
	// (ibView::OnChangeFilename); the client shows the title as it is given.
	if (document != nullptr && document->IsModified())
		title += wxT("*");
	return title;
}

bool ibClientChildFrame::Show(bool show)
{
	if (!show || m_frame == nullptr)
		return false;
	return m_frame->ActivateTab(this);
}

bool ibClientChildFrame::Close()
{
	if (m_frame == nullptr)
		return false;
	// ⚠ THE DOCUMENT IS ASKED HERE, in the work of the close — as the desktop's frame asked it before its views went
	// (the view's close → the document's Close → CanClose): a changed document asks the person whether to save it,
	// and the answer is waited for only in the work. Left to the views going at the end of the request
	// (DeleteAllViews → OnChangedViewList → OnSaveModified), the question stood inside the frame's drawing, which
	// waits for no answer — the call never came back (2026-10-06, a new object's form closed). Not when the frame
	// itself closes its documents: nobody is there to answer.
	ibDocument* const document = GetDocument();
	if (document != nullptr && !m_frame->IsClosingWindow() && !document->CanClose())
		return false;
	m_frame->MarkTabForClose(this);
	return true;
}
