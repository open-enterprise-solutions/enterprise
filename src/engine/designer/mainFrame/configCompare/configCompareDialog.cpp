#include "configCompareDialog.h"

#include "backend/backend_exception.h"
#include "backend/metaCollection/metaDiff.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metadata.h"
#include "backend/fileSystem/fs.h"
#include "frontend/win/theme/luna_toolbarart.h"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
#include <wx/stattext.h>

ibDialogConfigCompare::ibDialogConfigCompare(
	wxWindow* parent,
	ibValueMetaObject* leftRoot,
	ibValueMetaObject* rightRoot,
	const wxString& leftLabel,
	const wxString& rightLabel)
	: wxDialog(parent, wxID_ANY,
		wxString::Format(_("Compare configurations: %s <-> %s"), leftLabel, rightLabel),
		wxDefaultPosition,
		wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	// Interior palette — powder-blue dialog frame (see docs/ui-palette.md).
	SetBackgroundColour(wxColour(184, 201, 212));

	// Walk the diff up front. The walker is cheap (linear in node count)
	// so doing it in the ctor keeps the dialog construction self-contained
	// and the resulting model immutable from the UI's point of view.
	std::vector<ibMetaDiffRecord> records =
		ibMetaDiffWalker::Walk(leftRoot, rightRoot);

	m_model.reset(new ibDataViewMetaDiffModel(std::move(records)));
	// AssociateModel (called from BuildUI below) IncRefs internally, so
	// we don't need an extra IncRef here — refcount goes from 1 (wxObjectDataPtr)
	// to 2 (AssociateModel), and both decref on the way out.

	BuildUI(leftLabel, rightLabel);

	SetSize(FromDIP(wxSize(900, 600)));
	Centre(wxBOTH);

	RefreshApplyButtonState();
}

void ibDialogConfigCompare::BuildUI(const wxString& leftLabel,
	const wxString& rightLabel)
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// --- Toolbar -------------------------------------------------------

	m_toolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition,
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
	m_toolbar->Bind(wxEVT_MENU, &ibDialogConfigCompare::OnSelectAll, this,
		wxID_TOOL_SELECT_ALL);
	m_toolbar->Bind(wxEVT_MENU, &ibDialogConfigCompare::OnClearSelection, this,
		wxID_TOOL_CLEAR);
	m_toolbar->Bind(wxEVT_MENU, &ibDialogConfigCompare::OnExpandAll, this,
		wxID_TOOL_EXPAND_ALL);
	m_toolbar->Bind(wxEVT_MENU, &ibDialogConfigCompare::OnCollapseAll, this,
		wxID_TOOL_COLLAPSE_ALL);

	mainSizer->Add(m_toolbar, 0, wxEXPAND);

	// --- Data view -----------------------------------------------------

	m_dataView = new ibDataViewCtrl(this, wxID_ANY, wxDefaultPosition,
		wxDefaultSize,
		wxDV_VERT_RULES | wxDV_ROW_LINES | wxDV_SINGLE | wxDV_NO_HEADER);

	// Set Tree view mode BEFORE associating the model so BuildTree
	// runs once under the right mode (BuildTreeHelper fetches the
	// invisible-root's children with the IsOk()-guarded fix in
	// datavgen.cpp). Doing this in the opposite order causes
	// SetViewMode to wipe + async-refresh the just-built tree.
	m_dataView->SetViewMode(ibDataViewViewMode::ibDataViewTree);
	m_dataView->AssociateModel(m_model.get());

	m_dataView->Bind(wxEVT_DATAVIEW_ITEM_COLLAPSING,
		&ibDialogConfigCompare::OnItemCollapsing, this);

	// Refresh the Apply button state after every value change so a
	// per-row checkbox click immediately enables / disables the
	// primary action. Without this the button stays in whatever
	// state it had after the last bulk operation.
	m_dataView->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED,
		[this](ibDataViewEvent& event) {
			event.Skip();
			RefreshApplyButtonState();
		});

	// Expand the root config pair on first show — by this point the
	// dataview has paint cycle completion and the tree node for the
	// root config exists, so Expand reliably opens it.
	Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
		event.Skip();
		if (!event.IsShown() || m_rootInitiallyExpanded)
			return;
		m_rootInitiallyExpanded = true;
		CallAfter([this]() {
			if (m_dataView == nullptr || m_model.get() == nullptr)
				return;
			const ibDataViewItem rootItem = m_model->GetRootItem();
			if (rootItem.IsOk())
				m_dataView->Expand(rootItem);
		});
	});

	// Column 0 — merge checkbox. wxDATAVIEW_CELL_ACTIVATABLE makes the
	// toggle flip on single click; the model's HasValue() returns false
	// for Same rows so they render blank instead of with an inert box.
	ibDataViewColumn* colSelect = new ibDataViewColumn(
		_("Merge"),
		new ibDataViewToggleRenderer(wxT("bool"), wxDATAVIEW_CELL_ACTIVATABLE),
		ibDataViewMetaDiffModel::kColSelect,
		FromDIP(60), wxALIGN_CENTER,
		wxDATAVIEW_COL_RESIZABLE);
	m_dataView->AppendColumn(colSelect);

	// Column 1 — object name with per-class icon. IconText renderer
	// (variant carries ibDataViewIconText). Tree-shaped column gets
	// the disclosure triangle automatically because the model reports
	// IsContainer.
	ibDataViewColumn* colName = new ibDataViewColumn(
		_("Object"),
		new ibDataViewIconTextRenderer(wxT("ibDataViewIconText"), wxDATAVIEW_CELL_INERT),
		ibDataViewMetaDiffModel::kColName,
		FromDIP(260), wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE);
	m_dataView->AppendColumn(colName);

	// Put the expander triangle on the Object column. Default picks col 0,
	// which is the merge-checkbox here — that hides the tree shape.
	m_dataView->SetExpanderColumn(colName);

	ibDataViewColumn* colStatus = new ibDataViewColumn(
		_("Status"),
		new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
		ibDataViewMetaDiffModel::kColStatus,
		FromDIP(110), wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE);
	m_dataView->AppendColumn(colStatus);

	ibDataViewColumn* colLeft = new ibDataViewColumn(
		leftLabel,
		new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
		ibDataViewMetaDiffModel::kColLeft,
		FromDIP(200), wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE);
	m_dataView->AppendColumn(colLeft);

	ibDataViewColumn* colRight = new ibDataViewColumn(
		rightLabel,
		new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
		ibDataViewMetaDiffModel::kColRight,
		FromDIP(200), wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE);
	m_dataView->AppendColumn(colRight);

	mainSizer->Add(m_dataView, 1, wxEXPAND | wxALL, FromDIP(4));

	// --- Bottom bar: filter on the left, action buttons on the right --

	wxBoxSizer* bottomSizer = new wxBoxSizer(wxHORIZONTAL);

	bottomSizer->Add(new wxStaticText(this, wxID_ANY, _("Show:")),
		0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

	// Indexes match ibDataViewMetaDiffModel::FilterMode order: All=0,
	// Differences=1, SameOnly=2. Keep them in sync with OnFilterChanged.
	wxArrayString filterChoices;
	filterChoices.Add(_("All"));
	filterChoices.Add(_("Only differences"));
	filterChoices.Add(_("Only unchanged"));
	m_filterChoice = new wxChoice(this, wxID_ANY, wxDefaultPosition,
		wxDefaultSize, filterChoices);
	m_filterChoice->SetSelection(0);
	m_filterChoice->Bind(wxEVT_CHOICE, &ibDialogConfigCompare::OnFilterChanged, this);
	bottomSizer->Add(m_filterChoice, 0, wxALL, FromDIP(5));

	bottomSizer->Add(new wxStaticText(this, wxID_ANY, _("Direction:")),
		0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));

	// Direction selector. Index 0 = Pull (other → current); a second
	// Push entry is appended in RefreshDirectionChoice() when a save
	// callback for the right side is registered.
	m_directionLeftLabel = leftLabel;
	m_directionRightLabel = rightLabel;
	m_directionChoice = new wxChoice(this, wxID_ANY);
	m_directionChoice->Bind(wxEVT_CHOICE,
		&ibDialogConfigCompare::OnDirectionChanged, this);
	bottomSizer->Add(m_directionChoice, 0, wxALL, FromDIP(5));
	RefreshDirectionChoice();

	bottomSizer->AddStretchSpacer(1);

	m_buttonApply = new wxButton(this, wxID_OK, _("Apply merge"));
	// Terracotta — interior-palette focal accent for the primary action
	// (matches the launcher connectionDB "Save connection" choice).
	m_buttonApply->SetBackgroundColour(wxColour(0xD9, 0x77, 0x57));
	m_buttonApply->SetForegroundColour(*wxWHITE);
	m_buttonApply->Bind(wxEVT_BUTTON, &ibDialogConfigCompare::OnApplyMerge, this);

	m_buttonClose = new wxButton(this, wxID_CANCEL, _("Close"));

	bottomSizer->Add(m_buttonApply, 0, wxALL, FromDIP(5));
	bottomSizer->Add(m_buttonClose, 0, wxALL, FromDIP(5));

	mainSizer->Add(bottomSizer, 0, wxEXPAND);

	SetSizer(mainSizer);
}

void ibDialogConfigCompare::OnSelectAll(wxCommandEvent& WXUNUSED(event))
{
	m_model->SelectAllDifferences();
	RefreshApplyButtonState();
}

void ibDialogConfigCompare::OnClearSelection(wxCommandEvent& WXUNUSED(event))
{
	m_model->ClearAllSelection();
	RefreshApplyButtonState();
}

void ibDialogConfigCompare::OnExpandAll(wxCommandEvent& WXUNUSED(event))
{
	ExpandAllRecursive(ibDataViewItem(nullptr));
}

void ibDialogConfigCompare::OnCollapseAll(wxCommandEvent& WXUNUSED(event))
{
	CollapseAllRecursive(ibDataViewItem(nullptr));
}

void ibDialogConfigCompare::ExpandAllRecursive(const ibDataViewItem& parent)
{
	if (m_dataView == nullptr || m_model.get() == nullptr)
		return;

	// Walk via GetFirstFetch — the model only overrides that one, not
	// GetChildren (see configCompareModel.h note on the new fetch
	// contract). Non-paged: one call returns every child.
	ibDataViewItemArray children;
	m_model->GetFirstFetch(parent, ibDataViewItem(), -1, children);
	for (size_t i = 0; i < children.GetCount(); ++i) {
		const ibDataViewItem& child = children[i];
		if (m_model->IsContainer(child)) {
			m_dataView->Expand(child);
			ExpandAllRecursive(child);
		}
	}
}

void ibDialogConfigCompare::CollapseAllRecursive(const ibDataViewItem& parent)
{
	if (m_dataView == nullptr || m_model.get() == nullptr)
		return;

	// Post-order collapse: drill into descendants first so the visible
	// tree shrinks bottom-up without intermediate flickers from
	// re-layouting at each level.
	ibDataViewItemArray children;
	m_model->GetFirstFetch(parent, ibDataViewItem(), -1, children);
	for (size_t i = 0; i < children.GetCount(); ++i) {
		const ibDataViewItem& child = children[i];
		if (m_model->IsContainer(child)) {
			CollapseAllRecursive(child);
			m_dataView->Collapse(child);
		}
	}
}

void ibDialogConfigCompare::OnFilterChanged(wxCommandEvent& WXUNUSED(event))
{
	if (m_model.get() == nullptr || m_filterChoice == nullptr)
		return;

	using FM = ibDataViewMetaDiffModel::FilterMode;
	FM mode = FM::All;
	switch (m_filterChoice->GetSelection()) {
	case 1: mode = FM::Differences; break;
	case 2: mode = FM::SameOnly;    break;
	default: mode = FM::All;
	}

	// SetFilterMode triggers BeforeReset / AfterReset on the model;
	// AfterReset → Cleared → BuildTree on the dataview side, which
	// re-runs GetFirstFetch under the new visibility bitmap. The
	// BuildTreeHelper fix in datavgen.cpp now treats the invisible
	// root as a container, so this single call repopulates the tree
	// correctly without view-mode flips or AssociateModel detours.
	m_model->SetFilterMode(mode);

	CallAfter([this]() {
		if (m_dataView == nullptr || m_model.get() == nullptr)
			return;
		const ibDataViewItem rootItem = m_model->GetRootItem();
		if (rootItem.IsOk())
			m_dataView->Expand(rootItem);
	});

	RefreshApplyButtonState();
}

void ibDialogConfigCompare::OnItemCollapsing(ibDataViewEvent& event)
{
	// The root config pair stays open — collapsing it leaves the user
	// with a single uninformative row. Veto only that specific item;
	// every other branch behaves normally.
	if (m_model.get() != nullptr && event.GetItem() == m_model->GetRootItem())
		event.Veto();
}

void ibDialogConfigCompare::OnApplyMerge(wxCommandEvent& WXUNUSED(event))
{
	if (m_model.get() == nullptr || !m_model->HasSelection())
		return;

	const bool pull = (m_directionChoice == nullptr
		|| m_directionChoice->GetSelection() == 0);

	if (!pull && !m_rightSaveCallback) {
		wxMessageBox(
			_("Push direction has no save target — open the other "
			  "configuration from a writable source first."),
			_("Merge"), wxOK | wxICON_WARNING, this);
		return;
	}

	// For each selected merge-candidate, dispatch by status × direction:
	//   Pull:  OnlyInRight→ADD, OnlyInLeft→DELETE, Changed→REPLACE
	//   Push:  OnlyInLeft→ADD, OnlyInRight→DELETE, Changed→REPLACE
	// "Source" is the side we copy from; "target" is the side we
	// mutate. Reordered is V2 (skipped with a counter).
	const auto& records = m_model->GetRecords();
	int applied = 0;
	int skippedReorder = 0;

	for (size_t i = 0; i < records.size(); ++i) {
		if (!m_model->IsSelected(static_cast<int>(i)))
			continue;
		const ibMetaDiffRecord& rec = records[i];
		if (!rec.IsMergeCandidate())
			continue;

		// Descendants of a selected OnlyIn* parent — the parent's
		// recursive CopyObject covers the subtree on a single Apply.
		if (HasOnlyInAncestor(static_cast<int>(i)))
			continue;

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

		// Wrap each per-record apply in a try/catch — a DDL failure on
		// a single Catalog shouldn't abort the whole merge. Backend
		// exceptions get formatted into a list so the user sees which
		// rows failed.
		try {
			if (addOp)          { ApplyAdd(rec, pull);     ++applied; }
			else if (delOp)     { ApplyDelete(rec, pull);  ++applied; }
			else if (replaceOp) { ApplyReplace(rec, pull); ++applied; }
		}
		catch (const ibBackendException& err) {
			wxLogMessage(wxT("[merge] %s: %s"),
				rec.GetAnyObject() != nullptr
					? rec.GetAnyObject()->GetName()
					: wxString(),
				err.GetErrorDescription());
		}
		catch (...) {
			wxLogMessage(wxT("[merge] unknown error on %s"),
				rec.GetAnyObject() != nullptr
					? rec.GetAnyObject()->GetName()
					: wxString());
		}
	}

	// Persist the right-side config after a Push so the file (or
	// whatever the right backing is) reflects the mutations.
	bool savedRight = true;
	if (!pull && m_rightSaveCallback)
		savedRight = m_rightSaveCallback();

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
		wxOK | (savedRight ? wxICON_INFORMATION : wxICON_WARNING), this);
	EndModal(wxID_OK);
}

void ibDialogConfigCompare::OnDirectionChanged(wxCommandEvent& WXUNUSED(event))
{
	// No visual change yet — the Apply button stays enabled either way;
	// status text in the result dialog explains what just happened.
	RefreshApplyButtonState();
}

void ibDialogConfigCompare::RefreshDirectionChoice()
{
	if (m_directionChoice == nullptr)
		return;

	const int prevSelection = m_directionChoice->GetSelection();
	m_directionChoice->Clear();

	// Pull always exists — the left-side mutation requires no extra
	// save wiring (designer handles activeMetaData; compare-two-files
	// could rely on a separate left-save callback if it lands).
	m_directionChoice->Append(wxString::Format(_("%s -> %s"),
		m_directionRightLabel, m_directionLeftLabel));

	// Push exists only when there's a target to persist into.
	if (m_rightSaveCallback) {
		m_directionChoice->Append(wxString::Format(_("%s -> %s"),
			m_directionLeftLabel, m_directionRightLabel));
	}

	// Preserve prior selection when possible; otherwise default to Pull.
	const int newCount = static_cast<int>(m_directionChoice->GetCount());
	m_directionChoice->SetSelection(
		prevSelection >= 0 && prevSelection < newCount ? prevSelection : 0);
}

void ibDialogConfigCompare::ApplyAdd(const ibMetaDiffRecord& rec, bool pull)
{
	ibValueMetaObject* source = pull ? rec.m_right : rec.m_left;
	if (source == nullptr)
		return;
	ibValueMetaObject* targetParent = FindTargetParent(rec, pull);
	if (targetParent == nullptr)
		return;

	// Serialize the source subtree (CopyObject is recursive, so every
	// descendant comes along with its GUID preserved inside the buffer).
	ibWriterMemory writer;
	if (!source->CopyObject(writer))
		return;
	ibReaderMemory reader(writer.pointer(), writer.size());

	ibMetaData* meta = targetParent->GetMetaData();
	if (meta == nullptr)
		return;
	ibValueMetaObject* newObj = meta->CreateMetaObject(
		source->GetClassType(), targetParent, /*runObject*/ false);
	if (newObj == nullptr)
		return;

	if (newObj->PasteObject(reader)) {
		// The public PasteObject path runs PasteAndRunObject which
		// reads but discards the top-level GUID — restore it
		// explicitly so future compares pair the new object with
		// its source-side counterpart.
		newObj->SetCommonGuid(source->GetGuid());
	}
}

void ibDialogConfigCompare::ApplyDelete(const ibMetaDiffRecord& rec, bool pull)
{
	ibValueMetaObject* target = pull ? rec.m_left : rec.m_right;
	if (target == nullptr)
		return;
	ibMetaData* meta = target->GetMetaData();
	if (meta == nullptr)
		return;
	meta->RemoveMetaObject(target, target->GetParent());
}

void ibDialogConfigCompare::ApplyReplace(const ibMetaDiffRecord& rec, bool pull)
{
	ibValueMetaObject* source = pull ? rec.m_right : rec.m_left;
	ibValueMetaObject* target = pull ? rec.m_left  : rec.m_right;
	if (source == nullptr || target == nullptr)
		return;
	ibValueMetaObject* parent = target->GetParent();
	ibMetaData* meta = target->GetMetaData();
	if (meta == nullptr || parent == nullptr)
		return;

	// Serialize the source subtree BEFORE deleting the target — the
	// target's child objects shouldn't be alive when we deserialize,
	// but the source buffer must be filled while source is intact.
	ibWriterMemory writer;
	if (!source->CopyObject(writer))
		return;
	ibReaderMemory reader(writer.pointer(), writer.size());
	const ibGuid    sourceGuid  = source->GetGuid();
	const ibClassID sourceClsid = source->GetClassType();

	meta->RemoveMetaObject(target, parent);

	ibValueMetaObject* newObj = meta->CreateMetaObject(
		sourceClsid, parent, /*runObject*/ false);
	if (newObj == nullptr)
		return;
	if (newObj->PasteObject(reader))
		newObj->SetCommonGuid(sourceGuid);
}

bool ibDialogConfigCompare::HasOnlyInAncestor(int recordIndex) const
{
	if (m_model.get() == nullptr)
		return false;
	const auto& records = m_model->GetRecords();
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

ibValueMetaObject* ibDialogConfigCompare::FindTargetParent(
	const ibMetaDiffRecord& rec, bool pull) const
{
	if (m_model.get() == nullptr)
		return nullptr;
	const auto& records = m_model->GetRecords();
	int p = rec.m_parentIndex;
	while (p >= 0) {
		const ibMetaDiffRecord& parentRec = records[p];
		// Group / property rows aren't real meta objects — skip up to
		// the next paired-object ancestor and read its target side.
		if (!parentRec.IsGroup() && !parentRec.IsProperty()) {
			ibValueMetaObject* candidate = pull
				? parentRec.m_left
				: parentRec.m_right;
			if (candidate != nullptr)
				return candidate;
		}
		p = parentRec.m_parentIndex;
	}
	return nullptr;
}

void ibDialogConfigCompare::RefreshApplyButtonState()
{
	if (m_buttonApply == nullptr || m_model.get() == nullptr)
		return;
	m_buttonApply->Enable(m_model->HasSelection());
}
