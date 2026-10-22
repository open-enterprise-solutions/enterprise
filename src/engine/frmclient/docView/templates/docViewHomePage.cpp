#include "docViewHomePage.h"

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>

#include <memory>   // std::make_shared — a sash placed once

#include "frmclient/mainFrame/mainFrame.h"   // the window a cell's form talks to the server through
#include "frmclient/win/picture.h"           // ibProtocolPicture — a cell's icon

namespace {

// A cell's share of its column — the server's Height, one at the least.
long long CellWeight(const ibProtocolNode& cell)
{
	const long long height = cell.GetInt(ibProtocol::ibView::ibHomePage::Height);
	return height > 0 ? height : 1;
}

long long TotalWeight(const std::vector<ibProtocolNode>& cells, size_t from)
{
	long long total = 0;
	for (size_t idx = from; idx < cells.size(); idx++)
		total += CellWeight(cells[idx]);
	return total;
}

// Place a splitter's sash at `ratio` of its size ONCE, on the first layout that gives it a real size — the tree is
// built before the page is shown, so the size is not known yet. The gravity keeps the ratio through later resizes; the
// one-shot flag keeps a person's drag.
void ApplySashRatio(wxSplitterWindow* splitter, double ratio)
{
	splitter->SetSashGravity(ratio);

	auto applied = std::make_shared<bool>(false);
	splitter->Bind(wxEVT_SIZE, [splitter, ratio, applied](wxSizeEvent& event) {
		event.Skip();
		if (*applied)
			return;
		const wxSize size = event.GetSize();
		const int total = splitter->GetSplitMode() == wxSPLIT_HORIZONTAL ? size.y : size.x;
		if (total < 2 * splitter->GetMinimumPaneSize())
			return;   // not laid out yet — wait for the layout that is
		*applied = true;
		splitter->SetSashPosition(static_cast<int>(total * ratio));
	});
}

// The part of a patch a child of the view is drawn by — the child named so, or the one of that id among its children:
// none (no node) when the patch is none — the view was sent whole, and so is the child; a node with nothing in it when
// the patch does not name the child — it is as it was.
ibProtocolNode ChildPatch(const ibProtocolNode& patch, const char* name)
{
	if (!patch.IsNode())
		return patch;
	return patch.Has(name) ? patch.FindChild(name) : ibProtocolNode();
}

ibProtocolNode ChildPatch(const ibProtocolNode& patch, long long id)
{
	if (!patch.IsNode())
		return patch;
	for (const ibProtocolNode& changed : patch.Children()) {
		if (changed.GetId() == id)
			return changed;
	}
	return ibProtocolNode();
}

// A patch that changes nothing — a node with nothing in it.
bool IsUnchanged(const ibProtocolNode& patch)
{
	return patch.IsNode() && patch.Entries().empty();
}

} // namespace

//********************************************************************************************
//*                                         View                                             *
//********************************************************************************************

void ibHomePageView::Draw(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	std::vector<ibProtocolNode> columns;
	for (const ibProtocolNode& column : view.FindChild(ibProtocol::ibView::ibHomePage::Columns).Children()) {
		if (!column.Children().empty())
			columns.push_back(column);
	}
	if (columns.empty())
		return;

	// ⭐ LAID OUT ONCE — the server's page reads its workspace and opens its forms as it opens, and keeps them for its
	// life (frmserver's ibHomePageDocument): every view it draws after has the same columns and the same cells.
	if (m_workspace == nullptr) {
		if (columns.size() > 1) {
			wxSplitterWindow* const splitter = new wxSplitterWindow(m_viewFrame, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_THIN_SASH | wxSP_NOBORDER);
			splitter->SetMinimumPaneSize(splitter->FromDIP(80));
			wxWindow* const left = BuildColumn(splitter, columns[0].Children(), 0);
			wxWindow* const right = BuildColumn(splitter, columns[1].Children(), 0);
			splitter->SplitVertically(left, right);

			// The left column's share of the width — the template's.
			double gravity = 0.5;
			const wxString gravityText = view.GetString(ibProtocol::ibView::ibHomePage::Gravity);
			if (!gravityText.ToCDouble(&gravity))
				gravity = 0.5;
			ApplySashRatio(splitter, gravity);
			m_workspace = splitter;
		}
		else {
			m_workspace = BuildColumn(m_viewFrame, columns[0].Children(), 0);
		}

		wxBoxSizer* const frameSizer = new wxBoxSizer(wxVERTICAL);
		frameSizer->Add(m_workspace, 1, wxEXPAND);
		m_viewFrame->SetSizer(frameSizer);
		m_viewFrame->Layout();
	}

	// Each cell by what the patch changed of it — its title, its form — in the order the cells were laid out.
	const ibProtocolNode columnsPatch = ChildPatch(patch, ibProtocol::ibView::ibHomePage::Columns);
	std::size_t index = 0;
	for (const ibProtocolNode& column : columns) {
		const ibProtocolNode columnPatch = ChildPatch(columnsPatch, column.GetId());
		for (const ibProtocolNode& cell : column.Children()) {
			if (index >= m_cells.size())
				return;
			const ibHomePageCell& drawn = m_cells[index++];
			const ibProtocolNode cellPatch = ChildPatch(columnPatch, cell.GetId());
			if (IsUnchanged(cellPatch))
				continue;

			const wxString title = cell.GetString(ibProtocol::ibView::ibHomePage::Title);
			if (drawn.m_caption->GetLabel() != title)
				drawn.m_caption->SetLabel(title);

			const ibProtocolNode viewPatch = ChildPatch(cellPatch, ibProtocol::ibView::ibHomePage::View);
			if (drawn.m_host != nullptr && !IsUnchanged(viewPatch))
				drawn.m_host->Draw(cell.FindChild(ibProtocol::ibView::ibHomePage::View), viewPatch);
		}
	}
}

wxWindow* ibHomePageView::BuildColumn(wxWindow* parent, const std::vector<ibProtocolNode>& cells, size_t from)
{
	if (from >= cells.size())
		return nullptr;

	// The last cell of a column owns the rest of it — no sash below it.
	if (from + 1 == cells.size())
		return OpenCell(parent, cells[from]);

	wxSplitterWindow* const splitter = new wxSplitterWindow(parent, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_THIN_SASH | wxSP_NOBORDER);
	splitter->SetMinimumPaneSize(splitter->FromDIP(40));

	// Both panes are built with the SPLITTER as their parent, then the split just declares which is which.
	wxWindow* const first = OpenCell(splitter, cells[from]);
	wxWindow* const rest = BuildColumn(splitter, cells, from + 1);

	splitter->SplitHorizontally(first, rest);
	ApplySashRatio(splitter, static_cast<double>(CellWeight(cells[from])) / static_cast<double>(TotalWeight(cells, from)));

	return splitter;
}

wxWindow* ibHomePageView::OpenCell(wxWindow* parent, const ibProtocolNode& cell)
{
	// THE pane — how the page houses one form: its own window, a header saying what lives here, and the form's host
	// filling the rest.
	wxPanel* const pane = new wxPanel(parent, wxID_ANY);
	wxBoxSizer* const paneSizer = new wxBoxSizer(wxVERTICAL);
	pane->SetSizer(paneSizer);

	ibHomePageCell drawn;
	drawn.m_caption = BuildCellHeader(pane, paneSizer, cell);

	if (cell.Has(ibProtocol::ibView::ibHomePage::View)) {
		// What the person does in it goes to the server's form by the form's key — the view it draws carries it.
		const ibTabDocument* const document = static_cast<const ibTabDocument*>(GetDocument());
		drawn.m_host = new ibVisualHostClient(*ibFrontendMainFrame::GetFrame(), pane, document->GetTabId());
		paneSizer->Add(drawn.m_host, 1, wxEXPAND | wxTOP, pane->FromDIP(1));
	}
	else {
		// The form is gone or refused to open. The pane still stands — a splitter cannot hold a hole — and says what
		// happened.
		paneSizer->AddStretchSpacer();
		paneSizer->Add(new wxStaticText(pane, wxID_ANY, cell.GetString(ibProtocol::ibView::ibHomePage::Unavailable),
			wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL), 0, wxEXPAND);
		paneSizer->AddStretchSpacer();
	}

	m_cells.push_back(drawn);
	pane->Layout();

	return pane;
}

ibVisualHostClient* ibHomePageView::FindVisualHost(wxWindow* window) const
{
	for (const ibHomePageCell& cell : m_cells) {
		if (cell.m_host != nullptr && (cell.m_host == window || cell.m_host->IsDescendant(window)))
			return cell.m_host;
	}
	return nullptr;
}

wxStaticText* ibHomePageView::BuildCellHeader(wxWindow* pane, wxSizer* paneSizer, const ibProtocolNode& cell)
{
	wxBoxSizer* const headerSizer = new wxBoxSizer(wxHORIZONTAL);

	// The icon the server named — the OWNER's (a catalog's form reads as that catalog, a document's as that document),
	// the form's own, or the page's glyph when the form is gone.
	const wxBitmap icon = ibProtocolPicture(cell.GetString(ibProtocol::ibView::ibHomePage::Icon));
	if (icon.IsOk()) {
		headerSizer->Add(new wxStaticBitmap(pane, wxID_ANY, icon), 0,
			wxALIGN_CENTER_VERTICAL | wxRIGHT, pane->FromDIP(3));
	}

	// The strip is a LABEL, not a title bar: it must not eat height the form could use. The icon (16px) is the floor, so
	// the caption is kept a point below the default and every margin around the row is minimal.
	wxStaticText* const caption = new wxStaticText(pane, wxID_ANY, cell.GetString(ibProtocol::ibView::ibHomePage::Title));
	wxFont captionFont = caption->GetFont();
	captionFont.MakeBold();
	if (captionFont.GetPointSize() > 7)
		captionFont.SetPointSize(captionFont.GetPointSize() - 1);
	caption->SetFont(captionFont);
	headerSizer->Add(caption, 1, wxALIGN_CENTER_VERTICAL);

	paneSizer->Add(headerSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pane->FromDIP(2));
	paneSizer->Add(new wxStaticLine(pane, wxID_ANY), 0, wxEXPAND | wxTOP, pane->FromDIP(1));

	return caption;
}

//********************************************************************************************
//*                                       Document                                           *
//********************************************************************************************

ibHomePageDocument::ibHomePageDocument(long long tabId)
	: ibTabDocument(tabId)
{
}

void ibHomePageDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (ibHomePageView* const homeView = GetFirstView())
		homeView->Draw(view, patch);
}

wxCommandProcessor* ibHomePageDocument::GetCommandProcessor() const
{
	const ibHomePageView* const homeView = GetFirstView();
	if (const ibFrontendView* const view = homeView != nullptr ? homeView->GetActiveControlView() : nullptr)
		return view->GetDocument()->GetCommandProcessor();
	return ibFrontendDocument::GetCommandProcessor();
}

ibHomePageView* ibHomePageDocument::GetFirstView() const
{
	return static_cast<ibHomePageView*>(ibFrontendDocument::GetFirstView());
}

ibFrontendView* ibHomePageDocument::DoCreateView()
{
	return new ibHomePageView();
}
