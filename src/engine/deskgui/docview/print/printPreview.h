#ifndef __PRINT_PREVIEW_H__
#define __PRINT_PREVIEW_H__

/////////////////////////////////////////////////////////////////////////////
// Name:        deskgui/docview/print/printPreview.h
// Purpose:     the print preview window - wx's (src/common/prntbase.cpp) forked onto the theme's windows -
//              and the one setting wx has no place for, fitting the page's width.
/////////////////////////////////////////////////////////////////////////////

#include "deskgui/deskgui.h"
#include "deskgui/univ/frame.h"

#include <wx/print.h>

#include <memory>

// ⭐ A PRINTOUT THAT CAN BE FITTED TO THE PAGE'S WIDTH - the capability the preview's "Fit to page width"
// asks of what it shows. Only the spreadsheet's printout has it: a text or a form already is as wide as
// the page lets it be. Header-only, so a printout can carry it without linking the window below.
class ibFitToPageWidthPrintout {
public:
	virtual ~ibFitToPageWidthPrintout() = default;

	// Taken when the printout next lays its pages out (OnPreparePrinting) - the preview has it done again at once.
	virtual void SetFitToPageWidth(bool fit) = 0;
	virtual bool IsFitToPageWidth() const = 0;
};

class ibPrintPreview;
class ibPrintPreviewCanvas;
class ibPrintPreviewControlBar;

// THE PREVIEW WINDOW - wx's (wxPreviewFrame) as a frame of the theme: its title bar and status bar, a bar of the
// theme's controls above a canvas of its own (2026-11-25, Max: it stood on the screen as the system's window, not in
// our shell). wx's preview it is made over stays what is the port's - it has measured the page for the printer as it
// was made (DetermineScaling) and holds the two printouts; the window shows them through its ibPrintPreview: which
// page, at what zoom, rendered into what bitmap, and the pages laid out again when "Fit to page width" is ticked.
class DESKGUI_API ibPrintPreviewFrame : public ibFrame {
public:

	// over wx's preview, which it owns from then on
	ibPrintPreviewFrame(wxPrintPreviewBase* preview, wxWindow* parent, const wxString& title);
	virtual ~ibPrintPreviewFrame();

	// wx's (wxPreviewFrame::Initialize): the status bar, the bar above the canvas, the window sized to them, and every
	// other window of the program disabled while it is open
	void Initialize();

	ibPrintPreviewControlBar* GetControlBar() const { return m_controlBar; }

	// The session's choice, told to a printout printed WITHOUT the preview (File -> Print), so the paper is
	// the page the preview was last left showing. A printout that cannot be fitted is handed back untouched.
	static wxPrintout* ApplyFitToPageWidth(wxPrintout* printout);

private:

	// Esc closes it, as wx's
	void OnChar(wxKeyEvent& event);

	std::unique_ptr<ibPrintPreview> m_printPreview;
	ibPrintPreviewCanvas* m_previewCanvas = nullptr;
	ibPrintPreviewControlBar* m_controlBar = nullptr;
};

#endif // !__PRINT_PREVIEW_H__
