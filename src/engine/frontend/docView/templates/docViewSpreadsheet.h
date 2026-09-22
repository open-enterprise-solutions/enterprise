#ifndef __GRID_H__
#define __GRID_H__

#include "frontend/docView/docView.h"
#include "frontend/win/editor/gridEditor/gridEditor.h"
#include "backend/sheetFormat/sheetFormat.h"   // which formats this document can be saved as

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

// The view using a standard wxTextCtrl to show its contents
class FRONTEND_API ibSpreadsheetEditView : public ibMetaView
{
public:
	ibSpreadsheetEditView() : ibMetaView(), m_gridEditor(nullptr) {}

#if wxUSE_MENUS	
	wxMenuBar* CreateMenuBar() const;
#endif 

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, ibView* activeView, ibView* deactiveView) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	ibGridEditor* GetGridCtrl() const { return m_gridEditor; }

private:

	void OnCopy(wxCommandEvent& WXUNUSED(event)) { m_gridEditor->Copy(); }
	void OnPaste(wxCommandEvent& WXUNUSED(event)) { m_gridEditor->Paste(); }
	void OnSelectAll(wxCommandEvent& WXUNUSED(event)) { m_gridEditor->SelectAll(); }

	virtual wxPrintout* OnCreatePrintout() override;
	virtual void OnCreateToolbar(wxAuiToolBar* toolbar) override;

	void OnMenuEvent(wxCommandEvent& event);

protected:

	ibGridEditor* m_gridEditor;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(ibSpreadsheetEditView);
};

// ----------------------------------------------------------------------------
// ibTextDocument: ibDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

#include "backend/metaCollection/metaSpreadsheetObject.h"

class FRONTEND_API ibSpreadsheetDocument : public ibMetaDocument {
public:

	virtual wxIcon GetIcon() const {
		return ibBackendPicture::GetPictureAsIcon(g_metaCommonTemplateCLSID);
	}

	ibSpreadsheetDocument() : ibMetaDocument() {}
	virtual wxCommandProcessor* OnCreateCommandProcessor() override;
	
	virtual ibGridEditor* GetGridCtrl() const;

private:
	wxDECLARE_NO_COPY_CLASS(ibSpreadsheetDocument);
	wxDECLARE_ABSTRACT_CLASS(ibSpreadsheetDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class FRONTEND_API ibSpreadsheetFileDocument : public ibSpreadsheetDocument {
public:

	ibSpreadsheetFileDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& spreadSheetDocument = wxObjectDataPtr<ibBackendSpreadsheetObject>(new ibBackendSpreadsheetObject)) : ibSpreadsheetDocument(), m_spreadSheetDocument(spreadSheetDocument) { m_childDoc = false; }

	virtual bool OnCreate(const wxString& path, long flags) override;

	// ⭐ SAVE AS OFFERS EVERY FORMAT THAT CAN BE WRITTEN — our own file, an Excel
	// workbook, a Word document — because the registry says so, not because this
	// class lists them (backend/sheetFormat/). Open offers fewer: the same registry,
	// asked the other way, leaves out what it cannot read.
	virtual wxString GetSaveFilter() const override { return ibSheetFormatSaveFilter(); }

	virtual bool OnNewDocument() override {

		// notice that there is no need to either reset nor even check the
		// modified flag here as the document itself is a new object (this is only
		// called from CreateDocument()) and so it shouldn't be saved anyhow even
		// if it is modified -- this could happen if the user code creates
		// documents pre-filled with some user-entered (and which hence must not be
		// lost) information

		SetDocumentSaved(false);

		const wxString name = GetDocumentManager()->MakeNewDocumentName();

		SetTitle(name);
		SetFilename(name, true);
		Modify(true);

		return true;
	}

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxObjectDataPtr<ibBackendSpreadsheetObject> m_spreadSheetDocument;

private:

	wxDECLARE_NO_COPY_CLASS(ibSpreadsheetFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibSpreadsheetFileDocument);
};

class FRONTEND_API ibSpreadsheetEditDocument : public ibSpreadsheetDocument {
public:

	ibSpreadsheetEditDocument() : ibSpreadsheetDocument() {}

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool SaveAs() override;

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;

private:

	wxDECLARE_NO_COPY_CLASS(ibSpreadsheetEditDocument);
	wxDECLARE_DYNAMIC_CLASS(ibSpreadsheetEditDocument);
};

// ----------------------------------------------------------------------------
// The document and view a form's grid box holds (docview-fork.md, "the form is a facade")
// ----------------------------------------------------------------------------

// A spreadsheet file document — it holds the sheet the box shows and saves it as every format the registry
// writes — in no manager's list and no tab of its own.
class FRONTEND_API ibSpreadsheetGridBoxDocument : public ibSpreadsheetFileDocument {
public:

	ibSpreadsheetGridBoxDocument();

	// The one door a sheet is shown through: held here, and loaded into the editor while there is one.
	void SetSpreadsheetDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& spreadSheetDocument);

	// The box holds this document, not its views — none of them going takes it along.
	virtual void OnChangedViewList() override {}

private:

	wxDECLARE_NO_COPY_CLASS(ibSpreadsheetGridBoxDocument);
	wxDECLARE_DYNAMIC_CLASS(ibSpreadsheetGridBoxDocument);
};

// Its view, for as long as the box lives. The box's Create creates it in the box's parent — its OnCreate makes
// the editor the box returns; the box's Cleanup closes it and it stays empty: the editor is destroyed by the
// visual host, the document is the box's. Its frame is that parent, not a frame of its own, so there is no
// title to mark.
class FRONTEND_API ibSpreadsheetGridBoxView : public ibSpreadsheetEditView {
public:

	ibSpreadsheetGridBoxView() : ibSpreadsheetEditView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual bool OnClose(bool deleteWindow = true) override;
	virtual void OnChangeFilename() override {}

private:
	wxDECLARE_DYNAMIC_CLASS(ibSpreadsheetGridBoxView);
};

#endif