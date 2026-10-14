////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the designer's events
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

//********************************************************************************
//*                                    Tool                                      *
//********************************************************************************

#include "frmclient/backend/appData.h"
#include "frmclient/backend/mcp/mcpServer.h"
#include "frmclient/mainFrame/settings/settingsdialog.h"
#include "frmclient/mainFrame/settings/keybinderdialog.h"
#include "frmclient/mainFrame/settings/fontcolorsettingspanel.h"
#include "frmclient/mainFrame/settings/editorsettingspanel.h"
#include "frmclient/mainFrame/settings/mcpsettingspanel.h"

#include "frmclient/win/dlgs/userList.h"
#include "frmclient/window_ptr.h"

void ibFrontendMainFrameDesigner::ShowSchema(ibProtocolSchema schema, const ibProtocolNode& shown)
{
	// Administration → Users — the server's menu item (its UserList schema); the desktop's OnUsers as it is, the list
	// asked of the schema again by the dialog.
	if (schema == ibProtocolSchema::UserList) {
		ibWindowPtr<ibDialogUserList> dlg(new ibDialogUserList(this, wxID_ANY));
		dlg->Show();
		return;
	}

	// Tools → Options — the server's menu item (its McpServer schema); the dialog the desktop's OnToolsSettings
	// (designer/mainFrame/mainFrameDesignerEvent.cpp) as it is, its assistant access asked of the schema again.
	if (schema != ibProtocolSchema::McpServer) {
		ibFrontendMainFrame::ShowSchema(schema, shown);
		return;
	}

	ibDialogSettings dialog(this);

	ibDialogKeyBinder* keyBinder = dialog.GetKeyBinderDialog();

	for (unsigned int i = 0; i < m_keyBinder.GetNumCommands(); ++i)
	{
		keyBinder->AddCommand(m_keyBinder.GetCommand(i));
	}

	ibPanelFontColorSettings* fontColorSettings = dialog.GetFontColorSettingsPanel();
	fontColorSettings->SetSettings(m_fontColorSettings);

	ibPanelEditorSettings* editorSettings = dialog.GetEditorSettingsPanel();
	editorSettings->SetSettings(m_editorSettings);

	// ASSISTANT ACCESS. Unlike the pages above, this one is not the frame's own
	// state kept in an options file — it belongs to the PERSON in this base, so
	// it is read from and written back to the server that owns it.
	ibPanelMcpSettings* mcpSettings = dialog.GetMcpSettingsPanel();
	ibMcpServer* mcpServer = ibApplicationInstance::GetMcpServer();
	if (mcpServer != nullptr) {
		mcpSettings->SetSettings(mcpServer->GetSettings());
		mcpSettings->SetEndpoint(mcpServer->GetEndpoint());
	}

	if (dialog.ShowModal() == wxID_OK)
	{
		if (mcpServer != nullptr) {
			// Configure first, save second, both from the one value the page
			// holds — so what is written down and what is in force cannot differ.
			// A running server is NOT moved by this: it keeps the address it took
			// until somebody stops and starts it, which is the only way a page can
			// say one thing while a client talks to another and be caught at it.
			mcpServer->Configure(mcpSettings->GetSettings());
			mcpServer->SaveSettings(ibSession::Current());
		}

		m_keyBinder.ClearCommands();

		for (unsigned int i = 0; i < keyBinder->GetNumCommands(); ++i) {
			m_keyBinder.AddCommand(keyBinder->GetCommand(i));
		}

		m_keyBinder.UpdateWindow(this);

#if wxUSE_MENUBAR
		m_keyBinder.UpdateMenuBar(GetMenuBar());
#endif

		m_fontColorSettings = fontColorSettings->GetSettings();
		m_editorSettings = editorSettings->GetSettings();
	}

	SaveOptions();
}
