#ifndef _GRID_H__
#define _GRID_H__

#include "common/docInfo.h"
#include "frontend/grid/gridCommon.h"

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

// The view using a standard wxTextCtrl to show its contents
class CGridEditView : public CView
{
public:
	CGridEditView() : CView(), m_grid(NULL) {}

	virtual bool OnCreate(CDocument *doc, long flags) override;
	virtual void OnDraw(wxDC *dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	wxGrid *GetGridCtrl() const { return m_grid; }

private:

	void OnCopy(wxCommandEvent& WXUNUSED(event)) { m_grid->Copy(); }
	void OnPaste(wxCommandEvent& WXUNUSED(event)) { m_grid->Paste(); }
	void OnSelectAll(wxCommandEvent& WXUNUSED(event)) { m_grid->SelectAll(); }

	virtual wxPrintout *OnCreatePrintout() override;
	virtual void OnCreateToolbar(wxAuiToolBar *toolbar) override;

	void OnToolClicked(wxCommandEvent &event);
	void OnToolDropDown(wxAuiToolBarEvent& event);

	CGrid *m_grid;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(CGridEditView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

#include "metadata/metaObjects/metaGridObject.h"

class CGridDocument : public CDocument {
public:

	CGridDocument() : CDocument() { m_childDoc = false; }

	virtual bool OnCreate(const wxString& path, long flags) override;

	virtual wxGrid *GetGridCtrl() const = 0;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(CGridDocument);
	wxDECLARE_ABSTRACT_CLASS(CGridDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class CGridEditDocument : public CGridDocument {
public:
	CGridEditDocument() : CGridDocument() { }
	virtual wxGrid *GetGridCtrl()  const override;

	wxDECLARE_NO_COPY_CLASS(CGridEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CGridEditDocument);
};

#endif