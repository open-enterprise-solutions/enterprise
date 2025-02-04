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

CDocDesignerMDIFrame* CDocDesignerMDIFrame::GetFrame() {
	CDocMDIFrame* instance = CDocMDIFrame::GetFrame();
	if (instance != nullptr) {
		CDocDesignerMDIFrame* designer_instance =
			dynamic_cast<CDocDesignerMDIFrame*>(instance);
		wxASSERT(designer_instance);
		return designer_instance;
	}
	return nullptr;
}

///////////////////////////////////////////////////////////////////

CDocDesignerMDIFrame::CDocDesignerMDIFrame(const wxString& title,
	const wxPoint& pos,
	const wxSize& size) : CDocMDIFrame(title, pos, size),
	m_metadataTree(nullptr),
	m_outputWindow(nullptr), m_stackWindow(nullptr), m_watchWindow(nullptr),
	m_menuBar(nullptr)
{
	m_docManager = new CDesignerDocManager;
}

CDocDesignerMDIFrame::~CDocDesignerMDIFrame()
{
	wxDELETE(m_docManager);
}

void CDocDesignerMDIFrame::CreateGUI()
{
	CreateWideGui();
}

static bool s_setModify = false, s_modified = false;

void CDocDesignerMDIFrame::Modify(bool modify)
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

		if (!commonMetaData->IsConfigSave()) {
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
		s_setModify = commonMetaData->IsConfigSave();
	}
}

bool CDocDesignerMDIFrame::IsModified() const
{
	return s_modified;
}

void CDocDesignerMDIFrame::LoadOptions()
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

	m_keyBinder.AddCommandsFromMenuBar(m_menuBar);

	if (keyBindingNode != nullptr) {
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

void CDocDesignerMDIFrame::SaveOptions()
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
void CDocDesignerMDIFrame::Debugger_OnSessionStart()
{
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STEP_INTO, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STEP_OVER, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STOP_DEBUGGING, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STOP_PROGRAM, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, false);
}

void CDocDesignerMDIFrame::Debugger_OnSessionEnd()
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

void CDocDesignerMDIFrame::Debugger_OnEnterLoop()
{
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, false);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, true);
}

void CDocDesignerMDIFrame::Debugger_OnLeaveLoop()
{
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, false);
}
#pragma endregion 

bool CDocDesignerMDIFrame::Show(bool show)
{
	if (show && !m_metadataTree->Load())
		return false;

	bool ret = CDocMDIFrame::Show(show);
	if (ret) {
		if (!outputWindow->IsEmpty()) {
			outputWindow->SetFocus();
		}
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////

void CDocDesignerMDIFrame::OnInitializeConfiguration(eConfigType cfg)
{
	::SetDebuggerClientBridge(new CDebuggerClientBridge);
}

void CDocDesignerMDIFrame::OnDestroyConfiguration(eConfigType cfg)
{
	::SetDebuggerClientBridge(nullptr);
}

////////////////////////////////////////////////////////////////////////////////////////////////

#include "backend/metadataConfiguration.h"

bool CDocDesignerMDIFrame::AllowRun() const
{
	return commonMetaData->StartMainModule();
}

bool CDocDesignerMDIFrame::AllowClose() const
{
	bool allowClose = true;
	if (IsModified()) {
		const int answer = wxMessageBox("Configuration '" + commonMetaData->GetConfigName() + "' has been changed. Save?", wxT("Save project"), wxYES | wxNO | wxCANCEL | wxCENTRE | wxICON_QUESTION, (wxWindow*)this);
		if (answer == wxYES) {
			allowClose = commonMetaData->SaveConfiguration();
		}
		else if (answer == wxCANCEL) {
			allowClose = false;
		}
		else {
			allowClose = true;
		}
	}
	return allowClose && commonMetaData->ExitMainModule();
}