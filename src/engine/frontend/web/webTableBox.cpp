#include "webTableBox.h"

#include "frontend/visualView/ctrl/tableBox.h"
#include "frontend/visualView/ctrl/form.h"   // the owner form IS the backend form ActivateItem takes

#include "backend/backend_type.h"          // ibTypeDescription
#include "backend/compiler/value.h"        // the primitive clsids
#include "backend/clsid.h"                 // IsReference / IsEnum — the kind is IN the id

#include <algorithm>

// ---------------------------------------------------------------------------
// Names the browser knows things by
// ---------------------------------------------------------------------------

wxString ibWebTableBoxColumnKey(int controlId)
{
	return wxString::Format(wxT("c%d"), controlId);
}

wxString ibWebAlignName(int alignment)
{
	if ((alignment & wxALIGN_RIGHT) == wxALIGN_RIGHT)
		return wxT("right");
	if ((alignment & wxALIGN_CENTER_HORIZONTAL) == wxALIGN_CENTER_HORIZONTAL)
		return wxT("center");
	return wxT("left");
}

wxString ibWebGroupingName(int kind)
{
	switch (kind) {
	case ibColumnGroupHorizontal: return wxT("horizontal");
	case ibColumnGroupInCell:     return wxT("incell");
	default:                      return wxT("vertical");
	}
}

wxString ibWebValueTypeName(const ibTypeDescription& type)
{
	if (type.GetClsidCount() == 0)
		return wxT("string");
	const ibClassID clsid = type.GetFirstClsid();
	if (clsid == g_valueNumberCLSID)  return wxT("number");
	if (clsid == g_valueDateCLSID)    return wxT("date");
	if (clsid == g_valueBooleanCLSID) return wxT("boolean");
	if (clsid == g_valueStringCLSID)  return wxT("string");
	if (IsReference(clsid))           return wxT("reference");
	if (IsEnum(clsid))                return wxT("enum");
	return wxT("string");
}

// ---------------------------------------------------------------------------
// Column / group JSON
// ---------------------------------------------------------------------------

nlohmann::json ibWebTableBoxColumn::ToJSON() const
{
	auto node = ibWebWindow::ToJSON();
	node["caption"]     = m_caption;
	node["field"]       = m_field;
	node["width"]       = m_width;
	node["align"]       = m_align;
	node["headerAlign"] = m_headerAlign;
	node["valueType"]   = m_valueType;
	node["visible"]     = m_visible;
	node["resizable"]   = m_resizable;
	node["readOnly"]    = m_readOnly;
	node["sortable"]    = m_sortable;
	node["sortOrder"]   = m_sortOrder;
	node["showSelectButton"] = m_showSelectButton;
	node["showOpenButton"]   = m_showOpenButton;
	node["showClearButton"]  = m_showClearButton;
	return node;
}

// A cell edit. The column writes it through the same door the desktop's inline
// editor writes through — SetControlValue on the current line — so an OnChange
// script and a source-object update happen exactly as they do there.
bool ibWebTableBoxColumn::HandleRequest(const wxString& kind, const wxString& value)
{
	if (m_requestControl == nullptr)
		return false;
	if (kind == wxT("cell"))       return m_requestControl->WebCellChanged(value);
	if (kind == wxT("cellSelect")) return m_requestControl->WebCellChoose();
	if (kind == wxT("cellOpen"))   return m_requestControl->WebCellOpen();
	if (kind == wxT("cellClear"))  return m_requestControl->WebCellClear();
	return false;
}

nlohmann::json ibWebTableBoxColumnGroup::ToJSON() const
{
	auto node = ibWebWindow::ToJSON();
	node["caption"]   = m_caption;
	node["grouping"]  = m_grouping;
	node["showTitle"] = m_showTitle;
	node["align"]     = m_align;
	node["visible"]   = m_visible;
	return node;
}

nlohmann::json ibWebTableBox::ToJSON() const
{
	auto node = ibWebWindow::ToJSON();
	node["header"]     = m_header;
	node["footer"]     = m_footer;
	node["viewMode"]   = m_viewMode;
	node["choiceMode"] = m_choiceMode;
	node["pageSize"]   = m_pageSize;
	node["dataVersion"] = m_dataVersion;
	return node;
}

// ---------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------

namespace {

// Every column under this node, in the order the header draws them.
// Groups are transparent here — a group decides where its members sit,
// not whether they have values, so its columns are collected as if they
// hung on the table directly.
void CollectColumns(const ibValueFrame* node,
	std::vector<ibValueModelTableBoxColumn*>& out)
{
	if (node == nullptr)
		return;
	for (unsigned int i = 0; i < node->GetChildCount(); ++i) {
		ibValueFrame* child = dynamic_cast<ibValueFrame*>(node->GetChild(i));
		if (child == nullptr)
			continue;
		if (auto* column = dynamic_cast<ibValueModelTableBoxColumn*>(child)) {
			out.push_back(column);
			continue;
		}
		if (dynamic_cast<ibValueModelTableBoxColumnGroup*>(child) != nullptr)
			CollectColumns(child, out);
	}
}

// One cell, as a display string plus the kind of thing it is.
//
// The resolve order is the desktop renderer's (CheckedGetValue): a
// column that reaches PAST the row — a dot-path, or one rooted at the
// object above the table — is resolved by the table; a plain column is
// read off the row by its model column id.
bool ReadCell(const ibValueModelTableBox* table, ibValueModel* model,
	const ibDataViewItem& item, const ibValueModelTableBoxColumn* column,
	wxString& text, wxString& kind)
{
	wxVariant variant;
	if (!table->ResolveCellValue(item, column, variant)) {
		ibValue value;
		if (!model->GetValueByMetaID(item, column->GetModelColumn(), value))
			return false;
		ibValueModel::ValueToVariant(variant, value);
	}
	if (variant.IsNull())
		return false;
	text = variant.MakeString();
	kind = variant.GetType();
	return true;
}

// Whether any column of this row carries an inline editor. The desktop asks the
// same question through EditCurrentRow, which walks the columns looking for one
// the model calls editable and starts the editor there; here the browser has
// already started it, so all that is wanted is the yes or no.
bool EditableRow(const ibValueModelTableBox* table, ibValueModel* model,
	const ibDataViewItem& item)
{
	std::vector<ibValueModelTableBoxColumn*> columns;
	CollectColumns(table, columns);
	for (const ibValueModelTableBoxColumn* column : columns) {
		if (model->EditableLine(item, column->GetModelColumn()))
			return true;
	}
	return false;
}

} // namespace

// The window key of the control's current line, or -1 when the control
// has none or it names a row this window does not hold. Items compare by
// identity, which is what the window stores.
int ibWebTableBox::CurrentKey(ibValueModelTableBox* control) const
{
	if (control == nullptr)
		return -1;
	ibValueModel::ibValueModelReturnLine* const line = control->GetCurrentLine();
	if (line == nullptr)
		return -1;
	const ibDataViewItem current = line->GetLineItem();
	if (!current.IsOk())
		return -1;
	for (const WindowRow& row : m_window) {
		if (row.item == current)
			return row.key;
	}
	return -1;
}

nlohmann::json ibWebTableBox::FetchPage(ibValueModelTableBox* control,
	const wxString& dir, int count)
{
	nlohmann::json out = {
		{ "ok",      false },
		{ "control", GetControlId() },
		{ "rows",    nlohmann::json::array() },
	};

	if (control == nullptr) {
		out["reason"] = "no control";
		return out;
	}

	ibValueModel* model = control->GetTableModel();
	if (model == nullptr) {
		// A table whose source resolved to nothing — an unbound
		// tablebox, or a form built before its attribute existed. Not
		// an error, just empty.
		out["reason"] = "no model";
		return out;
	}

	if (count <= 0)
		count = m_pageSize;

	const bool first = dir != wxT("next") && dir != wxT("prev");
	if (first) {
		m_window.clear();
		m_nextKey = 0;
	}

	// An anchor only holds while its row is still attached to the model
	// — a refetch after the model reset leaves the old window pinned
	// but dead, and paging from it would ask about a row nobody has.
	const ibDataViewItem* anchor = nullptr;
	if (!first && !m_window.empty()) {
		const ibDataViewItem& end = dir == wxT("next")
			? m_window.back().item : m_window.front().item;
		if (end.IsOk() && end.GetID()->IsAttached())
			anchor = &end;
	}

	if (!first && anchor == nullptr) {
		// Asked to continue from a window that no longer stands. Say so
		// rather than silently answering with the top of the list, which
		// the client would append as if it were the next page.
		out["reason"] = "stale window";
		return out;
	}

	if (!first && static_cast<int>(m_window.size()) >= kWindowLimit) {
		out["ok"]      = true;
		out["hasMore"] = false;
		out["reason"]  = "window limit";
		out["dataVersion"] = model->GetViewGeneration();
		return out;
	}

	// A flat list over a source that knows folders asks for every row in
	// one order rather than recursing per folder; that is what the
	// ignore-parent sentinel says. Hierarchical and tree views ask for
	// the top level and drill from there.
	const bool flat = m_viewMode == wxT("list");
	const ibDataViewItem& parent = flat ? s_constIgnoreParent : ibDataViewItem();

	ibDataViewItemArray fetched;
	if (dir == wxT("next"))
		model->GetNextFetch(parent, *anchor, count, fetched);
	else if (dir == wxT("prev"))
		model->GetPrevFetch(parent, *anchor, count, fetched);
	else
		model->GetFirstFetch(parent, ibDataViewItem(), count, fetched);

	// Backward paging comes back in the direction it was walked; the
	// window is kept in display order either way.
	const bool backward = dir == wxT("prev");

	std::vector<WindowRow> page;
	page.reserve(fetched.GetCount());
	for (size_t i = 0; i < fetched.GetCount(); ++i) {
		WindowRow row;
		row.key  = m_nextKey++;
		row.item = fetched[backward ? fetched.GetCount() - 1 - i : i];
		page.push_back(std::move(row));
	}

	if (backward)
		m_window.insert(m_window.begin(), page.begin(), page.end());
	else
		m_window.insert(m_window.end(), page.begin(), page.end());

	std::vector<ibValueModelTableBoxColumn*> columns;
	CollectColumns(control, columns);

	nlohmann::json rows = nlohmann::json::array();
	for (const WindowRow& windowRow : page) {
		const ibDataViewItem& item = windowRow.item;
		nlohmann::json row = {
			{ "key",       windowRow.key },
			{ "container", model->IsContainer(item) },
		};

		// A GROUP node carries a caption instead of a row of its own —
		// the dimension value it folds by. The node self-describes it.
		wxString groupCaption;
		if (item.IsOk() && item.GetMode() == ibDataViewItem::Mode::Refcounted
			&& item.GetID()->GetGroupCaption(groupCaption))
			row["group"] = groupCaption;

		nlohmann::json cells = nlohmann::json::object();
		for (const ibValueModelTableBoxColumn* column : columns) {
			wxString text, kind;
			if (ReadCell(control, model, item, column, text, kind))
				cells[ibWebTableBoxColumnKey(column->GetControlID()).ToStdString()] = text;
		}
		row["cells"] = std::move(cells);
		rows.push_back(std::move(row));
	}

	out["ok"]    = true;
	out["reset"] = first;
	out["rows"]  = std::move(rows);
	out["count"] = static_cast<int>(page.size());
	// Which row the CONTROL is standing on. The browser highlights a row
	// because the server's current line says so, not because a click
	// happened in this particular DOM — so the mark survives a sort, a
	// command, or any other answer that rebuilds the grid. Absent when
	// the current line is outside the window we are holding.
	if (const int key = CurrentKey(control); key >= 0)
		out["currentKey"] = key;
	// "There is more that way" is only ever answered by asking, so what
	// is reported is what this page knows: a short page is the end.
	out["hasMore"] = static_cast<int>(page.size()) >= count;
	// The generation these rows were read at, taken AFTER the read: a
	// snapshot list bumps it per row while it materialises, so a number
	// taken before would already be stale by the time the page is out.
	// The browser compares the tree's dataVersion against this one, not
	// against the tree it last saw, so a fetch that moved the counter is
	// not mistaken for a change to the rows it just received.
	out["dataVersion"] = model->GetViewGeneration();
	out.erase("reason");
	return out;
}

// Sorting a list is an ORDER BY over the whole table, committed to the
// composer — not a reshuffle of the page in hand. The shape is the desktop's
// OnColumnClick verbatim, minus the header arrow it sets on the widget: read
// the column's own bound field, toggle it against what the composer already
// says, clear and re-sort, then read the list again from the top. The paged
// keyset anchor was built for the old order and is worthless now, which is why
// the window is thrown away rather than continued.
static bool SortByColumn(ibValueModelTableBox* table, int columnControlId)
{
	ibValueModel* model = table->GetTableModel();
	if (model == nullptr || !model->GetFeatures().Has(ibValueModel::Features::Sorting))
		return false;

	std::vector<ibValueModelTableBoxColumn*> columns;
	CollectColumns(table, columns);
	const auto found = std::find_if(columns.begin(), columns.end(),
		[columnControlId](const ibValueModelTableBoxColumn* column) {
			return column->GetControlID() == columnControlId;
		});
	if (found == columns.end())
		return false;

	const wxString field = (*found)->GetSourceFieldName();
	if (field.IsEmpty())
		return false;   // whole-attribute / foreign / unresolvable — nothing to sort by

	ibDataComposer& composer = model->GetModelComposer();
	bool ascending = true;
	wxString currentField; bool currentAscending = true;
	if (composer.SortCount() == 1 && composer.GetSortAt(0, currentField, currentAscending)
		&& currentField == field)
		ascending = !currentAscending;

	composer.ClearSorts();
	composer.Sort(field, ascending);
	model->RefetchAll();
	return true;
}

namespace {
// Every ibWebTableBoxColumn under this node, groups walked through.
void CollectWebColumns(const ibWebWindow* node, std::vector<ibWebTableBoxColumn*>& out)
{
	for (ibWebWindow* child : node->GetChildren()) {
		if (auto* column = dynamic_cast<ibWebTableBoxColumn*>(child))
			out.push_back(column);
		else
			CollectWebColumns(child, out);
	}
}
} // namespace

void ibWebTableBox::SyncSortOrders(const ibValueModelTableBox* control)
{
	ibValueModel* model = control != nullptr ? control->GetTableModel() : nullptr;
	if (model == nullptr)
		return;

	std::vector<ibValueModelTableBoxColumn*> columns;
	CollectColumns(control, columns);
	std::vector<ibWebTableBoxColumn*> webColumns;
	CollectWebColumns(this, webColumns);

	const ibDataComposer& composer = model->GetModelComposer();
	for (const ibValueModelTableBoxColumn* column : columns) {
		wxString order = wxT("none");
		const wxString field = column->GetSourceFieldName();
		for (size_t i = 0; !field.IsEmpty() && i < composer.SortCount(); ++i) {
			wxString sortField; bool ascending = true;
			if (composer.GetSortAt(i, sortField, ascending) && sortField == field) {
				order = ascending ? wxT("asc") : wxT("desc");
				break;
			}
		}
		// The two trees are paired by the key both sides derive from the
		// same control id, which is why neither has to hold the other.
		const wxString key = ibWebTableBoxColumnKey(column->GetControlID());
		for (ibWebTableBoxColumn* webColumn : webColumns)
			if (webColumn->GetFieldKey() == key)
				webColumn->SetSortOrder(order);
	}
}

bool ibWebTableBox::HandleRequest(const wxString& kind, const wxString& value)
{
	if (m_requestControl == nullptr)
		return false;

	if (kind == wxT("sort")) {
		long columnId = 0;
		if (!value.ToLong(&columnId))
			return false;
		if (!SortByColumn(m_requestControl, static_cast<int>(columnId)))
			return false;
		// The arrows are read off the composer by SyncSortOrders before the
		// tree is serialised, so nothing has to be written here.
		// The order changed under the window, so the keys in it name rows
		// nobody is looking at any more. The client re-fetches from the top.
		m_window.clear();
		m_nextKey = 0;
		return true;
	}

	const bool cursor   = kind == wxT("row");
	const bool activate = kind == wxT("activate");
	if (!cursor && !activate)
		return false;

	long key = 0;
	if (!value.ToLong(&key))
		return false;

	const auto found = std::find_if(m_window.begin(), m_window.end(),
		[key](const WindowRow& row) { return row.key == static_cast<int>(key); });
	if (found == m_window.end())
		return false;

	ibValueModel* model = m_requestControl->GetTableModel();
	if (model == nullptr)
		return false;
	if (!found->item.IsOk() || !found->item.GetID()->IsAttached())
		return false;

	// Opening a row moves the cursor onto it first: a command run from
	// the form that opens next reads the current line, and a double
	// click is also a click.
	m_requestControl->ApplyCurrentLine(model->GetRowAt(found->item));

	if (activate) {
		// The desktop road is ActivateRow, and this walks all three of its answers.
		//
		// A PICKER hands the row back to whoever opened the list — through the
		// dispatcher rather than straight at the handler, so a click on the bar's
		// Select tool and a double click are one road.
		//
		// An EDITABLE row raises nothing: the desktop starts its inline editor
		// there (EditCurrentRow), the browser has already started its own, and
		// opening a form on top of it would take the row out from under what is
		// being typed.
		//
		// Anything else — a list row — has the model raise the row's own value.
		if (m_requestControl->IsChoiceMode()) {
			m_requestControl->CallAsAction(ibValueModelTableBox::enTableSelect,
				m_requestControl->GetOwnerForm());
		}
		else if (!EditableRow(m_requestControl, model, found->item)) {
			model->ActivateItem(found->item, m_requestControl->GetOwnerForm());
		}
	}
	return true;
}
