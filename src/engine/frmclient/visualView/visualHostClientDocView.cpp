#include "visualHostClient.h"

#include <functional>

#include "frmclient/mainFrame/mainFrame.h"

// ----------------------------------------------------------------------------
// ibFormVisualCommandProcessor
// ----------------------------------------------------------------------------

bool ibFormVisualCommandProcessor::Undo()
{
	m_document.DoCommand(ibProtocolCommand::Undo);
	return true;
}

bool ibFormVisualCommandProcessor::Redo()
{
	m_document.DoCommand(ibProtocolCommand::Redo);
	return true;
}

bool ibFormVisualCommandProcessor::CanUndo() const
{
	return m_document.IsCommandEnabled(ibProtocolCommand::Undo);
}

bool ibFormVisualCommandProcessor::CanRedo() const
{
	return m_document.IsCommandEnabled(ibProtocolCommand::Redo);
}

// ----------------------------------------------------------------------------
// ibFormVisualEditView
// ----------------------------------------------------------------------------

void ibFormVisualEditView::Draw(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (m_visualHost != nullptr)
		m_visualHost->Draw(view, patch);
}

// What a control prints as — ONE answer for both searches below: the view it holds prints it (a grid box
// prints as the spreadsheet document does, a text box as the text document), and a bare control prints
// nothing.
static wxPrintout* CreateControlPrintout(const ibValueFrame* control)
{
	ibFrontendView* const view = control->GetControlView();
	return view != nullptr ? view->OnCreatePrintout() : nullptr;
}

wxPrintout* ibFormVisualEditView::OnCreatePrintout()
{
	ibValueForm* const form = m_visualHost != nullptr ? m_visualHost->GetValueForm() : nullptr;
	if (form == nullptr)
		return nullptr;

	// ⭐ THE ACTIVE CONTROL FIRST: the cursor in a table's cell means that table.
	if (const ibValueFrame* const control = form->GetActiveControl()) {
		if (wxPrintout* const printout = CreateControlPrintout(control))
			return printout;
	}

	// …AND FAILING THAT, THE ONE THE FORM HAS. A report or a printed form holds a single table, and it
	// is what Print means wherever the cursor stands. With several, which one is meant is the person's
	// to say — by putting the cursor in it.
	wxPrintout* only = nullptr;
	int printable = 0;

	std::function<void(ibValueFrame*)> walk = [&](ibValueFrame* frame) {
		for (unsigned int idx = 0; idx < frame->GetChildCount(); idx++) {
			ibValueFrame* child = frame->GetChild(idx);
			if (wxPrintout* printout = CreateControlPrintout(child)) {
				if (printable++ == 0)
					only = printout;
				else
					delete printout;
			}
			walk(child);
		}
	};

	walk(form);

	if (printable == 1)
		return only;

	delete only;
	return nullptr;
}

bool ibFormVisualEditView::OnCreate(ibFrontendDocument* doc, long flags)
{
	// What the person does in the form goes to the server through the main window — a view that is no form, named by
	// its tab.
	m_visualHost = new ibVisualHostClient(*ibFrontendMainFrame::GetFrame(), m_viewFrame, static_cast<ibFormVisualDocument*>(doc)->GetTabId());

	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_visualHost, 1, wxEXPAND);
	m_viewFrame->SetSizer(sizer);

	WatchFocus(true);
	// EVERY command passes by: which ids are the active control's is its view's to say.
	Bind(wxEVT_MENU, &ibFormVisualEditView::OnActiveControlCommand, this);

	return ibFrontendView::OnCreate(doc, flags);
}

bool ibFormVisualEditView::OnClose(bool deleteWindow)
{
	// The focus watch comes off before the window is let go.
	WatchFocus(false);
	return ibFrontendView::OnClose(deleteWindow);
}

//********************************************************************************************
//*                          The facade over the active control                              *
//********************************************************************************************

ibFrontendView* ibFormVisualEditView::GetActiveControlView() const
{
	const ibValueForm* const form = m_visualHost != nullptr ? m_visualHost->GetValueForm() : nullptr;
	const ibValueFrame* const control = form != nullptr ? form->GetActiveControl() : nullptr;
	return control != nullptr ? control->GetControlView() : nullptr;
}

#if wxUSE_MENUS
wxMenuBar* ibFormVisualEditView::CreateMenuBar() const
{
	if (ibFrontendView* view = GetActiveControlView())
		return view->CreateMenuBar();
	return nullptr;
}
#endif

void ibFormVisualEditView::OnCreateToolbar(wxAuiToolBar* toolbar)
{
	if (ibFrontendView* view = GetActiveControlView())
		view->OnCreateToolbar(toolbar);
}

void ibFormVisualEditView::OnActivateView(bool activate, ibFrontendView* WXUNUSED(activeView), ibFrontendView* deactiveView)
{
	if (ibFrontendView* view = GetActiveControlView())
		view->OnActivateView(activate, view, deactiveView);
}

void ibFormVisualEditView::WatchFocus(bool watch)
{
	wxWindow* const frame = dynamic_cast<wxWindow*>(GetFrame());
	if (frame == nullptr)
		return;

	if (watch)
		frame->Bind(wxEVT_CHILD_FOCUS, &ibFormVisualEditView::OnChildFocus, this);
	else
		frame->Unbind(wxEVT_CHILD_FOCUS, &ibFormVisualEditView::OnChildFocus, this);
}

// ⭐ THE ACTIVE CONTROL IS PUT WHERE THE FOCUS GOES — the first control of this form on the way from the
// focused window up. A focus outside the form (a menu, a toolbar, the preview) leaves it where it was. The
// chrome is rebuilt only when the view it comes from changes, and after the focus has settled.
void ibFormVisualEditView::OnChildFocus(wxChildFocusEvent& event)
{
	event.Skip();   // the focus goes where it was going; this only watches it

	if (ibValueForm* const form = m_visualHost != nullptr ? m_visualHost->GetValueForm() : nullptr) {
		wxWindow* const frame = dynamic_cast<wxWindow*>(GetFrame());
		for (wxWindow* window = wxWindow::FindFocus(); window != nullptr && window != frame; window = window->GetParent()) {
			if (ibValueFrame* control = m_visualHost->GetObjectBase(window)) {
				form->SetActiveControl(control);
				break;
			}
		}
	}

	const ibFrontendView* const shown = GetActiveControlView();
	if (shown == m_shownControlView)
		return;

	m_shownControlView = shown;

	// The chrome, and the view it now comes from is ACTIVATED, as a document's view is when its tab is —
	// a grid box shows its sheet's properties in the inspector, as a spreadsheet document does.
	CallAfter([this]() {
		if (mainFrame != nullptr && GetFrame() != nullptr) {
			mainFrame->ActivateView(this, true);
			OnActivateView(true, this, nullptr);
		}
	});
}

// ⭐ THE FORM IS A FACADE over the view its active control holds. That view answers its own commands, LOCALLY —
// it hands nothing further up, since up is where the command came from. Anything else goes on its usual way.
//
// Save and Save as are asked of the document behind the control, as the desktop asks them — and here that document is
// a copy of the server's, which saves itself where it is kept: handed to the server. Then they go on to the form's
// document, which saves the server's (ibFormVisualDocument::Save) — after it, on the same road.
void ibFormVisualEditView::OnActiveControlCommand(wxCommandEvent& event)
{
	ibFrontendView* const view = GetActiveControlView();
	if (view == nullptr) {
		event.Skip();
		return;
	}

	if (event.GetId() == wxID_SAVE || event.GetId() == wxID_SAVEAS) {
		view->GetDocument()->Save();
		event.Skip();
		return;
	}

	if (!view->ProcessEventLocally(event))
		event.Skip();
}

// ----------------------------------------------------------------------------
// ibFormVisualDocument
// ----------------------------------------------------------------------------

ibFormVisualDocument::ibFormVisualDocument(long long tabId)
	: ibTabDocument(tabId)
{
}

void ibFormVisualDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (ibFormVisualEditView* const formView = GetFirstView())
		formView->Draw(view, patch);
}

ibFormVisualEditView* ibFormVisualDocument::GetFirstView() const
{
	return static_cast<ibFormVisualEditView*>(ibFrontendDocument::GetFirstView());
}

wxCommandProcessor* ibFormVisualDocument::GetCommandProcessor() const
{
	// While the form's active control is a document of its own, the undo is that document's.
	const ibFormVisualEditView* const formView = GetFirstView();
	if (const ibFrontendView* const view = formView != nullptr ? formView->GetActiveControlView() : nullptr)
		return view->GetDocument()->GetCommandProcessor();
	return ibFrontendDocument::GetCommandProcessor();
}

wxCommandProcessor* ibFormVisualDocument::OnCreateCommandProcessor()
{
	return new ibFormVisualCommandProcessor(*this);
}

ibFrontendView* ibFormVisualDocument::DoCreateView()
{
	return new ibFormVisualEditView();
}
