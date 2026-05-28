#ifndef __WEB_CHILD_FRAME_H__
#define __WEB_CHILD_FRAME_H__

// Two-layer mirror of mainFrame/mainFrameChild.h's
// CAuiMDIChildFrame / ibAuiDocChildFrame:
//
//   Desktop: wxAuiMDIChildFrame  -> CAuiMDIChildFrame
//            wxDocChildFrameAny  -> ibAuiDocChildFrame
//                                   (holds wxDocument*+wxView* + wxDocManager*)
//
//   Web:     ibWebWindow         -> ibWebMDIChildFrame   (a tab node in the
//                                                          session's JSON tree)
//            ibWebMDIChildFrame  -> ibWebDocChildFrame   (adds doc+view+host
//                                                          ownership)
//
// Can't reuse wxDocChildFrameAny<> directly — its template layers pull
// wxFrame's native-window API into the base, which ibWebWindow doesn't
// have. The Doc/View pointers are plumbed by hand instead; destructor
// drops the view first, then removes the document from its docManager
// (same order ibAuiDocChildFrame uses).

#include <memory>

#include <wx/icon.h>
#include <wx/string.h>

#include "webWindow.h"

class wxDocument;
class wxView;
class wxDocManager;
class ibVisualHostClient;

// One "tab" in ibWebFrame's tab strip. Lives as a child ibWebWindow
// directly under the frame, so the session JSON renders it as a page
// container holding the form's control subtree.
class ibWebMDIChildFrame : public ibWebWindow {
public:
	ibWebMDIChildFrame(ibWebWindow* parent, const wxString& title);
	virtual ~ibWebMDIChildFrame() override = default;

	virtual wxString GetControlType() const override { return wxT("mdiChild"); }

	void            SetTitle(const wxString& title) { SetLabel(title); }
	const wxString& GetTitle() const { return GetLabel(); }

	// Desktop analogue: wxAuiMDIChildFrame::SetIcon paints the tab's
	// icon. On web we keep the icon in-process (as a wxIcon) and expose
	// its PNG bytes through GET /tab/<i>/icon. An empty (IsOk()==false)
	// icon tells the client to skip the <img> and fall back to text.
	void          SetIcon(const wxIcon& icon) { m_icon = icon; }
	const wxIcon& GetIcon() const             { return m_icon; }

private:
	wxIcon m_icon;
};

// Doc-aware child frame — owns the Document / View / Host triad for
// one open form. Destruction follows ibAuiDocChildFrame's order:
// activate-off + delete the view, then unregister + delete the
// document via its manager (if any).
class ibWebDocChildFrame : public ibWebMDIChildFrame {
public:
	ibWebDocChildFrame(wxDocument* doc, wxView* view,
		ibWebWindow* parent, const wxString& title);
	virtual ~ibWebDocChildFrame() override;

	wxDocument*         GetDocument() const { return m_doc; }
	wxView*             GetView()     const { return m_view; }
	// Host is owned by the view (ibFormVisualEditView::m_visualHost,
	// deleted in its dtor / OnClosingDocument). The tab holds a
	// non-owning edge via SetParent — surfaces host in the
	// ibWebWindow JSON tree without double-ownership. ~ibWebDocChildFrame
	// detaches the host before deleting the view so the base
	// ~ibWebWindow doesn't walk a dangling m_children entry.
	ibVisualHostClient* GetHost() const;

private:
	wxDocument*     m_doc;
	wxView*         m_view;
	wxDocManager*   m_docManager;
};

#endif
