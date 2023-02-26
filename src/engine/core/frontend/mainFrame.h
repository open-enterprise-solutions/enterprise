#ifndef _MAINFRAME_H__
#define _MAINFRAME_H__

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <wx/docview.h>
#include <wx/stc/stc.h>

class CMetadataTree;

#include "appData.h"

class CView;

#include "settings/keybinder.h"
#include "settings/fontcolorsettings.h"
#include "settings/editorsettings.h"

#define mainFrame            	(wxAuiDocMDIFrame::GetFrame())
#define mainFrameCreate(mode)   (wxAuiDocMDIFrame::InitializeFrame(mode))
#define mainFrameDestroy()  	(wxAuiDocMDIFrame::DestroyFrame())

#include "objinspect/objinspect.h"

#include "watch/watchWindow.h"
#include "output/outputWindow.h"
#include "stack/stackWindow.h"

#include "mainFrameDefs.h"

//********************************************************************************
//*                                 ID's                                         *
//********************************************************************************

#define wxCREATE_SDI_FRAME 0x16000

class CORE_API wxAuiDocMDIFrame : public wxAuiMDIParentFrame,
	public wxDocParentFrameAnyBase
{
	static wxAuiDocMDIFrame* s_instance;

protected:

	class CFrameManager : public wxAuiManager {
	public:
		CFrameManager(wxWindow* managedWnd = NULL,
			unsigned int flags = wxAUI_MGR_DEFAULT) :
			wxAuiManager(managedWnd, flags) {
		}

		void Refresh() { Repaint(); }
	};

	// Createf rame manager 
	CFrameManager m_mgr;

	KeyBinder             m_keyBinder;
	FontColorSettings     m_fontColorSettings;
	EditorSettings        m_editorSettings;

	wxAuiToolBar* m_mainFrameToolbar;
	wxAuiToolBar* m_docToolbar;

protected:

	CObjectInspector* m_objectInspector;

protected:

	virtual void CreatePropertyPane();

public:

	/**
	* Show property in mainFrame
	*/
	void ShowProperty();

public:

	static CObjectInspector* GetObjectInspector() {
		if (s_instance != NULL)
			return s_instance->m_objectInspector;
		return NULL;
	}

	static wxAuiDocMDIFrame* GetFrame() {
		return s_instance;
	}

	// Force the static appData instance to Init()
	static void InitializeFrame(enum eRunMode mode);
	static void DestroyFrame();

public:

	static wxWindow* CreateChildFrame(CView* view, const wxPoint& pos, const wxSize& size, long style = wxDEFAULT_FRAME_STYLE);

	KeyBinder             GetKeyBinder() const {
		return m_keyBinder;
	}

	FontColorSettings     GetFontColorSettings() const {
		return m_fontColorSettings;
	}

	EditorSettings        GetEditorSettings() const {
		return m_editorSettings;
	}

	virtual wxMenu* GetDefaultMenu(int idMenu) const {
		return NULL;
	}

	virtual COutputWindow* GetOutputWindow() const = 0;
	virtual CStackWindow* GetStackWindow() const = 0;
	virtual CWatchWindow* GetWatchWindow() const = 0;

	virtual CMetadataTree* GetMetadataTree() const {
		return NULL;
	}

	virtual void CreateGUI() = 0;

	virtual void Modify(bool modify) {}
	virtual bool IsModified() const {
		return false;
	}

	virtual wxAuiToolBar* GetMainFrameToolbar() const {
		return m_mainFrameToolbar;
	}

	virtual wxAuiToolBar* GetDocToolbar() const {
		return m_docToolbar;
	}

	void OnActivateView(bool activate, wxView* activeView, wxView* deactiveView);

	// bring window to front
	virtual void Raise() override;

protected:

	// hook the document manager into event handling chain here
	virtual bool TryBefore(wxEvent& event) override {
		// It is important to send the event to the base class first as
		// wxMDIParentFrame overrides its TryBefore() to send the menu events
		// to the currently active child valueForm and the child must get them
		// before our own TryProcessEvent() is executed, not afterwards.
		return wxAuiMDIParentFrame::TryBefore(event) || TryProcessEvent(event);
	}

protected:

	virtual bool AllowClose() const {
		return true;
	}

protected:

	wxAuiDocMDIFrame(const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE,
		const wxString& name = wxASCII_STR(wxFrameNameStr));

	bool Create(const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE,
		const wxString& name = wxASCII_STR(wxFrameNameStr));

public:

	virtual ~wxAuiDocMDIFrame();

	//Events 
	void OnExit(wxCommandEvent& WXUNUSED(event));
	void OnCloseWindow(wxCloseEvent& event);
};

#endif 