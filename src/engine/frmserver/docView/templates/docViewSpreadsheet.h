#ifndef __GRID_H__
#define __GRID_H__

#include "frmserver/docView/docView.h"
#include "backend/backend_spreadsheet.h"       // ibBackendSpreadsheetObject — the sheet the document holds
#include "backend/sheetFormat/sheetFormat.h"   // which formats this document can be saved as

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

// The view of a spreadsheet document — its sheet drawn as a gridbox draws its own: a gridbox's node, the sheet read by a
// client the same way (ibFetchSpreadsheet).
class FRMSERVER_API ibSpreadsheetEditView : public ibMetaView
{
public:
	ibSpreadsheetEditView() : ibMetaView() {}

	virtual void OnDraw(ibDataNode& frame) override;
	virtual ibClientViewKind GetViewKind() const override { return ibClientViewKind::Spreadsheet; }
	virtual bool Fetch(const ibDataNode& request, ibDataNode& response) override;
	// The sheet the person edited on the client, handed over (Change {Sheet}) — the document's sheet from then on; a cell
	// clicked (Cell {Row, Col}) — the value it is bound to, opened.
	virtual bool OnClientEvent(ibProtocolEvent event, const ibDataNode& args) override;

	wxDECLARE_DYNAMIC_CLASS(ibSpreadsheetEditView);
};

// ----------------------------------------------------------------------------
// ibSpreadsheetDocument: ibDocument and its sheet married
// ----------------------------------------------------------------------------

#include "backend/metaCollection/metaSpreadsheetObject.h"

class FRMSERVER_API ibSpreadsheetDocument : public ibMetaDocument {
public:

	virtual ibServerPicture GetIcon() const {
		return ibBackendPicture::GetServerPicture(g_metaCommonTemplateCLSID);
	}

	ibSpreadsheetDocument() : ibMetaDocument() {}

	// The sheet the document holds…
	virtual const ibBackendSpreadsheetObject& GetSpreadsheet() const = 0;
	// …changed: the sheet the person left, the document's from then on — modified.
	virtual void SetSpreadsheetDesc(ibSpreadsheetDescription&& spreadsheetDesc) = 0;

private:
	wxDECLARE_NO_COPY_CLASS(ibSpreadsheetDocument);
	wxDECLARE_ABSTRACT_CLASS(ibSpreadsheetDocument);
};

// ----------------------------------------------------------------------------
// A very simple spreadsheet document class
// ----------------------------------------------------------------------------

class FRMSERVER_API ibSpreadsheetFileDocument : public ibSpreadsheetDocument {
public:

	ibSpreadsheetFileDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& spreadSheetDocument = wxObjectDataPtr<ibBackendSpreadsheetObject>(new ibBackendSpreadsheetObject)) : ibSpreadsheetDocument(), m_spreadSheetDocument(spreadSheetDocument) { m_childDoc = false; }

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

	virtual const ibBackendSpreadsheetObject& GetSpreadsheet() const override { return *m_spreadSheetDocument; }
	virtual void SetSpreadsheetDesc(ibSpreadsheetDescription&& spreadsheetDesc) override {
		m_spreadSheetDocument->GetSpreadsheetDesc() = std::move(spreadsheetDesc);
		Modify(true);
	}

	// Every table format it writes — ours, a workbook, a Word document (backend/sheetFormat/).
	virtual wxString GetSaveFilter() const override;

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxObjectDataPtr<ibBackendSpreadsheetObject> m_spreadSheetDocument;

private:

	wxDECLARE_NO_COPY_CLASS(ibSpreadsheetFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibSpreadsheetFileDocument);
};

// ----------------------------------------------------------------------------
// The document and view a form's grid box holds (docview-fork.md, "the form is a facade")
// ----------------------------------------------------------------------------

// A spreadsheet file document — it holds the sheet the box shows and saves it as every format the registry
// writes — in no manager's list and no tab of its own.
class FRMSERVER_API ibSpreadsheetGridBoxDocument : public ibSpreadsheetFileDocument {
public:

	ibSpreadsheetGridBoxDocument();

	// The one door a sheet is shown through.
	void SetSpreadsheetDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& spreadSheetDocument) { m_spreadSheetDocument = spreadSheetDocument; }
	const wxObjectDataPtr<ibBackendSpreadsheetObject>& GetSpreadsheetDocument() const { return m_spreadSheetDocument; }

	// The box holds this document, not its views — none of them going takes it along.
	virtual void OnChangedViewList() override {}

private:

	wxDECLARE_NO_COPY_CLASS(ibSpreadsheetGridBoxDocument);
	wxDECLARE_DYNAMIC_CLASS(ibSpreadsheetGridBoxDocument);
};

// Its view, for as long as the box lives. Its frame is the box's, not one of its own, so there is no title to mark.
class FRMSERVER_API ibSpreadsheetGridBoxView : public ibSpreadsheetEditView {
public:

	ibSpreadsheetGridBoxView() : ibSpreadsheetEditView() {}

	virtual void OnChangeFilename() override {}

private:
	wxDECLARE_DYNAMIC_CLASS(ibSpreadsheetGridBoxView);
};

#endif
