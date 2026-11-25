/////////////////////////////////////////////////////////////////////////////
// Name:        deskgui/docview/print/printPreview.cpp
// Purpose:     the print preview window, its canvas and its bar with "Fit to page width" - FORKED from wx's
//              (src/common/prntbase.cpp: wxPrintPreviewBase, wxPreviewCanvas, wxPreviewControlBar, wxPreviewFrame)
/////////////////////////////////////////////////////////////////////////////

#include "printPreview.h"

#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/messageDialog.h"   // ibMessageBox - a page that could not be rendered
#include "deskgui/univ/panel.h"
#include "deskgui/univ/scrolwin.h"
#include "deskgui/univ/theme.h"
#include "deskgui/univ/ctrl/bmpButton.h"
#include "deskgui/univ/ctrl/checkBox.h"
#include "deskgui/univ/ctrl/choice.h"
#include "deskgui/univ/ctrl/staticText.h"
#include "deskgui/univ/ctrl/textCtrl.h"

#include <wx/artprov.h>
#include <wx/dcmemory.h>
#include <wx/dcprint.h>
#include <wx/sizer.h>
#if defined(__WXMSW__) && wxUSE_ENH_METAFILE
#include <wx/msw/enhmeta.h>
#endif

#include <algorithm>

// The choice outlives the window it was made in: the next preview of this session opens the way the last
// one was left. A plain flag - nothing here is a toolkit object, so a file-scope static is safe.
static bool s_fitToPageWidth = false;

namespace {

// The printout told whether to fit, when it can be; handed back either way.
wxPrintout* FitPrintoutToPageWidth(wxPrintout* printout, bool fit)
{
	if (ibFitToPageWidthPrintout* fits = dynamic_cast<ibFitToPageWidthPrintout*>(printout))
		fits->SetFitToPageWidth(fit);
	return printout;
}

} // namespace

//----------------------------------------------------------------------------
// ibPrintPreview
//----------------------------------------------------------------------------

// FORKED from wxPrintPreviewBase: a printout's pages as the window shows them - which page, at what zoom, rendered into
// what bitmap - on the theme's canvas and frame, which wx's can only be told of as its own kinds (it takes a
// wxPreviewCanvas, and casts its frame to a wxPreviewFrame). The wx preview it is made over keeps what is the port's:
// as it was made it measured the page for the printer and told the printout (DetermineScaling, each port its own way),
// it holds the two printouts, and it is what a printout knows as its preview (wxPrintout::IsPreview). Its own rendering
// is never asked for - that is where it casts its frame.
class ibPrintPreview {
public:

	explicit ibPrintPreview(wxPrintPreviewBase* preview);

	void SetCanvas(ibPrintPreviewCanvas* canvas) { m_previewCanvas = canvas; }
	void SetFrame(ibPrintPreviewFrame* frame) { m_previewFrame = frame; }
	ibPrintPreviewFrame* GetFrame() const { return m_previewFrame; }

	wxPrintout* GetPrintout() const { return m_preview->GetPrintout(); }
	wxPrintout* GetPrintoutForPrinting() const { return m_preview->GetPrintoutForPrinting(); }

	void SetCurrentPage(int pageNum);
	int GetCurrentPage() const { return m_currentPage; }
	int GetMinPage() const { return m_minPage; }
	int GetMaxPage() const { return m_maxPage; }

	void SetZoom(int percent);
	int GetZoom() const { return m_currentZoom; }

	// Fitted to the page's width or not - the session's choice, both printouts told, and the pages laid out again from
	// the first: a printout lays them out as it is prepared (OnPreparePrinting), which the next render does again. wx's
	// preview prepares a printout once and has no way to be told the pages changed - the window was built anew round
	// twins of the printouts for it (2026-09-22).
	void SetFitToPageWidth(bool fit);

	// The paper on the canvas, the page's bitmap over it
	bool PaintPage(wxDC& dc);
	// The current page rendered unless it is: true when it was now, for the canvas to paint it
	bool UpdatePageRendering();
	// The canvas's scrolled area the size of the paper at this zoom
	void AdjustScrollbars();
	void UpdateOnDPIChanged();

	// The printout for printing printed, the printer's dialog first - as wx's preview prints it, over the theme's frame
	bool Print();

private:

	// Where the paper and its printable part stand on the canvas
	void CalcRects(wxRect& pageRect, wxRect& paperRect) const;
	bool DrawBlankPage(wxDC& dc);

	void InvalidatePreviewBitmap();
	bool RenderPage(int pageNum);
	bool RenderPageIntoBitmap(wxBitmap& bmp, int pageNum);
	bool RenderPageIntoDC(wxDC& dc, int pageNum);

	const std::unique_ptr<wxPrintPreviewBase> m_preview;
	ibPrintPreviewCanvas* m_previewCanvas = nullptr;
	ibPrintPreviewFrame* m_previewFrame = nullptr;

	wxBitmap m_previewBitmap;
	bool m_previewFailed = false;
	bool m_printingPrepared = false;   // the printout has laid its pages out (OnPreparePrinting)

	int m_currentPage = 1;
	int m_currentZoom = 70;
	int m_minPage = 1;
	int m_maxPage = 1;

	int m_topMargin = 0;
	int m_leftMargin = 0;

	// the page as the printer has it, in its pixels, and their scale on the screen at 100%
	int m_pageWidth = 0;
	int m_pageHeight = 0;
	float m_previewScaleX = 1.0f;
	float m_previewScaleY = 1.0f;
};

//----------------------------------------------------------------------------
// ibPrintPreviewCanvas
//----------------------------------------------------------------------------

// FORKED from wxPreviewCanvas: the paper on the theme's scrolled panel, round it the desktop's colour where wx's took the
// system's workspace colour, the keys and the wheel of wx's. Painted whole as it is sized: the paper stands in its middle.
class ibPrintPreviewCanvas : public ibScrolled<ibPanel> {
public:

	ibPrintPreviewCanvas(ibPrintPreview* preview, wxWindow* parent);

private:

	// the bar of the frame it stands in: what its keys and its wheel ask
	ibPrintPreviewControlBar* GetControlBar() const;

	void OnPaint(wxPaintEvent& event);
	void OnIdle(wxIdleEvent& event);
	void OnDPIChanged(wxDPIChangedEvent& event);
	void OnChar(wxKeyEvent& event);
	void OnMouseWheel(wxMouseEvent& event);

	ibPrintPreview* const m_printPreview;
};

//----------------------------------------------------------------------------
// ibPrintPreviewControlBar
//----------------------------------------------------------------------------

// FORKED from wxPreviewControlBar: the bar above the canvas, of the theme's controls - Print when there is a printout
// to print, the pages (first, previous, the page's number of how many, next, last), the zoom, "Fit to page width" for a
// printout that can be fitted, and Close at the right edge. wx's controls of the page's number (wxPrintPageTextCtrl,
// wxPrintPageMaxCtrl) are a field and a label of the bar's. Not kept: the line it drew along its bottom - the canvas
// below is the desktop's colour - and the choice of buttons (wx's flags).
class ibPrintPreviewControlBar : public ibPanel {
public:

	ibPrintPreviewControlBar(ibPrintPreview* preview, wxWindow* parent);

	void CreateButtons();

	// The pages there are, known once the printout has laid them out - the first one shown
	void SetPageInfo(int minPage, int maxPage);

	// The zoom shown, the nearest of the list at or above it; the one chosen - 0 for none
	void SetZoomControl(int zoom);
	int GetZoomControl() const;

	// what the canvas's keys and wheel ask, as of wx's
	void OnNext();
	void OnPrevious();
	void OnFirst();
	void OnLast();
	void OnPrint();
	void DoZoomIn();
	void DoZoomOut();

private:

	void DoGotoPage(int page);
	// the number in the field taken - on Enter, or as the field is left: a page of the printout is gone to, anything
	// else is put back to the page shown
	void OnGotoPage();
	void DoZoom();

	bool IsNextEnabled() const;
	bool IsPreviousEnabled() const;
	bool IsFirstEnabled() const;
	bool IsLastEnabled() const;
	bool IsZoomInEnabled() const;
	bool IsZoomOutEnabled() const;

	ibPrintPreview* const m_printPreview;
	ibChoice* m_zoomControl = nullptr;
	ibTextCtrl* m_currentPageText = nullptr;
	ibStaticText* m_maxPageText = nullptr;
};

//----------------------------------------------------------------------------
// ibPrintPreview
//----------------------------------------------------------------------------

ibPrintPreview::ibPrintPreview(wxPrintPreviewBase* preview)
	: m_preview(preview)
{
	m_topMargin =
	m_leftMargin = wxRound(2 * wxSizerFlags::GetDefaultBorderFractional());

	// The page as wx's preview measured it for the printer, read back from the printout it told.
	wxPrintout* const printout = GetPrintout();
	printout->GetPageSizePixels(&m_pageWidth, &m_pageHeight);

	int screenX = 0, screenY = 0, printerX = 0, printerY = 0;
	printout->GetPPIScreen(&screenX, &screenY);
	printout->GetPPIPrinter(&printerX, &printerY);

	// At 100%, the page should look about page-size on the screen.
	if (printerX > 0 && printerY > 0) {
		m_previewScaleX = float(screenX) / printerX;
		m_previewScaleY = float(screenY) / printerY;
	}

	// The session's last choice, on the printouts - nothing is laid out yet (that waits for the first page to be
	// rendered), so telling them now is in time.
	FitPrintoutToPageWidth(GetPrintout(), s_fitToPageWidth);
	FitPrintoutToPageWidth(GetPrintoutForPrinting(), s_fitToPageWidth);
}

void ibPrintPreview::SetCurrentPage(int pageNum)
{
	if (m_currentPage == pageNum)
		return;

	m_currentPage = pageNum;

	InvalidatePreviewBitmap();

	if (m_previewCanvas) {
		AdjustScrollbars();

		m_previewCanvas->Refresh();
		m_previewCanvas->SetFocus();
	}
}

void ibPrintPreview::SetZoom(int percent)
{
	if (m_currentZoom == percent)
		return;

	m_currentZoom = percent;

	InvalidatePreviewBitmap();

	if (m_previewCanvas) {
		AdjustScrollbars();
		m_previewCanvas->Scroll(0, 0);
		m_previewCanvas->Refresh();
		m_previewCanvas->SetFocus();
	}
}

void ibPrintPreview::SetFitToPageWidth(bool fit)
{
	s_fitToPageWidth = fit;

	FitPrintoutToPageWidth(GetPrintout(), fit);
	FitPrintoutToPageWidth(GetPrintoutForPrinting(), fit);

	// From the first page: the pages are counted anew, and the one shown may not be among them.
	m_printingPrepared = false;
	m_currentPage = 1;

	InvalidatePreviewBitmap();

	if (m_previewCanvas) {
		AdjustScrollbars();
		m_previewCanvas->Scroll(0, 0);
		m_previewCanvas->Refresh();
	}
}

void ibPrintPreview::CalcRects(wxRect& pageRect, wxRect& paperRect) const
{
	// Calculate the rectangles for the printable area of the page and the
	// entire paper as they appear on the canvas on-screen.
	int canvasWidth, canvasHeight;
	m_previewCanvas->GetSize(&canvasWidth, &canvasHeight);

	const float zoomScale = m_currentZoom / 100.0f;
	const float screenPrintableWidth = zoomScale * m_previewCanvas->FromDIP(m_pageWidth) * m_previewScaleX;
	const float screenPrintableHeight = zoomScale * m_previewCanvas->FromDIP(m_pageHeight) * m_previewScaleY;

	const wxRect devicePaperRect = GetPrintout()->GetPaperRectPixels();
	wxCoord devicePrintableWidth, devicePrintableHeight;
	GetPrintout()->GetPageSizePixels(&devicePrintableWidth, &devicePrintableHeight);
	const float scaleX = screenPrintableWidth / devicePrintableWidth;
	const float scaleY = screenPrintableHeight / devicePrintableHeight;
	paperRect.width = wxCoord(scaleX * devicePaperRect.width);
	paperRect.height = wxCoord(scaleY * devicePaperRect.height);

	paperRect.x = (canvasWidth - paperRect.width) / 2;
	if (paperRect.x < m_leftMargin)
		paperRect.x = m_leftMargin;
	paperRect.y = (canvasHeight - paperRect.height) / 2;
	if (paperRect.y < m_topMargin)
		paperRect.y = m_topMargin;

	pageRect.x = paperRect.x - wxCoord(scaleX * devicePaperRect.x);
	pageRect.y = paperRect.y - wxCoord(scaleY * devicePaperRect.y);
	pageRect.width = wxCoord(screenPrintableWidth);
	pageRect.height = wxCoord(screenPrintableHeight);
}

void ibPrintPreview::InvalidatePreviewBitmap()
{
	m_previewBitmap = wxNullBitmap;
	// if there was a problem with rendering the preview, try again now
	// that it changed in some way (less memory may be needed, for example):
	m_previewFailed = false;
}

bool ibPrintPreview::UpdatePageRendering()
{
	if (m_previewBitmap.IsOk())
		return false;

	if (m_previewFailed)
		return false;

	if (!RenderPage(m_currentPage)) {
		m_previewFailed = true; // don't waste time failing again
		return false;
	}

	return true;
}

bool ibPrintPreview::PaintPage(wxDC& dc)
{
	if (!m_previewCanvas)
		return false;

	DrawBlankPage(dc);

	if (!m_previewBitmap.IsOk())
		return false;

	wxRect pageRect, paperRect;
	CalcRects(pageRect, paperRect);
	wxMemoryDC temp_dc;
	temp_dc.SelectObject(m_previewBitmap);

	dc.Blit(pageRect.x, pageRect.y,
		m_previewBitmap.GetWidth(), m_previewBitmap.GetHeight(), &temp_dc, 0, 0);

	temp_dc.SelectObject(wxNullBitmap);
	return true;
}

void ibPrintPreview::AdjustScrollbars()
{
	if (!m_previewCanvas)
		return;

	wxRect pageRect, paperRect;
	CalcRects(pageRect, paperRect);
	const int totalWidth = paperRect.width + 2 * m_leftMargin;
	const int totalHeight = paperRect.height + 2 * m_topMargin;
	const wxSize virtualSize = m_previewCanvas->GetVirtualSize();
	if (virtualSize.GetWidth() != totalWidth || virtualSize.GetHeight() != totalHeight)
		m_previewCanvas->SetScrollbars(10, 10, totalWidth / 10, totalHeight / 10, 0, 0, true);
}

bool ibPrintPreview::RenderPageIntoDC(wxDC& dc, int pageNum)
{
	wxPrintout* const printout = GetPrintout();
	printout->SetDC(&dc);
	printout->SetPageSizePixels(m_pageWidth, m_pageHeight);

	// Need to delay OnPreparePrinting() until here, so we have enough
	// information and a wxDC.
	if (!m_printingPrepared) {
		m_printingPrepared = true;

		printout->OnPreparePrinting();

		wxPrintPageRanges ranges;
		const wxPrintPageRange all = printout->GetPagesInfo(ranges);
		m_minPage = all.fromPage;
		m_maxPage = all.toPage;

		// The bar's page range - the frame known by its own kind, where wx's preview casts its frame to its own.
		if (m_previewFrame != nullptr && m_previewFrame->GetControlBar() != nullptr)
			m_previewFrame->GetControlBar()->SetPageInfo(m_minPage, m_maxPage);
	}

	printout->OnBeginPrinting();

	const wxPrintDialogData& printDialogData = m_preview->GetPrintDialogData();
	if (!printout->OnBeginDocument(printDialogData.GetFromPage(), printDialogData.GetToPage())) {
		ibMessageBox(_("Could not start document preview."), _("Print Preview Failure"), wxOK, m_previewFrame);
		return false;
	}

	printout->OnPrintPage(pageNum);
	printout->OnEndDocument();
	printout->OnEndPrinting();

	printout->SetDC(nullptr);

	return true;
}

bool ibPrintPreview::RenderPageIntoBitmap(wxBitmap& bmp, int pageNum)
{
#if defined(__WXMSW__) && wxUSE_ENH_METAFILE
	// wxWindowsPrintPreview's: the page printed into a metafile of the printer DC's properties, then played into the
	// bitmap. A screen DC has a resolution and metrics of its own, and a printout that lays its text out against the DC
	// it is handed would paginate the preview otherwise than the paper.
	wxPrinterDC printerDC(m_preview->GetPrintDialogData().GetPrintData());
	wxEnhMetaFileDC metaDC(printerDC, wxEmptyString, printerDC.GetSize().x, printerDC.GetSize().y);

	if (!RenderPageIntoDC(metaDC, pageNum))
		return false;

	const std::unique_ptr<wxEnhMetaFile> metafile(metaDC.Close());
	if (!metafile)
		return false;

	// now render the metafile:
	wxMemoryDC bmpDC;
	bmpDC.SelectObject(bmp);
	bmpDC.Clear();

	wxRect outRect(0, 0, bmp.GetWidth(), bmp.GetHeight());
	metafile->Play(&bmpDC, &outRect);

	return true;
#else
	wxMemoryDC memoryDC;
	memoryDC.SelectObject(bmp);
	memoryDC.Clear();

	return RenderPageIntoDC(memoryDC, pageNum);
#endif
}

bool ibPrintPreview::RenderPage(int pageNum)
{
	wxBusyCursor busy;

	if (!m_previewCanvas)
		return false;

	wxRect pageRect, paperRect;
	CalcRects(pageRect, paperRect);

	if (!m_previewBitmap.IsOk()) {
		m_previewBitmap.CreateWithLogicalSize(pageRect.width, pageRect.height, m_previewCanvas->GetDPIScaleFactor());

		if (!m_previewBitmap.IsOk()) {
			InvalidatePreviewBitmap();
			ibMessageBox(_("Sorry, not enough memory to create a preview."), _("Print Preview Failure"), wxOK, m_previewFrame);
			return false;
		}
	}

	if (!RenderPageIntoBitmap(m_previewBitmap, pageNum)) {
		InvalidatePreviewBitmap();
		ibMessageBox(_("Sorry, not enough memory to create a preview."), _("Print Preview Failure"), wxOK, m_previewFrame);
		return false;
	}

#if wxUSE_STATUSBAR
	if (m_previewFrame != nullptr)
		m_previewFrame->SetStatusText(m_maxPage != 0
			? wxString::Format(_("Page %d of %d"), pageNum, m_maxPage)
			: wxString::Format(_("Page %d"), pageNum));
#endif

	return true;
}

bool ibPrintPreview::DrawBlankPage(wxDC& dc)
{
	wxRect pageRect, paperRect;

	CalcRects(pageRect, paperRect);

	// Draw shadow, allowing for 1-pixel border AROUND the actual paper
	const wxCoord shadowOffset = dc.FromDIP(4);

	dc.SetPen(*wxBLACK_PEN);
	dc.SetBrush(*wxBLACK_BRUSH);
	dc.DrawRectangle(paperRect.x + shadowOffset, paperRect.y + paperRect.height,
		paperRect.width, shadowOffset);

	dc.DrawRectangle(paperRect.x + paperRect.width, paperRect.y + shadowOffset,
		shadowOffset, paperRect.height);

	// Draw blank page allowing for 1-pixel border AROUND the actual paper
	dc.SetPen(wxPen(*wxBLACK, dc.FromDIP(1), wxPENSTYLE_SOLID));
	dc.SetBrush(*wxWHITE_BRUSH);
	const wxCoord borderOffset = wxRound(dc.GetPen().GetWidth() / 2.0);
	dc.DrawRectangle(paperRect.x - borderOffset, paperRect.y - borderOffset,
		paperRect.width + 2 * borderOffset, paperRect.height + 2 * borderOffset);

	return true;
}

void ibPrintPreview::UpdateOnDPIChanged()
{
	InvalidatePreviewBitmap();
	UpdatePageRendering();

	if (m_previewCanvas) {
		AdjustScrollbars();

		m_previewCanvas->Refresh();
	}
}

bool ibPrintPreview::Print()
{
	wxPrintout* const printout = GetPrintoutForPrinting();
	if (printout == nullptr)
		return false;

	wxPrinter printer(&m_preview->GetPrintDialogData());
	return printer.Print(m_previewFrame, printout, true);
}

//----------------------------------------------------------------------------
// ibPrintPreviewCanvas
//----------------------------------------------------------------------------

ibPrintPreviewCanvas::ibPrintPreviewCanvas(ibPrintPreview* preview, wxWindow* parent)
	: ibScrolled<ibPanel>(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL | wxFULL_REPAINT_ON_RESIZE),
	  m_printPreview(preview)
{
	// As we rely on getting idle events, do this to ensure that we do receive
	// them even when using wxIDLE_PROCESS_SPECIFIED global idle mode.
	SetExtraStyle(wxWS_EX_PROCESS_IDLE);

	SetBackgroundColour(wxTHEME_COLOUR(DESKTOP));

	SetScrollbars(10, 10, 100, 100);

	// Use some reasonable default size for this window, roughly proportional
	// to the paper sheet.
	SetInitialSize(FromDIP(wxSize(600, 750)));

	Bind(wxEVT_PAINT, &ibPrintPreviewCanvas::OnPaint, this);
	Bind(wxEVT_IDLE, &ibPrintPreviewCanvas::OnIdle, this);
	Bind(wxEVT_DPI_CHANGED, &ibPrintPreviewCanvas::OnDPIChanged, this);
	Bind(wxEVT_CHAR, &ibPrintPreviewCanvas::OnChar, this);
#if wxUSE_MOUSEWHEEL
	Bind(wxEVT_MOUSEWHEEL, &ibPrintPreviewCanvas::OnMouseWheel, this);
#endif
}

ibPrintPreviewControlBar* ibPrintPreviewCanvas::GetControlBar() const
{
	ibPrintPreviewFrame* const frame = m_printPreview->GetFrame();
	return frame != nullptr ? frame->GetControlBar() : nullptr;
}

void ibPrintPreviewCanvas::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	ibThemePaintDC dc(this);
	PrepareDC(dc);

	m_printPreview->PaintPage(dc);
}

void ibPrintPreviewCanvas::OnIdle(wxIdleEvent& event)
{
	event.Skip();

	// prevent UpdatePageRendering() from being called recursively:
	static bool s_inIdle = false;
	if (s_inIdle)
		return;
	s_inIdle = true;

	if (m_printPreview->UpdatePageRendering())
		Refresh();

	s_inIdle = false;
}

void ibPrintPreviewCanvas::OnDPIChanged(wxDPIChangedEvent& event)
{
	m_printPreview->UpdateOnDPIChanged();

	event.Skip();
}

void ibPrintPreviewCanvas::OnChar(wxKeyEvent& event)
{
	ibPrintPreviewControlBar* const controlBar = GetControlBar();
	if (controlBar == nullptr) {
		event.Skip();
		return;
	}

	switch (event.GetKeyCode()) {
	case WXK_RETURN:
		controlBar->OnPrint();
		return;
	case (int)'+':
	case WXK_NUMPAD_ADD:
	case WXK_ADD:
		controlBar->DoZoomIn();
		return;
	case (int)'-':
	case WXK_NUMPAD_SUBTRACT:
	case WXK_SUBTRACT:
		controlBar->DoZoomOut();
		return;
	}

	if (!event.ControlDown()) {
		event.Skip();
		return;
	}

	switch (event.GetKeyCode()) {
	case WXK_PAGEDOWN:
		controlBar->OnNext(); break;
	case WXK_PAGEUP:
		controlBar->OnPrevious(); break;
	case WXK_HOME:
		controlBar->OnFirst(); break;
	case WXK_END:
		controlBar->OnLast(); break;
	default:
		event.Skip();
	}
}

#if wxUSE_MOUSEWHEEL

void ibPrintPreviewCanvas::OnMouseWheel(wxMouseEvent& event)
{
	ibPrintPreviewControlBar* const controlBar = GetControlBar();

	if (controlBar != nullptr && event.ControlDown() && event.GetWheelRotation() != 0) {
		const int currentZoom = controlBar->GetZoomControl();

		int delta;
		if (currentZoom < 100)
			delta = 5;
		else if (currentZoom <= 120)
			delta = 10;
		else
			delta = 50;

		if (event.GetWheelRotation() < 0)
			delta = -delta;

		const int newZoom = std::clamp(currentZoom + delta, 10, 200);
		if (newZoom != currentZoom) {
			controlBar->SetZoomControl(newZoom);
			m_printPreview->SetZoom(newZoom);
			Refresh();
		}
		return;
	}

	event.Skip();
}

#endif // wxUSE_MOUSEWHEEL

//----------------------------------------------------------------------------
// ibPrintPreviewControlBar
//----------------------------------------------------------------------------

ibPrintPreviewControlBar::ibPrintPreviewControlBar(ibPrintPreview* preview, wxWindow* parent)
	: ibPanel(parent),
	  m_printPreview(preview)
{
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { GetParent()->Close(true); }, wxID_PREVIEW_CLOSE);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnPrint(); }, wxID_PREVIEW_PRINT);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnPrevious(); }, wxID_PREVIEW_PREVIOUS);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnNext(); }, wxID_PREVIEW_NEXT);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnFirst(); }, wxID_PREVIEW_FIRST);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnLast(); }, wxID_PREVIEW_LAST);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DoZoomIn(); }, wxID_PREVIEW_ZOOM_IN);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DoZoomOut(); }, wxID_PREVIEW_ZOOM_OUT);

	Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(IsPreviousEnabled()); }, wxID_PREVIEW_PREVIOUS);
	Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(IsNextEnabled()); }, wxID_PREVIEW_NEXT);
	Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(IsFirstEnabled()); }, wxID_PREVIEW_FIRST);
	Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(IsLastEnabled()); }, wxID_PREVIEW_LAST);
	Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(IsZoomInEnabled()); }, wxID_PREVIEW_ZOOM_IN);
	Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(IsZoomOutEnabled()); }, wxID_PREVIEW_ZOOM_OUT);

	Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { DoZoom(); }, wxID_PREVIEW_ZOOM);
}

void ibPrintPreviewControlBar::CreateButtons()
{
	wxBoxSizer* const sizer = new wxBoxSizer(wxHORIZONTAL);

	// between the groups, and before the last one
	const int gap = wxRound(2 * wxSizerFlags::GetDefaultBorderFractional());
	const wxSizerFlags flags = wxSizerFlags().Border(wxLEFT | wxTOP | wxBOTTOM).Center();

	// toolbar-like bitmap buttons, not the smaller pictures beside a button's text: wxART_TOOLBAR
	const auto addButton = [this, sizer, &flags](wxWindowID id, const wxArtID& art, const wxString& tooltip) {
		ibBitmapButton* const button = new ibBitmapButton(this, id, wxArtProvider::GetBitmapBundle(art, wxART_TOOLBAR));
		button->SetToolTip(tooltip);
		sizer->Add(button, flags);
	};

	// Print, a group of its own - when there is a printout to print.
	if (m_printPreview->GetPrintoutForPrinting() != nullptr) {
		addButton(wxID_PREVIEW_PRINT, wxART_PRINT, _("Print"));
		sizer->AddSpacer(gap);
	}

	// The pages. The field is as wide as the longest number it may have to show: one or two digits in a field that
	// narrow looked uncomfortable (wx's).
	addButton(wxID_PREVIEW_FIRST, wxART_GOTO_FIRST, _("First page"));
	addButton(wxID_PREVIEW_PREVIOUS, wxART_GO_BACK, _("Previous page"));

	m_currentPageText = new ibTextCtrl(this, wxID_PREVIEW_GOTO, wxString(), wxDefaultPosition,
		wxSize(GetTextExtent(wxS("99999")).x, wxDefaultCoord), wxTE_PROCESS_ENTER);
	m_currentPageText->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { OnGotoPage(); });
	m_currentPageText->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) { OnGotoPage(); event.Skip(); });
	sizer->Add(m_currentPageText, flags);

	m_maxPageText = new ibStaticText(this, wxID_ANY, wxString(), wxDefaultPosition,
		wxSize(GetTextExtent(wxS("/ 99999")).x, wxDefaultCoord), wxST_NO_AUTORESIZE | wxALIGN_CENTRE);
	sizer->Add(m_maxPageText, flags);

	addButton(wxID_PREVIEW_NEXT, wxART_GO_FORWARD, _("Next page"));
	addButton(wxID_PREVIEW_LAST, wxART_GOTO_LAST, _("Last page"));
	sizer->AddSpacer(gap);

	// The zoom.
	addButton(wxID_PREVIEW_ZOOM_OUT, wxART_MINUS, _("Zoom Out"));

	const wxString choices[] =
	{
		wxT("10%"), wxT("15%"), wxT("20%"), wxT("25%"), wxT("30%"), wxT("35%"), wxT("40%"), wxT("45%"), wxT("50%"), wxT("55%"),
			wxT("60%"), wxT("65%"), wxT("70%"), wxT("75%"), wxT("80%"), wxT("85%"), wxT("90%"), wxT("95%"), wxT("100%"), wxT("110%"),
			wxT("120%"), wxT("150%"), wxT("200%")
	};
	const int n = WXSIZEOF(choices);

	m_zoomControl = new ibChoice(this, wxID_PREVIEW_ZOOM, wxDefaultPosition, wxDefaultSize, n, choices);
	sizer->Add(m_zoomControl, flags);
	SetZoomControl(m_printPreview->GetZoom());

	addButton(wxID_PREVIEW_ZOOM_IN, wxART_PLUS, _("Zoom In"));

	// "Fit to page width", beside the zoom - only for a printout that can be fitted: a box that changes nothing is a
	// promise the page does not keep.
	if (const ibFitToPageWidthPrintout* fits = dynamic_cast<const ibFitToPageWidthPrintout*>(m_printPreview->GetPrintout())) {
		sizer->AddSpacer(gap);

		ibCheckBox* const box = new ibCheckBox(this, wxID_ANY, _("Fit to page width"));
		box->SetValue(fits->IsFitToPageWidth());
		box->SetToolTip(_("Shrink a table wider than the page so that it is printed on one page across"));
		box->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) { m_printPreview->SetFitToPageWidth(event.IsChecked()); });
		sizer->Add(box, flags);
	}

	// Close, at the right edge - at least the same gap before it as between the groups.
	sizer->AddSpacer(gap);
	sizer->AddStretchSpacer();
	sizer->Add(new ibButton(this, wxID_PREVIEW_CLOSE, _("&Close")), wxSizerFlags().Border().Center());

	SetSizer(sizer);
}

void ibPrintPreviewControlBar::SetPageInfo(int minPage, int maxPage)
{
	m_currentPageText->ChangeValue(wxString::Format(wxS("%d"), minPage));
	m_maxPageText->SetLabel(wxString::Format(wxS("/ %d"), maxPage));
}

void ibPrintPreviewControlBar::SetZoomControl(int zoom)
{
	const unsigned int count = m_zoomControl->GetCount();
	for (unsigned int n = 0; n < count; n++) {
		long val;
		if (m_zoomControl->GetString(n).BeforeFirst(wxT('%')).ToLong(&val) && val >= long(zoom)) {
			m_zoomControl->SetSelection(n);
			return;
		}
	}

	m_zoomControl->SetSelection(count - 1);
}

int ibPrintPreviewControlBar::GetZoomControl() const
{
	long val;
	if (m_zoomControl->GetStringSelection().BeforeFirst(wxT('%')).ToLong(&val))
		return int(val);

	return 0;
}

void ibPrintPreviewControlBar::OnNext()
{
	if (IsNextEnabled())
		DoGotoPage(m_printPreview->GetCurrentPage() + 1);
}

void ibPrintPreviewControlBar::OnPrevious()
{
	if (IsPreviousEnabled())
		DoGotoPage(m_printPreview->GetCurrentPage() - 1);
}

void ibPrintPreviewControlBar::OnFirst()
{
	if (IsFirstEnabled())
		DoGotoPage(m_printPreview->GetMinPage());
}

void ibPrintPreviewControlBar::OnLast()
{
	if (IsLastEnabled())
		DoGotoPage(m_printPreview->GetMaxPage());
}

void ibPrintPreviewControlBar::OnPrint()
{
	m_printPreview->Print();
}

void ibPrintPreviewControlBar::DoZoomIn()
{
	if (IsZoomInEnabled()) {
		m_zoomControl->SetSelection(m_zoomControl->GetSelection() + 1);
		DoZoom();
	}
}

void ibPrintPreviewControlBar::DoZoomOut()
{
	if (IsZoomOutEnabled()) {
		m_zoomControl->SetSelection(m_zoomControl->GetSelection() - 1);
		DoZoom();
	}
}

void ibPrintPreviewControlBar::DoGotoPage(int page)
{
	m_printPreview->SetCurrentPage(page);
	m_currentPageText->ChangeValue(wxString::Format(wxS("%d"), page));
}

void ibPrintPreviewControlBar::OnGotoPage()
{
	long page = 0;
	if (m_currentPageText->GetValue().ToLong(&page)
		&& page >= m_printPreview->GetMinPage() && page <= m_printPreview->GetMaxPage()
		&& m_printPreview->GetPrintout()->HasPage(page)) {
		m_printPreview->SetCurrentPage(page);
		return;
	}

	m_currentPageText->ChangeValue(wxString::Format(wxS("%d"), m_printPreview->GetCurrentPage()));
}

void ibPrintPreviewControlBar::DoZoom()
{
	m_printPreview->SetZoom(GetZoomControl());
}

bool ibPrintPreviewControlBar::IsNextEnabled() const
{
	const int currentPage = m_printPreview->GetCurrentPage();
	return currentPage < m_printPreview->GetMaxPage() && m_printPreview->GetPrintout()->HasPage(currentPage + 1);
}

bool ibPrintPreviewControlBar::IsPreviousEnabled() const
{
	const int currentPage = m_printPreview->GetCurrentPage();
	return currentPage > m_printPreview->GetMinPage() && m_printPreview->GetPrintout()->HasPage(currentPage - 1);
}

bool ibPrintPreviewControlBar::IsFirstEnabled() const
{
	return m_printPreview->GetPrintout()->HasPage(m_printPreview->GetMinPage());
}

bool ibPrintPreviewControlBar::IsLastEnabled() const
{
	return m_printPreview->GetPrintout()->HasPage(m_printPreview->GetMaxPage());
}

bool ibPrintPreviewControlBar::IsZoomInEnabled() const
{
	const int sel = m_zoomControl->GetSelection();
	return sel != wxNOT_FOUND && sel < static_cast<int>(m_zoomControl->GetCount()) - 1;
}

bool ibPrintPreviewControlBar::IsZoomOutEnabled() const
{
	return m_zoomControl->GetSelection() > 0;
}

//----------------------------------------------------------------------------
// ibPrintPreviewFrame
//----------------------------------------------------------------------------

wxPrintout* ibPrintPreviewFrame::ApplyFitToPageWidth(wxPrintout* printout)
{
	return FitPrintoutToPageWidth(printout, s_fitToPageWidth);
}

ibPrintPreviewFrame::ibPrintPreviewFrame(wxPrintPreviewBase* preview, wxWindow* parent, const wxString& title)
	: ibFrame(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT),
	  m_printPreview(std::make_unique<ibPrintPreview>(preview))
{
	// the program's icon, as wx's gives it
	if (wxFrame* const topFrame = wxDynamicCast(wxTheApp->GetTopWindow(), wxFrame))
		SetIcons(topFrame->GetIcons());

	Bind(wxEVT_CHAR_HOOK, &ibPrintPreviewFrame::OnChar, this);
}

ibPrintPreviewFrame::~ibPrintPreviewFrame()
{
	// The windows that paint from the preview go before it does.
	DestroyChildren();
}

void ibPrintPreviewFrame::Initialize()
{
#if wxUSE_STATUSBAR
	CreateStatusBar();
#endif
	m_previewCanvas = new ibPrintPreviewCanvas(m_printPreview.get(), this);
	m_controlBar = new ibPrintPreviewControlBar(m_printPreview.get(), this);
	m_controlBar->CreateButtons();

	m_printPreview->SetCanvas(m_previewCanvas);
	m_printPreview->SetFrame(this);

	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);

	sizer->Add(m_controlBar, wxSizerFlags().Expand());
	sizer->Add(m_previewCanvas, wxSizerFlags(1).Expand());

	SetSizer(sizer);

	SetSize(sizer->ComputeFittingWindowSize(this));

	// We don't want to restrict shrinking the window vertically as it might be
	// too tall (see SetInitialSize() call in the canvas ctor), but we do
	// want to make it at least as wide as the control bar, as otherwise the
	// buttons wouldn't fit, and restricting it to at least its height
	// vertically is also quite reasonable.
	SetSizeHints(ClientToWindowSize(m_controlBar->GetBestSize()));

	SetWindowModality(wxWindowMode::AppModal);

	m_printPreview->AdjustScrollbars();
	m_previewCanvas->SetFocus();
	m_controlBar->SetFocus();
}

void ibPrintPreviewFrame::OnChar(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_ESCAPE)
		Close(true);
	else
		event.Skip();
}
