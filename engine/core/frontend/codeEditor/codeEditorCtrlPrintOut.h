#ifndef _EDIT_H_
#define _EDIT_H_

#include "codeEditorCtrl.h"

#if wxUSE_PRINTING_ARCHITECTURE

//----------------------------------------------------------------------------
//! EditPrint
class CCodeEditorPrintout : public wxPrintout {

public:

	//! constructor
	CCodeEditorPrintout(CCodeEditorCtrl *edit, const wxString& title = "");

	//! event handlers
	bool OnPrintPage(int page) wxOVERRIDE;
	bool OnBeginDocument(int startPage, int endPage) wxOVERRIDE;

	//! print functions
	bool HasPage(int page) wxOVERRIDE;
	void GetPageInfo(int *minPage, int *maxPage, int *selPageFrom, int *selPageTo) wxOVERRIDE;

private:
	CCodeEditorCtrl *m_edit;
	wxArrayInt m_pageEnds;
	wxRect m_pageRect;
	wxRect m_printRect;

	bool PrintScaling(wxDC *dc);
};

#endif // wxUSE_PRINTING_ARCHITECTURE

#endif // _EDIT_H_
