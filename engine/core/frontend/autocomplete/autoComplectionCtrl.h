#ifndef _AUTOCOMPLETIONCTRL_H__
#define _AUTOCOMPLETIONCTRL_H__

#include <wx/wx.h>
#include <wx/stc/stc.h>
#include <wx/docview.h>
#include <wx/listctrl.h>

#include <vector>
#include <map>

#include "autoComplectionCompile.h"
#include "frontend/settings/editorsettings.h"
#include "frontend/settings/fontcolorsettings.h"
#include "compiler/debugger/debugEvent.h"

#include "frontend/autocomplete/components/autoComplete.h"
#include "frontend/autocomplete/components/callTip.h"

class CDocument;
class CMemoryReader;

class CAutocomplectionCtrl : public wxStyledTextCtrl
{
	CDocument *m_document;

	CAutoComplete ac;
	CCallTip ct;

	friend class CDebuggerClient;
	friend class CAutoComplete;
	friend class CCallTip;
	friend class CAutocomplectionPrint;
	friend class CMetadataTree;

private:

	//Events
	void OnStyleNeeded(wxStyledTextEvent& event);
	void OnMarginClick(wxStyledTextEvent& event);
	void OnTextChange(wxStyledTextEvent& event);

	void OnKeyDown(wxKeyEvent& event);

	void OnDebugEvent(wxDebugEvent& event);
	void OnDebugToolTipEvent(wxDebugToolTipEvent& event);
	void OnDebugAutocompleteEvent(wxDebugAutocompleteEvent& event);

	void OnMouseMove(wxMouseEvent& event);

	//Support styling 
	void HighlightSyntax(unsigned int fromPos, unsigned int toPos, wxString &text);
	//Support debugger 
	void EditDebugPoint(int line);

public:

	//Update breakpoints 
	void UpdateBreakpoints(bool bDeleteCurrentBreakline = false);
	//Editor setting 
	void SetEditorSettings(const EditorSettings& settings);
	//Font setting 
	void SetFontColorSettings(const FontColorSettings& settings);

public:

	virtual void AppendText(const wxString& text) override;
	virtual void Replace(long from, long to, const wxString& text) override;

private:

	CPrecompileModule *m_precompileModule;

	bool m_bInitialized;
	int  m_bIndentationSize;
	bool m_bEnableAutoComplete;

	std::map<wxString, wxString> m_aExpressions;

private:

	void AddKeywordFromObject(const CValue &vObject);

	bool PrepareExpression(unsigned int currPos, wxString &sExpression, wxString &sKeyWord, wxString &sCurrWord, bool &hasPoint);
	void PrepareTooTipExpression(unsigned int currPos, wxString &sExpression, wxString &sCurrWord, bool &hasPoint);

	void PrepareTABs();
	void CalculateFoldLevels();

	void LoadSysKeyword();
	void LoadIntelliList();
	void LoadFromKeyWord(const wxString &keyWord);

	void LoadAutoComplete();
	void LoadToolTip(wxPoint pos);
	void LoadCallTip();

	void ShowAutoCompleteFromDebugger(CMemoryReader &commandReader);

public:

	enum eMarkers
	{
		Breakpoint = 1,
		CurrentLine,
		BreakLine,
	};

	CAutocomplectionCtrl();
	CAutocomplectionCtrl(CDocument *document, wxWindow *parent, wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0,
		const wxString& name = wxSTCNameStr);

	bool LoadModule();
	bool SaveModule();

	int GetRealPosition();
	int GetRealPositionFromPoint(wxPoint pt);

	void ShowCallTip(const wxString &sTitle) { ct.Show(GetRealPosition(), sTitle); }

	// hook the document manager into event handling chain here
	virtual bool TryBefore(wxEvent& event) override
	{
		wxEventType type = event.GetEventType();

		if (type == wxEVT_PAINT ||
			type == wxEVT_NC_PAINT ||
			type == wxEVT_ERASE_BACKGROUND)
		{
			return wxStyledTextCtrl::TryBefore(event);
		}

		if (ct.Active())
		{
			if (type == wxEVT_KEY_DOWN)
			{
				ct.CallEvent(event); return wxStyledTextCtrl::TryBefore(event);
			}
		}

		if (ac.CallEvent(event)) return true;
		if (ct.CallEvent(event)) return true;
		else return wxStyledTextCtrl::TryBefore(event);
	}

	void SetCurrentLine(int line, bool setLine = true);

	~CAutocomplectionCtrl();
};

#endif 