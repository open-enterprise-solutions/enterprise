////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include "frontend/help/helpPaneView.h"
#include "backend/debugger/debugClient.h"

#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include <wx/xml/xml.h>

#include "docManager/docManager.h"
#include "debugger/debugClientImpl.h"

///////////////////////////////////////////////////////////////////

ibFrontendDocMDIFrameDesigner* ibFrontendDocMDIFrameDesigner::GetFrame() {
	ibFrontendDocMDIFrame* instance = ibFrontendDocMDIFrame::GetFrame();
	if (instance != nullptr) {
		ibFrontendDocMDIFrameDesigner* designer_instance =
			dynamic_cast<ibFrontendDocMDIFrameDesigner*>(instance);
		wxASSERT(designer_instance);
		return designer_instance;
	}
	return nullptr;
}

///////////////////////////////////////////////////////////////////

ibFrontendDocMDIFrameDesigner::ibFrontendDocMDIFrameDesigner(const wxString& title,
	const wxPoint& pos,
	const wxSize& size) : ibFrontendDocMDIFrame(title, pos, size),

	m_metaWindow(nullptr),

	m_outputWindow(new ibOutputWindow(this, wxID_ANY)),
	m_localWindow(new ibLocalWindow(this, wxID_ANY)),
	m_stackWindow(new ibStackWindow(this, wxID_ANY)),
	m_watchWindow(new ibWatchWindow(this, wxID_ANY))
{
	m_docManager = new ibMetaDocManagerDesigner;
}

ibFrontendDocMDIFrameDesigner::~ibFrontendDocMDIFrameDesigner()
{
	wxDELETE(m_docManager);
}

void ibFrontendDocMDIFrameDesigner::CreateGUI()
{
	CreateWideGui();
}

static bool s_setModify = false, s_modified = false;

void ibFrontendDocMDIFrameDesigner::Modify(bool modify)
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

bool ibFrontendDocMDIFrameDesigner::IsModified() const
{
	return s_modified;
}

void ibFrontendDocMDIFrameDesigner::LoadOptions()
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
			// Help pane state — extract immediately into the
			// pending struct so the values survive after `document`
			// goes out of scope. Applied lazily inside EnsureHelpPane.
			for (wxXmlNode* h = root->GetChildren(); h; h = h->GetNext()) {
				if (h->GetName() != wxT("helpPane")) continue;
				m_pendingHelpState.has = true;
				m_pendingHelpState.currentId = h->GetAttribute(wxT("currentId"), wxEmptyString);
				h->GetAttribute(wxT("tab"),       wxEmptyString).ToLong(&m_pendingHelpState.tab);
				h->GetAttribute(wxT("fontBoost"), wxEmptyString).ToLong(&m_pendingHelpState.fontBoost);
				break;
			}
			// Plugin WebView pane visibility — multi-paneId map. Applied
			// later inside WirePluginWebPaneCallbacks at RegisterWebPane
			// time because the plugin that owns each pane hasn't loaded
			// yet at this point. visible="1" → AUI Show(true) on registration.
			for (wxXmlNode* s = root->GetChildren(); s; s = s->GetNext()) {
				if (s->GetName() != wxT("pluginWebPanes")) continue;
				for (wxXmlNode* p = s->GetChildren(); p; p = p->GetNext()) {
					if (p->GetName() != wxT("pane")) continue;
					const wxString id = p->GetAttribute(wxT("id"), wxEmptyString);
					if (id.IsEmpty()) continue;
					m_pendingPluginWebPaneVisible[id] =
						p->GetAttribute(wxT("visible"), wxT("0")) == wxT("1");
				}
				break;
			}
		}
	}

	wxMenuBar* mb = GetMenuBar();
	m_frameMenuBar = nullptr;
	SetMenuBar(mb);

	m_keyBinder.AddCommandsFromMenuBar(mb);

	// Always start from defaults so command ids added after the user's
	// options.xml was written still receive their shipped shortcuts.
	// Load() then overlays any user-customised bindings on top — its
	// callees overwrite the keys vector on a per-command basis, so
	// untouched ids keep their defaults. Without this, every newly
	// added shortcut (Ctrl+F1 / RawCtrl+F1 / etc.) silently fails to
	// install on existing installations.
	SetDefaultHotKeys();
	if (keyBindingNode != nullptr) {
		m_keyBinder.Load(keyBindingNode);
	}

	m_keyBinder.UpdateWindow(this);
	m_keyBinder.UpdateMenuBar(mb);

	UpdateEditorOptions();
}

void ibFrontendDocMDIFrameDesigner::SaveOptions()
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

	// Save help-pane state (last entry / tab / font boost). No-op when
	// the pane was never opened in this session.
	if (m_helpPane)
		m_helpPane->SaveStateToXml(root);

	// Save plugin WebView pane visibility per paneId. Walks the live AUI
	// pane registry (m_pluginWebPaneIds is the set we know about) rather
	// than caching pointers — a pane the user closed mid-session is gone
	// from the AUI manager and gets visible="0" written. Pre-loaded
	// entries that were never re-registered this session are preserved
	// verbatim so a plugin temporarily uninstalled doesn't lose its
	// remembered visibility.
	{
		std::unordered_map<wxString, bool> combined = m_pendingPluginWebPaneVisible;
		for (const wxString& paneId : m_pluginWebPaneOrder) {
			wxAuiPaneInfo& info = m_mgr.GetPane(paneId);
			combined[paneId] = info.IsOk() && info.IsShown();
		}
		if (!combined.empty()) {
			wxXmlNode* sp = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("pluginWebPanes"));
			for (const auto& kv : combined) {
				wxXmlNode* p = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("pane"));
				p->AddAttribute(wxT("id"),      kv.first);
				p->AddAttribute(wxT("visible"), kv.second ? wxT("1") : wxT("0"));
				sp->AddChild(p);
			}
			root->AddChild(sp);
		}
	}

	wxString directory =
		wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache) + wxT("\\OES");

	// Make sure the directory exists.
	wxFileName strFileName(directory, "options.xml");
	strFileName.Mkdir(0777, wxPATH_MKDIR_FULL);

	document.Save(strFileName.GetFullPath());
}

#pragma region debugger 
void ibFrontendDocMDIFrameDesigner::Debugger_OnSessionStart()
{
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STEP_INTO, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STEP_OVER, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STOP_DEBUGGING, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STOP_PROGRAM, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, false);
}

void ibFrontendDocMDIFrameDesigner::Debugger_OnSessionEnd()
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

void ibFrontendDocMDIFrameDesigner::Debugger_OnEnterLoop()
{
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, false);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, true);
}

void ibFrontendDocMDIFrameDesigner::Debugger_OnLeaveLoop()
{
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, true);
	m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, false);
}
#pragma endregion 

bool ibFrontendDocMDIFrameDesigner::Show(bool show)
{
	if (show && !m_metaWindow->Load())
		return false;

	bool ret = ibFrontendDocMDIFrame::Show(show);
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

bool ibFrontendDocMDIFrameDesigner::AllowRun() const
{
	// Designer is compile-only — no session runtime, no BeforeStart /
	// OnStart script events. Always allow frame show.
	return true;
}

bool ibFrontendDocMDIFrameDesigner::AllowClose() const
{
	// Unsaved-config confirmation is a designer-only concern; no
	// BeforeExit / OnExit script events (no runtime to fire them on).
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