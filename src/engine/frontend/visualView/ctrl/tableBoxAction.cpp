#include "tableBox.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/picturePredefined.h"          // g_pic*CLSID — the TableBox composes the standard command band
#include "backend/composition/listFilter.h"     // ibValueListSettings::GetFilter()->Add / Clear + ibComparisonKind
#include "backend/appData.h"
#include "form.h"

//****************************************************************************
//*                              actionData                                  *
//****************************************************************************

// The TableBox composes its command interface the way a form does (formAction.cpp): it MERGES the bound model's
// OWN narrow command set and DECORATES it with the standard, table-generic band — Select (choice), Filter /
// FilterByColumn / FilterClear, ViewMode. The ids are the TableBox's own (high base, like the form's enClose)
// so they never collide with a model's object-command ids; unknown ids are OBJECT commands and go to the model.
enum
{
	enTableSelect = 20000,
	enTableFilter,
	enTableFilterByColumn,
	enTableFilterClear,
	enTableViewMode,
};

ibValueModelTableBox::ibStandardCommandSet ibValueModelTableBox::GetStandardCommands(const ibFormID& formType)
{
	// Resolve the model: the created one, or (unbound path) the bound form-attribute's model.
	ibValuePtr<ibValueModel> resolved;
	ibValueModel* model = m_tableModel;
	if (model == nullptr && !m_propertySource->IsEmptyProperty() && m_formOwner != nullptr &&
		m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), resolved))
		model = resolved;

	if (model == nullptr)
		return ibStandardCommandSet();

	ibStandardCommandSet actionData(this);

	// 1) Select — always FIRST, only when this table is a picker (the TableBox's own affordance). View-state,
	//    not a data change → stays live in a view-only form.
	if (IsChoiceMode())
		actionData.AddAction(wxT("Select"), _("Select"), g_picSelectCLSID, true, enTableSelect).SetModify(false);

	// 2) The model's OWN command set, merged in — the model is just a command STORE (GetCommandCollection), the TableBox
	//    lays it out into the real action (name / caption / picture / separators), carrying each command's modify flag.
	std::vector<ibCommandItem> commands;
	model->GetCommandCollection(formType, commands);

	for (const ibCommandItem& c : commands) {
		if (c.m_actionId == wxNOT_FOUND)
			actionData.AddSeparator();
		else
			actionData.AddAction(c.m_name, c.m_caption, c.m_pictureDescription, c.m_pictureAndText, c.m_actionId).SetModify(c.m_modifiesData);
	}

	// 3) The standard view-state band — Filter / by-column / clear, ViewMode — never changes DATA, so it stays
	//    live in a view-only form (only the model's Add / Delete / Copy row greys out).
	actionData.AddSeparator();
	actionData.AddAction(wxT("Filter"), _("Filter"), g_picFilterCLSID, false, enTableFilter).SetModify(false);
	actionData.AddAction(wxT("FilterByColumn"), _("Filter by column"), g_picFilterSetCLSID, false, enTableFilterByColumn).SetModify(false);
	actionData.AddAction(wxT("FilterClear"), _("Filter clear"), g_picFilterClearCLSID, false, enTableFilterClear).SetModify(false);

	actionData.AddSeparator();
	actionData.AddAction(wxT("ViewMode"), _("View mode"), g_picHierarchyCLSID, false, enTableViewMode).SetModify(false);

	return actionData;
}

void ibValueModelTableBox::CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	if (m_tableModel == nullptr || appData->DesignerMode())
		return;

	// The FRONT owns the rows a command runs against — read them once here. m_selection = the selected row
	// (delete / edit / copy target). m_anchor = WHERE a new element is created — CREATE always anchors here,
	// NEVER on the selection — resolved PER VIEW MODE:
	//   List         → flat, no hierarchy → no anchor (a new element lands at the root).
	//   Hierarchical → the folder the user has drilled INTO — the frozen top-parent crumb (rendered separately
	//                  above the scroll area), NOT GetTopItem (that returns the first NON-frozen visible row).
	//                  Empty when at the root → a new element lands at the root.
	//   Tree         → the folder the user stands in: the current item if it IS a folder, else its parent folder.
	ibDataViewCommandContext ctx;
	ctx.m_selection = m_tableCurrentLine != nullptr ? m_tableCurrentLine->GetLineItem() : ibDataViewItem();
	if (auto* ctrl = dynamic_cast<ibDataViewCtrl*>(GetInnerWx())) {
		switch (ctrl->GetViewMode()) {
		case ibDataViewHierarchical:
			ctx.m_anchor = ctrl->GetDrillHierarchyItem();   // the drilled-into folder (the hierarchical-drill crumb, rendered separately above the scroll)
			break;
		case ibDataViewTree: {
			const ibDataViewItem cur = ctrl->GetCurrentItem();
			ctx.m_anchor = cur.IsContainer() ? cur : cur.GetParentItem();
			break;
		}
		case ibDataViewList:
		default:
			break;   // flat list — no anchor
		}
	}

	switch (lNumAction)
	{
	case enTableSelect:         Command_Choose(srcForm);             break;   // returns the current ReturnLine
	case enTableFilter:         Command_ShowListSettings();          break;   // direct → control
	case enTableFilterByColumn: Command_FilterByCurrentColumn();     break;   // direct → control + L5
	case enTableFilterClear:    Command_ClearFilter();               break;   // direct → L5
	case enTableViewMode:       Command_ShowViewMode();              break;   // direct → control
	default:
		// The model runs its command against the current ROW as-is (its Edit id has the eStartEditingFlag bit baked
		// in, so its own `case eEditValue` matches — a list opens the object form, a value-table does nothing there).
		// Then, if that bit is set, the FRONT ALSO forces the row's inline editor: a value-table / tabular row edits
		// inline (EditCurrentRow), a list row no-ops (not inline-editable — its form already opened). Tested per id.
		m_tableModel->CallAsCommand(lNumAction, ctx, srcForm);
		if (lNumAction & eStartEditingFlag)
			EditCurrentRow(ctx.m_selection);
		break;
	}
}

// Open the inline cell editor on `item`'s first editable cell (FRONT). Prefer the column the user is on; else the
// first editable column (skips a read-only one like the tabular line-number). false if no cell is editable (a list
// row — the caller opens the object form instead). Shared by double-click AND the Edit command's eStartEditingFlag
// intercept in CallAsAction.
bool ibValueModelTableBox::EditCurrentRow(const ibDataViewItem& item)
{
	if (!item.IsOk() || m_tableModel == nullptr)
		return false;

	auto* ctrl = dynamic_cast<ibDataViewCtrl*>(GetInnerWx());
	if (ctrl == nullptr)
		return false;

	ibDataViewColumn* editCol = nullptr;
	ibDataViewColumn* cur = ctrl->GetCurrentColumn();
	if (cur != nullptr && m_tableModel->EditableLine(item, cur->GetModelColumn()))
		editCol = cur;
	else
		for (unsigned int i = 0; i < ctrl->GetColumnCount(); ++i) {
			ibDataViewColumn* c = ctrl->GetColumnAt(i);
			if (c != nullptr && m_tableModel->EditableLine(item, c->GetModelColumn())) { editCol = c; break; }
		}

	if (editCol == nullptr)
		return false;

	ctrl->EditItem(item, editCol);
	return true;
}


//****************************************************************************
//*   Command handlers — the view-state band, driven DIRECTLY on the control *
//****************************************************************************

void ibValueModelTableBox::Command_Choose(ibBackendValueForm* srcForm)
{
	// Choice returns the CURRENT ROW as a value — the ReturnLine, which itself pins the model alive for as long
	// as the caller (the opener) holds it. NotifyChoice hands it over; no reference re-resolution on the model.
	ibValueModel::ibValueModelReturnLine* line = GetCurrentLine();
	if (line == nullptr || srcForm == nullptr)
		return;

	// The picker returns the row's SELECT value — a reference / key, defined PER LINE TYPE (GetSelectValue),
	// not the generic row value.
	ibValue selectValue = line->GetSelectValue();
	srcForm->NotifyChoice(selectValue);
}

void ibValueModelTableBox::Command_ShowListSettings()
{
	if (auto* ctrl = dynamic_cast<ibTableViewCtrl*>(GetInnerWx()))
		ctrl->ShowListSettings(m_tableModel);
}

void ibValueModelTableBox::Command_FilterByCurrentColumn()
{
	auto* ctrl = dynamic_cast<ibTableViewCtrl*>(GetInnerWx());
	if (ctrl == nullptr || m_tableModel == nullptr || m_tableModel->GetListSettings() == nullptr)
		return;

	// Current row + current column come straight off the live control — the model never pulls them.
	const ibDataViewItem sel = ctrl->GetSelection();
	ibDataViewColumn* col = ctrl->GetCurrentColumn();
	if (!sel.IsOk() || col == nullptr)
		return;

	const unsigned int colId = col->GetModelColumn();
	ibValue value;
	m_tableModel->GetValueByMetaID(sel, colId, value);      // reading a cell value is a plain data op
	const wxString name = m_tableModel->GetColumnNameByID(colId);
	if (!name.empty()) {
		m_tableModel->GetListSettings()->GetFilter()->Add(name, ibComparisonKind_Equal, value);
		m_tableModel->RefetchAll();
	}
}

void ibValueModelTableBox::Command_ClearFilter()
{
	if (m_tableModel == nullptr || m_tableModel->GetListSettings() == nullptr)
		return;
	m_tableModel->GetListSettings()->GetFilter()->Clear();
	m_tableModel->RefetchAll();
}

void ibValueModelTableBox::Command_ShowViewMode()
{
	if (auto* ctrl = dynamic_cast<ibTableViewCtrl*>(GetInnerWx()))
		ctrl->ShowViewMode();
}