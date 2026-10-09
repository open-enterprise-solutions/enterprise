////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : home page — the composite doc/view (N runtime forms, one tab)
////////////////////////////////////////////////////////////////////////////

#include "docViewHomePage.h"

#include "frmserver/client/clientFrame.h"                 // IsClosingWindow — the client leaving vs a tab's cross
#include "frmserver/visualView/visualHost.h"

#include "backend/backend_picture.h"                       // a cell's header icon, as it travels
#include "core/fnumber.h"                               // the column's share, a fraction
#include "backend/metaCollection/metaObjectMetadata.h"     // GetHomePage()
#include "backend/metaCollection/metaFormObject.h"         // ibValueMetaObjectFormBase / ibValueMetaObjectCommonForm
#include "backend/metaData.h"                              // FindAnyObjectByFilter
#include "backend/backend_exception.h"                     // ibBackendException — a cell that fails must not take the page down

wxIMPLEMENT_DYNAMIC_CLASS(ibHomePageDocument, ibDocument);
wxIMPLEMENT_DYNAMIC_CLASS(ibHomePageView, ibView);

//********************************************************************************************
//*                                       Document                                           *
//********************************************************************************************

ibHomePageDocument::ibHomePageDocument(const ibValueMetaObjectConfiguration* configuration)
	: ibDocument(), m_configuration(configuration)
{
	m_documentModified = false;

	// The workspace is read ONCE, here — the tab is a running layout of live forms from this point on (see
	// GetDescription).
	if (m_configuration != nullptr)
		m_description = m_configuration->GetHomePage();
}

ibHomePageDocument::~ibHomePageDocument()
{
}

const ibMetaData* ibHomePageDocument::GetMetaData() const
{
	return m_configuration != nullptr ? m_configuration->GetMetaData() : nullptr;
}

ibHomePageDocument* ibHomePageDocument::ShowHomePage(const ibValueMetaObjectConfiguration* configuration)
{
	ibDocManager* const documentManager = ibDocManager::GetDocumentManager();
	if (documentManager == nullptr)
		return nullptr;

	// One home page per client — its manager's documents say whether it is open already.
	for (ibDocument* document : documentManager->GetDocumentsVector()) {
		if (ibHomePageDocument* const openDoc = dynamic_cast<ibHomePageDocument*>(document)) {
			openDoc->Activate();
			return openDoc;
		}
	}

	ibHomePageDocument* homeDoc = documentManager->CreateDocument<ibHomePageDocument>(configuration);
	if (homeDoc == nullptr)
		return nullptr;

	// Nothing to SHOW — no tab at all. A configuration that does not use the start page (or that switched every
	// attachment off) should not pay an empty one. The question is asked of the shown items, not of the stored
	// ones: an invisible attachment renders nothing.
	if (homeDoc->GetDescription().GetShownItems(eHomePageColumn_Left).empty() &&
		homeDoc->GetDescription().GetShownItems(eHomePageColumn_Right).empty()) {
		wxDELETE(homeDoc);
		return nullptr;
	}

	homeDoc->SetTitle(_("Home page"));
	homeDoc->SetIcon(ibBackendPicture::GetServerPicture(g_picHomePageCLSID));
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
	// The page is going down and takes its forms with it — the ONE stretch where a cell's form may close. Raised
	// BEFORE the base call, because the cascade closes the children first and only then reaches OnCloseDocument. A
	// refused close (the page is not closable by hand) lowers it again, so the cells are locked the moment the page
	// stays.
	m_closingChildren = true;
	const bool closed = ibDocument::Close();
	m_closingChildren = closed;

	return closed;
}

ibDocChildFrameAnyBase* ibHomePageDocument::GetChildDocumentWindow(const ibDocument* WXUNUSED(child)) const
{
	// Every child of the page shows in the page's own tab: which cell it fills is the page's view's to draw.
	// Reached through the doc PARENT link — that link IS the composition.
	const ibView* const view = GetFirstView();
	return view != nullptr ? view->GetFrame() : nullptr;
}

void ibHomePageDocument::LockPageTab()
{
	// The start page is a LOCKED tab: the client's frame keeps it ahead of every normal one and lets no close
	// reach it — so "always first, never closed by hand" is the frame's own rule instead of a handful of vetoes.
	// Runs after OnCreate, because the page's tab is made in it.
	ibClientFrame* const frame = ibClientFrame::GetFrame();
	const ibView* const view = GetFirstView();
	if (frame != nullptr && view != nullptr)
		frame->LockTab(view->GetDocChildFrame());
}

bool ibHomePageDocument::OnSaveModified()
{
	// Not about saving — this is the gate ibDocument::CanClose consults, and the start page uses it to say "not by
	// hand". The client's FRAME closing its documents (the client leaving) is let through, or the client could
	// never go.
	const ibClientFrame* const frame = ibClientFrame::GetFrame();
	return frame == nullptr || frame->IsClosingWindow();
}

//********************************************************************************************
//*                                         View                                             *
//********************************************************************************************

namespace {

// An item's share of its column. 0 (the designer's "same height") counts as one share, so a column of unset items
// splits evenly — which is what "same" means.
inline unsigned int ItemWeight(const ibHomePageItem& item)
{
	return item.m_height != 0 ? item.m_height : 1;
}

} // namespace

bool ibHomePageView::OnCreate(ibDocument* doc, long WXUNUSED(flags))
{
	ibHomePageDocument* const homeDoc = dynamic_cast<ibHomePageDocument*>(doc);
	if (homeDoc == nullptr || m_viewFrame == nullptr)
		return false;

	const ibHomePageDescription& description = homeDoc->GetDescription();

	const std::vector<ibHomePageItem> leftItems = description.GetShownItems(eHomePageColumn_Left);
	const std::vector<ibHomePageItem> rightItems = description.GetShownItems(eHomePageColumn_Right);

	if (description.IsTwoColumns() && !leftItems.empty() && !rightItems.empty()) {
		m_columns.push_back(OpenColumn(leftItems, homeDoc));
		m_columns.push_back(OpenColumn(rightItems, homeDoc));
		m_columnGravity = description.GetColumnGravity();
	}
	else {
		// One column — either by template, or because the other column has nothing shown.
		const std::vector<ibHomePageItem>& single = leftItems.empty() ? rightItems : leftItems;
		m_columns.push_back(OpenColumn(single, homeDoc));
	}

	return !m_columns.front().empty();
}

std::vector<ibHomePageView::ibHomePageCell> ibHomePageView::OpenColumn(const std::vector<ibHomePageItem>& items,
	ibHomePageDocument* homeDoc)
{
	std::vector<ibHomePageCell> cells;
	for (const ibHomePageItem& item : items)
		cells.push_back(OpenCell(item, homeDoc));
	return cells;
}

ibHomePageView::ibHomePageCell ibHomePageView::OpenCell(const ibHomePageItem& item, ibHomePageDocument* homeDoc)
{
	ibHomePageCell cell;
	cell.m_item = item;
	cell.m_metaForm = FindItemForm(item, homeDoc);

	// The form opens as a CHILD DOCUMENT of the page and shows in the page's tab (the page answers where). Nothing
	// is passed down the form side — the composition is the doc parent.
	const ibValuePtr<ibValueForm> valueForm = CreateFormValue(cell.m_metaForm);
	if (valueForm && valueForm->ShowForm(homeDoc))
		cell.m_valueForm = valueForm;

	return cell;
}

void ibHomePageView::DrawCell(const ibHomePageCell& cell, ibDataNode& node) const
{
	node.SetValue(wxT("Height"), static_cast<s32>(ItemWeight(cell.m_item)));

	// The header — the icon of the OWNER (a catalog's form reads as that catalog, a document's as that document),
	// falling back to the form's own, and to the page's glyph when the form is gone. An embedded form has no tab
	// to carry either.
	const ibValueMetaObjectFormBase* const metaForm = cell.m_metaForm;
	ibServerPicture headerIcon;
	if (metaForm != nullptr) {
		const ibValueMetaObject* const owner = metaForm->GetParent();
		headerIcon = ibBackendPicture::GetServerPicture(owner != nullptr ? owner->GetClassType() : metaForm->GetClassType());
		if (!headerIcon.IsOk())
			headerIcon = ibBackendPicture::GetServerPicture(metaForm->GetClassType());
	}
	if (!headerIcon.IsOk())
		headerIcon = ibBackendPicture::GetServerPicture(g_picHomePageCLSID);
	if (headerIcon.IsOk())
		node.SetValue(wxT("Icon"), wxString(headerIcon.GetData()));

	const ibFormVisualDocument* const formDoc = cell.m_valueForm ? cell.m_valueForm->GetVisualDocument() : nullptr;
	ibFormVisualEditView* const formView = formDoc != nullptr ? formDoc->GetFirstView() : nullptr;
	if (formView != nullptr) {
		// The SHOWN title — GetControlTitle falls back to the object's / form's synonym, which is what a tab would
		// have displayed.
		node.SetValue(wxT("Title"), cell.m_valueForm->GetControlTitle());
		formView->OnDraw(node.Child(wxT("View")));
		return;
	}

	// The form is gone or refused to open. The cell still stands and says what happened.
	node.SetValue(wxT("Title"), metaForm != nullptr ? metaForm->GetFullName() : wxString(_("<not found>")));
	node.SetValue(wxT("Unavailable"), wxString(_("Form is not available")));
}

void ibHomePageView::OnDraw(ibDataNode& frame)
{
	// Two columns divide the width by the template's proportion: the left one's share of it.
	if (m_columns.size() > 1)
		frame.SetValue(wxT("Gravity"), ibNumber(m_columnGravity));

	ibDataNode& columns = frame.Child(wxT("Columns"));
	for (std::size_t c = 0; c < m_columns.size(); ++c) {
		ibDataNode& column = columns.AddChild(0, static_cast<ibMetaID>(c));
		for (std::size_t i = 0; i < m_columns[c].size(); ++i)
			DrawCell(m_columns[c][i], column.AddChild(0, static_cast<ibMetaID>(i)));
	}
}

const ibValueMetaObjectFormBase* ibHomePageView::FindItemForm(const ibHomePageItem& item,
	const ibHomePageDocument* homeDoc) const
{
	const ibMetaData* const metaData = homeDoc->GetMetaData();
	if (metaData == nullptr)
		return nullptr;

	// The attached form is addressed by metaId; the clsid filter keeps the typed find honest (a common form and an
	// object form are both form metaobjects, nothing else is).
	return metaData->FindAnyObjectByFilter<ibValueMetaObjectFormBase>(item.m_formId,
		{ g_metaCommonFormCLSID, g_metaFormCLSID }, true);
}

ibValuePtr<ibValueForm> ibHomePageView::CreateFormValue(const ibValueMetaObjectFormBase* metaForm) const
{
	if (metaForm == nullptr)
		return nullptr;

	ibFormPtr<ibBackendValueForm> backendForm;

	// An element PLACED on the start page gets an identity of its OWN — a fresh form guid. Without it a form's
	// identity falls back to its source, and a source is not always one per instance: every dynamic list over the
	// same object shares the table's guid. Two such forms would then be "the same form", and opening the list from
	// the menu would merely activate the page's copy — a cell, so nothing would appear. It is also what lets the
	// same form sit on the page twice. Nothing outside the page is affected: an object still finds ITS form by
	// source (ibValueRecordDataObject::GetForm).
	const ibUniqueKey formKey = wxNewUniqueGuid;

	try {
		// One verb, answered by the form's own kind — an object form asks the object that owns it, a common form
		// stands alone, and the access right comes back with the same call. No branch here, no cast: the page does
		// not care which kind it is holding.
		backendForm = metaForm->GetObjectForm(nullptr, formKey);
	}
	catch (const ibCoreException&) {
		// A form that refuses to build (access denied, a broken source) must not take the whole start page down
		// with it — the cell reports it and the others still open. Already reported where it happened
		// (ProcessExceptionError hands it to the frame) - saying it again puts one failure in the messages twice.
		return nullptr;
	}

	return ibValuePtr<ibValueForm>(backendForm);
}

void ibHomePageView::OnUpdate(ibView* WXUNUSED(sender), wxObject* WXUNUSED(hint))
{
	for (std::vector<ibHomePageCell>& column : m_columns) {
		for (ibHomePageCell& cell : column) {
			if (cell.m_valueForm)
				cell.m_valueForm->UpdateForm();
		}
	}
}

bool ibHomePageView::OnClose(bool deleteWindow)
{
	// No refusal here: the "may I close" question is answered by the document (ibHomePageDocument::OnSaveModified),
	// before any teardown starts.

	// The attached forms are CHILD documents — the document cascade closes them; the view only drops its own
	// references.
	m_columns.clear();

	return ibView::OnClose(deleteWindow);
}
