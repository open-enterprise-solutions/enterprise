#ifndef __CLIENT_CHILD_FRAME_H__
#define __CLIENT_CHILD_FRAME_H__

// ONE TAB of a client's frame — the server object a document's view shows in. The doc/view framework
// knows it as the view's frame (ibDocChildFrameAnyBase: the view is told its frame there), the client's
// frame keeps it in its tab list, and a client draws it from what the protocol answers about it: its
// title, its icon, and the document's view inside.
//
// It does not own the document: the document goes with its last view (ibDocument::DeleteAllViews), and
// the tab goes after it — the frame erases it once the view is gone.

#include <wx/string.h>

#include "frmserver/docView/docView.h"

class ibClientFrame;

class FRMSERVER_API ibClientChildFrame : public ibDocChildFrameAnyBase {
public:

	ibClientChildFrame(ibClientFrame* frame, s32 id, ibDocument* doc, ibView* view, const wxString& title);
	virtual ~ibClientChildFrame() override;

	// Its id in the frame — what a client names it by (the wire's NodeId of the tab): given when it opens, never
	// another tab's, so a tab closing shifts no other.
	s32 GetId() const { return m_id; }

	// Shown = the client's ACTIVE tab: the frame switches to it. A tab is never hidden on its own — it is
	// closed instead — so `false` changes nothing.
	virtual bool Show(bool show = true) override;

	// Closed = marked: the frame takes the view (and with it the document) down at the end of the request,
	// when nothing on the stack is inside them any more — then the tab.
	virtual bool Close() override;

	ibClientFrame* GetFrame() const { return m_frame; }

	// Its title — its document's once the document has one (a form's host sets it from the form's caption,
	// ibFormVisualEditView::SetCaption, as the desktop's tab followed its document), the one it opened with before.
	void     SetTitle(const wxString& title) { m_title = title; }
	wxString GetTitle() const;

	// The tab's icon — the document's own when it has one (a catalog's form shows the catalog's), the
	// form's otherwise. An empty icon tells the client to show the title alone.
	void                   SetIcon(const ibServerPicture& icon) { m_icon = icon; }
	const ibServerPicture& GetIcon() const { return m_icon; }

	// Locked — the frame keeps it ahead of the normal tabs and lets no close reach it (ibClientFrame::LockTab).
	void Lock() { m_locked = true; }
	bool IsLocked() const { return m_locked; }

private:

	ibClientFrame*  m_frame;
	s32             m_id;
	wxString        m_title;
	ibServerPicture m_icon;
	bool            m_locked = false;
};

#endif
