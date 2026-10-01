#ifndef __WEB_CHILD_FRAME_H__
#define __WEB_CHILD_FRAME_H__

// Two-layer mirror of mainFrame/mainFrameChild.h's
// ibAuiChildFrame / ibAuiDocChildFrame:
//
//   Desktop: wxAuiMDIChildFrame  -> ibAuiChildFrame
//            ibDocChildFrameAny  -> ibAuiDocChildFrame
//                                   (holds ibDocument*+ibView* + ibDocManager*)
//
//   Web:     ibWebWindow         -> ibWebChildFrame   (a tab node in the
//                                                          session's JSON tree)
//            ibWebChildFrame  -> ibWebDocChildFrame   (adds doc+view+host
//                                                          ownership)
//
// ibWebChildFrame exposes the same contract that ibDocChildFrameAny<>
// template expects from its ChildFrame parameter — default ctor, Create
// signature, OnActivate stub, Destroy/Raise/Close (added on ibWebWindow).
// This means `ibDocChildFrameAny<ibWebChildFrame, ibWebWindow>` is now
// a valid instantiation; the historical "can't reuse template" comment
// (and the hand-rolled doc/view plumbing below) is the legacy form kept
// until ibWebDocChildFrame is migrated onto the template.

#include <memory>

#include <wx/icon.h>
#include <wx/string.h>

#include "webWindow.h"
// Full include — ibDocChildFrameAny<> is a template, definition must be
// visible to derive ibWebDocChildFrame from it.
#include "frontend/docView/docView.h"

class ibVisualHostClient;

// One "tab" in ibWebFrame's tab strip. Lives as a child ibWebWindow
// directly under the frame, so the session JSON renders it as a page
// container holding the form's control subtree.
class ibWebChildFrame : public ibWebWindow {
public:
	// Default ctor — required for ibDocChildFrameAny<>'s default ctor
	// path (template inherits ChildFrame and forwards to its default).
	ibWebChildFrame() = default;

	ibWebChildFrame(ibWebWindow* parent, const wxString& title);
	virtual ~ibWebChildFrame() override = default;

	virtual wxString GetControlType() const override { return wxT("mdiChild"); }

	// Template-contract stub: ibDocChildFrameAny<>::Create calls
	// BaseClass::Create(parent, id, title, pos, size, style, name) on
	// its ChildFrame parameter. wxFrame supplies a real implementation;
	// web mirrors the signature with a thin SetParent/SetLabel stub —
	// position/size/style/name don't have analogues on the JSON tree
	// renderer, they stay unused.
	bool Create(ibWebWindow* parent,
		wxWindowID /*id*/,
		const wxString& title,
		const wxPoint& /*pos*/  = wxDefaultPosition,
		const wxSize&  /*size*/ = wxDefaultSize,
		long           /*style*/= 0,
		const wxString& /*name*/= wxString())
	{
		if (parent != nullptr) SetParent(parent);
		SetLabel(title);
		return true;
	}

	// Template-contract stub: ibDocChildFrameAny<>::OnActivate forwards
	// to BaseClass::OnActivate(event) and then activates m_childView.
	// wxFrame has a non-trivial OnActivate (focus tracking, MDI activate
	// dispatch); web doesn't need any of that — empty body is correct.
	virtual void OnActivate(wxActivateEvent& /*event*/) {}

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

// Doc-aware child frame — derives the doc/view/manager triad from
// ibDocChildFrameAny<> (the forked wx doc-view template). Fields
// `m_childDocument` and `m_childView` (typed ibDocument*/ibView*) live
// on ibDocChildFrameAnyBase; we add only the host accessor.
//
// Dtor caveat: by the time ~ibWebDocChildFrame fires, ibValueForm::Close
// has already run DeleteAllViews — which (per ibDocument's contract)
// deletes the view AND the document. So m_childDocument and m_childView
// hold dangling pointers at this point. We zero them BEFORE the base
// ~ibDocChildFrameAnyBase runs (which would otherwise call
// m_childView->SetDocChildFrame(nullptr) on a dangling pointer).
class ibWebDocChildFrame
	: public ibDocChildFrameAny<ibWebChildFrame, ibWebWindow>
{
public:
	ibWebDocChildFrame(ibDocument* doc, ibView* view,
		ibWebWindow* parent, const wxString& title)
		: ibDocChildFrameAny<ibWebChildFrame, ibWebWindow>(
			doc, view, parent, wxID_ANY, title)
	{
	}

	virtual ~ibWebDocChildFrame() override
	{
		// View/doc already deleted by DeleteAllViews — clear dangling
		// refs so ~ibDocChildFrameAnyBase doesn't dereference them.
		m_childView = nullptr;
		m_childDocument = nullptr;
	}

	// GetDocument() / GetView() inherited from ibDocChildFrameAnyBase.

	// Host is owned by the view (ibFormVisualEditView::m_visualHost,
	// deleted in its dtor / OnClosingDocument). The tab holds a
	// non-owning edge via SetParent — surfaces host in the
	// ibWebWindow JSON tree without double-ownership. The dynamic_cast
	// picks the view subclass that actually carries a visual host.
	ibVisualHostClient* GetHost() const;
};

#endif
