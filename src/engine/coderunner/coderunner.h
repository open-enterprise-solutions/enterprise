#ifndef __CODE_RUNNER_H__
#define __CODE_RUNNER_H__

#include <memory>
#include <vector>

#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/statusbr.h>
#include "deskgui/univ/frame.h"   // ibFrame and the menus it takes (ibMenuBar, ibMenu)
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/ctrl/choice.h"
#include "deskgui/univ/stc/stc.h"

///////////////////////////////////////////////////////////////////////////

#include "backend/backend_mainFrame.h"
#include "coderunner/win/editor/codeEditor/codeEditor.h"

///////////////////////////////////////////////////////////////////////////////
/// Class ibFrameCodeRunner
///////////////////////////////////////////////////////////////////////////////

// Sessionless / metadata-less script runner — uses its own copy of the
// old front's ibCodeEditor for syntax-highlight + fold + auto-indent + format/indent
// commands without any document or debug client. Only the Run /
// SyntaxCheck / Clear buttons live here; everything editor-related is
// inherited.
//
// The frame is the desktop's own (ibFrame), as the designer's and the application's: the theme's title bar and menubar
// round the window - a wxFrame stood with the system's caption and menu (2026-11-11, Max: "coderunner - onto our frame").
class ibFrameCodeRunner : public ibBackendDocFrame, public ibFrame {
protected:

	class ibCompileCode* m_compileCode;
	std::shared_ptr<class ibProcUnit> m_procUnit;

	// the context types' objects (EnumManager, ValueOutput, …) made for this runner — its compile module only
	// references them
	std::vector<class ibValue> m_listContextManager;

	ibCodeEditor*     m_codeEditor;
	ibChoice*         m_syntaxChoice;
	ibButton*         m_buttonSyntaxCheck;
	ibButton*         m_buttonRunCode;
	ibButton*         m_buttonClearOutput;
	ibStyledTextCtrl* m_output;
	wxStatusBar*      m_statusBar;

	void SyntaxCheckOnButtonClick(wxCommandEvent& event);
	void RunCodeOnButtonClick(wxCommandEvent& event);
	void ClearOutputOnButtonClick(wxCommandEvent& event);
	void SyntaxChoiceOnChange(wxCommandEvent& event);

	void OnToolbarCommand(wxCommandEvent& event);

	void OnMenuOpen(wxCommandEvent& event);
	void OnMenuSave(wxCommandEvent& event);
	void OnMenuSaveAs(wxCommandEvent& event);
	void OnMenuExit(wxCommandEvent& event);
	void OnMenuAbout(wxCommandEvent& event);
	void OnMenuEdit(wxCommandEvent& event);

	wxString m_currentFile;

public:

	virtual void SetTitle(const ibString& title) override {
		ibFrame::SetTitle(title);
	}

	virtual void SetStatusText(const ibString& text, int number = 0) override {
		ibFrame::SetStatusText(text, number);
	};

	virtual void Message(const ibString& strMessage, ibStatusMessage status) override {
		AppendOutput(strMessage);
	};

	virtual void ClearMessage() override {
		m_output->SetReadOnly(false);
		m_output->ClearAll();
		m_output->SetReadOnly(true);
	};

	virtual void RaiseFrame() override { ibFrame::Raise(); }
	virtual void RefreshFrame() override { ibFrame::Refresh(); }

	void AppendOutput(const wxString& str) const {
		m_output->SetReadOnly(false);
		m_output->AppendText(str + "\n");
		m_output->SetReadOnly(true);
	}

	ibFrameCodeRunner(wxWindow* parent, wxWindowID id = wxID_ANY,
		const wxString& title = _("Code runner"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxSize(1100, 750),
		long style = wxDEFAULT_FRAME_STYLE | wxTAB_TRAVERSAL);
	virtual ~ibFrameCodeRunner();
};

#endif
