#include "docViewConfigCompare.h"

#include "backend/backend_exception.h"
#include "backend/fileSystem/fs.h"
#include "backend/metaCollection/metaDiff.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaData.h"

#include "frontend/win/ctrls/dataview/dataview.h"
#include "frontend/win/theme/luna_toolbarart.h"

#include <wx/aui/auibar.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

// ============================================================
//   ibConfigCompareDocument
// ============================================================

wxIMPLEMENT_DYNAMIC_CLASS(ibConfigCompareDocument, ibDocument);

ibConfigCompareDocument::ibConfigCompareDocument() : ibDocument()
{
}

void ibConfigCompareDocument::Configure(
	ibValueMetaObject* leftRoot,
	ibValueMetaObject* rightRoot,
	const wxString& leftLabel,
	const wxString& rightLabel,
	std::function<bool()> rightSaveCallback,
	std::function<void()> appliedCallback)
{
	m_leftRoot  = leftRoot;
	m_rightRoot = rightRoot;
	m_leftLabel  = leftLabel;
	m_rightLabel = rightLabel;
	m_rightSaveCallback = std::move(rightSaveCallback);
	m_appliedCallback   = std::move(appliedCallback);

	// Walk the diff up front. Linear in node count; keeps the document
	// construction self-contained so the view sees an immutable model.
	std::vector<ibMetaDiffRecord> records =
		ibMetaDiffWalker::Walk(leftRoot, rightRoot);
	m_model.reset(new ibDataViewMetaDiffModel(std::move(records)));

	SetTitle(wxString::Format(_("Compare: %s <-> %s"), leftLabel, rightLabel));
}

// ============================================================
//   ibConfigCompareView
// ============================================================

wxIMPLEMENT_DYNAMIC_CLASS(ibConfigCompareView, ibView);

bool ibConfigCompareView::OnCreate(ibDocument* doc, long flags)
{
	m_compareDoc = dynamic_cast<ibConfigCompareDocument*>(doc);
	if (m_compareDoc == nullptr) return false;

	BuildLayout(m_viewFrame);
	RefreshApplyButtonState();

	// Defer root-pair expansion until the dataview has finished its
	// initial BuildTree. CallAfter posts to the next idle, by which
	// point Expand can find the root node.
	CallAfter([this]() {
		if (m_dataView == nullptr || m_compareDoc == nullptr
			|| m_compareDoc->GetModel() == nullptr) return;
		const ibDataViewItem rootItem = m_compareDoc->GetModel()->GetRootItem();
		if (rootItem.IsOk()) m_dataView->Expand(rootItem);
	});

	return ibView::OnCreate(doc, flags);
}

void ibConfigCompareView::BuildLayout(wxWindow* parent)
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// --- Toolbar -------------------------------------------------------

	m_toolbar = new wxAuiToolBar(parent, wxID_ANY, wxDefaultPosition,
		wxDefaultSize, wxAUI_TB_HORZ_TEXT);
	m_toolbar->SetArtProvider(new wxAuiLunaToolBarArt());

	m_toolbar->AddTool(wxID_TOOL_SELECT_ALL, _("Select all differences"),
		wxNullBitmap, _("Mark every non-Same row for merge"));
	m_toolbar->AddTool(wxID_TOOL_CLEAR, _("Clear"),
		wxNullBitmap, _("Unmark every row"));
	m_toolbar->AddSeparator();
	m_toolbar->AddTool(wxID_TOOL_EXPAND_ALL, _("Expand all"),
		wxNullBitmap, _("Expand every tree node"));
	m_toolbar->AddTool(wxID_TOOL_COLLAPSE_ALL, _("Collapse all"),
		wxNullBitmap, _("Collapse every tree node"));

	m_toolbar->Realize();
	m_toolbar->Bind(wxEVT_MENU, &ibConfigCompareView::OnSelectAll, this,
		wxID_TOOL_SELECT_ALL);
	m_toolbar->Bind(wxEVT_MENU, &ibConfigCompareView::OnClearSelection, this,
		wxID_TOOL_CLEAR);
	m_toolbar->Bind(wxEVT_MENU, &ibConfigCompareView::OnExpandAll, this,
		wxID_TOOL_EXPAND_ALL);
	m_toolbar->Bind(wxEVT_MENU, &ibConfigCompareView::OnCollapseAll, this,
		wxID_TOOL_COLLAPSE_ALL);

	mainSizer->Add(m_toolbar, 0, wxEXPAND);

	// --- Data view -----------------------------------------------------

	m_dataView = new ibDataViewCtrl(parent, wxID_ANY, wxDefaultPosition,
		wxDefaultSize,
		wxDV_VERT_RULES | wxDV_ROW_LINES | wxDV_SINGLE | wxDV_NO_HEADER);

	// Set Tree view mode BEFORE associating the model so BuildTree
	// runs once under the right mode.
	m_dataView->SetViewMode(ibDataViewViewMode::ibDataViewTree);

	// Column 0 — merge checkbox.
	ibDataViewColumn* colSelect = new ibDataViewColumn(
		_("Merge"),
		new ibDataViewToggleRenderer(wxT("bool"), wxDATAVIEW_CELL_ACTIVATABLE),
		ibDataViewMetaDiffModel::kColSelect,
		parent->FromDIP(60), wxALIGN_CENTER,
		wxDATAVIEW_COL_RESIZABLE);
	m_dataView->GetRootColumnGroup()->AppendColumn(colSelect);

	// Column 1 — object name with per-class icon.
	ibDataViewColumn* colName = new ibDataViewColumn(
		_("Object"),
		new ibDataViewIconTextRenderer(wxT("ibDataViewIconText"), wxDATAVIEW_CELL_INERT),
		ibDataViewMetaDiffModel::kColName,
		parent->FromDIP(260), wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE);
	m_dataView->GetRootColumnGroup()->AppendColumn(colName);

	// Expander on the Object column (default would pick col 0, which is
	// the merge checkbox — hides the tree shape).
	m_dataView->SetExpanderColumn(colName);

	ibDataViewColumn* colStatus = new ibDataViewColumn(
		_("Status"),
		new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
		ibDataViewMetaDiffModel::kColStatus,
		parent->FromDIP(110), wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE);
	m_dataView->GetRootColumnGroup()->AppendColumn(colStatus);

	ibDataViewColumn* colLeft = new ibDataViewColumn(
		m_compareDoc->GetLeftLabel(),
		new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
		ibDataViewMetaDiffModel::kColLeft,
		parent->FromDIP(200), wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE);
	m_dataView->GetRootColumnGroup()->AppendColumn(colLeft);

	ibDataViewColumn* colRight = new ibDataViewColumn(
		m_compareDoc->GetRightLabel(),
		new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
		ibDataViewMetaDiffModel::kColRight,
		parent->FromDIP(200), wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE);
	m_dataView->GetRootColumnGroup()->AppendColumn(colRight);

	// AssociateModel AFTER columns — mirrors predefinedEditor / audit log
	// pattern.
	m_dataView->AssociateModel(m_compareDoc->GetModel());

	m_dataView->Bind(wxEVT_DATAVIEW_ITEM_COLLAPSING,
		&ibConfigCompareView::OnItemCollapsing, this);

	m_dataView->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED,
		[this](ibDataViewEvent& event) {
			event.Skip();
			RefreshApplyButtonState();
		});

	mainSizer->Add(m_dataView, 1, wxEXPAND | wxALL, parent->FromDIP(4));

	// --- Bottom bar: filter on the left, action buttons on the right --

	wxBoxSizer* bottomSizer = new wxBoxSizer(wxHORIZONTAL);

	bottomSizer->Add(new wxStaticText(parent, wxID_ANY, _("Show:")),
		0, wxALIGN_CENTER_VERTICAL | wxLEFT, parent->FromDIP(8));

	wxArrayString filterChoices;
	filterChoices.Add(_("All"));
	filterChoices.Add(_("Only differences"));
	filterChoices.Add(_("Only unchanged"));
	m_filterChoice = new wxChoice(parent, wxID_ANY, wxDefaultPosition,
		wxDefaultSize, filterChoices);
	m_filterChoice->SetSelection(0);
	m_filterChoice->Bind(wxEVT_CHOICE, &ibConfigCompareView::OnFilterChanged, this);
	bottomSizer->Add(m_filterChoice, 0, wxALL, parent->FromDIP(5));

	bottomSizer->Add(new wxStaticText(parent, wxID_ANY, _("Direction:")),
		0, wxALIGN_CENTER_VERTICAL | wxLEFT, parent->FromDIP(12));

	m_directionChoice = new wxChoice(parent, wxID_ANY);
	m_directionChoice->Bind(wxEVT_CHOICE,
		&ibConfigCompareView::OnDirectionChanged, this);
	bottomSizer->Add(m_directionChoice, 0, wxALL, parent->FromDIP(5));
	RefreshDirectionChoice();

	bottomSizer->AddStretchSpacer(1);

	m_buttonApply = new wxButton(parent, wxID_TOOL_APPLY_MERGE, _("Apply merge"));
	// Terracotta — interior-palette focal accent for the primary action.
	m_buttonApply->SetBackgroundColour(wxColour(0xD9, 0x77, 0x57));
	m_buttonApply->SetForegroundColour(*wxWHITE);
	m_buttonApply->Bind(wxEVT_BUTTON, &ibConfigCompareView::OnApplyMerge, this);

	bottomSizer->Add(m_buttonApply, 0, wxALL, parent->FromDIP(5));

	mainSizer->Add(bottomSizer, 0, wxEXPAND);

	parent->SetSizer(mainSizer);
	parent->Layout();
}

void ibConfigCompareView::RefreshDirectionChoice()
{
	if (m_directionChoice == nullptr || m_compareDoc == nullptr)
		return;

	const int prevSelection = m_directionChoice->GetSelection();
	m_directionChoice->Clear();

	// Pull always exists — left-side mutation needs no extra save
	// wiring (designer handles activeMetaData; the applied callback
	// rebuilds the tree).
	m_directionChoice->Append(wxString::Format(_("%s -> %s"),
		m_compareDoc->GetRightLabel(), m_compareDoc->GetLeftLabel()));

	// Push exists only when there's a save target.
	if (m_compareDoc->GetRightSaveCallback()) {
		m_directionChoice->Append(wxString::Format(_("%s -> %s"),
			m_compareDoc->GetLeftLabel(), m_compareDoc->GetRightLabel()));
	}

	const int newCount = static_cast<int>(m_directionChoice->GetCount());
	m_directionChoice->SetSelection(
		prevSelection >= 0 && prevSelection < newCount ? prevSelection : 0);
}

void ibConfigCompareView::OnSelectAll(wxCommandEvent& WXUNUSED(event))
{
	if (m_compareDoc == nullptr || m_compareDoc->GetModel() == nullptr) return;
	m_compareDoc->GetModel()->SelectAllDifferences();
	RefreshApplyButtonState();
}

void ibConfigCompareView::OnClearSelection(wxCommandEvent& WXUNUSED(event))
{
	if (m_compareDoc == nullptr || m_compareDoc->GetModel() == nullptr) return;
	m_compareDoc->GetModel()->ClearAllSelection();
	RefreshApplyButtonState();
}

void ibConfigCompareView::OnExpandAll(wxCommandEvent& WXUNUSED(event))
{
	ExpandAllRecursive(ibDataViewItem(nullptr));
}

void ibConfigCompareView::OnCollapseAll(wxCommandEvent& WXUNUSED(event))
{
	CollapseAllRecursive(ibDataViewItem(nullptr));
}

void ibConfigCompareView::ExpandAllRecursive(const ibDataViewItem& parent)
{
	if (m_dataView == nullptr || m_compareDoc == nullptr
		|| m_compareDoc->GetModel() == nullptr) return;

	ibDataViewItemArray children;
	m_compareDoc->GetModel()->GetFirstFetch(parent, ibDataViewItem(), -1, children);
	for (size_t i = 0; i < children.GetCount(); ++i) {
		const ibDataViewItem& child = children[i];
		if (m_compareDoc->GetModel()->IsContainer(child)) {
			m_dataView->Expand(child);
			ExpandAllRecursive(child);
		}
	}
}

void ibConfigCompareView::CollapseAllRecursive(const ibDataViewItem& parent)
{
	if (m_dataView == nullptr || m_compareDoc == nullptr
		|| m_compareDoc->GetModel() == nullptr) return;

	ibDataViewItemArray children;
	m_compareDoc->GetModel()->GetFirstFetch(parent, ibDataViewItem(), -1, children);
	for (size_t i = 0; i < children.GetCount(); ++i) {
		const ibDataViewItem& child = children[i];
		if (m_compareDoc->GetModel()->IsContainer(child)) {
			CollapseAllRecursive(child);
			m_dataView->Collapse(child);
		}
	}
}

void ibConfigCompareView::OnFilterChanged(wxCommandEvent& WXUNUSED(event))
{
	if (m_compareDoc == nullptr || m_compareDoc->GetModel() == nullptr
		|| m_filterChoice == nullptr) return;

	using FM = ibDataViewMetaDiffModel::FilterMode;
	FM mode = FM::All;
	switch (m_filterChoice->GetSelection()) {
	case 1: mode = FM::Differences; break;
	case 2: mode = FM::SameOnly;    break;
	default: mode = FM::All;
	}

	m_compareDoc->GetModel()->SetFilterMode(mode);

	CallAfter([this]() {
		if (m_dataView == nullptr || m_compareDoc == nullptr
			|| m_compareDoc->GetModel() == nullptr) return;
		const ibDataViewItem rootItem = m_compareDoc->GetModel()->GetRootItem();
		if (rootItem.IsOk())
			m_dataView->Expand(rootItem);
	});

	RefreshApplyButtonState();
}

void ibConfigCompareView::OnItemCollapsing(ibDataViewEvent& event)
{
	// Root config row stays open — collapsing it leaves the user with a
	// single uninformative row. Veto only that specific item.
	if (m_compareDoc != nullptr && m_compareDoc->GetModel() != nullptr
		&& event.GetItem() == m_compareDoc->GetModel()->GetRootItem()) {
		event.Veto();
	}
}

void ibConfigCompareView::OnApplyMerge(wxCommandEvent& WXUNUSED(event))
{
	if (m_compareDoc == nullptr || m_compareDoc->GetModel() == nullptr) return;
	ibDataViewMetaDiffModel* model = m_compareDoc->GetModel();
	if (!model->HasSelection()) return;

	const bool pull = (m_directionChoice == nullptr
		|| m_directionChoice->GetSelection() == 0);

	if (!pull && !m_compareDoc->GetRightSaveCallback()) {
		wxMessageBox(
			_("Push direction has no save target - open the other "
			  "configuration from a writable source first."),
			_("Merge"), wxOK | wxICON_WARNING, m_viewFrame);
		return;
	}

	const auto& records = model->GetRecords();
	int applied = 0;
	int skippedReorder = 0;

	for (size_t i = 0; i < records.size(); ++i) {
		if (!model->IsSelected(static_cast<int>(i))) continue;
		const ibMetaDiffRecord& rec = records[i];
		if (!rec.IsMergeCandidate()) continue;

		// Descendants of a selected OnlyIn* parent — parent's recursive
		// CopyObject covers the subtree on a single Apply.
		if (HasOnlyInAncestor(static_cast<int>(i))) continue;

		bool addOp = false, delOp = false, replaceOp = false;
		switch (rec.m_status) {
		case ibMetaDiffStatus::OnlyInRight:
			if (pull) addOp = true; else delOp = true;
			break;
		case ibMetaDiffStatus::OnlyInLeft:
			if (pull) delOp = true; else addOp = true;
			break;
		case ibMetaDiffStatus::Changed:
			replaceOp = true;
			break;
		case ibMetaDiffStatus::Reordered:
			++skippedReorder;
			break;
		default:
			break;
		}

		try {
			if (addOp)          { ApplyAdd(rec, pull);     ++applied; }
			else if (delOp)     { ApplyDelete(rec, pull);  ++applied; }
			else if (replaceOp) { ApplyReplace(rec, pull); ++applied; }
		}
		catch (const ibBackendException& err) {
			ibJournalInfo(wxT("designer"), wxT("[merge] %s: %s"),
				rec.GetAnyObject() != nullptr
					? rec.GetAnyObject()->GetName()
					: wxString(),
				err.GetErrorDescription());
		}
		catch (...) {
			ibJournalInfo(wxT("designer"), wxT("[merge] unknown error on %s"),
				rec.GetAnyObject() != nullptr
					? rec.GetAnyObject()->GetName()
					: wxString());
		}
	}

	// Persist the right-side config after a Push.
	bool savedRight = true;
	if (!pull && m_compareDoc->GetRightSaveCallback())
		savedRight = m_compareDoc->GetRightSaveCallback()();

	wxString msg = wxString::Format(_("Merge applied to %d item(s)."), applied);
	if (skippedReorder > 0) {
		msg += wxT("\n");
		msg += wxString::Format(
			_("%d reorder-only change(s) skipped (not supported in V1)."),
			skippedReorder);
	}
	msg += wxT("\n\n");
	if (pull)
		msg += _("Use Configuration > Save configuration to persist the changes.");
	else if (savedRight)
		msg += _("The other-side file has been saved.");
	else
		msg += _("Warning: failed to save the other-side file.");

	wxMessageBox(msg, _("Merge"),
		wxOK | (savedRight ? wxICON_INFORMATION : wxICON_WARNING), m_viewFrame);

	// Notify caller (designer mainFrame typically rebuilds m_metaWindow
	// so the user sees the result against activeMetaData).
	if (m_compareDoc->GetAppliedCallback())
		m_compareDoc->GetAppliedCallback()();
}

void ibConfigCompareView::OnDirectionChanged(wxCommandEvent& WXUNUSED(event))
{
	RefreshApplyButtonState();
}

void ibConfigCompareView::RefreshApplyButtonState()
{
	if (m_buttonApply == nullptr || m_compareDoc == nullptr
		|| m_compareDoc->GetModel() == nullptr) return;
	m_buttonApply->Enable(m_compareDoc->GetModel()->HasSelection());
}

void ibConfigCompareView::ApplyAdd(const ibMetaDiffRecord& rec, bool pull)
{
	ibValueMetaObject* source = pull ? rec.m_right : rec.m_left;
	if (source == nullptr) return;
	ibValueMetaObject* targetParent = FindTargetParent(rec, pull);
	if (targetParent == nullptr) return;

	ibMetaData* meta = targetParent->GetMetaData();
	if (meta == nullptr) return;

	// ⚠ COPIED THROUGH THE SOURCE'S OWN METADATA, pasted through the TARGET'S. A compare holds two
	// configurations open at once and they are not the same object — which is the whole reason the
	// door is asked for rather than reached for.
	ibMetaData* sourceMeta = source->GetMetaData();
	if (sourceMeta == nullptr) return;

	ibWriterMemory writer;
	if (!sourceMeta->CopyMetaObject(source, writer)) return;
	ibReaderMemory reader(writer.pointer(), writer.size());

	if (ibValueMetaObject* newObj = meta->PasteMetaObject(source->GetClassType(), targetParent, reader))
		newObj->SetCommonGuid(source->GetGuid());
}

void ibConfigCompareView::ApplyDelete(const ibMetaDiffRecord& rec, bool pull)
{
	ibValueMetaObject* target = pull ? rec.m_left : rec.m_right;
	if (target == nullptr) return;
	ibMetaData* meta = target->GetMetaData();
	if (meta == nullptr) return;
	meta->RemoveMetaObject(target, target->GetParent());
}

void ibConfigCompareView::ApplyReplace(const ibMetaDiffRecord& rec, bool pull)
{
	ibValueMetaObject* source = pull ? rec.m_right : rec.m_left;
	ibValueMetaObject* target = pull ? rec.m_left  : rec.m_right;
	if (source == nullptr || target == nullptr) return;
	ibValueMetaObject* parent = target->GetParent();
	ibMetaData* meta = target->GetMetaData();
	if (meta == nullptr || parent == nullptr) return;

	// The source lives in the OTHER configuration — see ApplyAdd.
	ibMetaData* sourceMeta = source->GetMetaData();
	if (sourceMeta == nullptr) return;

	ibWriterMemory writer;
	if (!sourceMeta->CopyMetaObject(source, writer)) return;
	ibReaderMemory reader(writer.pointer(), writer.size());
	const ibGuid    sourceGuid  = source->GetGuid();
	const ibClassID sourceClsid = source->GetClassType();

	// ⚠ THE REPLACE STOPS IF THE DELETE DID. It used to go on and paste the replacement beside an
	// object that refused to go, leaving both — now that the door answers, the answer is read.
	if (!meta->RemoveMetaObject(target, parent))
		return;

	if (ibValueMetaObject* newObj = meta->PasteMetaObject(sourceClsid, parent, reader))
		newObj->SetCommonGuid(sourceGuid);
}

bool ibConfigCompareView::HasOnlyInAncestor(int recordIndex) const
{
	if (m_compareDoc == nullptr || m_compareDoc->GetModel() == nullptr)
		return false;
	const auto& records = m_compareDoc->GetModel()->GetRecords();
	int p = records[recordIndex].m_parentIndex;
	while (p >= 0) {
		const ibMetaDiffRecord& rec = records[p];
		if (rec.m_status == ibMetaDiffStatus::OnlyInLeft
			|| rec.m_status == ibMetaDiffStatus::OnlyInRight) {
			return true;
		}
		p = rec.m_parentIndex;
	}
	return false;
}

ibValueMetaObject* ibConfigCompareView::FindTargetParent(
	const ibMetaDiffRecord& rec, bool pull) const
{
	if (m_compareDoc == nullptr || m_compareDoc->GetModel() == nullptr)
		return nullptr;
	const auto& records = m_compareDoc->GetModel()->GetRecords();
	int p = rec.m_parentIndex;
	while (p >= 0) {
		const ibMetaDiffRecord& parentRec = records[p];
		if (!parentRec.IsGroup() && !parentRec.IsProperty()) {
			ibValueMetaObject* candidate = pull
				? parentRec.m_left
				: parentRec.m_right;
			if (candidate != nullptr) return candidate;
		}
		p = parentRec.m_parentIndex;
	}
	return nullptr;
}

void ibConfigCompareView::OnUpdate(ibView* /*sender*/, wxObject* /*hint*/)
{
	// Model is immutable for the doc's lifetime — no per-update refresh
	// needed beyond the initial expand in OnCreate.
}

void ibConfigCompareView::OnDraw(wxDC* /*dc*/)
{
	// Toolbar / dataview / buttons draw themselves.
}

bool ibConfigCompareView::OnClose(bool deleteWindow)
{
	if (deleteWindow) {
		if (m_viewFrame != nullptr) {
			m_viewFrame->Destroy();
			SetFrame(nullptr);
		}
	}

	return ibView::OnClose(deleteWindow);
}
