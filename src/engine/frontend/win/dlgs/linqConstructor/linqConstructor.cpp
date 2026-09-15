////////////////////////////////////////////////////////////////////////////
//	Description : The LINQ constructor (linqConstructor.h)
////////////////////////////////////////////////////////////////////////////

#include "linqConstructor.h"

#include "backend/backend_exception.h"          // ibEvalModeScope - every probe runs as a completion
#include "backend/compiler/scriptComplete.h"    // the three IntelliSense doors, and the compiler's outline
#include "backend/compiler/translateCode.h"     // GetKeyWord - the words are the lexer's
#include "backend/compiler/codeDef.h"           // KEY_FROM ... KEY_RESTRICT
#include "backend/compiler/typeCtor.h"          // ibCtorAbstractType::GetClassIcon - a metatype's own picture
#include "backend/metaData.h"                   // GetAvailableCtor
#include "backend/metaCollection/metaObject.h"  // the module's name and configuration
#include "backend/stringUtils.h"
#include "frontend/artProvider/artProvider.h"   // wxART_FRONTEND and the product's own pictures
#include "frontend/mainFrame/mainFrame.h"       // the editor settings the block is coloured with
#include "frontend/win/ctrls/dataview/dataview.h"   // ibDataViewListCtrl - the platform's list, edited in place
#include "frontend/win/ctrls/dataview/dataviewEditOnActivate.h"   // …opened by a double-click
#include "frontend/win/dlgs/callbackDropTarget.h"   // a drop is the arrow's verb, reached by the mouse
#include "frontend/win/editor/codeEditor/codeEditor.h"   // the block, coloured as the module is

#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/imaglist.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/textdlg.h>
#include <wx/toolbar.h>
#include <wx/utils.h>

#include <algorithm>

namespace {

// What a node of a source tree stands for.
enum class ibNodeKind {
	Group,       // "Visible here" - only a heading
	Container,   // an expression whose members are more places to go (`Data`, `Data.Catalogs`)
	Variable,    // a name visible where the window opened, that can be opened
	Value,       // a plain value visible there - a number, a date, a flag: for a condition, not a table
	Source,      // an expression whose ROWS are read - a table, a query's result
	Field,       // a field of a source's row
	Hint,        // a line that says something - under a source of unknown columns, a door to write one
};

class ibLinqNode : public wxTreeItemData {
public:
	ibLinqNode(ibNodeKind kind, const wxString& expression, const wxString& field = wxString())
		: m_kind(kind), m_expression(expression), m_field(field) {
		// Nothing to ask for: born loaded, so opening it never clears it.
		m_loaded = (kind == ibNodeKind::Group || kind == ibNodeKind::Field || kind == ibNodeKind::Hint
			|| kind == ibNodeKind::Value);
	}

	ibNodeKind m_kind;
	wxString   m_expression;   // for a field, the expression of its source
	wxString   m_field;
	bool       m_loaded;
};

ibLinqNode* NodeOf(const wxTreeCtrl* tree, const wxTreeItemId& item)
{
	return tree != nullptr && item.IsOk() ? static_cast<ibLinqNode*>(tree->GetItemData(item)) : nullptr;
}

wxString Word(int key)
{
	return ibTranslateCode::GetKeyWord(key);
}

// The alias a probe query binds. A name nobody writes: the probe is thrown away with its compile.
const wxChar* const kProbeRow = wxT("linqRow");

// The two arrows between panes - drawn by code point, since a source literal carries ASCII only.
const wxString kArrowIn  = wxString(wxUniChar(0x203A));
const wxString kArrowOut = wxString(wxUniChar(0x2039));

} // namespace

ibDialogLinqConstructor::ibDialogLinqConstructor(wxWindow* parent, const wxString& text, unsigned int caret,
	const ibValueMetaObject* module, bool readOnly)
	: wxDialog(parent, wxID_ANY, _("LINQ query constructor"), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	m_text(text), m_caret(std::min<unsigned int>(caret, (unsigned int)text.length())), m_module(module)
{
	m_anchor = m_caret;
	const wxString moduleName = m_module != nullptr ? m_module->GetName() : wxString(wxT("module"));
	const ibMetaData* metaData = m_module != nullptr ? m_module->GetMetaData() : nullptr;

	// ⭐ A QUERY THE CARET STANDS IN IS OPENED, NOT WRITTEN ANEW. Where it is and where it ends is the
	// compiler's to say - it bracketed the text while reading it; the parts are read back out of that
	// text by the block's own reader, and OK replaces that stretch.
	ibLinqBlock opened;
	wxString openRefusal;
	{
		const ibBackendException::ibEvalModeScope answering(eval_complete);
		std::vector<ibQueryOutline> outlines;
		try {
			outlines = ibOutlineScriptQueries(m_text, moduleName, metaData);
		}
		catch (...) {
		}
		const ibQueryOutline* here = nullptr;
		for (const ibQueryOutline& outline : outlines) {
			if (outline.m_isRestrict || outline.m_textTo <= outline.m_textFrom)
				continue;
			if (m_caret < outline.m_textFrom || m_caret > outline.m_textTo)
				continue;
			if (here == nullptr || outline.m_textFrom > here->m_textFrom)
				here = &outline;   // the innermost, when one query stands inside another
		}
		if (here != nullptr) {
			// ⭐ WHERE THE QUERY ENDS IS THE LEXER'S TO SAY, not the compiler's bracket: the bracket ends
			// where the compiler STOPPED, and in a query left half-written - `On g. Equals c.` - that is
			// mid-way, so OK replaced the first half and left the rest standing after the new block. The
			// reader is handed the module from the query's start and says how much of it the query is.
			const wxString rest = m_text.Mid(here->m_textFrom);
			size_t consumed = 0;
			if (ibLinqBlock::Parse(rest, opened, openRefusal, &consumed)) {
				// The stretch itself, without the whitespace around it.
				const wxString read = rest.Left(consumed);
				const wxString trimmedLeft = wxString(read).Trim(false);
				const wxString trimmed = wxString(trimmedLeft).Trim(true);
				m_replacing = true;
				m_replaceFrom = here->m_textFrom + (unsigned int)(read.length() - trimmedLeft.length());
				m_replaceTo = m_replaceFrom + (unsigned int)trimmed.length();
				m_anchor = m_replaceFrom;
			}
		}
	}

	// A probe is appended to the text up to the START of the anchor's line: the statements above are
	// finished, the one the block stands in may not be (`q = |`), and a probe glued onto half a
	// statement asks the walk about a sentence nobody wrote.
	const int lineStart = m_text.Left(m_anchor).Find(wxT('\n'), /*fromEnd*/ true);
	m_probeHead = lineStart == wxNOT_FOUND ? wxString() : m_text.Left(lineStart + 1);

	// WHAT IS VISIBLE WHERE THE BLOCK STANDS - the dropdown's own list, the values in it: the module's
	// variables, the procedure's locals and parameters, the object's own attributes and tabular sections.
	{
		const ibBackendException::ibEvalModeScope answering(eval_complete);
		std::vector<ibCaretName> names;
		try {
			ibNamesAtCaret(m_text, m_anchor, m_module, names);
		}
		catch (...) {
		}
		for (const ibCaretName& name : names) {
			if (name.m_callable)
				continue;
			if (name.m_origin != ibNameOrigin::Declared && name.m_origin != ibNameOrigin::Member)
				continue;
			if (std::find(m_variables.begin(), m_variables.end(), name.m_name) == m_variables.end())
				m_variables.push_back(name.m_name);
		}
	}

	// ASKED ONCE, for every tree: which visible names are places to open and which are plain values
	// (a leaf with no arrow, for a condition), and what `Data` holds - its sections head every tree,
	// the way the metadata kinds head the query constructor's.
	{
		wxBusyCursor busy;
		for (const wxString& name : m_variables)
			m_variableOpens.push_back(!IsPlainValue(MembersOf(name)));
		m_sections = MembersOf(wxT("Data")).m_names;
	}

	const int gap = FromDIP(6);
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);
	m_notebook = new wxNotebook(this, wxID_ANY);

	const auto makeList = [&](wxWindow* page) {
		ibDataViewListCtrl* list = new ibDataViewListCtrl(page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
			wxDV_ROW_LINES | wxDV_SINGLE);
		list->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, [this](ibDataViewEvent&) { ShowResult(); });
		ibDataViewEditOnActivate(list);   // a double-click opens the cell - the constructors' one way
		return list;
	};
	const auto removeSelected = [this](ibDataViewListCtrl* list) {
		const int row = list->GetSelectedRow();
		if (row == wxNOT_FOUND)
			return;
		list->DeleteItem(row);
		ShowResult();
	};
	const auto moveSelected = [this](ibDataViewListCtrl* list, int delta) {
		const int row = list->GetSelectedRow();
		const int target = row + delta;
		if (row == wxNOT_FOUND || target < 0 || target >= list->GetItemCount())
			return;
		for (unsigned int col = 0; col < (unsigned int)list->GetStore()->m_cols.size(); ++col) {
			wxVariant here, there;
			list->GetValue(here, row, col);
			list->GetValue(there, target, col);
			list->SetValue(there, row, col);
			list->SetValue(here, target, col);
		}
		list->SelectRow(target);
		ShowResult();
	};
	// THE ARROWS BETWEEN PANES, as the query constructor has them: in, and back out.
	const auto arrows = [&](wxWindow* page, std::function<void()> in, std::function<void()> out) {
		wxBoxSizer* column = new wxBoxSizer(wxVERTICAL);
		column->AddStretchSpacer();
		wxButton* forward = new wxButton(page, wxID_ANY, kArrowIn, wxDefaultPosition, FromDIP(wxSize(28, 24)), wxBU_EXACTFIT);
		forward->Bind(wxEVT_BUTTON, [in](wxCommandEvent&) { in(); });
		column->Add(forward, wxSizerFlags().Border(wxBOTTOM, gap));
		if (out) {
			wxButton* back = new wxButton(page, wxID_ANY, kArrowOut, wxDefaultPosition, FromDIP(wxSize(28, 24)), wxBU_EXACTFIT);
			back->Bind(wxEVT_BUTTON, [out](wxCommandEvent&) { out(); });
			column->Add(back);
		}
		column->AddStretchSpacer();
		return column;
	};
	const auto pane = [&](wxWindow* page, const wxString& title, wxToolBar* bar, wxWindow* body) {
		wxBoxSizer* column = new wxBoxSizer(wxVERTICAL);
		column->Add(new wxStaticText(page, wxID_ANY, title), wxSizerFlags().Border(wxBOTTOM, gap / 2));
		if (bar != nullptr) {
			bar->Realize();
			column->Add(bar, wxSizerFlags().Expand());
		}
		column->Add(body, wxSizerFlags(1).Expand());
		return column;
	};

	// ---- Tables and fields: what can be read | the tables read | the columns of the answer ----
	{
		wxPanel* page = new wxPanel(m_notebook);
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);

		wxTreeCtrl* tree = MakeSourceTree(page);
		row->Add(pane(page, _("Available tables"), nullptr, tree), wxSizerFlags(3).Expand().Border(wxALL, gap));
		row->Add(arrows(page, [this] { AddSelectedAsSource(false); }, [this, removeSelected] { removeSelected(m_sources); }),
			wxSizerFlags().Expand());

		m_sources = makeList(page);
		m_sources->AppendTextColumn(_("Name in the query"), wxDATAVIEW_CELL_EDITABLE, FromDIP(90));
		m_sources->AppendTextColumn(_("Reads"), wxDATAVIEW_CELL_EDITABLE, FromDIP(170));
		m_sources->AppendToggleColumn(Word(KEY_JOIN), wxDATAVIEW_CELL_ACTIVATABLE, FromDIP(40));
		m_sources->AppendTextColumn(Word(KEY_ON), wxDATAVIEW_CELL_EDITABLE, FromDIP(100));
		m_sources->AppendTextColumn(Word(KEY_EQUALS), wxDATAVIEW_CELL_EDITABLE, FromDIP(100));
		wxToolBar* sourceBar = MakeBar(page);
		AddTool(sourceBar, _("Add as join"), wxART_ADD, [this] { AddSelectedAsSource(true); });
		AddTool(sourceBar, _("Delete"), wxART_DELETE, [this, removeSelected] { removeSelected(m_sources); });
		row->Add(pane(page, _("Tables"), sourceBar, m_sources), wxSizerFlags(4).Expand().Border(wxALL, gap));

		row->Add(arrows(page, [this] { AddSelectedAsField(); }, [this, removeSelected] { removeSelected(m_fields); }),
			wxSizerFlags().Expand());

		m_fields = makeList(page);
		m_fields->AppendTextColumn(_("Column"), wxDATAVIEW_CELL_EDITABLE, FromDIP(110));
		m_fields->AppendTextColumn(_("Expression"), wxDATAVIEW_CELL_EDITABLE, FromDIP(190));
		wxToolBar* fieldBar = MakeBar(page);
		AddTool(fieldBar, _("Add an expression"), wxART_ADD, [this] {
			wxVector<wxVariant> values;
			values.push_back(wxVariant(wxString::Format(wxT("Column%d"), m_fields->GetItemCount() + 1)));
			values.push_back(wxVariant(wxString()));
			m_fields->AppendItem(values);
			m_fields->SelectRow(m_fields->GetItemCount() - 1);
		});
		AddTool(fieldBar, _("Delete"), wxART_DELETE, [this, removeSelected] { removeSelected(m_fields); });
		AddTool(fieldBar, _("Move up"), wxART_UP, [this, moveSelected] { moveSelected(m_fields, -1); });
		AddTool(fieldBar, _("Move down"), wxART_DOWN, [this, moveSelected] { moveSelected(m_fields, +1); });
		wxBoxSizer* fieldPane = pane(page, _("Fields"), fieldBar, m_fields);
		// Said only while the query groups: then these are not the answer's only columns, and not rows.
		m_fieldsNote = new wxStaticText(page, wxID_ANY, wxEmptyString);
		m_fieldsNote->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
		m_fieldsNote->Hide();
		fieldPane->Add(m_fieldsNote, wxSizerFlags().Expand().Border(wxTOP, gap / 2));
		row->Add(fieldPane, wxSizerFlags(4).Expand().Border(wxALL, gap));

		m_sources->SetDropTarget(new ibCallbackDropTarget([this] { AddSelectedAsSource(m_sources->GetItemCount() > 0); }));
		m_fields->SetDropTarget(new ibCallbackDropTarget([this] { AddSelectedAsField(); }));

		page->SetSizer(row);
		m_notebook->AddPage(page, _("Tables and fields"));
		m_pageTrees.push_back(tree);
	}

	// ---- Conditions: what can be read | the conditions, a `Where` each ----
	{
		wxPanel* page = new wxPanel(m_notebook);
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);

		wxTreeCtrl* tree = MakeSourceTree(page);
		row->Add(pane(page, _("Available tables"), nullptr, tree), wxSizerFlags(3).Expand().Border(wxALL, gap));
		row->Add(arrows(page, [this] { AddSelectedToCondition(); }, [this, removeSelected] { removeSelected(m_conditions); }),
			wxSizerFlags().Expand());

		m_conditions = makeList(page);
		m_conditions->AppendTextColumn(_("Condition"), wxDATAVIEW_CELL_EDITABLE, FromDIP(420));
		// A condition on the rows is read before grouping; one on the GROUPS after it, over the group's
		// name - `grp.Values.Count() > 1`.
		m_conditions->AppendToggleColumn(_("On groups"), wxDATAVIEW_CELL_ACTIVATABLE, FromDIP(80));
		wxToolBar* bar = MakeBar(page);
		AddTool(bar, _("Add a condition"), wxART_ADD, [this] {
			wxVector<wxVariant> values;
			values.push_back(wxVariant(wxString()));
			values.push_back(wxVariant(false));
			m_conditions->AppendItem(values);
			m_conditions->SelectRow(m_conditions->GetItemCount() - 1);
		});
		AddTool(bar, _("Delete"), wxART_DELETE, [this, removeSelected] { removeSelected(m_conditions); });
		row->Add(pane(page, wxString::Format(_("Each line is a '%s'. A variable visible here is written by its name - it is the parameter."),
			Word(KEY_WHERE)), bar, m_conditions), wxSizerFlags(5).Expand().Border(wxALL, gap));
		m_conditions->SetDropTarget(new ibCallbackDropTarget([this] { AddSelectedToCondition(); }));

		page->SetSizer(row);
		m_notebook->AddPage(page, _("Conditions"));
		m_pageTrees.push_back(tree);
	}

	// ---- Grouping: what can be read | the keys | the totals - the query constructor's own page ----
	{
		wxPanel* page = new wxPanel(m_notebook);
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);

		wxTreeCtrl* tree = MakeSourceTree(page);
		row->Add(pane(page, _("Available tables"), nullptr, tree), wxSizerFlags(3).Expand().Border(wxALL, gap));

		wxBoxSizer* lists = new wxBoxSizer(wxVERTICAL);

		// THE KEYS: rows with the same keys become one row of the answer, and every key is its column.
		wxBoxSizer* keyRow = new wxBoxSizer(wxHORIZONTAL);
		keyRow->Add(arrows(page, [this] { AddSelectedAsGroupKey(); }, [this, removeSelected] { removeSelected(m_groupKeys); }),
			wxSizerFlags().Expand());
		m_groupKeys = makeList(page);
		m_groupKeys->AppendTextColumn(_("Column"), wxDATAVIEW_CELL_EDITABLE, FromDIP(110));
		m_groupKeys->AppendTextColumn(_("Grouped by"), wxDATAVIEW_CELL_EDITABLE, FromDIP(220));
		wxToolBar* keyBar = MakeBar(page);
		AddTool(keyBar, _("Delete"), wxART_DELETE, [this, removeSelected] { removeSelected(m_groupKeys); });
		AddTool(keyBar, _("Move up"), wxART_UP, [this, moveSelected] { moveSelected(m_groupKeys, -1); });
		AddTool(keyBar, _("Move down"), wxART_DOWN, [this, moveSelected] { moveSelected(m_groupKeys, +1); });
		keyRow->Add(pane(page, wxString::Format(_("Group by (%s ... %s)"), Word(KEY_GROUP), Word(KEY_BY)), keyBar, m_groupKeys),
			wxSizerFlags(1).Expand().Border(wxALL, gap));
		lists->Add(keyRow, wxSizerFlags(1).Expand());

		// THE TOTALS: what each group makes of its rows - a sum, a count, the smallest, the largest, the mean.
		wxBoxSizer* totalRow = new wxBoxSizer(wxHORIZONTAL);
		totalRow->Add(arrows(page, [this] { AddSelectedAsTotal(); }, [this, removeSelected] { removeSelected(m_totals); }),
			wxSizerFlags().Expand());
		m_totals = makeList(page);
		m_totals->AppendTextColumn(_("Column"), wxDATAVIEW_CELL_EDITABLE, FromDIP(110));
		wxArrayString functions;
		for (const ibLinqTotal total : { ibLinqTotal::Sum, ibLinqTotal::Count, ibLinqTotal::Min, ibLinqTotal::Max, ibLinqTotal::Average })
			functions.Add(ibLinqTotalName(total));
		m_totals->AppendColumn(new ibDataViewColumn(_("Total"),
			new ibDataViewChoiceRenderer(functions, wxDATAVIEW_CELL_EDITABLE), 1, FromDIP(80), wxALIGN_LEFT), wxT("string"));
		m_totals->AppendTextColumn(_("Of the field"), wxDATAVIEW_CELL_EDITABLE, FromDIP(170));
		wxToolBar* totalBar = MakeBar(page);
		AddTool(totalBar, _("Count the rows"), wxART_ADD, [this] {
			wxString name = wxT("Count");
			const auto taken = [this](const wxString& candidate) {
				for (int at = 0; at < m_totals->GetItemCount(); ++at)
					if (stringUtils::CompareString(m_totals->GetTextValue(at, 0), candidate))
						return true;
				return false;
			};
			for (int n = 1; taken(name); ++n)
				name = wxString::Format(wxT("Count%d"), n);
			wxVector<wxVariant> values;
			values.push_back(wxVariant(name));
			values.push_back(wxVariant(ibLinqTotalName(ibLinqTotal::Count)));
			values.push_back(wxVariant(wxString()));
			m_totals->AppendItem(values);
			ShowResult();
		});
		AddTool(totalBar, _("Delete"), wxART_DELETE, [this, removeSelected] { removeSelected(m_totals); });
		totalRow->Add(pane(page, _("Totals over each group's rows"), totalBar, m_totals),
			wxSizerFlags(1).Expand().Border(wxALL, gap));
		lists->Add(totalRow, wxSizerFlags(1).Expand());

		// THE GROUP'S NAME, and what it means, said where it is set.
		wxBoxSizer* intoRow = new wxBoxSizer(wxHORIZONTAL);
		intoRow->Add(new wxStaticText(page, wxID_ANY, wxString::Format(_("The group's name (%s)"), Word(KEY_INTO))),
			wxSizerFlags().CenterVertical().Border(wxRIGHT, gap));
		m_groupInto = new wxTextCtrl(page, wxID_ANY);
		intoRow->Add(m_groupInto, wxSizerFlags(1));
		lists->Add(intoRow, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, gap * 2));
		wxStaticText* meaning = new wxStaticText(page, wxID_ANY,
			_("Rows with the same keys become one row of the answer: its columns are the keys and the totals. "
			  "The order then sorts the groups - by a key, or by a total's column name. Past the name, a condition "
			  "marked 'On groups' or a field reads the group: <name>.Key, <name>.Values. "
			  "Without a name the answer is the groups themselves, and no column can be chosen."));
		meaning->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
		meaning->Wrap(FromDIP(560));
		lists->Add(meaning, wxSizerFlags().Expand().Border(wxALL, gap * 2));

		row->Add(lists, wxSizerFlags(5).Expand());

		m_groupKeys->SetDropTarget(new ibCallbackDropTarget([this] { AddSelectedAsGroupKey(); }));
		m_totals->SetDropTarget(new ibCallbackDropTarget([this] { AddSelectedAsTotal(); }));

		page->SetSizer(row);
		m_notebook->AddPage(page, _("Grouping"));
		m_pageTrees.push_back(tree);
	}

	// ---- Order: what can be read | the keys, in order ----
	{
		wxPanel* page = new wxPanel(m_notebook);
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);

		wxTreeCtrl* tree = MakeSourceTree(page);
		row->Add(pane(page, _("Available tables"), nullptr, tree), wxSizerFlags(3).Expand().Border(wxALL, gap));
		row->Add(arrows(page, [this] { AddSelectedToOrder(); }, [this, removeSelected] { removeSelected(m_order); }),
			wxSizerFlags().Expand());

		m_order = makeList(page);
		m_order->AppendTextColumn(_("Expression"), wxDATAVIEW_CELL_EDITABLE, FromDIP(320));
		m_order->AppendToggleColumn(Word(KEY_DESCENDING), wxDATAVIEW_CELL_ACTIVATABLE, FromDIP(90));
		wxToolBar* bar = MakeBar(page);
		// An order written by hand - a total's column name, when the query groups.
		AddTool(bar, _("Add an expression"), wxART_ADD, [this] {
			wxVector<wxVariant> values;
			values.push_back(wxVariant(wxString()));
			values.push_back(wxVariant(false));
			m_order->AppendItem(values);
			m_order->SelectRow(m_order->GetItemCount() - 1);
		});
		AddTool(bar, _("Delete"), wxART_DELETE, [this, removeSelected] { removeSelected(m_order); });
		AddTool(bar, _("Move up"), wxART_UP, [this, moveSelected] { moveSelected(m_order, -1); });
		AddTool(bar, _("Move down"), wxART_DOWN, [this, moveSelected] { moveSelected(m_order, +1); });
		row->Add(pane(page, _("Order"), bar, m_order), wxSizerFlags(5).Expand().Border(wxALL, gap));
		m_order->SetDropTarget(new ibCallbackDropTarget([this] { AddSelectedToOrder(); }));

		page->SetSizer(row);
		m_notebook->AddPage(page, _("Order"));
		m_pageTrees.push_back(tree);
	}

	// ---- Other: the query constructor's "Record selection" - a switch and a number of rows ----
	{
		wxPanel* page = new wxPanel(m_notebook);
		wxStaticBoxSizer* selection = new wxStaticBoxSizer(wxVERTICAL, page, _("Record selection"));
		wxWindow* box = selection->GetStaticBox();

		// ⭐ A NUMBER OF ROWS IS A NUMBER, so it is a counter with arrows, switched on by its word - as the
		// query constructor's `First`. A variable read out of a block opened (`Take pageSize`) is kept,
		// and said beside the counter, until the counter is touched.
		const auto countRow = [&](ibRowCount& count, int key, const wxString& meaning) {
			wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
			count.m_use = new wxCheckBox(box, wxID_ANY, Word(key));
			row->Add(count.m_use, wxSizerFlags().CenterVertical().Border(wxRIGHT, gap));
			count.m_count = new wxSpinCtrl(box, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(90, -1)),
				wxSP_ARROW_KEYS, 1, 1000000, 10);
			count.m_count->Disable();
			row->Add(count.m_count, wxSizerFlags().CenterVertical().Border(wxRIGHT, gap));
			wxStaticText* said = new wxStaticText(box, wxID_ANY, meaning);
			said->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
			row->Add(said, wxSizerFlags().CenterVertical().Border(wxRIGHT, gap));
			count.m_kept = new wxStaticText(box, wxID_ANY, wxEmptyString);
			count.m_kept->Hide();
			row->Add(count.m_kept, wxSizerFlags().CenterVertical());
			selection->Add(row, wxSizerFlags().Border(wxALL, gap / 2));

			ibRowCount* const held = &count;
			count.m_use->Bind(wxEVT_CHECKBOX, [this, held](wxCommandEvent&) {
				held->m_count->Enable(held->m_use->IsChecked());
				ShowResult();
			});
			count.m_count->Bind(wxEVT_SPINCTRL, [this, held](wxSpinEvent&) {
				KeepCount(*held, wxString());   // touched: the number is what it says now
				ShowResult();
			});
		};
		countRow(m_skipCount, KEY_SKIP, _("rows to pass over"));
		countRow(m_takeCount, KEY_TAKE, _("rows at most"));

		m_distinct = new wxCheckBox(box, wxID_ANY, wxString::Format(_("%s - no repeated rows"), Word(KEY_DISTINCT)));
		selection->Add(m_distinct, wxSizerFlags().Border(wxALL, gap / 2));

		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(selection, wxSizerFlags().Border(wxALL, gap * 2));
		page->SetSizer(sizer);
		m_notebook->AddPage(page, _("Other"));
		m_pageTrees.push_back(nullptr);
	}

	top->Add(m_notebook, wxSizerFlags(1).Expand().Border(wxALL, gap));

	// HOW IT IS USED, said once: the tree on the left is what can be read, and everything moves out of it
	// the same three ways.
	wxStaticText* howTo = new wxStaticText(this, wxID_ANY,
		_("Drag a table or a field from the tree on the left into a list, double-click it, or press the arrow. "
		  "A table whose columns are not known opens to a line that lets you write a field by hand."));
	howTo->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	top->Add(howTo, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, gap * 2));

	// ---- the block, coloured as the module colours it, and what the compiler made of it ----
	m_block = new ibCodeEditor(nullptr, this, wxID_ANY);
	m_block->SetMinSize(FromDIP(wxSize(-1, 110)));
	if (mainFrame != nullptr)
		m_block->RefreshEditor();
	for (int margin = 0; margin < 5; ++margin)
		m_block->SetMarginWidth(margin, 0);   // no line numbers, breakpoints or folds for a preview
	m_block->SetReadOnly(true);
	top->Add(m_block, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, gap));

	m_verdict = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
	top->Add(m_verdict, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap * 2));
	top->Add(CreateStdDialogButtonSizer(readOnly ? wxCANCEL : (wxOK | wxCANCEL)),
		wxSizerFlags().Right().Border(wxALL, gap * 2));

	m_groupInto->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { ShowResult(); });
	m_distinct->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { ShowResult(); });
	Bind(wxEVT_BUTTON, &ibDialogLinqConstructor::OnOk, this, wxID_OK);

	SetSizerAndFit(top);
	SetSize(FromDIP(wxSize(1060, 700)));
	CentreOnParent();

	if (m_replacing)
		Open(opened);
	else if (!openRefusal.IsEmpty())
		wxMessageBox(openRefusal + wxT("\n\n")
			+ _("The window opens empty, and OK writes a new block at the caret."), GetTitle(), wxOK | wxICON_INFORMATION, parent);

	ShowResult();
}

// ---------------------------------------------------------------------------
//  IntelliSense, asked
// ---------------------------------------------------------------------------

ibDialogLinqConstructor::ibProbe ibDialogLinqConstructor::Ask(const wxString& probe) const
{
	ibProbe answer;

	// ⭐ AS A COMPLETION, like the dropdown: answering compiles the text and walks it, values are made
	// on the way, and a name not written yet is ordinary here rather than an error.
	const ibBackendException::ibEvalModeScope answering(eval_complete);
	std::vector<ibCaretValue> values;
	try {
		if (!ibValueAtCaret(probe, (unsigned int)probe.length(), m_module, values))
			return answer;
	}
	catch (...) {
		return answer;
	}

	answer.m_resolved = !values.empty();
	for (ibCaretValue& branch : values) {
		ibValue& value = branch.m_value;
		if (answer.m_type.IsEmpty()) {
			try {
				answer.m_type = value.GetClassName();
			}
			catch (...) {
			}
		}
		for (long i = 0; i < value.GetNProps(); i++) {
			// The same filter the editor applies: a scope-local name belongs to its frame.
			if (value.IsPropScoped(i))
				continue;
			const wxString name = value.GetPropName(i);
			if (std::find(answer.m_names.begin(), answer.m_names.end(), name) == answer.m_names.end())
				answer.m_names.push_back(name);
		}
	}
	return answer;
}

ibDialogLinqConstructor::ibProbe ibDialogLinqConstructor::MembersOf(const wxString& expression) const
{
	return Ask(m_probeHead + wxT("linqProbe = ") + expression + wxT("."));
}

ibDialogLinqConstructor::ibProbe ibDialogLinqConstructor::RowOf(const wxString& expression) const
{
	// A ROW is what the source yields, and it is asked where a row is: inside a query over it, after
	// the dot of its alias - exactly where a person writing the query by hand stands.
	return Ask(m_probeHead + wxT("linqProbe = ") + Word(KEY_FROM) + wxT(" ") + kProbeRow + wxT(" ")
		+ Word(KEY_IN) + wxT(" ") + expression + wxT(" ") + Word(KEY_WHERE) + wxT(" ") + kProbeRow + wxT("."));
}

// ---------------------------------------------------------------------------
//  The trees
// ---------------------------------------------------------------------------

int ibDialogLinqConstructor::IconIndex(wxTreeCtrl* tree, const wxString& key, const std::function<wxIcon()>& make)
{
	std::map<wxString, int>& known = m_treeIcons[tree];
	const auto found = known.find(key);
	if (found != known.end())
		return found->second;

	int index = -1;
	const wxIcon icon = make();
	if (icon.IsOk() && tree->GetImageList() != nullptr)
		index = tree->GetImageList()->Add(icon);
	known[key] = index;
	return index;
}

// A METATYPE'S OWN PICTURE, taken from its registration - `Data.Catalogs` holds catalogs, and a catalog
// is what the type registry calls "Catalog". One rule turns the section's name into the kind's
// (`ChartsOf...` into `ChartOf...`, a plural into its singular), so a metatype added tomorrow is dressed
// the day it registers.
int ibDialogLinqConstructor::KindIcon(wxTreeCtrl* tree, const wxString& section)
{
	return IconIndex(tree, wxT("kind:") + section, [this, &section]() -> wxIcon {
		wxString kind = section;
		if (kind.StartsWith(wxT("ChartsOf")))
			kind = wxT("ChartOf") + kind.Mid(8);
		else if (kind.EndsWith(wxT("s")))
			kind.RemoveLast();
		const ibMetaData* metaData = m_module != nullptr ? m_module->GetMetaData() : nullptr;
		if (metaData != nullptr) {
			if (const ibCtorAbstractType* ctor = metaData->GetAvailableCtor(kind)) {
				const wxIcon icon = ctor->GetClassIcon();
				if (icon.IsOk())
					return icon;
			}
		}
		return wxArtProvider::GetIcon(wxART_COMMON_FOLDER, wxART_METATREE, wxSize(16, 16));
	});
}

bool ibDialogLinqConstructor::IsPlainValue(const ibProbe& members)
{
	if (!members.m_resolved || !members.m_names.empty())
		return false;
	for (const wxChar* plain : { wxT("Number"), wxT("String"), wxT("Date"), wxT("Boolean") })
		if (members.m_type == plain)
			return true;
	return false;
}

// One place under `parent`. Asked (`probe`) whether it is only a plain value, which then stays a leaf;
// a place under a section of `Data` is a table and is not asked.
void ibDialogLinqConstructor::AddPlace(wxTreeCtrl* tree, const wxTreeItemId& parent, const wxString& name,
	const wxString& expression, int icon, bool probe)
{
	const bool plain = probe && IsPlainValue(MembersOf(expression));
	const wxTreeItemId item = tree->AppendItem(parent, name, icon, -1,
		new ibLinqNode(plain ? ibNodeKind::Value : ibNodeKind::Container, expression));
	tree->SetItemHasChildren(item, !plain);
}

wxTreeCtrl* ibDialogLinqConstructor::MakeSourceTree(wxWindow* parent)
{
	// The modern arrow and no lines - the same as every tree of the query constructor.
	wxTreeCtrl* tree = new wxTreeCtrl(parent, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(250, 300)),
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	tree->AssignImageList(new wxImageList(16, 16));

	const wxTreeItemId root = tree->AddRoot(wxEmptyString);

	// ⭐ THE CONFIGURATION'S TABLES FIRST, the way the query constructor opens on its kinds: `Data`'s
	// sections at the top, each in its kind's picture, their tables inside. The expression each stands
	// for is the one LINQ reads by - `Data.Catalogs.Goods`.
	for (const wxString& section : m_sections) {
		const wxTreeItemId item = tree->AppendItem(root, section, KindIcon(tree, section), -1,
			new ibLinqNode(ibNodeKind::Container, wxT("Data.") + section));
		tree->SetItemHasChildren(item, true);
	}

	// …AND WHAT THIS PLACE IN THE MODULE CAN SEE: a table filled above, another query's answer, the
	// object's own tabular section - opened like the rest. A plain value is a leaf, for a condition.
	const wxTreeItemId visible = tree->AppendItem(root, _("Visible here"),
		IconIndex(tree, wxT("visible"), [] { return wxArtProvider::GetIcon(wxART_LOCAL_VARIABLE, wxART_SERVICE, wxSize(16, 16)); }),
		-1, new ibLinqNode(ibNodeKind::Group, wxString()));
	const int variableIcon = IconIndex(tree, wxT("variable"),
		[] { return wxArtProvider::GetIcon(wxART_VARIABLE, wxART_AUTOCOMPLETE, wxSize(16, 16)); });
	for (size_t i = 0; i < m_variables.size(); ++i) {
		const bool opens = i < m_variableOpens.size() && m_variableOpens[i];
		const wxTreeItemId item = tree->AppendItem(visible, m_variables[i], variableIcon, -1,
			new ibLinqNode(opens ? ibNodeKind::Variable : ibNodeKind::Value, m_variables[i]));
		tree->SetItemHasChildren(item, opens);
	}
	if (m_variables.empty())
		tree->AppendItem(visible, _("(no variables above this place)"), -1, -1, new ibLinqNode(ibNodeKind::Hint, wxString()));

	tree->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogLinqConstructor::OnExpanding, this);
	tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibDialogLinqConstructor::OnActivated, this);
	tree->Bind(wxEVT_TREE_BEGIN_DRAG, &ibDialogLinqConstructor::OnBeginDrag, this);
	tree->Expand(visible);
	return tree;
}

// ⭐ A DRAG IS THE ARROW, REACHED BY THE MOUSE: the dragged node is selected, and the list it is dropped
// on runs its own verb on the selection (ibCallbackDropTarget). NOT `Allow()`ed - that would start the
// tree's own native drag beside this one, which MSW refuses (see queryConstructorEdit.cpp).
void ibDialogLinqConstructor::OnBeginDrag(wxTreeEvent& event)
{
	wxTreeCtrl* tree = wxDynamicCast(event.GetEventObject(), wxTreeCtrl);
	if (tree == nullptr || NodeOf(tree, event.GetItem()) == nullptr)
		return;
	tree->SelectItem(event.GetItem());
	wxTextDataObject payload(wxT("linq"));
	wxDropSource drag(payload, tree);
	drag.DoDragDrop(wxDrag_CopyOnly);
}

void ibDialogLinqConstructor::FillFields(wxTreeCtrl* tree, const wxTreeItemId& item, const wxString& expression, const ibProbe& row)
{
	const int fieldIcon = IconIndex(tree, wxT("field"), [] { return ibValue::GetIconGroup(); });
	for (const wxString& field : row.m_names)
		tree->AppendItem(item, field, fieldIcon, -1, new ibLinqNode(ibNodeKind::Field, expression, field));

	if (!row.m_names.empty())
		return;

	// A SOURCE WHOSE ROWS THE WALK CANNOT DESCRIBE is still a source: its fields are written in by
	// hand, on the author's responsibility - a double click on this line asks for one.
	tree->AppendItem(item, row.m_resolved
		? wxString::Format(_("%s - its columns are not known before it runs: double-click to write one"), row.m_type)
		: _("not read as a table here - double-click to write a field by hand"),
		-1, -1, new ibLinqNode(ibNodeKind::Hint, expression));
}

void ibDialogLinqConstructor::OnExpanding(wxTreeEvent& event)
{
	wxTreeCtrl* tree = wxDynamicCast(event.GetEventObject(), wxTreeCtrl);
	const wxTreeItemId item = event.GetItem();
	ibLinqNode* node = NodeOf(tree, item);
	if (node == nullptr || node->m_loaded)
		return;
	node->m_loaded = true;

	wxBusyCursor busy;
	tree->DeleteChildren(item);

	// ⭐ ONE RULE FOR EVERY PLACE, read off what IntelliSense answers about it:
	//   * a STRUCTURE (`Data`, `Data.Catalogs`) is a place with places in it - asked for its rows it would
	//     answer key-value pairs, which is true and useless;
	//   * something whose ROWS have fields is a table - its children are the fields;
	//   * something with MEMBERS and no rows (`RegisterRecords`, a reference) is a place: a record set, a
	//     tabular section waits inside it;
	//   * something iterated whose rows name nothing (a table filled at run time), or that did not resolve
	//     at all (an argument of no known type), is a table whose fields are written by hand.
	const ibProbe members = MembersOf(node->m_expression);
	const bool structure = members.m_type == wxT("Structure");
	const ibProbe rows = structure ? ibProbe() : RowOf(node->m_expression);
	const bool underData = node->m_expression.StartsWith(wxT("Data."));

	if (!structure && !rows.m_names.empty()) {
		node->m_kind = ibNodeKind::Source;
		FillFields(tree, item, node->m_expression, rows);
	}
	else if (!members.m_names.empty()) {
		node->m_kind = ibNodeKind::Container;   // a variable that turned out to be a place, not a table
		// A section's members are tables of its kind; anything else's are asked what they are.
		const wxString section = node->m_expression.AfterFirst(wxT('.')).BeforeFirst(wxT('.'));
		const int icon = underData ? KindIcon(tree, section)
			: IconIndex(tree, wxT("variable"), [] { return wxArtProvider::GetIcon(wxART_VARIABLE, wxART_AUTOCOMPLETE, wxSize(16, 16)); });
		for (const wxString& member : members.m_names)
			AddPlace(tree, item, member, node->m_expression + wxT(".") + member, icon, /*probe*/ !underData);
	}
	else if (structure) {
		// A section with nothing in it - a configuration with no constants - is empty, not a table.
		tree->AppendItem(item, _("(nothing here)"), -1, -1, new ibLinqNode(ibNodeKind::Hint, wxString()));
	}
	else {
		node->m_kind = ibNodeKind::Source;
		FillFields(tree, item, node->m_expression, rows);
	}

	if (!tree->ItemHasChildren(item))
		tree->SetItemHasChildren(item, false);
}

wxTreeCtrl* ibDialogLinqConstructor::CurrentTree() const
{
	const int page = m_notebook != nullptr ? m_notebook->GetSelection() : wxNOT_FOUND;
	return page >= 0 && (size_t)page < m_pageTrees.size() ? m_pageTrees[page] : nullptr;
}

void ibDialogLinqConstructor::OnActivated(wxTreeEvent& event)
{
	wxTreeCtrl* tree = wxDynamicCast(event.GetEventObject(), wxTreeCtrl);
	ibLinqNode* node = NodeOf(tree, event.GetItem());
	if (node == nullptr) {
		event.Skip();
		return;
	}

	// A double click does what the arrow of its tab does.
	const int page = m_notebook->GetSelection();
	switch (node->m_kind) {
	case ibNodeKind::Field:
		if (page == 0) AddSelectedAsField();
		else if (page == 1) AddSelectedToCondition();
		else if (page == 2) AddSelectedAsGroupKey();   // a key, as the query constructor's first list
		else if (page == 3) AddSelectedToOrder();
		break;
	case ibNodeKind::Source:
		if (page == 0) AddSelectedAsSource(m_sources->GetItemCount() > 0);
		else event.Skip();
		break;
	case ibNodeKind::Value:
		if (page == 1) AddSelectedToCondition();
		break;
	case ibNodeKind::Hint:
		if (!node->m_expression.IsEmpty())
			WriteFieldByHand(tree, event.GetItem());
		break;
	default:
		event.Skip();   // a place to go into, not a thing to pick
		break;
	}
}

// ⭐ ON THE AUTHOR'S RESPONSIBILITY: a field of a source whose rows the walk cannot describe is written
// in by name. It lands in the tree like any other field and is used the same way; nothing here can
// check it, and the compiler's verdict under the block is what does.
void ibDialogLinqConstructor::WriteFieldByHand(wxTreeCtrl* tree, const wxTreeItemId& hint)
{
	const wxTreeItemId source = tree->GetItemParent(hint);
	ibLinqNode* node = NodeOf(tree, hint);
	if (!source.IsOk() || node == nullptr)
		return;

	const wxString name = wxString(wxGetTextFromUser(
		_("The field's name, as it is written after a dot. The constructor cannot check it - the compiler's verdict under the block will."),
		GetTitle(), wxEmptyString, this)).Trim(true).Trim(false);
	if (name.IsEmpty())
		return;

	const wxTreeItemId field = tree->InsertItem(source, tree->GetPrevSibling(hint), name,
		IconIndex(tree, wxT("field"), [] { return ibValue::GetIconGroup(); }), -1,
		new ibLinqNode(ibNodeKind::Field, node->m_expression, name));
	tree->SelectItem(field);
}

// ---------------------------------------------------------------------------
//  The verbs
// ---------------------------------------------------------------------------

void ibDialogLinqConstructor::AddSelectedAsSource(bool join)
{
	wxTreeCtrl* tree = CurrentTree();
	const wxTreeItemId item = tree != nullptr ? tree->GetSelection() : wxTreeItemId();
	ibLinqNode* node = NodeOf(tree, item);
	if (node == nullptr)
		return;
	// A place not opened yet does not know whether it is a table: opening it decides.
	if ((node->m_kind == ibNodeKind::Container || node->m_kind == ibNodeKind::Variable) && !node->m_loaded)
		tree->Expand(item);
	if (node->m_kind == ibNodeKind::Variable || node->m_kind == ibNodeKind::Source || node->m_kind == ibNodeKind::Field)
		AddSource(node->m_expression, join);
	else
		wxBell();
}

wxString ibDialogLinqConstructor::QualifiedField(const wxString& sourceExpression, const wxString& field)
{
	wxString alias = AliasOf(sourceExpression);
	if (alias.IsEmpty()) {
		AddSource(sourceExpression, m_sources->GetItemCount() > 0);
		alias = AliasOf(sourceExpression);
	}
	return alias + wxT(".") + field;
}

void ibDialogLinqConstructor::AddSelectedAsField()
{
	wxTreeCtrl* tree = CurrentTree();
	const wxTreeItemId item = tree != nullptr ? tree->GetSelection() : wxTreeItemId();
	ibLinqNode* node = NodeOf(tree, item);
	if (node == nullptr) {
		wxBell();
		return;
	}

	const auto addOne = [this](const wxString& sourceExpression, const wxString& field) {
		const wxString expression = QualifiedField(sourceExpression, field);
		wxString name = field;
		const auto taken = [&](const wxString& candidate) {
			for (int row = 0; row < m_fields->GetItemCount(); ++row)
				if (stringUtils::CompareString(m_fields->GetTextValue(row, 0), candidate))
					return true;
			return false;
		};
		for (int n = 1; taken(name); ++n)
			name = field + wxString::Format(wxT("%d"), n);

		wxVector<wxVariant> values;
		values.push_back(wxVariant(name));
		values.push_back(wxVariant(expression));
		m_fields->AppendItem(values);
	};

	if (node->m_kind == ibNodeKind::Field) {
		addOne(node->m_expression, node->m_field);
	}
	else {
		// A TABLE brought to the fields brings all of its own - opened first when it is not yet.
		if ((node->m_kind == ibNodeKind::Container || node->m_kind == ibNodeKind::Variable) && !node->m_loaded)
			tree->Expand(item);
		if (node->m_kind != ibNodeKind::Source) {
			wxBell();
			return;
		}
		wxTreeItemIdValue cookie;
		for (wxTreeItemId child = tree->GetFirstChild(item, cookie); child.IsOk(); child = tree->GetNextChild(item, cookie)) {
			const ibLinqNode* field = NodeOf(tree, child);
			if (field != nullptr && field->m_kind == ibNodeKind::Field)
				addOne(field->m_expression, field->m_field);
		}
	}
	ShowResult();
}

void ibDialogLinqConstructor::AddSelectedToCondition()
{
	wxTreeCtrl* tree = CurrentTree();
	ibLinqNode* node = NodeOf(tree, tree != nullptr ? tree->GetSelection() : wxTreeItemId());
	if (node == nullptr)
		return;

	wxString piece;
	if (node->m_kind == ibNodeKind::Field)
		piece = QualifiedField(node->m_expression, node->m_field) + wxT(" = ");
	else if (node->m_kind == ibNodeKind::Variable || node->m_kind == ibNodeKind::Value)
		piece = node->m_expression;   // THE PARAMETER IS THE VARIABLE - written by its name
	else {
		wxBell();
		return;
	}

	// Onto the selected condition when there is one - `g.Price > ` waiting for its right-hand side is
	// exactly where a variable goes - otherwise a condition of its own.
	const int row = m_conditions->GetSelectedRow();
	if (row != wxNOT_FOUND && node->m_kind != ibNodeKind::Field) {
		const wxString was = m_conditions->GetTextValue(row, 0);
		m_conditions->SetTextValue(was + (was.IsEmpty() || was.EndsWith(wxT(" ")) ? wxString() : wxString(wxT(" "))) + piece, row, 0);
	}
	else {
		wxVector<wxVariant> values;
		values.push_back(wxVariant(piece));
		values.push_back(wxVariant(false));
		m_conditions->AppendItem(values);
		m_conditions->SelectRow(m_conditions->GetItemCount() - 1);
	}
	ShowResult();
}

bool ibDialogLinqConstructor::SelectedField(wxString& expression, wxString& name)
{
	wxTreeCtrl* tree = CurrentTree();
	ibLinqNode* node = NodeOf(tree, tree != nullptr ? tree->GetSelection() : wxTreeItemId());
	if (node == nullptr || node->m_kind != ibNodeKind::Field) {
		wxBell();
		return false;
	}
	expression = QualifiedField(node->m_expression, node->m_field);

	// The field's own name, numbered when a column of the answer already has it.
	const auto taken = [this](const wxString& candidate) {
		for (const ibDataViewListCtrl* columns : { m_groupKeys, m_totals, m_fields })
			for (int at = 0; at < columns->GetItemCount(); ++at)
				if (stringUtils::CompareString(columns->GetTextValue(at, 0), candidate))
					return true;
		return false;
	};
	name = node->m_field;
	for (int n = 1; taken(name); ++n)
		name = node->m_field + wxString::Format(wxT("%d"), n);
	return true;
}

void ibDialogLinqConstructor::NameTheGroup()
{
	if (!wxString(m_groupInto->GetValue()).Trim(true).Trim(false).IsEmpty())
		return;
	// `grp`, numbered when a variable visible here or a source already has the name.
	const auto taken = [this](const wxString& candidate) {
		for (const wxString& variable : m_variables)
			if (stringUtils::CompareString(variable, candidate))
				return true;
		for (int at = 0; at < m_sources->GetItemCount(); ++at)
			if (stringUtils::CompareString(m_sources->GetTextValue(at, 0), candidate))
				return true;
		return false;
	};
	wxString name = wxT("grp");
	for (int n = 1; taken(name); ++n)
		name = wxString::Format(wxT("grp%d"), n);
	m_groupInto->ChangeValue(name);
}

void ibDialogLinqConstructor::AddSelectedAsGroupKey()
{
	wxString expression, name;
	if (!SelectedField(expression, name))
		return;

	// ⭐ A KEY IS A COLUMN OF THE ANSWER. The same field already among the fields - picked there before the
	// query grouped - would be written a second time, reading a row the answer no longer has: it moves.
	for (int at = m_fields->GetItemCount() - 1; at >= 0; --at)
		if (stringUtils::CompareString(wxString(m_fields->GetTextValue(at, 1)).Trim(true).Trim(false), expression)) {
			name = m_fields->GetTextValue(at, 0);
			m_fields->DeleteItem(at);
		}

	wxVector<wxVariant> values;
	values.push_back(wxVariant(name));
	values.push_back(wxVariant(expression));
	m_groupKeys->AppendItem(values);
	m_groupKeys->SelectRow(m_groupKeys->GetItemCount() - 1);
	NameTheGroup();
	ShowResult();
}

void ibDialogLinqConstructor::AddSelectedAsTotal()
{
	wxString expression, name;
	if (!SelectedField(expression, name))
		return;

	// A total of a field that was among the fields replaces it there, as a key does.
	for (int at = m_fields->GetItemCount() - 1; at >= 0; --at)
		if (stringUtils::CompareString(wxString(m_fields->GetTextValue(at, 1)).Trim(true).Trim(false), expression)) {
			name = m_fields->GetTextValue(at, 0);
			m_fields->DeleteItem(at);
		}

	wxVector<wxVariant> values;
	values.push_back(wxVariant(name));
	values.push_back(wxVariant(ibLinqTotalName(ibLinqTotal::Sum)));
	values.push_back(wxVariant(expression));
	m_totals->AppendItem(values);
	m_totals->SelectRow(m_totals->GetItemCount() - 1);
	ShowResult();
}

void ibDialogLinqConstructor::AddSelectedToOrder()
{
	wxTreeCtrl* tree = CurrentTree();
	ibLinqNode* node = NodeOf(tree, tree != nullptr ? tree->GetSelection() : wxTreeItemId());
	if (node == nullptr || node->m_kind != ibNodeKind::Field) {
		wxBell();
		return;
	}
	wxVector<wxVariant> values;
	values.push_back(wxVariant(QualifiedField(node->m_expression, node->m_field)));
	values.push_back(wxVariant(false));
	m_order->AppendItem(values);
	ShowResult();
}

wxString ibDialogLinqConstructor::AliasOf(const wxString& sourceExpression) const
{
	for (int row = 0; row < m_sources->GetItemCount(); ++row) {
		if (m_sources->GetTextValue(row, 1) == sourceExpression)
			return m_sources->GetTextValue(row, 0);
	}
	return wxString();
}

wxString ibDialogLinqConstructor::NewAlias(const wxString& expression) const
{
	// The first letter of what is read, lower case - `g` for Goods - numbered when it is taken, by
	// another source or by a name visible here (an alias shadowing a variable would change what the
	// block reads).
	wxString base = expression.AfterLast(wxT('.')).Left(1).Lower();
	if (base.IsEmpty())
		base = wxT("r");
	const auto taken = [&](const wxString& candidate) {
		for (int row = 0; row < m_sources->GetItemCount(); ++row)
			if (stringUtils::CompareString(m_sources->GetTextValue(row, 0), candidate))
				return true;
		for (const wxString& name : m_variables)
			if (stringUtils::CompareString(name, candidate))
				return true;
		return false;
	};
	wxString alias = base;
	for (int n = 1; taken(alias); ++n)
		alias = base + wxString::Format(wxT("%d"), n);
	return alias;
}

void ibDialogLinqConstructor::AddSource(const wxString& expression, bool join)
{
	if (m_sources->GetItemCount() == 0)
		join = false;   // a join needs something to join to

	const wxString alias = NewAlias(expression);
	const wxString first = m_sources->GetItemCount() > 0 ? m_sources->GetTextValue(0, 0) : wxString();

	wxVector<wxVariant> values;
	values.push_back(wxVariant(alias));
	values.push_back(wxVariant(expression));
	values.push_back(wxVariant(join));
	// The keys start as the two aliases and a dot - the place to write them, not a guess at them.
	values.push_back(wxVariant(join ? first + wxT(".") : wxString()));
	values.push_back(wxVariant(join ? alias + wxT(".") : wxString()));
	m_sources->AppendItem(values);
	ShowResult();
}

void ibDialogLinqConstructor::Open(const ibLinqBlock& block)
{
	for (const ibLinqBlockSource& source : block.m_sources) {
		wxVector<wxVariant> values;
		values.push_back(wxVariant(source.m_alias));
		values.push_back(wxVariant(source.m_expression));
		values.push_back(wxVariant(source.m_join));
		values.push_back(wxVariant(source.m_leftKey));
		values.push_back(wxVariant(source.m_rightKey));
		m_sources->AppendItem(values);
	}
	for (const ibLinqBlockField& field : block.m_fields) {
		wxVector<wxVariant> values;
		values.push_back(wxVariant(field.m_name));
		values.push_back(wxVariant(field.m_expression));
		m_fields->AppendItem(values);
	}
	for (const bool onGroups : { false, true })
		for (const wxString& condition : onGroups ? block.m_groupConditions : block.m_conditions) {
			wxVector<wxVariant> values;
			values.push_back(wxVariant(condition));
			values.push_back(wxVariant(onGroups));
			m_conditions->AppendItem(values);
		}
	for (const ibLinqBlockOrder& order : block.m_order) {
		wxVector<wxVariant> values;
		values.push_back(wxVariant(order.m_expression));
		values.push_back(wxVariant(order.m_descending));
		m_order->AppendItem(values);
	}
	for (const ibLinqBlockField& key : block.m_groupKeys) {
		wxVector<wxVariant> values;
		values.push_back(wxVariant(key.m_name));
		values.push_back(wxVariant(key.m_expression));
		m_groupKeys->AppendItem(values);
	}
	for (const ibLinqBlockTotal& total : block.m_totals) {
		wxVector<wxVariant> values;
		values.push_back(wxVariant(total.m_name));
		values.push_back(wxVariant(ibLinqTotalName(total.m_function)));
		values.push_back(wxVariant(total.m_expression));
		m_totals->AppendItem(values);
	}
	m_groupInto->ChangeValue(block.m_groupInto);
	OpenCount(m_skipCount, block.m_skip);
	OpenCount(m_takeCount, block.m_take);
	m_distinct->SetValue(block.m_distinct);
}

ibLinqBlock ibDialogLinqConstructor::Collect() const
{
	ibLinqBlock block;
	const auto text = [](const ibDataViewListCtrl* list, int row, unsigned int col) {
		return wxString(list->GetTextValue(row, col)).Trim(true).Trim(false);
	};

	for (int row = 0; row < m_sources->GetItemCount(); ++row) {
		ibLinqBlockSource source;
		source.m_alias      = text(m_sources, row, 0);
		source.m_expression = text(m_sources, row, 1);
		source.m_join       = m_sources->GetToggleValue(row, 2);
		source.m_leftKey    = text(m_sources, row, 3);
		source.m_rightKey   = text(m_sources, row, 4);
		block.m_sources.push_back(source);
	}
	for (int row = 0; row < m_fields->GetItemCount(); ++row) {
		ibLinqBlockField field;
		field.m_name       = text(m_fields, row, 0);
		field.m_expression = text(m_fields, row, 1);
		block.m_fields.push_back(field);
	}
	for (int row = 0; row < m_conditions->GetItemCount(); ++row)
		(m_conditions->GetToggleValue(row, 1) ? block.m_groupConditions : block.m_conditions).push_back(text(m_conditions, row, 0));
	for (int row = 0; row < m_order->GetItemCount(); ++row) {
		ibLinqBlockOrder order;
		order.m_expression = text(m_order, row, 0);
		order.m_descending = m_order->GetToggleValue(row, 1);
		block.m_order.push_back(order);
	}
	for (int row = 0; row < m_groupKeys->GetItemCount(); ++row) {
		ibLinqBlockField key;
		key.m_name       = text(m_groupKeys, row, 0);
		key.m_expression = text(m_groupKeys, row, 1);
		block.m_groupKeys.push_back(key);
	}
	for (int row = 0; row < m_totals->GetItemCount(); ++row) {
		ibLinqBlockTotal total;
		total.m_name = text(m_totals, row, 0);
		const wxString function = text(m_totals, row, 1);
		for (const ibLinqTotal candidate : { ibLinqTotal::Sum, ibLinqTotal::Count, ibLinqTotal::Min, ibLinqTotal::Max, ibLinqTotal::Average })
			if (stringUtils::CompareString(ibLinqTotalName(candidate), function))
				total.m_function = candidate;
		total.m_expression = text(m_totals, row, 2);
		block.m_totals.push_back(total);
	}
	block.m_groupInto = wxString(m_groupInto->GetValue()).Trim(true).Trim(false);
	block.m_skip      = CountText(m_skipCount);
	block.m_take      = CountText(m_takeCount);
	block.m_distinct  = m_distinct->IsChecked();
	return block;
}

void ibDialogLinqConstructor::KeepCount(ibRowCount& count, const wxString& variable)
{
	count.m_variable = variable;
	count.m_kept->SetLabelText(variable.IsEmpty() ? wxString() : wxString::Format(_("now: %s"), variable));
	if (count.m_kept->IsShown() != !variable.IsEmpty()) {
		count.m_kept->Show(!variable.IsEmpty());
		count.m_kept->GetParent()->Layout();
	}
}

void ibDialogLinqConstructor::OpenCount(ibRowCount& count, const wxString& text)
{
	const wxString written = wxString(text).Trim(true).Trim(false);
	long number = 0;
	const bool isNumber = !written.IsEmpty() && written.ToLong(&number) && number > 0;
	count.m_use->SetValue(!written.IsEmpty());
	count.m_count->Enable(!written.IsEmpty());
	if (isNumber)
		count.m_count->SetValue((int)std::min<long>(number, count.m_count->GetMax()));
	KeepCount(count, isNumber || written.IsEmpty() ? wxString() : written);
}

wxString ibDialogLinqConstructor::CountText(const ibRowCount& count)
{
	if (count.m_use == nullptr || !count.m_use->IsChecked())
		return wxString();
	if (!count.m_variable.IsEmpty())
		return count.m_variable;
	return wxString::Format(wxT("%d"), count.m_count->GetValue());
}

wxString ibDialogLinqConstructor::IndentAt(unsigned int position) const
{
	// The text in front of `position` on its line, a tab kept a tab and anything else a space.
	const int lineStart = m_text.Left(position).Find(wxT('\n'), /*fromEnd*/ true) + 1;
	wxString indent;
	for (unsigned int i = (unsigned int)lineStart; i < position && i < m_text.length(); ++i)
		indent += m_text[i] == wxT('\t') ? wxT('\t') : wxT(' ');
	return indent;
}

wxString ibDialogLinqConstructor::GetBlock(wxString& refusal) const
{
	return Collect().Render(IndentAt(m_anchor), refusal);
}

bool ibDialogLinqConstructor::GetReplacedSpan(unsigned int& from, unsigned int& to) const
{
	from = m_replaceFrom;
	to = m_replaceTo;
	return m_replacing;
}

wxToolBar* ibDialogLinqConstructor::MakeBar(wxWindow* parent)
{
	wxToolBar* bar = new wxToolBar(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_FLAT | wxTB_HORIZONTAL | wxTB_NODIVIDER);
	// 16x16 - a toolbar above a list must not be the loudest thing on the page.
	bar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
	return bar;
}

void ibDialogLinqConstructor::AddTool(wxToolBar* bar, const wxString& label, const wxString& artId, std::function<void()> action)
{
	// THE PRODUCT'S OWN PICTURES first (wxART_FRONTEND serves ADD / EDIT / DELETE / UP / DOWN), the
	// stock ones after, and the word when there is no picture at all - never an empty square.
	const wxSize size = FromDIP(wxSize(16, 16));
	wxBitmapBundle bitmap = wxArtProvider::GetBitmapBundle(artId, wxART_FRONTEND, size);
	if (!bitmap.IsOk())
		bitmap = wxArtProvider::GetBitmapBundle(artId, wxASCII_STR(wxART_MENU), size);

	const int id = wxWindow::NewControlId();
	if (bitmap.IsOk())
		bar->AddTool(id, label, bitmap, label);
	else {
		bar->SetWindowStyle(bar->GetWindowStyle() | wxTB_TEXT);
		bar->AddTool(id, label, wxBitmapBundle(), label);
	}
	bar->Bind(wxEVT_TOOL, [action](wxCommandEvent&) { action(); }, id);
}

// ---------------------------------------------------------------------------
//  The block, and the compiler's reading of it
// ---------------------------------------------------------------------------

void ibDialogLinqConstructor::ShowResult()
{
	if (m_block == nullptr || m_verdict == nullptr)
		return;

	// While the query groups, the fields tab says what its list has become.
	if (m_fieldsNote != nullptr && m_groupKeys != nullptr) {
		const bool grouped = m_groupKeys->GetItemCount() > 0;
		if (grouped) {
			const wxString group = wxString(m_groupInto->GetValue()).Trim(true).Trim(false);
			m_fieldsNote->SetLabelText(wxString::Format(
				_("The query groups its rows. The answer's columns are the keys and the totals of the Grouping tab, "
				  "then these - written over the group: %s.Key, %s.Values."),
				group.IsEmpty() ? wxString(wxT("grp")) : group, group.IsEmpty() ? wxString(wxT("grp")) : group));
			// Wrapped to the list it stands under - a fixed width ran past a narrow pane and was cut off.
			const int width = m_fields->GetClientSize().x;
			m_fieldsNote->Wrap(width > FromDIP(80) ? width : FromDIP(300));
		}
		m_fieldsNote->Show(grouped);
		m_fieldsNote->GetParent()->Layout();
	}

	// ⭐ THE PREVIEW SHOWS THE BLOCK AS IT STANDS, a wrong part included, and the refusal under it says what
	// is wrong: blank at the first wrong cell, it showed nothing of the rest. OK still writes only a block
	// with nothing refused (OnOk asks GetBlock).
	wxString refusal;
	const wxString block = Collect().RenderDraft(IndentAt(m_anchor), refusal);
	m_block->SetReadOnly(false);
	m_block->SetText(block);
	m_block->SetReadOnly(true);

	if (!refusal.IsEmpty() || block.IsEmpty()) {
		m_verdict->SetForegroundColour(wxColour(0xC0, 0x30, 0x30));
		m_verdict->SetLabelText(refusal);
		return;
	}

	// ⭐ THE COMPILER READS IT WHERE IT WILL STAND - the module with the block written in, over the
	// query it replaces or at the caret - and says what it understood: which names the query binds,
	// where each came from, which columns the answer has. The window keeps no opinion of its own.
	const wxString text = m_text.Left(m_anchor) + block + m_text.Mid(m_replacing ? m_replaceTo : m_anchor);
	std::vector<ibQueryOutline> outlines;
	{
		const ibBackendException::ibEvalModeScope answering(eval_complete);
		try {
			outlines = ibOutlineScriptQueries(text,
				m_module != nullptr ? m_module->GetName() : wxString(wxT("module")),
				m_module != nullptr ? m_module->GetMetaData() : nullptr);
		}
		catch (...) {
		}
	}

	// The query that starts where the block does: the compiler notes a query from where its `From`
	// was looked for, which may be a little before the word itself.
	const ibQueryOutline* read = nullptr;
	for (const ibQueryOutline& outline : outlines) {
		if (outline.m_isRestrict || outline.m_textFrom > m_anchor + 1)
			continue;
		if (outline.m_textTo != 0 && outline.m_textTo <= m_anchor)
			continue;
		if (read == nullptr || outline.m_textFrom > read->m_textFrom)
			read = &outline;
	}

	if (read == nullptr) {
		m_verdict->SetForegroundColour(wxColour(0xC0, 0x30, 0x30));
		m_verdict->SetLabelText(_("The compiler did not read a query at this place - look at the block."));
		return;
	}

	wxString names;
	for (const ibQueryOutlineBinding& binding : read->m_bindings) {
		names += (names.IsEmpty() ? wxString() : wxString(wxT(", "))) + binding.m_name + wxT(" (") + binding.m_origin;
		if (!binding.m_offers.empty())
			names += wxString::Format(_(", %u fields"), (unsigned)binding.m_offers.size());
		names += wxT(")");
	}
	wxString columns;
	for (const wxString& column : read->m_columns)
		columns += (columns.IsEmpty() ? wxString() : wxString(wxT(", "))) + column;

	m_verdict->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
	m_verdict->SetLabelText(wxString::Format(_("The compiler reads it: %s. Columns of the answer: %s."),
		names, columns.IsEmpty() ? wxString(_("the row itself")) : columns));
}

void ibDialogLinqConstructor::OnOk(wxCommandEvent& event)
{
	wxString refusal;
	if (GetBlock(refusal).IsEmpty()) {
		wxMessageBox(refusal, GetTitle(), wxOK | wxICON_WARNING, this);
		return;
	}
	event.Skip();   // the dialog's own OK: EndModal(wxID_OK)
}
