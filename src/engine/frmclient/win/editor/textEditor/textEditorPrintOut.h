#ifndef __TEXT_EDIT_PRINT_H__
#define __TEXT_EDIT_PRINT_H__

#include "textEditor.h"

#if wxUSE_PRINTING_ARCHITECTURE

//----------------------------------------------------------------------------
//! EditPrint
class ibTextEditorPrintout : public wxPrintout {

public:

	//! constructor
	ibTextEditorPrintout(wxStyledTextCtrl *edit, const wxString& title = "");

	//! event handlers
	bool OnPrintPage(int page) wxOVERRIDE;
	bool OnBeginDocument(int startPage, int endPage) wxOVERRIDE;

	//! print functions
	bool HasPage(int page) wxOVERRIDE;
	void GetPageInfo(int *minPage, int *maxPage, int *selPageFrom, int *selPageTo) wxOVERRIDE;

private:
	wxStyledTextCtrl*m_edit;
	wxArrayInt m_pageEnds;
	wxRect m_pageRect;
	wxRect m_printRect;

	bool PrintScaling(wxDC *dc);
};

#endif // wxUSE_PRINTING_ARCHITECTURE

#endif // _EDIT_H_
