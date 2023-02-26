////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include "core/compiler/debugger/debugClient.h"

#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include <wx/xml/xml.h>

wxAuiDocDesignerMDIFrame::wxAuiDocDesignerMDIFrame(const wxString& title,
	const wxPoint& pos,
	const wxSize& size) : wxAuiDocMDIFrame(title, pos, size),
	m_metadataTree(NULL),
	m_outputWindow(NULL), m_stackWindow(NULL), m_watchWindow(NULL),
	m_menuBar(NULL)
{
	Connect(wxEVT_DEBUG_EVENT, wxDebugEventHandler(wxAuiDocDesignerMDIFrame::OnDebugEvent), NULL, this);
	debugClient->AddHandler(this);
}

wxAuiDocDesignerMDIFrame::~wxAuiDocDesignerMDIFrame()
{
	Disconnect(wxEVT_DEBUG_EVENT, wxDebugEventHandler(wxAuiDocDesignerMDIFrame::OnDebugEvent), NULL, this);
	debugClient->RemoveHandler(this);
}

void wxAuiDocDesignerMDIFrame::CreateGUI()
{
	CreateWideGui();
}

#include "core/metadata/metadata.h"
#include "core/frontend/metatree/metatreeWnd.h"

bool wxAuiDocDesignerMDIFrame::AllowClose() const
{
	bool allowClose = true;
	if (IsModified()) {
		int answer = wxMessageBox("Configuration '" + metadata->GetMetadataName() + "' has been changed. Save?", wxT("Save project"), wxYES | wxNO | wxCANCEL | wxCENTRE | wxICON_QUESTION, (wxWindow*)this);
		if (answer == wxYES) {
			allowClose = metadata->SaveMetadata();
		}
		else if (answer == wxCANCEL) {
			allowClose = false;
		}
		else {
			allowClose = true;
		}
	}
	return allowClose;
}

static bool s_setModify = false, s_modified = false;

void wxAuiDocDesignerMDIFrame::Modify(bool modify)
{
	wxAuiPaneInfo& paneInfo = m_mgr.GetPane(wxAUI_PANE_METADATA);

	if (paneInfo.IsOk()) {

		wxString caption = _("Configuration") + ' ';

		if (s_setModify && modify) {
			caption += '*'; s_modified = true; 
		}
		else {
			s_modified = false; 
		}

		if (!metadata->IsConfigSave()) {
			caption += wxT("<!>");
		}

		if (caption != paneInfo.caption) {
			paneInfo.Caption(caption);
			m_mgr.Refresh();
		}
	}

	if (!s_setModify) {
		s_setModify = true;
	}
	else if (modify == false) {
		s_setModify = metadata->IsConfigSave();
	}
}

bool wxAuiDocDesignerMDIFrame::IsModified() const
{
	return s_modified;
}

void wxAuiDocDesignerMDIFrame::LoadOptions()
{
	// Disable logging since it's ok if the options file is not there.
	wxLogNull logNo;
	wxXmlNode* keyBindingNode = NULL;

	wxString directory =
		wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache) + wxT("\\OES");

	// Make sure the directory exists.
	wxFileName fileName(directory, "options.xml");

	wxXmlDocument document;

	if (document.Load(fileName.GetFullPath())) {
		wxXmlNode* root = document.GetRoot();
		if (root->GetName() == "options") {
			wxXmlNode* node = root->GetChildren();
			while (node != NULL) {
				wxString data;
				if (node->GetName() == "editor") {
					m_editorSettings.Load(node);
				}
				else if (node->GetName() == "fontcolor") {
					m_fontColorSettings.Load(node);
				}
				else if (node->GetName() == "keybindings") {
					// Save the node and we'll load when we're done.
					keyBindingNode = node;
				}
				node = node->GetNext();
			}
		}
	}

	m_keyBinder.AddCommandsFromMenuBar(m_menuBar);

	if (keyBindingNode != NULL) {
		m_keyBinder.Load(keyBindingNode);
	}
	else {
		SetDefaultHotKeys();
	}

	SetMenuBar(m_menuBar);

	m_keyBinder.UpdateWindow(this);
	m_keyBinder.UpdateMenuBar(GetMenuBar());

	UpdateEditorOptions();
}

void wxAuiDocDesignerMDIFrame::SaveOptions()
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

bool wxAuiDocDesignerMDIFrame::Show(bool show)
{
	if (!m_metadataTree->Load())
		return false;

	return wxAuiDocMDIFrame::Show(show);
}