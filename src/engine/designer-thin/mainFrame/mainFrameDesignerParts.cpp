////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the designer's panes
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

// ---------------------------------------------------------------------------
// Syntax-helper sidebar — lazy AUI pane, the desktop's (designer/mainFrame/mainFrameDesignerParts.cpp). The corpus is
// the core's, read on this side: the pane holds it.
// ---------------------------------------------------------------------------

#include "designer-thin/syntaxHelper/helpPaneView.h"

void ibFrontendMainFrameDesigner::EnsureHelpPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_HELP).IsOk()) return;

	// The corpus speaks the application's language — the desktop's process locale, here the server's (its About).
	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::About));
	const wxString locale = Call(ibProtocolMethod::Schema, params, answer) ? answer.GetString(ibProtocolName::Locale) : wxString();

	m_helpPane = new ibHelpPaneView(this, locale);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_HELP);
	paneInfo.Caption(_("Syntax Helper"));
	paneInfo.Right();
	paneInfo.Layer(1);
	paneInfo.MinSize(320, 480);
	paneInfo.BestSize(360, 600);
	paneInfo.CloseButton(true);
	paneInfo.MaximizeButton(false);
	paneInfo.MinimizeButton(false);
	paneInfo.Show(true);

	m_mgr.AddPane(m_helpPane, paneInfo);
	m_mgr.Update();
}

void ibFrontendMainFrameDesigner::ToggleHelpPane()
{
	const bool firstCreate = !m_mgr.GetPane(wxAUI_PANE_HELP).IsOk();
	EnsureHelpPane();
	wxAuiPaneInfo& pane = m_mgr.GetPane(wxAUI_PANE_HELP);
	if (!pane.IsOk()) return;
	// EnsureHelpPane already adds the pane visible. On the first
	// invocation a naive "flip" would immediately hide it; only toggle
	// on subsequent invocations.
	if (!firstCreate) pane.Show(!pane.IsShown());
	m_mgr.Update();
}
