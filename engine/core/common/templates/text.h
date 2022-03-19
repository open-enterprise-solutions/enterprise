#ifndef _TEXT_H__
#define _TEXT_H__

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

#include "common/docInfo.h"
#include "frontend/codeEditor/codeEditorCtrl.h"

// The view using a standard wxTextCtrl to show its contents
class CTextEditView : public CView
{
	//Editor setting 
	void SetEditorSettings(const EditorSettings& settings);
	//Font setting 
	void SetFontColorSettings(const FontColorSettings& settings);

public:
	CTextEditView() : CView(), m_text(NULL) {}

	virtual bool OnCreate(CDocument *doc, long flags) override;
	virtual void OnDraw(wxDC *dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	wxStyledTextCtrl *GetText() const { return m_text; }

private:

	void OnCopy(wxCommandEvent& WXUNUSED(event)) { m_text->Copy(); }
	void OnPaste(wxCommandEvent& WXUNUSED(event)) { m_text->Paste(); }
	void OnSelectAll(wxCommandEvent& WXUNUSED(event)) { m_text->SelectAll(); }

	wxStyledTextCtrl *m_text;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(CTextEditView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class CTextDocument : public CDocument
{
public:
	CTextDocument() : CDocument() { m_bChildDoc = false; }

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