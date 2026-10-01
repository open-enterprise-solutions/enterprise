#ifndef __PRINT_PREVIEW_H__
#define __PRINT_PREVIEW_H__

/////////////////////////////////////////////////////////////////////////////
// Name:        frontend/docView/print/printPreview.h
// Purpose:     the print preview window, forked from wxPreviewFrame for the
//              one setting wx has no place for - fitting the page's width.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/frontend.h"

#include <wx/print.h>

// ⭐ A PRINTOUT THAT CAN BE FITTED TO THE PAGE'S WIDTH - the capability the preview's "Fit to page width"
// asks of what it shows. Only the spreadsheet's printout has it: a text or a form already is as wide as
// the page lets it be. Header-only, so a printout can carry it without linking the window below.
class ibFitToPageWidthPrintout {
public:
	virtual ~ibFitToPageWidthPrintout() = default;

	virtual void SetFitToPageWidth(bool fit) = 0;
	virtual bool IsFitToPageWidth() const = 0;

	// A TWIN over the same document, as this one would print it. A preview that paginates again needs new
	// printouts, and asking the printout is the one way that does not depend on where the focus is now -
	// a form picks what it prints by the focused control, and while the box is being ticked the focus is
	// in the preview.
	virtual wxPrintout* Clone() const = 0;
};

// THE PREVIEW WINDOW. A wxPrintPreview paginates ONCE - the first time a page is rendered - and keeps no
// way to be told the pages have changed. Fitting to the width IS a new pagination (fewer column bands,
// more rows on a page), so when the box is ticked or cleared the window builds a fresh preview in place,
// over twins of its printouts: the same window, its size, its zoom.
//
// ⚠ A wxPreviewFrame and nothing looser: wx reaches the page counter through the frame with a C-style
// cast to that class (wxPrintPreviewBase::RenderPageIntoDC), so a window of any other kind would be read
// as one anyway.
class FRONTEND_API ibPrintPreviewFrame : public wxPreviewFrame {
public:

	ibPrintPreviewFrame(wxPrintPreviewBase* preview, wxWindow* parent, const wxString& title);

	virtual void CreateControlBar() override;

	// The session's choice, told to a printout printed WITHOUT the preview (File -> Print), so the paper is
	// the page the preview was last left showing. A printout that cannot be fitted is handed back untouched.
	static wxPrintout* ApplyFitToPageWidth(wxPrintout* printout);

private:

	// Paginate again with the page's width fitted or not - a new preview over new printouts.
	void SetFitToPageWidth(bool fit);
};

#endif // !__PRINT_PREVIEW_H__
