#ifndef __GRID_H__
#define __GRID_H__

#include "sfrontend/docView/docView.h"
#include "backend/backend_spreadsheet.h"       // ibBackendSpreadsheetObject — the sheet the document holds
#include "backend/sheetFormat/sheetFormat.h"   // which formats this document can be saved as

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

// The view of a spreadsheet document.
class SFRONTEND_API ibSpreadsheetEditView : public ibMetaView
{
public:
	ibSpreadsheetEditView() : ibMetaView() {}

	wxDECLARE_DYNAMIC_CLASS(ibSpreadsheetEditView);
};

// ----------------------------------------------------------------------------
// ibSpreadsheetDocument: ibDocument and its sheet married
// ----------------------------------------------------------------------------

#include "backend/metaCollection/metaSpreadsheetObject.h"

class SFRONTEND_API ibSpreadsheetDocument : public ibMetaDocument {
public:

	virtual ibServerPicture GetIcon() const {
		return ibBackendPicture::GetServerPicture(g_metaCommonTemplateCLSID);
	}

	ibSpreadsheetDocument() : ibMetaDocument() {}

private:
	wxDECLARE_NO_COPY_CLASS(ibSpreadsheetDocument);
	wxDECLARE_ABSTRACT_CLASS(ibSpreadsheetDocument);
};

// ----------------------------------------------------------------------------
// A very simple spreadsheet document class
// ----------------------------------------------------------------------------

class SFRONTEND_API ibSpreadsheetFileDocument : public ibSpreadsheetDocument {
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

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxObjectDataPtr<ibBackendSpreadsheetObject> m_spreadSheetDocument;

private:

	wxDECLARE_NO_COPY_CLASS(ibSpreadsheetFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibSpreadsheetFileDocument);
};

#endif
