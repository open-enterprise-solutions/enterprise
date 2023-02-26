#ifndef _TEXT_H__
#define _TEXT_H__

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

#include "frontend/docView/docView.h"
#include "frontend/codeEditor/codeEditorCtrl.h"

#include <wx/fdrepdlg.h>

// The view using a standard wxTextCtrl to show its contents
class CTextEditView : public CView
{
	//Editor setting 
	void SetEditorSettings(const EditorSettings& settings);
	//Font setting 
	void SetFontColorSettings(const FontColorSettings& settings);

public:
	CTextEditView() : CView(), m_textEditor(NULL) {}

	virtual bool OnCreate(CDocument *doc, long flags) override;
	virtual void OnDraw(wxDC *dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	virtual wxPrintout* OnCreatePrintout() override;

	wxStyledTextCtrl *GetText() const { return m_textEditor; }

private:

	void OnCopy(wxCommandEvent& WXUNUSED(event)) { m_textEditor->Copy(); }
	void OnPaste(wxCommandEvent& WXUNUSED(event)) { m_textEditor->Paste(); }
	void OnSelectAll(wxCommandEvent& WXUNUSED(event)) { m_textEditor->SelectAll(); }

	void OnFind(wxFindDialogEvent& event);

	wxStyledTextCtrl * m_textEditor;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(CTextEditView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class CTextDocument : public CDocument
{
public:
	CTextDocument() : CDocument() { m_childDoc = false; }

	virtual bool OnCreate(const wxString& path, long flags) override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	virtual wxStyledTextCtrl* GetTextCtrl() const = 0;

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	void OnTextChange(wxCommandEvent& event);

	wxDECLARE_NO_COPY_CLASS(CTextDocument);
	wxDECLARE_ABSTRACT_CLASS(CTextDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class CTextEditDocument : public CTextDocument
{
public:
	CTextEditDocument() : CTextDocument() { }
	virtual wxStyledTextCtrl* GetTextCtrl() const override;

	wxDECLARE_NO_COPY_CLASS(CTextEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CTextEditDocument);
};

#endif