////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include "compiler/debugger/debugClient.h"

#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include <wx/xml/xml.h>

CMainFrameDesigner::CMainFrameDesigner(const wxString& title,
	const wxPoint& pos,
	const wxSize& size) : CMainFrame(title, pos, size), m_menuBar(NULL)
{
	Connect(wxEVT_DEBUG_EVENT, wxDebugEventHandler(CMainFrameDesigner::OnDebugEvent), NULL, this);
	debugClient->AddHandler(this);
}

CMainFrameDesigner::~CMainFrameDesigner()
{
	Disconnect(wxEVT_DEBUG_EVENT, wxDebugEventHandler(CMainFrameDesigner::OnDebugEvent), NULL, this);
	debugClient->RemoveHandler(this);
}

void CMainFrameDesigner::CreateGUI()
{
	InitializeDefaultMenu();
	CreateWideGui();
}

#include "metadata/metadata.h"

static bool setModify = false; 

void CMainFrameDesigner::Modify(bool modify)
{
	wxAuiPaneInfo &paneInfo = m_mgr.GetPane("metadata");

	if (paneInfo.IsOk()) {

		wxString caption = _("Configuration") + ' ';

		if (setModify && modify) {
			caption += '*';
		}

		if (!metadata->IsConfigSave()) {
			caption += wxT("<!>");
		}

		if (caption != paneInfo.caption) {
			paneInfo.Caption(caption);
			m_mgr.Refresh();
		}
	}

	if (!setModify) {
		setModify = true; 
	}
	else if (modify == false) {
		setModify = metadata->IsConfigSave();
	}
}

#include "frontend/metatree/metatreeWnd.h"

void CMainFrameDesigner::LoadOptions()
{
	// Disable logging since it's ok if the options file is not there.
	wxLogNull logNo;
	wxXmlNode* keyBindingNode = NULL;

	wxString directory =
		wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache) + wxT("\\OES");

	// Make sure the directory exists.
	wxFileName fileName(directory, "options.xml");

	wxXmlDocument document;

	if (document.Load(fileName.GetFullPath()))
	{
		wxXmlNode* root = document.GetRoot();

		if (root->GetName() == "options")
		{
			wxXmlNode* node = root->GetChildren();
			while (node != NULL)
			{
				wxString data;

				if (node->GetName() == "editor")
				{
					m_editorSettings.Load(node);
				}
				else if (node->GetName() == "fontcolor")
				{
					m_fontColorSettings.Load(node);
				}
				else if (node->GetName() == "keybindings")
				{
					// Save the node and we'll load when we're done.
					keyBindingNode = node;
				}

				node = node->GetNext();
			}
		}
	}

	m_keyBinder.AddCommandsFromMenuBar(m_menuBar);

	if (keyBindingNode != NULL)
	{
		m_keyBinder.Load(keyBindingNode);
	}
	else
	{
		SetDefaultHotKeys();
	}

	SetMenuBar(m_menuBar);

	m_keyBinder.UpdateWindow(this);
	m_keyBinder.UpdateMenuBar(GetMenuBar());

	UpdateEditorOptions();
}

void CMainFrameDesigner::SaveOptions()
{
	// Disable logging since it's ok if the options file saving isn't successful.
	wxLogNull logNo;

	wxXmlDocument document;

	wxXmlNode* root = new wxXmlNode(wxXML_ELEMENT_NODE, "options");
	document.SetRoot(root);

	// Save the font and color settings.
	root->AddChild(m_fontColorSettings.Save("fontcolor"));

	// Save the editor settins.
	root->AddChild(m_editorSettings.Save("editor"));

	// Save the key bindings.
	root->AddChild(m_keyBinder.Save("keybindings"));

	wxString directory = 
		wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache) + wxT("\\OES");

	// Make sure the directory exists.
	wxFileName fileName(directory, "options.xml");
	fileName.Mkdir(0777, wxPATH_MKDIR_FULL);

	document.Save(fileName.GetFullPath());
}

bool CMainFrameDesigner::Show(bool show)
{
	if (!metatreeWnd->Load()) {
		return false;
	}

	return CMainFrame::Show(show);
}