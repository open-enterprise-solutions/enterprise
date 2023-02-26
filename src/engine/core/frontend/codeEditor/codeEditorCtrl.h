#ifndef _AUTOCOMPLETIONCTRL_H__
#define _AUTOCOMPLETIONCTRL_H__

#include <wx/docview.h>
#include <wx/cmdproc.h>
#include <wx/listctrl.h>
#include <wx/stc/stc.h>

#include <vector>
#include <map>

#include "codeEditorInterpreter.h"
#include "frontend/settings/editorsettings.h"
#include "frontend/settings/fontcolorsettings.h"
#include "core/compiler/debugger/debugEvent.h"

#include "frontend/codeEditor/components/autoComplete.h"
#include "frontend/codeEditor/components/callTip.h"

class CDocument;
class CMemoryReader;

class CModuleCommandProcessor : public wxCommandProcessor {
	wxStyledTextCtrl* m_codeEditor;
public:

	CModuleCommandProcessor(wxStyledTextCtrl* codeEditor) :
		wxCommandProcessor(), m_codeEditor(codeEditor) {}

	virtual bool Undo() override {
		m_codeEditor->Undo();
		return true;
	}

	virtual bool Redo() override {
		m_codeEditor->Redo();
		return true;
	}

	virtual bool CanUndo() const override {
		return m_codeEditor->CanUndo();
	}

	virtual bool CanRedo() const override {
		return m_codeEditor->CanRedo();
	}
};

class CCodeEditorCtrl : public wxStyledTextCtrl
{
	CAutoComplete ac;
	CCallTip ct;

	CDocument* m_document;

	friend class CDebuggerClient;
	friend class CAutoComplete;
	friend class CCallTip;
	friend class CCodeEditorPrintout;
	friend class CMetadataTree;

private:

	//Events
	void OnStyleNeeded(wxStyledTextEvent& event);
	void OnMarginClick(wxStyledTextEvent& event);
	void OnTextChange(wxStyledTextEvent& event);

	void OnKeyDown(wxKeyEvent& event);
	void OnDebugEvent(wxDebugEvent& event);

	void OnMouseMove(wxMouseEvent& event);

	//Support styling 
	void HighlightSyntax(unsigned int fromPos, unsigned int toPos, const wxString& text);

	//Support debugger 
	void EditDebugPoint(int line);

public:

	//Update breakpoints 
	void RefreshBreakpoint(bool bDeleteCurrentBreakline = false);
	//Editor setting 
	void SetEditorSettings(const EditorSettings& settings);
	//Font setting 
	void SetFontColorSettings(const FontColorSettings& settings);

public:

	virtual void AppendText(const wxString& text) override;
	virtual void Replace(long from, long to, const wxString& text) override;

private:

	CPrecompileModule* m_precompileModule;

	bool m_bInitialized;
	int  m_bIndentationSize;
	bool m_bEnableAutoComplete;

	std::map<wxString, wxString> m_expressions;

private:

	void AddKeywordFromObject(const CValue& vObject);

	bool PrepareExpression(unsigned int currPos, wxString& sExpression, wxString& sKeyWord, wxString& sCurrWord, bool& hasPoint);
	void PrepareTooTipExpression(unsigned int currPos, wxString& sExpression, wxString& sCurrWord, bool& hasPoint);

	void PrepareTABs();
	void CalculateFoldLevels();

	void LoadSysKeyword();
	void LoadIntelliList();
	void LoadFromKeyWord(const wxString& keyWord);

	void LoadAutoComplete();
	void LoadToolTip(const wxPoint& pos);
	void LoadCallTip();

	void ShowAutoComp(const debugAutoCompleteData_t& autoCompleteData);

public:

	enum eMarkers {
		Breakpoint = 1,
		CurrentLine,
		BreakLine,
	};

	CCodeEditorCtrl();
	CCodeEditorCtrl(CDocument* document, wxWindow* parent, wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0,
		const wxString& name = wxSTCNameStr);
	virtual ~CCodeEditorCtrl();

	bool LoadModule();
	bool SaveModule();

	int GetRealPosition();
	int GetRealPositionFromPoint(const wxPoint& pt);

	void ShowCallTip(const wxString& title) {
		ct.Show(GetRealPosition(), title);
	}

	void RefreshEditor();

	void FindText(const wxString& findString, int wxflags);

	void ShowGotoLine();
	void ShowMethods();

	bool SyntaxControl(bool throwMessage = true) const;

	// hook the document manager into event handling chain here
	virtual bool TryBefore(wxEvent& event) override {
		wxEventType type = event.GetEventType();
		if (type == wxEVT_PAINT ||
			type == wxEVT_NC_PAINT ||
			type == wxEVT_ERASE_BACKGROUND) {
			return wxStyledTextCtrl::TryBefore(event);
		}
		if (ct.Active()) {
			if (type == wxEVT_KEY_DOWN) {
				ct.CallEvent(event); return wxStyledTextCtrl::TryBefore(event);
			}
		}

		if (ac.CallEvent(event))
			return true;
		if (ct.CallEvent(event))
			return true;
		else return wxStyledTextCtrl::TryBefore(event);
	}

	void SetCurrentLine(int line, bool setLine = true);
};

#endif 