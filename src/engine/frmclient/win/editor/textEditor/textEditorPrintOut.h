#ifndef __TEXT_EDIT_PRINT_H__
#define __TEXT_EDIT_PRINT_H__

#include "textEditor.h"
#include "frmclient/frmclient.h"

#include <wx/weakref.h>

#if wxUSE_PRINTING_ARCHITECTURE

//----------------------------------------------------------------------------
//! EditPrint
class FRMCLIENT_API ibTextEditorPrintout : public wxPrintout {

public:

	//! constructor
	ibTextEditorPrintout(ibStyledTextCtrl *edit, const wxString& title = "");
	virtual ~ibTextEditorPrintout();

	//! event handlers
	virtual bool OnPrintPage(int page) override;
	virtual bool OnBeginDocument(int startPage, int endPage) override;

	//! print functions
	virtual bool HasPage(int page) override;
	virtual void GetPageInfo(int *minPage, int *maxPage, int *selPageFrom, int *selPageTo) override;

private:
	// THE PAPER: the editor's text as it goes on paper - a view of its own over the same document, hidden, in the light
	// look's colours on white (ibFontColorSettings::Apply, wxApp::Appearance::Light). The editor itself paints in the
	// look in force, and in the dark one printed light grey on black. Made inside the editor, it goes with it.
	wxWeakRef<ibStyledTextCtrl> m_paper;
	wxArrayInt m_pageEnds;
	wxRect m_pageRect;
	wxRect m_printRect;

	bool PrintScaling(wxDC *dc);
};

#endif // wxUSE_PRINTING_ARCHITECTURE

#endif // _EDIT_H_
