////////////////////////////////////////////////////////////////////////////
//	Description : a table handed in, described (queryTempTableDialog.h)
////////////////////////////////////////////////////////////////////////////

#include "queryTempTableDialog.h"
#include "queryConstructorInternal.h"   // the grid model and the type cell: a list, a keyboard and a door

#include "backend/query/queryConstructorModel.h"
#include "frontend/win/dlgs/typeSelector.h"

#include <wx/textctrl.h>

namespace {

enum { kTempColName = kGridCol1, kTempColType = kGridCol2 };

// The name a field goes by in the statement: its path is written against the table's own name, so a field
// called after a keyword (`Order`, `Value`) still reads — after a dot a keyword is a name.
ibQueryAstExprPtr FieldOf(const wxString& table, const wxString& field)
{
	auto column = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
	column->m_path = { table, field };
	return column;
}

} // namespace

wxString ibDialogQueryTempTable::NameOf(const ibQuerySource& source)
{
	const wxString name = source.m_name.empty() ? wxString() : source.m_name.front();
	return source.m_parameter ? wxT("&") + name : name;
}

std::vector<ibDialogQueryTempTable::Field> ibDialogQueryTempTable::Read(const ibQuerySelect& reader,
                                                                        const ibQuerySource& source,
                                                                        const ibQueryConstructorModel& model)
{
	std::vector<Field> fields;
	for (const ibQueryConstructorField& field : model.FieldsTakenFrom(reader, source))
		fields.push_back({ field.m_name, field.m_type });
	return fields;
}

void ibDialogQueryTempTable::Write(ibQuerySelect& reader, ibQuerySource& source, const ibQueryConstructorModel& model,
                                   const wxString& name, const std::vector<Field>& fields)
{
	// WHAT THE SELECT TOOK FROM THE TABLE GOES, and the new fields stand where the first of it stood — the order
	// the author gave the rest of the list is theirs.
	const wxString oldName = ibQuerySourceName(source);
	auto takenFrom = [&oldName](const ibQueryProjection& projection) {
		const ibQueryAstExprPtr& e = projection.m_expr;
		const ibQueryAstExprPtr& column = e && e->m_kind == ibQueryAstExprKind::Cast ? e->m_arg : e;
		return column && column->m_kind == ibQueryAstExprKind::Column && !column->m_arg
			&& column->m_path.size() == 2 && column->m_path.front().IsSameAs(oldName, false);
	};
	size_t at = reader.m_projections.size();
	for (size_t i = reader.m_projections.size(); i-- > 0;)
		if (takenFrom(reader.m_projections[i])) {
			reader.m_projections.erase(reader.m_projections.begin() + static_cast<long>(i));
			at = i;
		}
	at = std::min(at, reader.m_projections.size());

	// THE NAME SAYS WHERE THE ROWS COME FROM — the ampersand a parameter, none a temporary table.
	const bool parameter = name.StartsWith(wxT("&"));
	const wxString table = parameter ? name.Mid(1) : name;
	source.m_parameter = parameter;
	source.m_name      = { table };
	source.m_alias.clear();   // it is called what it is
	source.m_args.clear();
	source.m_subquery.reset();
	if (!oldName.IsEmpty() && !oldName.IsSameAs(table, false))   // a table just added was called nothing yet
		queryctor::ibQueryRenameSourceReferences(reader, oldName, table);

	std::vector<ibQueryProjection> made;
	for (const Field& field : fields) {
		ibQueryProjection projection;
		projection.m_expr  = model.CastTo(FieldOf(table, field.m_name), field.m_type);
		projection.m_alias = field.m_name;
		made.push_back(std::move(projection));
	}
	reader.m_projections.insert(reader.m_projections.begin() + static_cast<long>(at), made.begin(), made.end());
	// A select that names no field reads every field it has — a table added with none said is read whole.
	reader.m_selectAll = reader.m_projections.empty();
}

ibDialogQueryTempTable::ibDialogQueryTempTable(wxWindow* parent, const wxString& name,
                                               std::vector<Field> fields,
                                               const ibQueryConstructorModel& model,
                                               const ibMetaData* metaData, bool readOnly)
	: wxDialog(parent, wxID_ANY, _("Temporary table"), wxDefaultPosition, wxSize(560, 420),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, m_model(model)
	, m_metaData(metaData)
	, m_readOnly(readOnly)
	, m_fields(std::move(fields))
{
	{
		const wxBitmap picture = wxArtProvider::GetBitmap(wxART_TEMP_TABLE, wxART_FRONTEND, FromDIP(wxSize(16, 16)));
		if (picture.IsOk()) {
			wxIcon icon;
			icon.CopyFromBitmap(picture);
			SetIcon(icon);
		}
	}

	wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);

	// ---- the name, and with it where the rows come from --------------------------------------
	wxBoxSizer* nameRow = new wxBoxSizer(wxHORIZONTAL);
	nameRow->Add(new wxStaticText(this, wxID_ANY, _("Table name:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	m_nameBox = new wxTextCtrl(this, wxID_ANY, name);
	m_nameBox->Enable(!m_readOnly);
	nameRow->Add(m_nameBox, 1, wxALIGN_CENTER_VERTICAL);
	outer->Add(nameRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
	// (EscapeMnemonics — a label reads `&` as the key it underlines, and this one means the ampersand itself;
	// Wrap — one line of it was cut at the window's edge.)
	wxStaticText* hint = new wxStaticText(this, wxID_ANY, wxControl::EscapeMnemonics(
		_("&Name reads a value table given as the parameter Name; a name alone reads the temporary table of "
		  "that name, made earlier in the package or brought by the temporary tables manager.")));
	hint->Wrap(FromDIP(530));
	outer->Add(hint, 0, wxEXPAND | wxALL, FromDIP(6));

	// ---- the verbs -------------------------------------------------------------------------
	wxToolBar* bar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
	bar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
	auto tool = [this, bar](const wxString& label, const wxString& art, std::function<void()> act) {
		const int id = wxWindow::NewControlId();
		bar->AddTool(id, label, wxArtProvider::GetBitmapBundle(art, wxART_FRONTEND, wxSize(16, 16)), label);   // normal DPI
		bar->EnableTool(id, !m_readOnly);
		bar->Bind(wxEVT_TOOL, [act](wxCommandEvent&) { act(); }, id);
	};
	tool(_("Add"),       wxART_ADD,    [this] { wxCommandEvent e; OnAdd(e); });
	tool(_("Delete"),    wxART_DELETE, [this] { wxCommandEvent e; OnRemove(e); });
	bar->AddSeparator();
	tool(_("Move up"),   wxART_UP,     [this] { MoveField(-1); });
	tool(_("Move down"), wxART_DOWN,   [this] { MoveField(+1); });
	bar->Realize();
	outer->Add(bar, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));

	// ---- the fields ------------------------------------------------------------------------
	m_rows = new ibQueryGridModel();
	m_rows->SetReader([this](unsigned int row, unsigned int col) -> wxString {
		if (row >= m_fields.size())
			return wxEmptyString;
		return col == kTempColName ? m_fields[row].m_name : TypeText(m_fields[row].m_type);
	});
	// A NAME IS A NAME and a TYPE IS WHAT THE PARSER READS — each refused where it is typed, with the old
	// text kept, rather than carried to OK as a statement nothing can read.
	m_rows->SetWriter([this](unsigned int row, unsigned int col, const wxString& typed) -> bool {
		if (m_readOnly || row >= m_fields.size())
			return false;
		const wxString text = wxString(typed).Trim(true).Trim(false);
		if (col == kTempColName) {
			if (!ibQueryLexer::IsIdentifier(text)) {
				wxMessageBox(wxString::Format(_("'%s' cannot be a field name.\n\nA name is one word: letters, "
					"digits and underscores, with no spaces or punctuation."), text), GetTitle(), wxOK | wxICON_WARNING, this);
				return false;
			}
			m_fields[row].m_name = text;
			return true;
		}
		ibTypeDescription type;
		if (!ReadType(text, type)) {
			wxMessageBox(wxString::Format(_("'%s' is not a type a field can be described by: Number(15, 2), "
				"String(50), Date, Boolean, or a table a reference points at, such as Catalog.Items."), text),
				GetTitle(), wxOK | wxICON_WARNING, this);
			return false;
		}
		m_fields[row].m_type = type;
		return true;
	});

	m_grid = new ibDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
	m_rows->SetOnChanged([this] { ShowFields(); });
	m_grid->AssociateModel(m_rows);
	ibDataViewEditOnActivate(m_grid);
	m_grid->GetRootColumnGroup()->AppendColumn(queryctor::TextColumn(_("Field name"), kTempColName,
		FromDIP(200), !m_readOnly));
	// THE TYPE: the primitives in the list, anything typed as the text writes it, and "..." for the
	// product's own type picker — which is where a reference and the qualifiers are chosen.
	m_grid->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Type"),
		new queryctor::ibExpressionCellRenderer(
			[] { return ibQueryCastPrimitiveWords(); },
			[this](wxString& text) { return PickType(text); },
			m_readOnly ? wxDATAVIEW_CELL_INERT : wxDATAVIEW_CELL_EDITABLE),
		kTempColType, FromDIP(280), wxAlignment::wxALIGN_LEFT));
	outer->Add(m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));

	wxStdDialogButtonSizer* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	outer->Add(buttons, 0, wxALIGN_RIGHT | wxALL, FromDIP(6));

	SetSizer(outer);
	Bind(wxEVT_BUTTON, &ibDialogQueryTempTable::OnOk, this, wxID_OK);

	Bind(wxEVT_INIT_DIALOG, [this](wxInitDialogEvent& event) {
		event.Skip();
		if (m_readOnly)
			return;
		if (m_nameBox->GetValue().IsEmpty() || m_fields.empty())
			m_nameBox->SetFocus();
		else
			m_grid->SetFocus();
	});

	ShowFields();
}

wxString ibDialogQueryTempTable::GetTableName() const
{
	return m_nameBox != nullptr ? wxString(m_nameBox->GetValue()).Trim(true).Trim(false) : wxString();
}

wxString ibDialogQueryTempTable::TypeText(const ibTypeDescription& type) const
{
	const ibQueryAstExprPtr cast = m_model.CastTo(FieldOf(wxT("T"), wxT("F")), type);
	return cast && cast->m_kind == ibQueryAstExprKind::Cast ? ibRenderQueryCastTarget(*cast) : wxString();
}

bool ibDialogQueryTempTable::ReadType(const wxString& text, ibTypeDescription& type) const
{
	type = ibTypeDescription();
	if (text.IsEmpty())
		return true;   // no type said — the field goes uncast
	try {
		ibQueryParser parser;
		const ibQueryAstExprPtr cast = parser.ParseExpression(wxT("CAST(T.F AS ") + text + wxT(")"));
		if (!cast || cast->m_kind != ibQueryAstExprKind::Cast)
			return false;
		type = m_model.TypeOfCast(*cast);
	}
	catch (const ibCoreException&) {
		return false;
	}
	return type.IsOk();
}

bool ibDialogQueryTempTable::PickType(wxString& text)
{
	ibTypeDescription type;
	ReadType(text, type);
	if (!ibShowTypeSelector(this, ibSelectorDataType::ibSelectorDataType_reference, std::vector<ibClassID>(),
			type, m_metaData, !m_readOnly, /*single*/true))
		return false;
	const wxString written = TypeText(type);
	// A type the text cannot say is not offered as if it could be: the field would come back untyped.
	if (written.IsEmpty() && type.IsOk()) {
		wxMessageBox(_("This type cannot be written into a query: a field is described by a primitive type "
			"or by a table a reference points at."), GetTitle(), wxOK | wxICON_WARNING, this);
		return false;
	}
	text = written;
	return true;
}

long ibDialogQueryTempTable::SelectedRow() const
{
	if (m_grid == nullptr || m_rows == nullptr)
		return -1;
	const ibDataViewItem item = m_grid->GetSelection();
	return item.IsOk() ? static_cast<long>(m_rows->GetRow(item)) : -1;
}

void ibDialogQueryTempTable::ShowFields()
{
	if (m_rows != nullptr)
		m_rows->SetRowCount(static_cast<unsigned int>(m_fields.size()));
}

void ibDialogQueryTempTable::OnAdd(wxCommandEvent&)
{
	if (m_readOnly)
		return;
	// A NAME NOBODY IN THIS TABLE HAS, so the row is valid the moment it appears.
	auto taken = [this](const wxString& candidate) {
		for (const Field& field : m_fields)
			if (field.m_name.IsSameAs(candidate, false))
				return true;
		return false;
	};
	Field field;
	for (unsigned int n = 1; ; ++n) {
		field.m_name = wxString::Format(wxT("Field%u"), n);
		if (!taken(field.m_name))
			break;
	}
	m_fields.push_back(field);
	ShowFields();
	m_grid->Select(m_rows->GetItem(static_cast<unsigned int>(m_fields.size() - 1)));
}

void ibDialogQueryTempTable::OnRemove(wxCommandEvent&)
{
	const long row = SelectedRow();
	if (m_readOnly || row < 0 || static_cast<size_t>(row) >= m_fields.size())
		return;
	m_fields.erase(m_fields.begin() + row);
	ShowFields();
}

void ibDialogQueryTempTable::MoveField(int delta)
{
	const long row = SelectedRow();
	if (m_readOnly || row < 0 || static_cast<size_t>(row) >= m_fields.size())
		return;
	const long target = row + delta;
	if (target < 0 || static_cast<size_t>(target) >= m_fields.size())
		return;
	std::swap(m_fields[row], m_fields[target]);
	ShowFields();
	m_grid->Select(m_rows->GetItem(static_cast<unsigned int>(target)));
}

void ibDialogQueryTempTable::OnOk(wxCommandEvent& event)
{
	if (m_readOnly) {
		event.Skip();
		return;
	}
	// ⚠ THE CELL STILL OPEN IS WRITTEN FIRST. The type cell's editor is a list and a "..." in one panel, and
	// the press on OK takes the focus from the list, not from the panel — so the grid never heard the edit
	// end, the window closed over it, and a type just chosen was gone when the table was opened again
	// (Max, 2026-09-28).
	if (m_grid != nullptr)
		m_grid->FinishEditing();
	// The name is a word the language reads — after an ampersand when the rows come from a parameter.
	const wxString name = GetTableName();
	if (!ibQueryLexer::IsIdentifier(name.StartsWith(wxT("&")) ? name.Mid(1) : name)) {
		wxMessageBox(wxString::Format(_("'%s' cannot be a table name.\n\nA name is one word: letters, digits "
			"and underscores, with no spaces or punctuation - after an ampersand when the table is a parameter."), name),
			GetTitle(), wxOK | wxICON_WARNING, this);
		m_nameBox->SetFocus();
		return;
	}
	// TWO FIELDS OF ONE NAME are one column the next statement cannot tell apart.
	for (size_t i = 0; i < m_fields.size(); ++i)
		for (size_t j = i + 1; j < m_fields.size(); ++j)
			if (m_fields[i].m_name.IsSameAs(m_fields[j].m_name, false)) {
				wxMessageBox(wxString::Format(_("Two fields are called '%s'."), m_fields[i].m_name),
					GetTitle(), wxOK | wxICON_WARNING, this);
				return;
			}
	event.Skip();
}
