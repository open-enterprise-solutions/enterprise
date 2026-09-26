#ifndef __CODE_RUNNER_H__
#define __CODE_RUNNER_H__

#include <wx/intl.h>
#include <wx/string.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/statusbr.h>
#include <wx/frame.h>
#include <wx/stc/stc.h>
#include <wx/toolbar.h>
#include <wx/menu.h>

///////////////////////////////////////////////////////////////////////////

#include "backend/backend_mainFrame.h"
#include "frontend/win/editor/codeEditor/codeEditor.h"

///////////////////////////////////////////////////////////////////////////////
/// Class ibFrameCodeRunner
///////////////////////////////////////////////////////////////////////////////

// Sessionless / metadata-less script runner — uses frontend's
// ibCodeEditor for syntax-highlight + fold + auto-indent + format/indent
// commands without any document or debug client. Only the Run /
// SyntaxCheck / Clear buttons live here; everything editor-related is
// inherited.
class ibFrameCodeRunner : public ibBackendDocFrame, public wxFrame {
protected:

	class ibCompileCode* m_compileCode;
	class ibProcUnit* m_procUnit;

	ibCodeEditor*     m_codeEditor;
	wxChoice*         m_syntaxChoice;
	wxButton*         m_buttonSyntaxCheck;
	wxButton*         m_buttonRunCode;
	wxButton*         m_buttonClearOutput;
	wxStyledTextCtrl* m_output;
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

	virtual wxFrame* GetFrameHandler() const { return const_cast<ibFrameCodeRunner*>(this); }
	virtual class ibMetaData* FindMetadataByPath(const wxString& strFileName) const { return nullptr; }

	virtual void SetTitle(const wxString& title) {
		wxFrame::SetTitle(title);
	}

	virtual void SetStatusText(const wxString& text, int number = 0) {
		wxFrame::SetStatusText(text, number);
	};

	virtual void Message(const wxString& strMessage, ibStatusMessage status) {
		AppendOutput(strMessage);
	};

	virtual void ClearMessage() {
		m_output->SetReadOnly(false);
		m_output->ClearAll();
		m_output->SetReadOnly(true);
	};

	virtual void RaiseFrame() { wxFrame::Raise(); }
	virtual void RefreshFrame() { wxFrame::Refresh(); }

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
