////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include "backend/debugger/debugClient.h"

#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include <wx/xml/xml.h>

#include "docManager/docManager.h"
#include "debugger/debugClientImpl.h"

///////////////////////////////////////////////////////////////////

ibFrontendMainFrameDesigner* ibFrontendMainFrameDesigner::GetFrame() {
	ibFrontendMainFrame* instance = ibFrontendMainFrame::GetFrame();
	if (instance != nullptr) {
		ibFrontendMainFrameDesigner* designer_instance =
			dynamic_cast<ibFrontendMainFrameDesigner*>(instance);
		wxASSERT(designer_instance);
		return designer_instance;
	}
	return nullptr;
}

///////////////////////////////////////////////////////////////////

ibFrontendMainFrameDesigner::ibFrontendMainFrameDesigner(ibSessionHolder&& holder,
	const wxString& title,
	const wxPoint& pos,
	const wxSize& size) : ibFrontendMainFrame(std::move(holder), title, pos, size),

	m_metaWindow(nullptr),

	m_outputWindow(new ibOutputWindow(this, wxID_ANY)),
	m_stackWindow(new ibStackWindow(this, wxID_ANY)),
	m_watchWindow(new ibWatchWindow(this, wxID_ANY)),
	m_localWindow(new ibLocalWindow(this, wxID_ANY))
{
	m_docManager = new ibDocManagerDesigner;
}

ibFrontendMainFrameDesigner::~ibFrontendMainFrameDesigner()
{
	wxDELETE(m_docManager);
}

void ibFrontendMainFrameDesigner::CreateGUI()
{
	CreateWideGui();
}

static bool s_setModify = false, s_modified = false;

void ibFrontendMainFrameDesigner::Modify(bool modify)
{
	wxAuiPaneInfo& paneInfo = m_mgr.GetPane(wxAUI_PANE_METADATA);

	if (paneInfo.IsOk()) {

		wxString caption = _("Configuration") + ' ';

		if (s_setModify && modify) {
			caption += wxT('*'); s_modified = true;
		}
		else {
			s_modified = false;
		}

		if (!activeMetaData->IsConfigSave()) {
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
		s_setModify = activeMetaData->IsConfigSave();
	}
}

bool ibFrontendMainFrameDesigner::IsModified() const
{
	return s_modified;
}

void ibFrontendMainFrameDesigner::LoadOptions()
{
	// Disable logging since it's ok if the options file is not there.
	wxLogNull logNo;
	wxXmlNode* keyBindingNode = nullptr;

	wxString directory =
		wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache) + wxT("\\OES");

	// Make sure the directory exists.
	wxFileName strFileName(directory, "options.xml");

	wxXmlDocument document;

	if (document.Load(strFileName.GetFullPath())) {
		wxXmlNode* root = document.GetRoot();
		if (root->GetName() == "options") {
			wxXmlNode* node = root->GetChildren();
			while (node != nullptr) {
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

	wxMenuBar* mb = GetMenuBar();
	m_frameMenuBar = nullptr;
	SetMenuBar(mb);

	m_keyBinder.AddCommandsFromMenuBar(mb);

	if (keyBindingNode != nullptr) {
		m_keyBinder.Load(keyBindingNode);
	}
	else {
		SetDefaultHotKeys();
	}

	m_keyBinder.UpdateWindow(this);
	m_keyBinder.UpdateMenuBar(mb);

	UpdateEditorOptions();
}

void ibFrontendMainFrameDesigner::SaveOptions()
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
	wxFileName strFileName(directory, "options.xml");
	strFileName.Mkdir(0777, wxPATH_MKDIR_FULL);

	document.Save(strFileName.GetFullPath());
}

#pragma region debugger 
void ibFrontendMainFrameDesigner::Debugger_OnSessionStart()
{
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STEP_INTO, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STEP_OVER, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STOP_DEBUGGING, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STOP_PROGRAM, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, false);
}

void ibFrontendMainFrameDesigner::Debugger_OnSessionEnd()
{
	if (!debugClient->HasConnections()) {
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STEP_INTO, false);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STEP_OVER, false);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, false);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STOP_DEBUGGING, false);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STOP_PROGRAM, false);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, false);
	}
}

void ibFrontendMainFrameDesigner::Debugger_OnEnterLoop()
{
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, false);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, true);
}

void ibFrontendMainFrameDesigner::Debugger_OnLeaveLoop()
{
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, false);
}
#pragma endregion 

bool ibFrontendMainFrameDesigner::AllowRun()
{
	// The Designer's "may I come up?" is its metadata tree: load it, and
	// refuse the show if it cannot be loaded. Runs from the base Show
	// AFTER CreateGUI, so m_metaWindow and the panes around it exist —
	// this used to sit at the top of Show(), before anything was built.
	return m_metaWindow != nullptr && m_metaWindow->Load();
}

bool ibFrontendMainFrameDesigner::Show(bool show)
{
	bool ret = ibFrontendMainFrame::Show(show);
	if (ret) {
		if (!outputWindow->IsEmpty()) {
			outputWindow->SetFocus();
		}
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////

#include "backend/metadataConfiguration.h"

bool ibFrontendMainFrameDesigner::AllowClose()
{
	// The Designer has no session runtime and therefore no open/close
	// script events at all — AllowRun stays at the inherited yes. Its
	// only question is its own: an unsaved configuration.
	if (!ibFrontendMainFrame::AllowClose())
		return false;

	if (activeMetaData != nullptr && IsModified()) {
		const int answer = wxMessageBox(wxString::Format(_("Configuration '%s' has been changed. Save?"), activeMetaData->GetConfigName()),
			wxTheApp->GetAppDisplayName(), wxYES | wxNO | wxCANCEL | wxCENTRE | wxICON_QUESTION, (wxWindow*)this);
		if (answer == wxYES)
			return activeMetaData->SaveDatabase();
		if (answer == wxCANCEL)
			return false;
	}
	return true;
}