#pragma once

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/intl.h>
#include <wx/stc/stc.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/button.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/sizer.h>
#include <wx/statusbr.h>
#include <wx/frame.h>

///////////////////////////////////////////////////////////////////////////

#include "backend/backend_mainFrame.h"

///////////////////////////////////////////////////////////////////////////////
/// Class CFrameCodeRunner
///////////////////////////////////////////////////////////////////////////////

class CFrameCodeRunner : public IBackendDocMDIFrame, public wxFrame {
	void PrepareTABs();
	void HighlightSyntaxAndCalculateFoldLevel(
		const int fromLine, const int toLine,
		const int fromPos, const int toPos,
		const wxString& strCode
	);
	void SetFontColorSettings();
protected:

	class CCompileCode* m_compileCode;
	class CProcUnit* m_procUnit;

	wxStyledTextCtrl* m_codeEditor;
	wxButton* m_buttonSyntaxCheck;
	wxButton* m_buttonRunCode;
	wxButton* m_buttonClearOutput;
	wxStyledTextCtrl* m_output;
	wxStatusBar* m_statusBar;

	// Virtual event handlers, override them in your derived class
	void SyntaxCheckOnButtonClick(wxCommandEvent& event);
	void RunCodeOnButtonClick(wxCommandEvent& event);
	void ClearOutputOnButtonClick(wxCommandEvent& event);
	void OnKeyDown(wxKeyEvent& event);

	void OnStyleNeeded(wxStyledTextEvent& event);
	void OnMarginClick(wxStyledTextEvent& event);
	void OnChagedText(wxStyledTextEvent& event);
	void OnNeedShow(wxStyledTextEvent& event);

public:

	virtual wxFrame* GetFrameHandler() const { return const_cast<CFrameCodeRunner*>(this); }
	virtual class IMetaData* FindMetadataByPath(const wxString& strFileName) const { return nullptr; }

	virtual void SetTitle(const wxString& title) {
		wxFrame::SetTitle(title);
	}

	virtual void SetStatusText(const wxString& text, int number = 0) {
		wxFrame::SetStatusText(text, number);
	};

	virtual void Message(const wxString& strMessage, eStatusMessage status) {
		AppendOutput(strMessage);
	};

	virtual void ClearMessage() {
		m_output->SetReadOnly(false);
		m_output->ClearAll();
		m_output->SetReadOnly(true);
	};

	virtual void RefreshFrame() {
		wxFrame::Refresh();
	};

	void AppendOutput(const wxString& str) const {
		m_output->SetReadOnly(false);
		m_output->AppendText(str + "\n");
		m_output->SetReadOnly(true);
	}

	CFrameCodeRunner(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = "Code runner", const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(638, 338), long style = wxDEFAULT_FRAME_STYLE | wxTAB_TRAVERSAL);
	virtual ~CFrameCodeRunner();
};

