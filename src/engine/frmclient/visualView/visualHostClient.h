#ifndef _FRMCLIENT_VIEW_HOST_H__
#define _FRMCLIENT_VIEW_HOST_H__

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <wx/wx.h>
#include <wx/scrolwin.h>

#include "frmclient/docView/docView.h"
#include "frmclient/docView/tabDocument.h"
#include "protocol/protocol.h"
#include "protocol/protocolNode.h"

#include "ctrl/frame.h"   // ibViewFetcher
#include "ctrl/form.h"

class ibFrontendMainFrame;
class ibViewCommandBar;

// THE VIEW OF A TAB — the tab's form as the server writes it (the frame's View), drawn: the desktop's ibVisualHost
// (frontend/visualView/visualHost.h) — its inner scrolling window, the one that holds the controls — over the wire's
// node instead of the form. The same walk builds it (GenerateControl): a sizer item is no control, it carries how its
// one child sits in the sizer above; a window goes into the sizer it sits in, and so does a sizer.
//
// A frame of the same SHAPE — the same controls in the same places — is drawn into the controls there; one of another
// shape (a control added, removed, moved, or built otherwise) is built anew.
class FRMCLIENT_API ibVisualHostClient : public wxScrolledCanvas {
public:

	ibVisualHostClient(ibFrontendMainFrame& frame, wxWindow* parent, long long tabId);
	virtual ~ibVisualHostClient();

	// The tab's View, drawn — a form, or the one control a view that is no form is (a spreadsheet document's sheet, a
	// gridbox's node); anything else is not drawn. `patch` — what of it the answer changed (the frame's patch's View):
	// only that is drawn again; none (a frame sent whole) — the whole view.
	void Draw(const ibProtocolNode& view, const ibProtocolNode& patch);

	// What the person did with a control, to the server — after the event that said so: the answer draws the view
	// again, and a control is not torn down inside its own handler.
	void Send(long long controlId, ibProtocolEvent event, const ibProtocolNode& args);
	// …or a state event left for the next call to carry (ibFrontendMainFrame::Hold); false — Send it.
	bool Hold(long long controlId, ibProtocolEvent event, const ibProtocolNode& args);

	// A control's fetches, of the view drawn now — the form it is on, named by its key; a view that is no form, by its tab.
	ibViewFetcher MakeFetcher(long long controlId) const;
	// The form drawn — its controls, and the one active now (the desktop's GetValueForm).
	ibValueForm* GetValueForm() const { return m_valueForm.get(); }
	// The tab it draws in — whose document the form's is.
	long long GetTabId() const { return m_tabId; }
	// The control a window drawn is — none: no control's (the desktop's GetObjectBase).
	ibValueFrame* GetObjectBase(const wxObject* wxobject) const;

private:

	// An event as a call names it — the control, what happened to it, and the view it is of: sent, or held (Send, Hold).
	ibProtocolNode EventOf(long long controlId, ibProtocolEvent event, const ibProtocolNode& args) const;

	// The form's own: its colours, its controls' orientation, its command bar.
	void UpdateForm(const ibProtocolNode& form);

	void Build(const ibProtocolNode& form);
	// A node built — a control in the window and the sizer it sits in, as the sizer item above it says (an empty
	// node: none) — and what is in it, under the control it is in (none: the form).
	void Generate(const ibProtocolNode& node, wxWindow* parent, wxSizer* sizer, const ibProtocolNode& item,
		ibValueFrame* parentControl);
	// A frame of the same shape, into the controls drawn.
	void UpdateNode(const ibProtocolNode& node);
	// …and only what its patch changed: a control — or the form itself — whose node in the patch has entries of its own.
	void UpdateNode(const ibProtocolNode& node, const ibProtocolNode& patch);
	void Clear();

	// The label columns lined up (the desktop's CalculateLabelSize), and the scrolling fitted to what is drawn.
	void AlignLabels();
	void UpdateVirtualSize();

	ibFrontendMainFrame& m_mainFrame;
	const long long      m_tabId;      // the tab it draws in — what a view that is no form is named by

	wxString    m_formKey;             // the form drawn, as an event names it; empty — a view that is no form
	std::string m_shape;               // the view as last built
	wxBoxSizer* m_sizer = nullptr;     // this window's own: the command bar above the controls (the desktop's chrome)
	wxBoxSizer* m_content = nullptr;   // the form's controls', laid out as the form says

	std::unique_ptr<ibViewCommandBar> m_commandBar;   // its window is this one's child

	std::map<long long, ibValueFrame*>   m_controls;   // by their node ids — held by the form's tree
	std::map<long long, ibProtocolNode>  m_drawn;      // their nodes as last drawn, less what is in them
	std::unique_ptr<ibValueForm>         m_valueForm;  // the form — its own controls in order are its children
};

// ----------------------------------------------------------------------------
// The form's document and view — the desktop's ibFormVisualDocument / ibFormVisualEditView
// (frontend/visualView/visualHostClient.h). The runtime that raised, closed and redrew the form there is the server's
// here: a tab the frame brings is a form raised, under the tab that owns it (Parent); a tab the frame no longer has is
// a form closed; the active tab's View is the form redrawn (ibFrontendMainFrame::DrawTabs).
// ----------------------------------------------------------------------------

class ibFormVisualDocument;

// The form's undo — the server's: what may be undone and redone now is the tab's answer, and either is done there.
class FRMCLIENT_API ibFormVisualCommandProcessor : public wxCommandProcessor {
public:

	explicit ibFormVisualCommandProcessor(const ibFormVisualDocument& document) : m_document(document) {}

	virtual bool Undo() override;
	virtual bool Redo() override;

	virtual bool CanUndo() const override;
	virtual bool CanRedo() const override;

private:

	const ibFormVisualDocument& m_document;
};

class FRMCLIENT_API ibFormVisualEditView : public ibFrontendView {
public:

	// The tab's View — the frame's while the tab is the active one — drawn into it, by what the patch changed (none: whole).
	void Draw(const ibProtocolNode& view, const ibProtocolNode& patch);

	virtual wxPrintout* OnCreatePrintout() override;

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual bool OnClose(bool deleteWindow = true) override;
	virtual void OnDraw(wxDC* WXUNUSED(dc)) override {}

	// The title as the server writes it — a modified form's ends with `*` already.
	virtual void OnChangeFilename() override;

	ibVisualHostClient* GetVisualHost() const { return m_visualHost; }

	// ⭐ THE SAME DOC/VIEW, A FACADE OVER THE FORM'S ACTIVE CONTROL (ibValueForm::GetActiveControl, which this view
	// puts as the focus moves; docview-fork.md). When it holds a view of its own (ibValueFrame::GetControlView —
	// the grid box, the text box), the form hands that view the menu, the toolbar, the commands, activation,
	// printing, saving and undo, as a view that shows it would.
#if wxUSE_MENUS
	virtual wxMenuBar* CreateMenuBar() const override;
#endif
	virtual void OnCreateToolbar(wxAuiToolBar* toolbar) override;
	virtual void OnActivateView(bool activate, ibFrontendView* activeView, ibFrontendView* deactiveView) override;

	// The active control's view — null when it is a bare control, or there is none.
	ibFrontendView* GetActiveControlView() const;

private:

	ibVisualHostClient* m_visualHost = nullptr;   // the one window in the tab, filling it

	// The focus is watched on the view's window, which an EMBEDDED form does not own and which outlives
	// it — so the watch is taken off in OnClose, before the view lets the window go.
	void WatchFocus(bool watch);
	void OnChildFocus(wxChildFocusEvent& event);
	void OnActiveControlCommand(wxCommandEvent& event);

	const ibFrontendView* m_shownControlView = nullptr;   // whose chrome is shown now — compared, never followed
};

// The tab of a form — the form's view in it, and the form's undo.
class FRMCLIENT_API ibFormVisualDocument : public ibTabDocument {
public:

	explicit ibFormVisualDocument(long long tabId);

	// The form drawn — the frame's View, as the patch changed it.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	// While the form's active control is a document of its own, the undo is that document's.
	virtual wxCommandProcessor* GetCommandProcessor() const override;

	ibFormVisualEditView* GetFirstView() const;

protected:

	virtual ibFrontendView* DoCreateView() override;
};

#endif
