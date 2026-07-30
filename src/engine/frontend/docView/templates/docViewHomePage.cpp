////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : home page — the composite doc/view (N runtime forms, one tab)
////////////////////////////////////////////////////////////////////////////

#include "docViewHomePage.h"

#include <wx/splitter.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/icon.h>

#include "frontend/mainFrame/mainFrame.h"                  // IsClosingWindow — window teardown vs a tab's [x]
#include "frontend/visualView/ctrl/form.h"
#include "frontend/visualView/visualHostClient.h"

#include "backend/metadataConfiguration.h"                 // activeMetaData + ibValueMetaObjectConfiguration
#include "backend/metaCollection/metaObjectMetadata.h"     // GetHomePage()
#include "backend/metaCollection/metaFormObject.h"         // ibValueMetaObjectFormBase / ibValueMetaObjectCommonForm
#include "backend/metaCollection/genericData.h"            // ibValueMetaObjectGenericData::CreateObjectForm
#include "backend/system/systemManager.h"                  // ibValueSystemFunction::Message (a form that refuses to build)
#include "backend/backend_exception.h"                     // ibBackendException — a cell that fails must not take the page down

#include <memory>

wxIMPLEMENT_DYNAMIC_CLASS(ibHomePageDocument, ibDocument);
wxIMPLEMENT_DYNAMIC_CLASS(ibHomePageView, ibView);

//********************************************************************************************
//*                                       Document                                           *
//********************************************************************************************

ibHomePageDocument* ibHomePageDocument::ms_instance = nullptr;

ibHomePageDocument::ibHomePageDocument() : ibDocument()
{
	m_documentModified = false;

	// The workspace is read ONCE, here — the tab is a running layout of live forms from this
	// point on (see GetDescription).
	if (const ibValueMetaObjectConfiguration* commonObject =
		activeMetaData != nullptr ? activeMetaData->GetCommonMetaObject() : nullptr) {
		m_description = commonObject->GetHomePage();
	}

	ms_instance = this;
}

ibHomePageDocument::~ibHomePageDocument()
{
	if (ms_instance == this)
		ms_instance = nullptr;
}

ibHomePageDocument* ibHomePageDocument::ShowHomePage()
{
	if (ms_instance != nullptr) {
		ms_instance->Activate();
		return ms_instance;
	}

	ibDocManager* const documentManager = ibDocManager::GetDocumentManager();
	if (documentManager == nullptr)
		return nullptr;

	ibHomePageDocument* homeDoc = documentManager->CreateDocument<ibHomePageDocument>();
	if (homeDoc == nullptr)
		return nullptr;

	// Nothing to SHOW — no tab at all. A configuration that does not use the start page (or
	// that switched every attachment off) should not pay an empty one. The question is asked
	// of the shown items, not of the stored ones: an invisible attachment renders nothing.
	if (homeDoc->GetDescription().GetShownItems(eHomePageColumn_Left).empty() &&
		homeDoc->GetDescription().GetShownItems(eHomePageColumn_Right).empty()) {
		wxDELETE(homeDoc);
		return nullptr;
	}

	homeDoc->SetTitle(_("Home page"));
	homeDoc->SetIcon(ibBackendPicture::GetPictureAsIcon(g_picHomePageCLSID));
	documentManager->AddDocument(homeDoc);

	if (!homeDoc->OnCreate(wxEmptyString, 0)) {
		homeDoc->DeleteAllViews();
		return nullptr;
	}

	homeDoc->LockPageTab();

	return homeDoc;
}

bool ibHomePageDocument::Close()
{
	// The page is going down and takes its forms with it — the ONE stretch where a pane's form
	// may close. Raised BEFORE the base call, because the cascade closes the children first and
	// only then reaches OnCloseDocument. A refused close (the page is not closable by hand)
	// lowers it again, so the panes are locked the moment the page stays.
	m_closingChildren = true;
	const bool closed = ibDocument::Close();
	m_closingChildren = closed;

	return closed;
}

ibFrontendWindow* ibHomePageDocument::GetChildDocumentWindow(const ibDocument* child) const
{
	// The page's view owns the splitter tree, so it is the one that knows which pane a child
	// belongs in. Reached through the doc PARENT link — that link IS the composition.
	const ibHomePageView* const view = wxDynamicCast(GetFirstView(), ibHomePageView);
	return view != nullptr ? view->GetCellWindow(child) : nullptr;
}

void ibHomePageDocument::LockPageTab()
{
	// The start page is a LOCKED tab. wx keeps locked pages ahead of every normal one (a new
	// tab can only be inserted after them) and lets neither a drag nor the close button reach
	// them — so "always first, never movable, never closed by hand" becomes the notebook's own
	// rule instead of a handful of vetoes. Runs after OnCreate, because the page joins the
	// notebook in ShowFrame at the end of it.
	ibFrontendMainFrame* const shell = ibFrontendMainFrame::GetFrame();
	wxAuiMDIClientWindow* const clientWindow = shell != nullptr ? shell->GetClientWindow() : nullptr;
	if (clientWindow == nullptr)
		return;

	const ibHomePageView* const view = wxDynamicCast(GetFirstView(), ibHomePageView);
	wxWindow* const pageWindow = view != nullptr ? view->GetFrame() : nullptr;
	if (pageWindow == nullptr)
		return;

	const int pageIndex = clientWindow->GetPageIndex(pageWindow);
	if (pageIndex != wxNOT_FOUND)
		clientWindow->SetPageKind((size_t)pageIndex, wxAuiTabKind::Locked);
}

//********************************************************************************************
//*                                         View                                             *
//********************************************************************************************

namespace {

// An item's share of its column. 0 (the designer's "same height") counts as one share, so a
// column of unset items splits evenly — which is what "same" means.
inline unsigned int ItemWeight(const ibHomePageItem& item)
{
	return item.m_height != 0 ? item.m_height : 1;
}

unsigned int TotalWeight(const std::vector<ibHomePageItem>& items, size_t from)
{
	unsigned int total = 0;
	for (size_t idx = from; idx < items.size(); idx++)
		total += ItemWeight(items[idx]);
	return total;
}

// Place a splitter's sash at `ratio` of its size ONCE, on the first layout that gives it a
// real size — the tree is built before the frame is shown, so the size is not known yet. The
// gravity keeps the ratio through later resizes; the one-shot flag keeps a user's drag.
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
		splitter->SetSashPosition((int)(total * ratio));
	});
}

} // namespace

void ibHomePageView::BuildCellHeader(wxWindow* pane, wxSizer* paneSizer,
	const ibValueMetaObjectFormBase* metaForm, const wxString& title)
{
	wxBoxSizer* const headerSizer = new wxBoxSizer(wxHORIZONTAL);

	// The icon of the OWNER — a catalog's form reads as that catalog, a document's as that
	// document — falling back to the form's own, and to the page's glyph when the form is gone.
	wxIcon headerIcon;
	if (metaForm != nullptr) {
		const ibValueMetaObject* const owner = metaForm->GetParent();
		headerIcon = owner != nullptr ? owner->GetIcon() : metaForm->GetIcon();
		if (!headerIcon.IsOk())
			headerIcon = metaForm->GetIcon();
	}
	if (!headerIcon.IsOk())
		headerIcon = ibBackendPicture::GetPictureAsIcon(g_picHomePageCLSID);

	if (headerIcon.IsOk()) {
		wxBitmap iconBitmap;
		iconBitmap.CopyFromIcon(headerIcon);
		headerSizer->Add(new wxStaticBitmap(pane, wxID_ANY, iconBitmap), 0,
			wxALIGN_CENTER_VERTICAL | wxRIGHT, pane->FromDIP(3));
	}

	// The strip is a LABEL, not a title bar: it must not eat height the form could use. The
	// icon (16px) is the floor, so the caption is kept a point below the default and every
	// margin around the row is minimal.
	wxStaticText* const caption = new wxStaticText(pane, wxID_ANY, title);
	wxFont captionFont = caption->GetFont();
	captionFont.MakeBold();
	if (captionFont.GetPointSize() > 7)
		captionFont.SetPointSize(captionFont.GetPointSize() - 1);
	caption->SetFont(captionFont);
	headerSizer->Add(caption, 1, wxALIGN_CENTER_VERTICAL);

	paneSizer->Add(headerSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pane->FromDIP(2));
	paneSizer->Add(new wxStaticLine(pane, wxID_ANY), 0, wxEXPAND | wxTOP, pane->FromDIP(1));
}

wxWindow* ibHomePageView::OpenCell(wxWindow* parent, const ibHomePageItem& item, ibHomePageDocument* homeDoc)
{
	// THE pane — this is how the page houses one child: its own window, a header saying what
	// lives here, and the form's host filling the rest. The pane is what the splitter divides
	// and what the child gets as its frame.
	wxPanel* const pane = new wxPanel(parent, wxID_ANY);
	wxBoxSizer* const paneSizer = new wxBoxSizer(wxVERTICAL);
	pane->SetSizer(paneSizer);

	const ibValueMetaObjectFormBase* const metaForm = FindItemForm(item);
	ibValueForm* const valueForm = CreateFormValue(metaForm);

	ibHomePageCell cell;
	cell.m_item = item;
	cell.m_window = pane;

	if (valueForm != nullptr) {
		// The form opens as a CHILD DOCUMENT of the page and asks the page where to render;
		// the pane being filled right now (m_openingPane) is the answer. Nothing is passed
		// down the form side — the composition is the doc parent.
		m_openingPane = pane;
		const bool opened = valueForm->ShowForm(homeDoc);
		m_openingPane = nullptr;

		if (opened) {
			ibFormVisualDocument* const formDoc = valueForm->GetVisualDocument();
			ibFormVisualEditView* const formView = formDoc != nullptr ? formDoc->GetFirstView() : nullptr;
			ibVisualHostClient* const host = formView != nullptr ? formView->GetVisualHost() : nullptr;

			if (host != nullptr) {
				// The SHOWN title — GetControlTitle falls back to the object's / form's
				// synonym, which is what a tab would have displayed.
				BuildCellHeader(pane, paneSizer, metaForm, valueForm->GetControlTitle());
				paneSizer->Add(host, 1, wxEXPAND | wxTOP, pane->FromDIP(1));

				cell.m_formDoc = formDoc;
				cell.m_valueForm = valueForm;
				m_cells.push_back(cell);

				pane->Layout();
				return pane;
			}
		}
	}

	// The form is gone or refused to open. The pane still stands — a splitter cannot hold a
	// hole — and says what happened.
	BuildCellHeader(pane, paneSizer, metaForm,
		metaForm != nullptr ? metaForm->GetFullName() : _("<not found>"));
	paneSizer->AddStretchSpacer();
	paneSizer->Add(new wxStaticText(pane, wxID_ANY, _("Form is not available"),
		wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL), 0, wxEXPAND);
	paneSizer->AddStretchSpacer();

	m_cells.push_back(cell);
	pane->Layout();

	return pane;
}

wxWindow* ibHomePageView::BuildColumn(wxWindow* parent, const std::vector<ibHomePageItem>& items, size_t from,
	ibHomePageDocument* homeDoc)
{
	if (from >= items.size())
		return nullptr;

	// The last item of a column owns the rest of it — no sash below it.
	if (from + 1 == items.size())
		return OpenCell(parent, items[from], homeDoc);

	wxSplitterWindow* splitter = new wxSplitterWindow(parent, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_THIN_SASH | wxSP_NOBORDER);
	splitter->SetMinimumPaneSize(splitter->FromDIP(40));

	// Both panes are built with the SPLITTER as their parent, then the split just declares
	// which is which — so a form's window IS a pane, with no wrapper in between.
	wxWindow* const first = OpenCell(splitter, items[from], homeDoc);
	wxWindow* const rest = BuildColumn(splitter, items, from + 1, homeDoc);

	splitter->SplitHorizontally(first, rest);
	ApplySashRatio(splitter, (double)ItemWeight(items[from]) / (double)TotalWeight(items, from));

	return splitter;
}

bool ibHomePageView::OnCreate(ibDocument* doc, long flags)
{
	ibHomePageDocument* const homeDoc = wxDynamicCast(doc, ibHomePageDocument);
	if (homeDoc == nullptr || m_viewFrame == nullptr)
		return false;

	const ibHomePageDescription& description = homeDoc->GetDescription();

	const std::vector<ibHomePageItem> leftItems = description.GetShownItems(eHomePageColumn_Left);
	const std::vector<ibHomePageItem> rightItems = description.GetShownItems(eHomePageColumn_Right);

	wxWindow* const frame = m_viewFrame;

	// The page's OWN frame is the ground everything is inserted into: the splitter tree is
	// built in it, and the attached forms render into its panes.
	if (description.IsTwoColumns() && !leftItems.empty() && !rightItems.empty()) {
		wxSplitterWindow* columns = new wxSplitterWindow(frame, wxID_ANY,
			wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_THIN_SASH | wxSP_NOBORDER);
		columns->SetMinimumPaneSize(columns->FromDIP(80));
		columns->SplitVertically(
			BuildColumn(columns, leftItems, 0, homeDoc),
			BuildColumn(columns, rightItems, 0, homeDoc));
		ApplySashRatio(columns, description.GetColumnGravity());
		m_workspace = columns;
	}
	else {
		// One column — either by template, or because the other column has nothing shown.
		const std::vector<ibHomePageItem>& single = leftItems.empty() ? rightItems : leftItems;
		m_workspace = BuildColumn(frame, single, 0, homeDoc);
	}

	if (m_workspace == nullptr)
		return false;

	// The child frame hosts exactly one child; a sizer makes that explicit instead of relying
	// on the frame's single-child auto-layout.
	wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
	frameSizer->Add(m_workspace, 1, wxEXPAND);
	frame->SetSizer(frameSizer);
	frame->Layout();

	return true;
}

const ibValueMetaObjectFormBase* ibHomePageView::FindItemForm(const ibHomePageItem& item) const
{
	ibMetaDataConfigurationBase* const metaData = activeMetaData;
	if (metaData == nullptr)
		return nullptr;

	// The attached form is addressed by metaId; the clsid filter keeps the typed find honest
	// (a common form and an object form are both form metaobjects, nothing else is).
	return metaData->FindAnyObjectByFilter<ibValueMetaObjectFormBase>(item.m_formId,
		{ g_metaCommonFormCLSID, g_metaFormCLSID }, true);
}

ibValueForm* ibHomePageView::CreateFormValue(const ibValueMetaObjectFormBase* metaForm) const
{
	if (metaForm == nullptr)
		return nullptr;

	ibBackendValueForm* backendForm = nullptr;

	// An element PLACED on the start page gets an identity of its OWN — a fresh form guid.
	// Without it a form's identity falls back to its source, and a source is not always one per
	// instance: every dynamic list over the same object shares the table's guid. Two such forms
	// would then be "the same form", and opening the list from the menu would merely activate
	// the page's copy — a pane, so nothing would appear. It is also what lets the same form sit
	// on the page twice. Nothing outside the page is affected: an object still finds ITS form
	// by source (ibValueRecordDataObject::GetForm).
	const ibUniqueKey formKey = wxNewUniqueGuid;

	try {
		// One verb, answered by the form's own kind — an object form asks the object that owns
		// it, a common form stands alone, and the access right comes back with the same call.
		// No branch here, no cast: the page does not care which kind it is holding.
		backendForm = metaForm->GetObjectForm(nullptr, formKey);
	}
	catch (const ibBackendException& err) {
		// A form that refuses to build (access denied, a broken source) must not take the whole
		// start page down with it — the cell reports it and the others still open.
		ibValueSystemFunction::Message(err.GetErrorDescription(), ibStatusMessage::ibStatusMessage_Error);
		return nullptr;
	}

	return dynamic_cast<ibValueForm*>(backendForm);
}

ibFrontendWindow* ibHomePageView::GetCellWindow(const ibDocument* childDoc) const
{
	// Asked DURING an open: the child document is still being built, so it is not in any cell
	// yet — the pane it is being opened into answers.
	if (m_openingPane != nullptr)
		return m_openingPane;

	// Asked later (the close rules): match the child to the cell it filled.
	for (const ibHomePageCell& cell : m_cells) {
		if (cell.m_formDoc != nullptr && cell.m_formDoc == childDoc)
			return cell.m_window;
	}

	return nullptr;
}

void ibHomePageView::OnUpdate(ibView* WXUNUSED(sender), wxObject* WXUNUSED(hint))
{
	for (ibHomePageCell& cell : m_cells) {
		if (cell.m_valueForm != nullptr)
			cell.m_valueForm->UpdateForm();
	}
}

bool ibHomePageDocument::OnSaveModified()
{
	// Not about saving — this is the gate ibDocument::CanClose consults, and the start page
	// uses it to say "not by hand". The WINDOW closing its documents (AllowClose / Destroy)
	// is let through, or the application could never exit. File → Close All therefore closes
	// every other tab and leaves this one standing, which is exactly right.
	const ibFrontendMainFrame* const shell = ibFrontendMainFrame::GetFrame();
	return shell == nullptr || shell->IsClosingWindow();
}

bool ibHomePageView::OnClose(bool deleteWindow)
{
	// No refusal here: the "may I close" question is answered by the document
	// (ibHomePageDocument::OnSaveModified), before any teardown starts.

	// The attached forms are CHILD documents — the document cascade closes them; the view only
	// drops its own references and its window.
	m_cells.clear();
	m_workspace = nullptr;

	if (deleteWindow && GetFrame() != nullptr) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	return ibView::OnClose(deleteWindow);
}
