#include "docViewConfigCompare.h"

#include "designer-thin/backend/metaCollection/metaDiff.h"
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/ctrls/dataview/dataview.h"
#include "frmclient/win/theme/luna_toolbarart.h"

#include <wx/aui/auibar.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

// ============================================================
//   ibConfigCompareDocument
// ============================================================

ibConfigCompareDocument::ibConfigCompareDocument() : ibFrontendDocument()
{
}

void ibConfigCompareDocument::Configure(
	std::vector<ibMetaDiffRecord> records,
	const wxString& leftLabel,
	const wxString& rightLabel,
	ibProtocol::ibSchema::ibConfigCompare::ibMode mode,
	const wxString& fileId,
	const wxString& file2Id,
	const wxString& localRightPath,
	std::function<void()> appliedCallback)
{
	m_leftLabel  = leftLabel;
	m_rightLabel = rightLabel;
	m_mode = mode;
	m_fileId = fileId;
	m_file2Id = file2Id;
	m_localRightPath = localRightPath;
	m_appliedCallback = std::move(appliedCallback);
	m_model.reset(new ibDataViewMetaDiffModel(std::move(records)));
	SetTitle(ibString::Format(_("Compare: %s <-> %s"), leftLabel, rightLabel));
}

ibFrontendView* ibConfigCompareDocument::DoCreateView()
{
	return new ibConfigCompareView();
}

// ============================================================
//   ibConfigCompareView
// ============================================================

bool ibConfigCompareView::OnCreate(ibFrontendDocument* doc, long flags)
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

	return ibFrontendView::OnCreate(doc, flags);
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
	m_directionChoice->Append(ibString::Format(_("%s -> %s"),
		m_compareDoc->GetRightLabel(), m_compareDoc->GetLeftLabel()));

	// Push exists only when there's a save target (file path to download into).
	if (m_compareDoc->CanPush()) {
		m_directionChoice->Append(ibString::Format(_("%s -> %s"),
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

	if (!pull && !m_compareDoc->CanPush()) {
		wxMessageBox(
			_("Push direction has no save target - open the other "
			  "configuration from a writable source first."),
			_("Merge"), wxOK | wxICON_WARNING, m_viewFrame);
		return;
	}

	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr) return;

	const auto& records = model->GetRecords();
	ibProtocolNode ops;
	int skippedReorder = 0;

	for (size_t i = 0; i < records.size(); ++i) {
		if (!model->IsSelected(static_cast<int>(i))) continue;
		const ibMetaDiffRecord& rec = records[i];
		if (!rec.IsMergeCandidate()) continue;
		if (HasOnlyInAncestor(static_cast<int>(i))) continue;

		ibProtocol::ibSchema::ibConfigCompare::ibOp opKind{};
		bool haveOp = false;
		switch (rec.m_status) {
		case ibMetaDiffStatus::OnlyInRight:
			opKind = pull ? ibProtocol::ibSchema::ibConfigCompare::ibOp::Add
			              : ibProtocol::ibSchema::ibConfigCompare::ibOp::Delete;
			haveOp = true;
			break;
		case ibMetaDiffStatus::OnlyInLeft:
			opKind = pull ? ibProtocol::ibSchema::ibConfigCompare::ibOp::Delete
			              : ibProtocol::ibSchema::ibConfigCompare::ibOp::Add;
			haveOp = true;
			break;
		case ibMetaDiffStatus::Changed:
			opKind = ibProtocol::ibSchema::ibConfigCompare::ibOp::Replace;
			haveOp = true;
			break;
		case ibMetaDiffStatus::Reordered:
			++skippedReorder;
			break;
		default:
			break;
		}
		if (!haveOp) continue;

		ibProtocolNode row = ops.AddChild();
		row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Op, static_cast<int>(opKind));
		row.SetValue(ibProtocol::ibSchema::ibConfigCompare::LeftId, static_cast<int>(rec.m_leftId));
		row.SetValue(ibProtocol::ibSchema::ibConfigCompare::RightId, static_cast<int>(rec.m_rightId));
		row.SetValue(ibProtocol::ibSchema::ibConfigCompare::ParentId, static_cast<int>(FindTargetParent(rec, pull)));
	}

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibConfigCompare::Mode, static_cast<int>(m_compareDoc->GetMode()));
	if (!m_compareDoc->GetFileId().IsEmpty())
		args.SetValue(ibProtocol::ibSchema::ibConfigCompare::File, m_compareDoc->GetFileId());
	if (!m_compareDoc->GetFile2Id().IsEmpty())
		args.SetValue(ibProtocol::ibSchema::ibConfigCompare::File2, m_compareDoc->GetFile2Id());
	args.SetValue(ibProtocol::ibSchema::ibConfigCompare::Pull, pull);
	args.SetValue(ibProtocol::ibSchema::ibConfigCompare::Ops, ops);

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema,
		static_cast<long long>(ibProtocol::ibSchema::ibKind::ConfigCompare));
	params.SetValue(ibProtocol::ibSchema::Command,
		static_cast<long long>(ibProtocol::ibSchema::ibCommand::Merge));
	params.SetValue(ibProtocol::ibSchema::Args, args);

	// WHAT WAS APPLIED THE SERVER SAYS ITSELF, as it applies it (the merge's own message). What is left here is the
	// client's: the other side's file taken down where it came from, and the changes this tab does not send.
	const bool ok = frame->Call(ibProtocol::ibMethod::Schema, params, answer);
	bool savedRight = true;
	wxString msg;
	if (!ok) {
		msg = _("Merge failed.") + wxT("\n\n") + _("The server refused the merge.");
	}
	else if (!pull && m_compareDoc->CanPush()) {
		const wxString outId = (m_compareDoc->GetMode()
			== ibProtocol::ibSchema::ibConfigCompare::ibMode::TwoFiles)
			? m_compareDoc->GetFile2Id()
			: m_compareDoc->GetFileId();
		savedRight = frame->DownloadFile(outId, m_compareDoc->GetLocalRightPath());
		msg = savedRight ? _("The other-side file has been saved.") : _("Warning: failed to save the other-side file.");
	}
	if (skippedReorder > 0) {
		if (!msg.IsEmpty())
			msg += wxT("\n");
		msg += ibString::Format(
			_("%d reorder-only change(s) skipped (not supported in V1)."),
			skippedReorder);
	}

	if (!msg.IsEmpty())
		wxMessageBox(msg, _("Merge"),
			wxOK | ((ok && savedRight) ? wxICON_INFORMATION : wxICON_WARNING), m_viewFrame);

	if (ok && m_compareDoc->GetAppliedCallback())
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

// The desktop's, by number: the nearest object above the row that the target side has (pull — the left), its id there;
// 0 — none.
ibMetaID ibConfigCompareView::FindTargetParent(
	const ibMetaDiffRecord& rec, bool pull) const
{
	if (m_compareDoc == nullptr || m_compareDoc->GetModel() == nullptr)
		return 0;
	const auto& records = m_compareDoc->GetModel()->GetRecords();
	int p = rec.m_parentIndex;
	while (p >= 0) {
		const ibMetaDiffRecord& parentRec = records[p];
		if (!parentRec.IsGroup() && !parentRec.IsProperty()) {
			const ibMetaID candidate = pull
				? parentRec.m_leftId
				: parentRec.m_rightId;
			if (candidate != 0) return candidate;
		}
		p = parentRec.m_parentIndex;
	}
	return 0;
}

void ibConfigCompareView::OnUpdate(ibFrontendView* /*sender*/, wxObject* /*hint*/)
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

	return ibFrontendView::OnClose(deleteWindow);
}
