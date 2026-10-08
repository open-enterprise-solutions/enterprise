/////////////////////////////////////////////////////////////////////////////
// Name:        frontend/docView/print/printPreview.cpp
// Purpose:     the print preview window, and its bar with "Fit to page width"
/////////////////////////////////////////////////////////////////////////////

#include "printPreview.h"

#include <wx/checkbox.h>
#include <wx/sizer.h>

#include <functional>

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

// wx's own bar, with the box added before the Close button that ends it. Shown only for a printout that
// can be fitted: a box that changes nothing is a promise the page does not keep.
class ibPrintPreviewControlBar : public wxPreviewControlBar {
public:

	ibPrintPreviewControlBar(wxPrintPreviewBase* preview, long buttons, wxWindow* parent,
		bool canFitToPageWidth, bool fitToPageWidth, const std::function<void(bool)>& onFitToPageWidth)
		: wxPreviewControlBar(preview, buttons, parent),
		m_canFitToPageWidth(canFitToPageWidth), m_fitToPageWidth(fitToPageWidth), m_onFitToPageWidth(onFitToPageWidth)
	{
	}

	virtual void CreateButtons() override
	{
		wxPreviewControlBar::CreateButtons();

		if (!m_canFitToPageWidth)
			return;

		wxCheckBox* box = new wxCheckBox(this, wxID_ANY, _("Fit to page width"));
		box->SetValue(m_fitToPageWidth);
		box->SetToolTip(_("Shrink a table wider than the page so that it is printed on one page across"));
		box->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) { m_onFitToPageWidth(event.IsChecked()); });

		// The bar ends [..., gap, stretch, Close] (wxPreviewControlBar::CreateButtons, AddAtEnd): the box
		// goes in front of that gap, beside the zoom, and Close stays at the right edge.
		wxSizer* sizer = GetSizer();
		const size_t count = sizer->GetItemCount();
		const size_t at = count >= 3 ? count - 3 : count;

		sizer->Insert(at, box, wxSizerFlags().Border(wxLEFT | wxTOP | wxBOTTOM).Center());
		sizer->InsertSpacer(at, wxRound(2 * wxSizerFlags::GetDefaultBorderFractional()));
	}

private:
	const bool m_canFitToPageWidth;
	const bool m_fitToPageWidth;
	const std::function<void(bool)> m_onFitToPageWidth;
};

} // namespace

wxPrintout* ibPrintPreviewFrame::ApplyFitToPageWidth(wxPrintout* printout)
{
	return FitPrintoutToPageWidth(printout, s_fitToPageWidth);
}

ibPrintPreviewFrame::ibPrintPreviewFrame(wxPrintPreviewBase* preview, wxWindow* parent, const wxString& title)
	: wxPreviewFrame(preview, parent, title)
{
	// The session's last choice, on the printouts made before this window - nothing has been paginated yet
	// (that waits for the first page to be rendered), so telling them now is in time.
	FitPrintoutToPageWidth(preview->GetPrintout(), s_fitToPageWidth);
	FitPrintoutToPageWidth(preview->GetPrintoutForPrinting(), s_fitToPageWidth);
}

void ibPrintPreviewFrame::CreateControlBar()
{
	long buttons = wxPREVIEW_DEFAULT;
	if (m_printPreview->GetPrintoutForPrinting() != nullptr)
		buttons |= wxPREVIEW_PRINT;

	const ibFitToPageWidthPrintout* fits = dynamic_cast<const ibFitToPageWidthPrintout*>(m_printPreview->GetPrintout());

	// ⚠ LATER, AND ON THE WINDOW: the tick arrives inside the box's own event, and a refit destroys the bar
	// the box stands on. Queued on the frame, it runs once that event has returned.
	m_controlBar = new ibPrintPreviewControlBar(m_printPreview, buttons, this,
		fits != nullptr, fits != nullptr && fits->IsFitToPageWidth(),
		[this](bool fit) { CallAfter([this, fit]() { SetFitToPageWidth(fit); }); });

	m_controlBar->CreateButtons();
}

void ibPrintPreviewFrame::SetFitToPageWidth(bool fit)
{
	s_fitToPageWidth = fit;

	const ibFitToPageWidthPrintout* shown = dynamic_cast<const ibFitToPageWidthPrintout*>(m_printPreview->GetPrintout());
	if (shown == nullptr)
		return;

	// The same pair OnPreview made - one printout to show, one for the Print button when there was one -
	// as twins of the one on the screen, and the print settings the preview was opened with.
	wxPrintout* toShow = FitPrintoutToPageWidth(shown->Clone(), fit);
	wxPrintout* toPrint = m_printPreview->GetPrintoutForPrinting() != nullptr
		? FitPrintoutToPageWidth(shown->Clone(), fit) : nullptr;

	wxPrintDialogData printDialogData(m_printPreview->GetPrintDialogData());
	wxPrintPreview* fresh = new wxPrintPreview(toShow, toPrint, &printDialogData);
	if (!fresh->IsOk()) {
		delete fresh;   // …and the two printouts with it
		return;
	}

	fresh->SetZoom(m_printPreview->GetZoom());

	// Out with the old - the canvas and the bar first, both hold the preview, then the preview, which
	// deletes both of its printouts (wxPrintPreviewBase's destructor).
	wxSizer* sizer = GetSizer();
	sizer->Detach(m_controlBar);
	sizer->Detach(m_previewCanvas);
	m_controlBar->Destroy();
	m_previewCanvas->Destroy();
	delete m_printPreview;

	// In with the new, the way wxPreviewFrame::InitializeWithModality puts them: bar above, canvas below.
	m_printPreview = fresh;

	CreateCanvas();
	CreateControlBar();

	fresh->SetCanvas(m_previewCanvas);
	fresh->SetFrame(this);

	sizer->Insert(0, m_controlBar, wxSizerFlags().Expand());
	sizer->Insert(1, m_previewCanvas, wxSizerFlags(1).Expand());
	Layout();

	fresh->AdjustScrollbars(m_previewCanvas);
	m_previewCanvas->Refresh();
	m_controlBar->SetFocus();
}
