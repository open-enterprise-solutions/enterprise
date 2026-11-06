#include "predefinedEditor.h"

#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/statbox.h>
#include <wx/valtext.h>   // wxTextValidator — a number typed as nothing but a number

#include "core/stringUtils.h"
#include "frmclient/backend/compiler/value.h"           // ibValue — a value ticked or typed, as it travels
#include "frmclient/backend/system/value/valueType.h"   // ibValueTypeDescription::AdjustValue — typed text, a value of the type
#include "frmclient/visualView/ctrl/frame.h"
#include "frmclient/win/dlgs/selectPredefined.h"        // ibShowPredefinedSelector — a declared value picked
#include "frmclient/win/dlgs/textWithDotsCell.h"        // ibTextWithDotsRenderer, ibTextCtrl — a value picked through "..."
#include "frmclient/win/dlgs/typeSelector.h"            // ibShowTypeSelector — a type picked
#include "gui/univ/theme/luna_toolbarart.h"
#include "protocol/protocolDesigner.h"                          // ibProtocol::ibRequest::ibPredefinedSelector — what that window is given

enum
{
	model_name,
	model_code,
	model_description
};

void ibDialogPredefinedEditor::ibDataViewPredefinedTreeStore::GetValue(ibVariant& variant,
	const ibDataViewItem& item, unsigned int col) const
{
	if (item.GetID() == (void*)1)
	{
		if (col == model_name)
			variant = _("Predefined items");

		return;
	}

	ibPredefinedItem* predefined =
		static_cast<ibPredefinedItem*>(item.GetID());

	if (predefined != nullptr) {
		if (col == model_name)
			variant = predefined->GetPredefinedName();
		else if (col == model_code)
			variant = predefined->GetPredefinedCode();
		else if (col == model_description)
			variant = predefined->GetPredefinedDescription();
	}
}

bool ibDialogPredefinedEditor::ibDataViewPredefinedTreeStore::IsContainer(const ibDataViewItem& item) const
{
	if (item.GetID() == (void*)1)
		return true;

	ibPredefinedItem* predefined =
		static_cast<ibPredefinedItem*>(item.GetID());

	// A folder holds; so does any item another stands under — where the kind's items stand under one another (an
	// account under an account).
	if (predefined != nullptr) {
		if (predefined->IsPredefinedFolder())
			return true;
		for (const auto& object : m_valueMetaObjectHierarchy->GetPredefinedValueArray())
			if (object->GetPredefinedParent().get() == predefined)
				return true;
		return false;
	}

	return true;
}

unsigned int ibDialogPredefinedEditor::ibDataViewPredefinedTreeStore::GetFirstFetch(
	const ibDataViewItem& parent,
	const ibDataViewItem& /*anchor*/, int /*count*/,
	ibDataViewItemArray& out) const
{
	// Root-level request: return every predefined whose parent is null.
	// Folder-level request: return predefineds whose GetPredefinedParent()
	// matches the requested folder. Prior code returned the flat list for
	// every call, which forced the tree to render everything at the root and
	// never displayed folder nesting even though the backend stored it.
	const ibPredefinedItem* parentObj = parent.IsOk()
		? static_cast<const ibPredefinedItem*>(parent.GetID())
		: nullptr;

	for (const auto& object : m_valueMetaObjectHierarchy->GetPredefinedValueArray()) {
		const ibPredefinedItem* const itemParent = object->GetPredefinedParent().get();
		if (itemParent == parentObj)
			out.Add(ibDataViewItem(object.get()));
	}

	return out.Count();
}

/////////////////////////////////////////////////////////////////////////////////////

namespace {

// What an attribute or a section is shown as: its caption, its name where it has none.
wxString Caption(const ibString& title, const ibString& name)
{
	return title.IsEmpty() ? wxString(name) : wxString(title);
}

// A FLAG — what a tick says: an attribute of the boolean type alone.
bool IsFlag(const ibPredefinedAttributeDescription& attribute)
{
	return attribute.m_type.GetClsidCount() == 1 && attribute.m_type.ContainType(ibValueTypes::TYPE_BOOLEAN);
}

// PICKED THROUGH ITS "..." — a type, or one of the closed set (the values its references and enumerations declare): what
// text cannot be. Text is a string, a number or a date.
bool IsPicked(const ibPredefinedAttributeDescription& attribute)
{
	if (attribute.m_holdsType || !attribute.m_choices.empty())
		return true;
	for (const ibClassID& clsid : attribute.m_type.GetClsidList()) {
		const ibValueTypes held = ibValue::GetVTByID(clsid);
		if (held == ibValueTypes::TYPE_STRING || held == ibValueTypes::TYPE_NUMBER || held == ibValueTypes::TYPE_DATE)
			return false;
	}
	return true;
}

// A value ticked or typed, as every value travels.
ibPredefinedCellDescription Packed(const ibValue& value)
{
	ibPredefinedCellDescription cell;
	cell.m_title = value.GetString();
	value.Serialize(cell.m_value);
	return cell;
}

// The value a cell holds where it holds one, else the attribute's default — what a new row or an item that never declared
// a required attribute starts from.
std::optional<ibPredefinedCellDescription> Held(const ibPredefinedAttributeDescription& attribute,
	const ibPredefinedRowDescription& row)
{
	const auto cell = row.find(attribute.m_id);
	if (cell != row.end())
		return cell->second;
	if (attribute.m_default)
		return ibPredefinedCellDescription{ attribute.m_default->m_title, attribute.m_default->m_value };
	return std::nullopt;
}

// …as a table's cell shows it: a tick, or how it reads.
ibVariant Shown(const ibPredefinedAttributeDescription& attribute, const std::optional<ibPredefinedCellDescription>& held)
{
	if (IsFlag(attribute))
		return ibVariant(held && ibValue::FromNode(held->m_value).GetBoolean());
	return ibVariant(held ? held->m_title : ibString());
}

// The names of a type's classes, as the server named them (ibValue::ReadClasses).
wxString DescribeTypes(const ibTypeDescription& type)
{
	wxString described;
	for (const ibClassID& clsid : type.GetClsidList()) {
		const ibCtorAbstractType* const ctor = ibValue::GetAvailableCtor(clsid);
		if (ctor == nullptr)
			continue;
		if (!described.IsEmpty())
			described += wxT(", ");
		described += ctor->GetClassName();
	}
	return described;
}

// ONE OF THE CLOSED SET, PICKED in the designer's own window for a declared value (ibShowPredefinedSelector): one branch,
// the attribute's caption — what is being chosen, named where a type is not (a platform enumeration has no class this
// side names) — and the values declared for it under it. `title` — how the value reads: what the field shows, and what
// TakeText finds the value by.
bool PickValue(wxWindow* parent, const ibPredefinedAttributeDescription& attribute, wxString& title)
{
	ibProtocolNode request;
	ibProtocolNode branch = request.Child(ibProtocol::ibRequest::ibPredefinedSelector::Types).AddChild();
	branch.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Title, Caption(attribute.m_title, attribute.m_name));
	for (std::size_t idx = 0; idx < attribute.m_choices.size(); idx++)
		branch.AddChild()
			.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Name, attribute.m_choices[idx].m_title)
			.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Id, static_cast<long long>(idx));

	ibProtocolNode response;
	if (!ibShowPredefinedSelector(parent, request, response))
		return false;
	const long long id = response.GetInt(ibProtocol::ibRequest::ibPredefinedSelector::Id, -1);
	if (id < 0 || id >= static_cast<long long>(attribute.m_choices.size()))
		return false;
	title = attribute.m_choices[static_cast<std::size_t>(id)].m_title;
	return true;
}

// ⭐ WHAT A BOX OR A LIST SAYS, AS A VALUE — `text` as the person left it, `held` what it held as it came. Unchanged, the
// value it came with (nothing typed is parsed again); one of the closed set, that member as the set carries it; typed,
// made a value of the attribute's type. Empty: not declared.
std::optional<ibPredefinedCellDescription> TakeText(const ibPredefinedAttributeDescription& attribute, const wxString& text,
	const std::optional<ibPredefinedCellDescription>& held)
{
	if (text.IsEmpty())
		return std::nullopt;
	if (held && wxString(held->m_title) == text)
		return held;
	for (const ibPredefinedChoiceDescription& choice : attribute.m_choices)
		if (wxString(choice.m_title) == text)
			return ibPredefinedCellDescription{ choice.m_title, choice.m_value };
	if (!attribute.m_choices.empty())
		return std::nullopt;
	return Packed(ibValueTypeDescription::AdjustValue(attribute.m_type, ibValue(text)));
}

// ⭐ WHAT A BOX TAKES, AS THE TYPE IT IS TYPED INTO SAYS (#646) — a number nothing but a number; a string no longer than
// its length (0: any). A type of several classes says no one of them.
void LimitTyping(wxTextCtrl* box, const ibTypeDescription& type)
{
	if (type.GetClsidCount() != 1)
		return;
	if (type.ContainType(ibValueTypes::TYPE_NUMBER))
		box->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
	else if (type.ContainType(ibValueTypes::TYPE_STRING) && type.GetLength() > 0)
		box->SetMaxLength(type.GetLength());
}

// A required value not given: the person told which, and the window kept.
bool Refuse(const wxString& what)
{
	wxMessageBox(ibString::Format(_("\"%s\" must be filled in"), what));
	return false;
}

}

ibDialogPredefinedEditor::ibDialogPredefinedItem::ibDialogPredefinedItem(ibDialogPredefinedEditor* owner,
	const ibGuid& itemGuid, const ibPredefinedItem* source) :
	wxDialog(owner, wxID_ANY, _("Predefined value"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	m_itemGuid(itemGuid), m_described(owner->GetPredefinedDesc())
{

	const int kPad = FromDIP(5);
	// A copy is a new item: the next code, and rows of its own.
	const bool made = source == nullptr || source->GetPredefinedGuid() != itemGuid;

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* fields = new wxFlexGridSizer(2, kPad, kPad * 2);
	fields->AddGrowableCol(1, 1);

	// A folder, where the kind tells folders from items.
	if (m_described.m_folders) {
		fields->Add(new wxStaticText(this, wxID_ANY, _("Kind")), 0, wxALIGN_CENTER_VERTICAL);
		m_checkFolder = new wxCheckBox(this, wxID_ANY, _("Folder (group)"));
		m_checkFolder->SetValue(source != nullptr && source->IsPredefinedFolder());
		fields->Add(m_checkFolder, 1, wxEXPAND);
	}

	// A parent, where items stand under one another.
	if (m_described.m_parent) {
		fields->Add(new wxStaticText(this, wxID_ANY, _("Parent")), 0, wxALIGN_CENTER_VERTICAL);
		m_choiceParent = new wxChoice(this, wxID_ANY);
		m_choiceParent->Append(_("<none>"));
		m_parentOptions.push_back(wxObjectDataPtr<ibPredefinedItem>());   // index 0 → no parent
		const wxObjectDataPtr<ibPredefinedItem> current =
			source != nullptr ? source->GetPredefinedParent() : wxObjectDataPtr<ibPredefinedItem>();
		int selected = 0;
		for (const auto& candidate : owner->GetParentCandidates(m_itemGuid)) {
			m_parentOptions.push_back(candidate);
			m_choiceParent->Append(candidate->GetPredefinedName());
			if (current != nullptr && candidate->GetPredefinedGuid() == current->GetPredefinedGuid())
				selected = static_cast<int>(m_parentOptions.size()) - 1;
		}
		m_choiceParent->SetSelection(selected);
		fields->Add(m_choiceParent, 1, wxEXPAND);
	}

	fields->Add(new wxStaticText(this, wxID_ANY, _("Name")), 0, wxALIGN_CENTER_VERTICAL);
	m_textName = new wxTextCtrl(this, wxID_ANY, source != nullptr ? wxString(source->GetPredefinedName()) : wxString());
	fields->Add(m_textName, 1, wxEXPAND);

	// A code, where the kind has one — a new item's the next — and a description, each typed as its type takes it.
	const wxString code = made ? wxString(m_described.m_nextCode.m_title) : wxString(source->GetPredefinedCode());
	if (!m_described.m_nextCode.IsEmpty() || !code.IsEmpty()) {
		fields->Add(new wxStaticText(this, wxID_ANY, _("Code")), 0, wxALIGN_CENTER_VERTICAL);
		m_textCode = new wxTextCtrl(this, wxID_ANY, code);
		LimitTyping(m_textCode, m_described.m_code.m_type);
		fields->Add(m_textCode, 1, wxEXPAND);
	}

	fields->Add(new wxStaticText(this, wxID_ANY, ibGETTEXT_IN_CONTEXT("item name", "Description")), 0, wxALIGN_CENTER_VERTICAL);
	m_textDescription = new wxTextCtrl(this, wxID_ANY, source != nullptr ? wxString(source->GetPredefinedDescription()) : wxString());
	LimitTyping(m_textDescription, m_described.m_description.m_type);
	fields->Add(m_textDescription, 1, wxEXPAND);

	// What the kind says an item declares — a row each.
	const ibPredefinedRowDescription noRow;
	for (const ibPredefinedAttributeDescription& attribute : m_described.m_attributes)
		AppendAttribute(this, fields, attribute, source != nullptr ? source->GetPredefinedRow() : noRow);
	mainSizer->Add(fields, 0, wxEXPAND | wxALL, kPad * 2);

	// …and the sections whose rows it owns: one in a frame of its own, several on pages.
	const std::map<ibMetaID, ibPredefinedRowDescriptionVector> noTables;
	const std::map<ibMetaID, ibPredefinedRowDescriptionVector>& tables = source != nullptr ? source->GetPredefinedTables() : noTables;
	if (m_described.m_tables.size() == 1) {
		const ibPredefinedTableDescription& table = m_described.m_tables.front();
		wxStaticBoxSizer* box = new wxStaticBoxSizer(wxVERTICAL, this, Caption(table.m_title, table.m_name));
		box->Add(AppendSection(box->GetStaticBox(), table, tables), 1, wxEXPAND);
		mainSizer->Add(box, 1, wxEXPAND | wxLEFT | wxRIGHT, kPad * 2);
	}
	else if (m_described.m_tables.size() > 1) {
		wxNotebook* pages = new wxNotebook(this, wxID_ANY);
		for (const ibPredefinedTableDescription& table : m_described.m_tables) {
			wxPanel* page = new wxPanel(pages);
			page->SetSizer(AppendSection(page, table, tables));
			pages->AddPage(page, Caption(table.m_title, table.m_name));
		}
		mainSizer->Add(pages, 1, wxEXPAND | wxLEFT | wxRIGHT, kPad * 2);
	}

	wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
	m_sdbSizerOK = new wxButton(this, wxID_OK);
	m_sdbSizerOK->Bind(wxEVT_BUTTON, &ibDialogPredefinedItem::OnCommandOK, this);
	buttons->AddButton(m_sdbSizerOK);
	wxButton* cancel = new wxButton(this, wxID_CANCEL);
	cancel->Bind(wxEVT_BUTTON, &ibDialogPredefinedItem::OnCommandCancel, this);
	buttons->AddButton(cancel);
	buttons->Realize();
	mainSizer->Add(buttons, 0, wxEXPAND | wxALL, kPad);

	wxDialog::SetSizer(mainSizer);
	mainSizer->SetSizeHints(this);
	const wxSize minSize = FromDIP(wxSize(m_described.m_tables.empty() ? 380 : 520, -1));
	if (GetSize().x < minSize.x)
		SetSize(minSize.x, GetSize().y);

	wxDialog::Layout();
	wxDialog::Centre(wxBOTH);

	wxIcon dlg_icon;
	dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_metaAttributeCLSID));
	wxDialog::SetIcon(dlg_icon);
	wxDialog::SetFocus();
}

// ONE ATTRIBUTE, ITS EDITOR CHOSEN BY WHAT IT HOLDS: a flag — a tick; a type or a closed set — our text editor, not typed
// into, its "..." the type picker or the designer's window for a declared value, its "×" clearing it; anything else typed.
// A value the set no longer has stays what it was, shown as it reads: the server refuses it when the item is written,
// saying why.
void ibDialogPredefinedEditor::ibDialogPredefinedItem::AppendAttribute(wxWindow* parent, wxSizer* sizer,
	const ibPredefinedAttributeDescription& attribute, const ibPredefinedRowDescription& row)
{
	auto editor = std::make_unique<ibAttributeEditor>();
	editor->m_attribute = &attribute;
	editor->m_held = Held(attribute, row);

	sizer->Add(new wxStaticText(parent, wxID_ANY, Caption(attribute.m_title, attribute.m_name)), 0, wxALIGN_CENTER_VERTICAL);

	wxWindow* control = nullptr;
	if (IsFlag(attribute)) {
		editor->m_check = new wxCheckBox(parent, wxID_ANY, wxEmptyString);
		editor->m_check->SetValue(editor->m_held && ibValue::FromNode(editor->m_held->m_value).GetBoolean());
		sizer->Add(editor->m_check, 1, wxEXPAND);
		control = editor->m_check;
	}
	else if (IsPicked(attribute)) {
		editor->m_picked = new ibTextCtrl(parent, wxID_ANY, editor->m_held ? wxString(editor->m_held->m_title) : wxString());
		editor->m_picked->ShowSelectButton(true);
		editor->m_picked->ShowClearButton(true);
		editor->m_picked->ShowOpenButton(false);
		editor->m_picked->SetEditable(false);   // the read-only switch greys the buttons too: given back below
		editor->m_picked->EnableSelectButton(true);
		editor->m_picked->EnableClearButton(true);
		ibAttributeEditor* const held = editor.get();
		editor->m_picked->Bind(wxEVT_CONTROL_BUTTON_SELECT, [this, held](wxCommandEvent&) {
			if (held->m_attribute->m_holdsType) {
				PickType(*held);
				return;
			}
			wxString title = held->m_picked->GetValue();
			if (PickValue(this, *held->m_attribute, title))
				held->m_picked->ChangeValue(title);
		});
		editor->m_picked->Bind(wxEVT_CONTROL_BUTTON_CLEAR, [held](wxCommandEvent&) {
			held->m_held.reset();
			held->m_picked->ChangeValue(wxEmptyString);
		});
		sizer->Add(editor->m_picked, 1, wxEXPAND);
		control = editor->m_picked;
	}
	else {
		editor->m_text = new wxTextCtrl(parent, wxID_ANY, editor->m_held ? wxString(editor->m_held->m_title) : wxString());
		LimitTyping(editor->m_text, attribute.m_type);
		sizer->Add(editor->m_text, 1, wxEXPAND);
		control = editor->m_text;
	}

	if (!attribute.m_help.IsEmpty())
		control->SetToolTip(attribute.m_help);
	m_attributes.push_back(std::move(editor));
}

// ONE SECTION: whether the item declares its rows (not declared — the rows are the data's), and the rows, a column per
// attribute chosen as an attribute's editor is. Rows are added up to the section's limit; where a row's place means
// something (row N is dimension N) they move up and down.
wxSizer* ibDialogPredefinedEditor::ibDialogPredefinedItem::AppendSection(wxWindow* parent,
	const ibPredefinedTableDescription& table, const std::map<ibMetaID, ibPredefinedRowDescriptionVector>& tables)
{
	const int kPad = FromDIP(5);
	auto section = std::make_unique<ibSectionEditor>();
	section->m_table = &table;
	const auto declared = tables.find(table.m_id);

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	section->m_declared = new wxCheckBox(parent, wxID_ANY, _("Declared by the configuration"));
	section->m_declared->SetValue(declared != tables.end());
	sizer->Add(section->m_declared, 0, wxALL, kPad);

	section->m_rows = new ibDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 140)));
	for (unsigned int col = 0; col < table.m_columns.size(); col++) {
		const ibPredefinedAttributeDescription& column = table.m_columns[col];
		const wxString caption = Caption(column.m_title, column.m_name);
		if (IsFlag(column)) {
			section->m_rows->AppendToggleColumn(caption, wxDATAVIEW_CELL_ACTIVATABLE);
		}
		else if (IsPicked(column) && !column.m_holdsType) {
			// One of the closed set, through the cell's "..." — the window an attribute's "..." opens — and written into the
			// row itself: the window takes the focus, and the cell's editor closes on what it held before the pick.
			const ibPredefinedAttributeDescription* const described = &column;
			ibDataViewListCtrl* const rows = section->m_rows;
			section->m_rows->AppendColumn(new ibDataViewColumn(caption, new ibTextWithDotsRenderer(this,
				[this, described, rows, col](wxString& text) {
					const int row = rows->GetSelectedRow();
					if (row == wxNOT_FOUND || !PickValue(this, *described, text))
						return false;
					rows->SetTextValue(text, static_cast<unsigned int>(row), col);
					return true;
				}, /*typed*/false),
				col, FromDIP(180)), wxT("string"));
		}
		else {
			section->m_rows->AppendTextColumn(caption, column.m_holdsType ? wxDATAVIEW_CELL_INERT : wxDATAVIEW_CELL_EDITABLE);
		}
	}

	if (declared != tables.end()) {
		for (const ibPredefinedRowDescription& line : declared->second) {
			wxVector<ibVariant> values;
			for (const ibPredefinedAttributeDescription& column : table.m_columns)
				values.push_back(Shown(column, Held(column, line)));
			section->m_rows->AppendItem(values);
			section->m_held.push_back(line);
		}
	}
	sizer->Add(section->m_rows, 1, wxEXPAND | wxLEFT | wxRIGHT, kPad);

	wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
	section->m_add = new wxButton(parent, wxID_ANY, _("Add"));
	section->m_remove = new wxButton(parent, wxID_ANY, _("Delete"));
	buttons->Add(section->m_add, 0, wxRIGHT, kPad);
	buttons->Add(section->m_remove, 0, wxRIGHT, kPad);
	// A row's place is its line number in every section: moved, it is a different line.
	section->m_up = new wxButton(parent, wxID_ANY, _("Move up"));
	section->m_down = new wxButton(parent, wxID_ANY, _("Move down"));
	buttons->Add(section->m_up, 0, wxRIGHT, kPad);
	buttons->Add(section->m_down, 0, wxRIGHT, kPad);
	sizer->Add(buttons, 0, wxALL, kPad);

	ibSectionEditor* const held = section.get();
	section->m_declared->Bind(wxEVT_CHECKBOX, [this, held](wxCommandEvent&) { EnableSection(*held); });
	section->m_add->Bind(wxEVT_BUTTON, [this, held](wxCommandEvent&) {
		wxVector<ibVariant> values;
		for (const ibPredefinedAttributeDescription& column : held->m_table->m_columns)
			values.push_back(Shown(column, Held(column, ibPredefinedRowDescription())));
		held->m_rows->AppendItem(values);
		held->m_held.push_back(ibPredefinedRowDescription());
		held->m_rows->SelectRow(static_cast<unsigned int>(held->m_rows->GetItemCount() - 1));
		EnableSection(*held);
	});
	section->m_remove->Bind(wxEVT_BUTTON, [this, held](wxCommandEvent&) {
		const int row = held->m_rows->GetSelectedRow();
		if (row == wxNOT_FOUND)
			return;
		held->m_rows->DeleteItem(static_cast<unsigned int>(row));
		held->m_held.erase(held->m_held.begin() + row);
		EnableSection(*held);
	});
	// ORDER IS MEANING HERE: row N is dimension N, so a row moves with what it held as it came.
	const auto move = [held](int delta) {
		const int row = held->m_rows->GetSelectedRow();
		const int target = row + delta;
		if (row == wxNOT_FOUND || target < 0 || target >= held->m_rows->GetItemCount())
			return;
		for (unsigned int col = 0; col < held->m_table->m_columns.size(); col++) {
			ibVariant from, to;
			held->m_rows->GetValue(from, static_cast<unsigned int>(row), col);
			held->m_rows->GetValue(to, static_cast<unsigned int>(target), col);
			held->m_rows->SetValue(to, static_cast<unsigned int>(row), col);
			held->m_rows->SetValue(from, static_cast<unsigned int>(target), col);
		}
		std::swap(held->m_held[row], held->m_held[target]);
		held->m_rows->SelectRow(static_cast<unsigned int>(target));
	};
	section->m_up->Bind(wxEVT_BUTTON, [move](wxCommandEvent&) { move(-1); });
	section->m_down->Bind(wxEVT_BUTTON, [move](wxCommandEvent&) { move(+1); });

	m_sections.push_back(std::move(section));
	EnableSection(*held);
	return sizer;
}

void ibDialogPredefinedEditor::ibDialogPredefinedItem::EnableSection(ibSectionEditor& section)
{
	const bool editable = !m_readOnly && section.m_declared->GetValue();
	const unsigned int count = static_cast<unsigned int>(section.m_rows->GetItemCount());
	section.m_declared->Enable(!m_readOnly);
	section.m_rows->Enable(editable);
	section.m_add->Enable(editable);
	section.m_remove->Enable(editable && count > 0);
	section.m_up->Enable(editable && count > 1);
	section.m_down->Enable(editable && count > 1);
}

// A TYPE, PICKED among the ones the attribute offers (named as the server named them), and held as every value travels:
// the attribute's class ({t} — the one a type is held as) and the description as its payload ({v}).
void ibDialogPredefinedEditor::ibDialogPredefinedItem::PickType(ibAttributeEditor& editor)
{
	ibTypeDescription type;
	if (editor.m_held)
		if (const std::optional<ibDataValue> payload = editor.m_held->m_value.FindField(kValueFieldData))
			ibTypeDescriptionMemory::ReadNode(*payload, type);
	if (!ibShowTypeSelector(this, ibSelectorDataType::ibSelectorDataType_any, editor.m_attribute->m_offered, type,
			nullptr, !m_readOnly, /*single*/false))
		return;

	ibDataValue written;
	if (type.GetClsidCount() == 0 || editor.m_attribute->m_type.GetClsidCount() != 1
		|| !ibTypeDescriptionMemory::WriteNode(written, type)) {
		editor.m_held.reset();
	}
	else {
		ibDataNode packed;
		packed.SetValue(kValueFieldClsid, ibString::Format(ibT("%llu"),
			static_cast<unsigned long long>(editor.m_attribute->m_type.GetClsidList().front())));
		packed.AddField(kValueFieldData, written);
		editor.m_held = ibPredefinedCellDescription{ DescribeTypes(type), packed };
	}
	editor.m_picked->ChangeValue(editor.m_held ? wxString(editor.m_held->m_title) : wxString());
}

bool ibDialogPredefinedEditor::ibDialogPredefinedItem::TakeRow(ibPredefinedRowDescription& row) const
{
	for (const auto& editor : m_attributes) {
		const ibPredefinedAttributeDescription& attribute = *editor->m_attribute;
		std::optional<ibPredefinedCellDescription> taken;
		if (editor->m_check != nullptr)
			taken = Packed(ibValue(editor->m_check->GetValue()));
		else if (attribute.m_holdsType)
			taken = editor->m_held;
		else if (editor->m_picked != nullptr)
			taken = TakeText(attribute, editor->m_picked->GetValue(), editor->m_held);
		else
			taken = TakeText(attribute, editor->m_text->GetValue(), editor->m_held);

		if (taken)
			row.insert_or_assign(attribute.m_id, *taken);
		else if (attribute.m_required)
			return Refuse(Caption(attribute.m_title, attribute.m_name));
	}
	return true;
}

bool ibDialogPredefinedEditor::ibDialogPredefinedItem::TakeTables(std::map<ibMetaID, ibPredefinedRowDescriptionVector>& tables) const
{
	for (const auto& section : m_sections) {
		if (!section->m_declared->GetValue())
			continue;   // not declared: the rows are the data's

		const std::vector<ibPredefinedAttributeDescription>& columns = section->m_table->m_columns;
		ibPredefinedRowDescriptionVector& rows = tables[section->m_table->m_id];
		for (unsigned int row = 0; row < static_cast<unsigned int>(section->m_rows->GetItemCount()); row++) {
			const ibPredefinedRowDescription& was = section->m_held[row];
			ibPredefinedRowDescription line;
			for (unsigned int col = 0; col < columns.size(); col++) {
				const ibPredefinedAttributeDescription& column = columns[col];
				const auto held = was.find(column.m_id);
				const std::optional<ibPredefinedCellDescription> came = held != was.end()
					? std::optional<ibPredefinedCellDescription>(held->second) : std::nullopt;
				std::optional<ibPredefinedCellDescription> taken;
				if (IsFlag(column))
					taken = Packed(ibValue(section->m_rows->GetToggleValue(row, col)));
				else if (column.m_holdsType)
					taken = came;
				else
					taken = TakeText(column, section->m_rows->GetTextValue(row, col), came);

				if (taken)
					line.insert_or_assign(column.m_id, *taken);
				else if (column.m_required)
					return Refuse(ibString::Format(_("%s, row %u"), Caption(column.m_title, column.m_name), row + 1));
			}
			rows.push_back(std::move(line));
		}
	}
	return true;
}

void ibDialogPredefinedEditor::ibDialogPredefinedItem::SetReadOnly(bool r)
{
	m_readOnly = r;
	if (m_checkFolder != nullptr)
		m_checkFolder->Enable(!r);
	if (m_choiceParent != nullptr)
		m_choiceParent->Enable(!r);
	m_textName->Enable(!r);
	if (m_textCode != nullptr)
		m_textCode->Enable(!r);
	m_textDescription->Enable(!r);

	for (const auto& editor : m_attributes) {
		if (editor->m_check != nullptr)
			editor->m_check->Enable(!r);
		if (editor->m_picked != nullptr) {
			editor->m_picked->EnableSelectButton(!r || editor->m_attribute->m_holdsType);   // a type is still looked at
			editor->m_picked->EnableClearButton(!r);
		}
		if (editor->m_text != nullptr)
			editor->m_text->Enable(!r);
	}
	for (const auto& section : m_sections)
		EnableSection(*section);

	m_sdbSizerOK->Enable(!r);
}

void ibDialogPredefinedEditor::ibDialogPredefinedItem::OnCommandOK(wxCommandEvent& event)
{
	const wxString strName = m_textName->GetValue();
	const wxString strCode = m_textCode != nullptr ? m_textCode->GetValue() : wxString();
	const wxString strDescription = m_textDescription->GetValue();

	if (strName.IsEmpty() || (m_textCode != nullptr && strCode.IsEmpty()) || strDescription.IsEmpty()) {
		wxMessageBox(_("All key fields are not filled in!"));
		return;
	}

	if (stringUtils::CheckCorrectName(strName) >= 0) {
		wxMessageBox(_("The \"name\" field is filled in incorrectly!"));
		return;
	}

	const wxObjectDataPtr<ibPredefinedItem> named = GetOwner()->FindPredefinedValue(strName);
	if (named != nullptr && named->GetPredefinedGuid() != m_itemGuid) {
		wxMessageBox(_("The \"name\" field has not unique!"));
		return;
	}

	// The item as the window says it, in the node form of the one schema.
	ibPredefinedItemDescription item;
	if (!TakeRow(item.m_row) || !TakeTables(item.m_tables))
		return;
	item.m_guid = m_itemGuid;
	item.m_name = strName;
	// A code and a description as a typed cell is taken: a value of the field's type (#646).
	item.m_code = TakeText(m_described.m_code, strCode, std::nullopt).value_or(ibPredefinedCellDescription());
	item.m_description = TakeText(m_described.m_description, strDescription, std::nullopt).value_or(ibPredefinedCellDescription());
	item.m_folder = m_checkFolder != nullptr && m_checkFolder->GetValue();
	const int parentIdx = m_choiceParent != nullptr ? m_choiceParent->GetSelection() : 0;
	if (parentIdx > 0 && parentIdx < static_cast<int>(m_parentOptions.size()))
		item.m_parent = m_parentOptions[parentIdx]->GetPredefinedGuid();

	// Refused by the server — the person was told why, and the window stays for them to put it right.
	const bool done = m_itemGuid.isValid()
		? GetOwner()->SetPredefinedValue(item)
		: GetOwner()->AppendPredefinedValue(item);
	if (done)
		event.Skip();
}

/////////////////////////////////////////////////////////////////////////////////////

void ibDialogPredefinedEditor::CreateDialogView()
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* sizerList = new wxBoxSizer(wxVERTICAL);

	m_toolbarTableEditor = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_TEXT);
	m_toolbarTableEditor->SetArtProvider(new wxAuiLunaToolBarArt());

	m_toolAdd = m_toolbarTableEditor->AddTool(wxID_TOOL_ADD, _("Add"), ibBackendPicture::GetPicture(g_picAddCLSID), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, nullptr);
	m_toolCopy = m_toolbarTableEditor->AddTool(wxID_TOOL_COPY, _("Copy"), ibBackendPicture::GetPicture(g_picCopyCLSID), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, nullptr);
	m_toolEdit = m_toolbarTableEditor->AddTool(wxID_TOOL_EDIT, _("Edit"), ibBackendPicture::GetPicture(g_picEditCLSID), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, nullptr);
	m_toolDelete = m_toolbarTableEditor->AddTool(wxID_TOOL_DELETE, _("Delete"), ibBackendPicture::GetPicture(g_picDeleteCLSID), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, nullptr);

	m_toolbarTableEditor->EnableTool(wxID_TOOL_ADD, m_tableModelStore->IsEnabled(ibDataViewItem(nullptr), model_name));
	m_toolbarTableEditor->EnableTool(wxID_TOOL_COPY, m_tableModelStore->IsEnabled(ibDataViewItem(nullptr), model_name));
	m_toolbarTableEditor->EnableTool(wxID_TOOL_EDIT, m_tableModelStore->IsEnabled(ibDataViewItem(nullptr), model_name));
	m_toolbarTableEditor->EnableTool(wxID_TOOL_DELETE, m_tableModelStore->IsEnabled(ibDataViewItem(nullptr), model_name));

	m_toolbarTableEditor->SetForegroundColour(wxDefaultStypeFGColour);
	m_toolbarTableEditor->SetBackgroundColour(wxDefaultStypeBGColour);

	m_toolbarTableEditor->Realize();
	m_toolbarTableEditor->Connect(wxEVT_MENU, wxCommandEventHandler(ibDialogPredefinedEditor::OnCommandMenu), nullptr, this);

	sizerList->Add(m_toolbarTableEditor, 0, wxALL | wxEXPAND, 0);

	ibDataViewColumn* columnName = new ibDataViewColumn(_("Name"), new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT), model_name, FromDIP(125), wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE);

	ibDataViewColumn* columnCode = new ibDataViewColumn(_("Code"), new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT), model_code, FromDIP(100), wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE);

	ibDataViewColumn* columnDescription = new ibDataViewColumn(ibGETTEXT_IN_CONTEXT("item name", "Description"), new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT), model_description, FromDIP(175), wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE);

	m_tableEditor = new ibDataViewCtrl(this, wxID_ANY);

	m_tableEditor->Bind(ibEVT_DATAVIEW_ITEM_ACTIVATED, &ibDialogPredefinedEditor::OnItemActivated, this);
	m_tableEditor->Bind(ibEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibDialogPredefinedEditor::OnContextMenu, this);

	m_tableEditor->Connect(wxEVT_MENU, wxCommandEventHandler(ibDialogPredefinedEditor::OnCommandMenu), nullptr, this);

	m_tableEditor->GetRootColumnGroup()->AppendColumn(columnName);
	m_tableEditor->GetRootColumnGroup()->AppendColumn(columnCode);
	m_tableEditor->GetRootColumnGroup()->AppendColumn(columnDescription);

	m_tableEditor->SetForegroundColour(wxDefaultStypeFGColour);
	//m_tableEditor->SetBackgroundColour(wxDefaultStypeBGColour);

	m_tableEditor->AssociateModel(m_tableModelStore);

	sizerList->Add(m_tableEditor, 1, wxALL | wxEXPAND, FromDIP(5));

	wxDialog::SetSizer(sizerList);
	wxDialog::Layout();
	wxDialog::Centre(wxBOTH);

	wxIcon dlg_icon;
	dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_metaAttributeCLSID));

	wxDialog::SetIcon(dlg_icon);
	wxDialog::SetFocus();
}

void ibDialogPredefinedEditor::OnContextMenu(ibDataViewEvent& event)
{
	wxMenu menu;

	for (unsigned int idx = 0; idx < m_toolbarTableEditor->GetToolCount(); idx++) {
		wxAuiToolBarItem* tool = m_toolbarTableEditor->FindToolByIndex(idx);
		if (tool) {
			wxMenuItem* item = menu.Append(tool->GetId(), tool->GetLabel());
			item->Enable(m_tableModelStore->IsEnabled(ibDataViewItem(nullptr), model_name));
			item->SetBitmap(tool->GetBitmapBundle());
		}
	}

	m_tableEditor->PopupMenu(&menu);
}

void ibDialogPredefinedEditor::OnCommandMenu(wxCommandEvent& event)
{
	const ibDataViewItem& selection =
		m_tableEditor->GetSelection();

	int result = wxID_CANCEL;

	if (event.GetId() == wxID_TOOL_ADD) {
		ibDialogPredefinedItem dlg(this);
		result = dlg.ShowModal();
	}
	else if (event.GetId() == wxID_TOOL_COPY) {
		if (selection.IsOk()) {

			ibPredefinedItem* predefined =
				static_cast<ibPredefinedItem*>(selection.GetID());

			ibDialogPredefinedItem dlg(this, wxNewUniqueGuid, predefined);
			dlg.SetReadOnly(!m_tableModelStore->IsEnabled(selection, model_name));
			result = dlg.ShowModal();
		}
	}
	else if (event.GetId() == wxID_TOOL_EDIT) {
		if (selection.IsOk()) {

			ibPredefinedItem* predefined =
				static_cast<ibPredefinedItem*>(selection.GetID());

			ibDialogPredefinedItem dlg(this, predefined->GetPredefinedGuid(), predefined);
			dlg.SetReadOnly(!m_tableModelStore->IsEnabled(selection, model_name));
			result = dlg.ShowModal();
		}
	}
	else if (event.GetId() == wxID_TOOL_DELETE) {

		if (selection.IsOk()) {
			ibPredefinedItem* predefined =
				static_cast<ibPredefinedItem*>(selection.GetID());

			DeletePredefinedValue(predefined->GetPredefinedGuid());
			result = wxID_OK;
		}
	}

	if (result == wxID_OK) m_tableModelStore->Cleared();

	event.Skip();
}

void ibDialogPredefinedEditor::OnItemActivated(ibDataViewEvent& event)
{
	const ibDataViewItem& selection =
		m_tableEditor->GetSelection();

	ibPredefinedItem* predefined =
		static_cast<ibPredefinedItem*>(selection.GetID());
	if (predefined == nullptr)
		return;

	ibDialogPredefinedItem dlg(this, predefined->GetPredefinedGuid(), predefined);
	dlg.SetReadOnly(!m_tableModelStore->IsEnabled(selection, model_name));

	if (dlg.ShowModal() == wxID_OK)
		m_tableModelStore->Cleared();

	event.Skip();
}
