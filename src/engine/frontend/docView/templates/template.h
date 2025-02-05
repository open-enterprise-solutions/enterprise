#ifndef _GRID_H__
#define _GRID_H__

#include "frontend/docView/docView.h"
#include "frontend/mainFrame/grid/gridCommon.h"

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

// The view using a standard wxTextCtrl to show its contents
class FRONTEND_API CGridEditView : public CMetaView
{
public:
	CGridEditView() : CMetaView(), m_gridEditor(nullptr) {}

	virtual bool OnCreate(CMetaDocument *doc, long flags) override;
	virtual void OnDraw(wxDC *dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	wxGrid *GetGridCtrl() const { return m_gridEditor; }

private:

	void OnCopy(wxCommandEvent& WXUNUSED(event)) { m_gridEditor->Copy(); }
	void OnPaste(wxCommandEvent& WXUNUSED(event)) { m_gridEditor->Paste(); }
	void OnSelectAll(wxCommandEvent& WXUNUSED(event)) { m_gridEditor->SelectAll(); }

	virtual wxPrintout *OnCreatePrintout() override;
	virtual void OnCreateToolbar(wxAuiToolBar *toolbar) override;

	void OnToolClicked(wxCommandEvent &event);
	void OnToolDropDown(wxAuiToolBarEvent& event);

	CGrid *m_gridEditor;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(CGridEditView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

#include "backend/metaCollection/metaGridObject.h"

class FRONTEND_API CGridDocument : public CMetaDocument {
public:

	CGridDocument() : CMetaDocument() { m_childDoc = false; }

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

class FRONTEND_API CGridEditDocument : public CGridDocument {
public:
	CGridEditDocument() : CGridDocument() { }
	virtual wxGrid *GetGridCtrl()  const override;

	wxDECLARE_NO_COPY_CLASS(CGridEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CGridEditDocument);
};

#endif