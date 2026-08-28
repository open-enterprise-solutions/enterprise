#include <wx/tokenzr.h>   // an area may name several groupings, comma-separated
#include "frontend/win/dlgs/settings/composer/composerSettings.h"

#include "backend/srcDataObject.h"                 // ibSourceExplorer — the available fields
#include "frontend/win/dlgs/settings/settingsFieldTree.h"      // which fields this composition offers — one answer
#include "frontend/win/dlgs/settings/settingsFilterEditor.h"   // SHARED with the list's world
#include "frontend/win/dlgs/settings/settingsSortEditor.h"     // …and so is this one
#include "frontend/win/dlgs/settings/settingsStyle.h"          // how a settings surface LOOKS — said once, for both worlds
#include "frontend/win/dlgs/settings/savedSettings.h"          // the shelf window — shared with the list's world
#include "backend/settings/settingsComposer.h"                 // saving / restoring a composer's settings
#include "frontend/win/dlgs/callbackDropTarget.h"              // dropping a field onto a list — the same-process drag
#include "frontend/win/dlgs/queryConstructor/queryConstructor.h" // the Query tab's constructor button
#include "frontend/win/dlgs/queryConstructor/queryExpressionDialog.h" // the resource expression editor
#include "frontend/win/dlgs/typeSelector.h"              // the product.s type picker — a parameter declares its type
#include "frontend/win/dlgs/queryConstructor/queryConstructorInternal.h" // ibExpressionCellRenderer — the Totals tab's own cell
#include "backend/query/queryable.h"                        // ibPeriodUnits — the engine's own list of period words
#include "frontend/win/editor/codeEditor/codeEditor.h"  // the script editor behind a parameter expression
#include "frontend/mainFrame/mainFrame.h"                // the shared editor / font-colour settings
#include "frontend/docView/docView.h"                    // ibMetaDocument — the composer editor's document mode
#include "backend/metaCollection/metaComposerObject.h"   // ibValueMetaObjectComposer — what a composer document was opened ON
#include "backend/propertyManager/property/variant/variantComposition.h"  // the SNAPSHOT a Settings property stores
#include "backend/compiler/compileCode.h"                 // the OK-time syntax check: compile, do not run
#include "backend/compiler/compileModule.h"               // ibCompileModule — the parent a check attaches to
#include "backend/moduleManager/moduleManager.h"          // the module manager: common functions + common modules
#include "backend/session/session.h"                      // EditModuleManagerFor — designer or runtime, one seam
#include "backend/appData.h"                              // the ACTIVE configuration, when the composition names none
#include "backend/metaData.h"                             // the compile cache the parent module comes from
#include "backend/metadataConfiguration.h"                // ibMetaDataConfigurationBase — the active configuration IS one
#include "backend/moduleInfo.h"                           // ibRuntimeModuleDataObject::GetCompileModule
#include "backend/backend_exception.h"

#include <algorithm>   // std::min — MSVC drags it in transitively, libstdc++ does not

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/splitter.h>
#include <wx/statbox.h>
#include <wx/toolbar.h>
#include <wx/artprov.h>
#include <wx/menu.h>
#include <wx/choicdlg.h>  // wxGetSingleChoiceIndex — which saved setting to rename / drop
#include <wx/stc/stc.h>   // wxStyledTextCtrl — the query editor
#include <wx/dnd.h>       // wxDropSource / wxTextDataObject — the drag half

#include "frontend/win/dlgs/callbackDropTarget.h"   // the drop half: a drop raises the same verb a button does
#include "frontend/win/dlgs/rowValueCell.h"          // ibRowValueCellRenderer — a field and a kind are VALUES

namespace {
enum {
	ID_QUERY_BUILD = wxID_HIGHEST + 1,
	ID_RESOURCE_ADD,
	ID_RESOURCE_REMOVE,
	ID_RESOURCE_EXPR,
	// ONE VERB ADDS A LEVEL, and the form it opens is where the level is described — its fields, or
	// none of them, which is the detail records. A second command for the empty case would be a
	// second way to make one node.
	ID_LEVEL_ADD,
	// ⭐ AND A SECOND VERB THAT ADDS AN OUTPUT OF THE OTHER SHAPE — a TABLE, which opens with two
	// undeletable nodes, Rows and Columns (Max, 2026-08-25). It is not a second way to make a level:
	// a level is the same thing on either axis, and this says which SHAPE is being started.
	ID_TABLE_ADD,
	ID_LEVEL_REMOVE,
	ID_LEVEL_UP,
	ID_LEVEL_DOWN,
	ID_VARIANT_ADD,
	ID_VARIANT_COPY,
	ID_VARIANT_REMOVE,
	ID_PARAM_ADD,
	ID_PARAM_REMOVE,
	// The FIELDS OF ONE GROUPING — its own verbs, distinct from the ones that add a grouping to the
	// structure: one composes a level, the other adds a level to the report.
	ID_GROUPFIELD_ADD,
	ID_GROUPFIELD_REMOVE,
	ID_GROUPFIELD_UP,
	ID_GROUPFIELD_DOWN,
	// WHAT A NODE SHOWS — one set, so one triple of ids. There were two of everything here while
	// "available" was a page of its own.
	ID_SELECTED_ADD,
	// …AND THE `Auto` ROW, which is added rather than switched on: it is a row of this table like
	// any other, and what it says is WHERE everything the storey above chose lands.
	ID_SELECTED_AUTO,
	ID_SELECTED_REMOVE,
	ID_SELECTED_COPY,
	ID_SELECTED_UP,
	ID_SELECTED_DOWN,
};

// WHICH AGGREGATES FIT THIS FIELD — asked of the ENGINE, never listed here.
//
// `ibQueryLowering::AggregatesFor` is the one door: `CheckNames` reads it as a REFUSAL ("SUM
// cannot be taken over 'Description'") and a window reads it as WHAT TO OFFER. A hand-written
// list beside it drifts, and the drift is a dropdown offering something the query then refuses —
// which is exactly what a fixed { SUM, COUNT, MIN, MAX, AVG } did to every string field.
// WHICH FIELD A ROW STANDS FOR, by index into the panel's list. Carried on the row so the answer
// comes from the field object, never from parsing its label back.
class ibFieldItemData : public wxTreeItemData {
public:
	explicit ibFieldItemData(size_t index) : m_index(index) {}
	size_t GetIndex() const { return m_index; }
private:
	size_t m_index;
};

} // namespace


// THE RESOURCES, as a dataview model: WHAT is folded and HOW. Two columns because they are two
// facts about one line — a field and the aggregate over it — and a single caption ("SUM(Amount)")
// could be read but not sorted, measured or edited a column at a time.
//
// Read straight from the composer, which is the store: the window keeps no copy of what it shows.
class ibResourceModel : public ibDataViewVirtualListModel {
public:
	enum { kColField = 0, kColExpression, kColAlias, kColScope };

	// ⭐⭐ ON THE DESCRIPTION THIS WINDOW IS EDITING — the same road the structure model takes, and
	// asked through a callback so no copy is kept here. A resource is a LINE OF THE DESCRIPTION; it
	// used to be read and written straight into the running composer, which is filled FROM the
	// description at a run — so what this window showed and what was saved were two different lists,
	// and only the composer's survived until the report was closed (Max, 2026-08-24).
	// ⭐ `groupings` — WHAT AN AREA MAY NAME, asked the same way and for the same reason: the window
	// keeps no copy, so adding a grouping is visible here at once. Used to REFUSE a name that names
	// nothing, at the moment it is typed — a report whose resource points at a grouping that is not
	// there comes back empty, and "empty" is the least useful thing a report can say.
	explicit ibResourceModel(std::function<std::vector<ibResourceDescription>*()> resources,
	                         std::function<std::vector<wxString>()> groupings = nullptr)
		: m_resources(std::move(resources)), m_groupings(std::move(groupings)) { ResetFromList(); }

	std::vector<ibResourceDescription>* List() const { return m_resources ? m_resources() : nullptr; }

	void ResetFromList() {
		const std::vector<ibResourceDescription>* list = List();
		Reset(list != nullptr ? (unsigned int)list->size() : 0u);
	}

	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		const std::vector<ibResourceDescription>* list = List();
		if (list == nullptr || row >= list->size())
			return;   // BOUNDS FIRST — a queued paint can outlive the line it was queued for
		const wxString& func = (*list)[row].m_func;
		const wxString& path = (*list)[row].m_path;

		if (col == kColField)
			variant = path;
		else if (col == kColExpression)
			// No function means the text IS the expression — the same rule the renderer follows.
			variant = func.IsEmpty() ? path : func + wxT("(") + path + wxT(")");
		else if (col == kColAlias)
			// ⚠ SHOWN EMPTY WHEN THERE IS NONE, not filled in with the name it would get. A hint in
			// an editable cell becomes a real value on the first click through it — the very trap
			// the level's alias cell documents one file over.
			variant = (*list)[row].m_alias;
		else if (col == kColScope)
			// ⭐⭐ OVER WHICH GROUPING THIS FIGURE IS COMPUTED — the composition's half of the same
			// question the query constructor asks in its own "computed over" column. Empty is the
			// ordinary answer and is shown empty: the area then comes from the ladder, and the figure
			// means one thing on each heading, exactly as every resource did before this existed.
			variant = (*list)[row].m_scope;
	}

	// ⭐ THE EXPRESSION IS EDITED IN THE CELL, the way the query constructor's Totals tab edits its
	// own (Max, 2026-08-19: "look at the query constructor"). What arrives is either one of the ready
	// calls the cell offered — `SUM(Amount)` — or anything a person wrote in the editor behind "...".
	// Both land here as text, and the split is the same one the store already speaks: a FUNC and its
	// argument, or an empty func meaning "the text is the expression".
	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override {
		std::vector<ibResourceDescription>* list = List();
		if (list == nullptr || row >= list->size())
			return false;

		wxString text = variant.GetString();
		text.Trim(true).Trim(false);

		// THE NAME IS TYPED, NOT PARSED — and clearing it is a legitimate edit: the figure goes back
		// to being named after its argument. So an empty cell is stored here, unlike an empty
		// EXPRESSION, which is not a resource at all.
		if (col == kColAlias) {
			(*list)[row].m_alias = text;
			return true;
		}
		// …AND THE AREA IS A NAME TOO — a grouping of this composition, or several separated by
		// commas. Stored as given; whether such a grouping exists is the ENGINE's judgement (the
		// lowering refuses an unknown one by name), so this cell grows no second opinion about it.
		// Clearing it is a legitimate edit: the figure goes back to folding by the ladder.
		if (col == kColScope) {
			// ⭐⭐ AND IT IS CHECKED HERE, AS IT IS TYPED. Empty is legitimate — the figure goes back to
			// folding by the ladder — but a NAME must name a grouping this composition declares.
			// Left unchecked, a misspelt area is discovered by the reader, in front of a report that
			// shows nothing (Max, 2026-08-27: "when I put a wrong name nothing checks it at all").
			if (!text.IsEmpty() && m_groupings) {
				const std::vector<wxString> known = m_groupings();
				wxStringTokenizer names(text, wxT(","));
				while (names.HasMoreTokens()) {
					wxString one = names.GetNextToken();
					one.Trim(true).Trim(false);
					if (one.IsEmpty())
						continue;
					bool found = false;
					for (const wxString& had : known)
						if (had.IsSameAs(one, false)) { found = true; break; }
					if (!found) {
						wxMessageBox(wxString::Format(
							_("\"%s\" is not a grouping of this report: a figure can only be computed over one of its own groupings."),
							one), _("Computed over"), wxOK | wxICON_WARNING);
						return false;   // the cell keeps what it had — nothing silently wrong is stored
					}
				}
			}
			(*list)[row].m_scope = text;
			return true;
		}
		if (col != kColExpression)
			return false;
		if (text.IsEmpty())
			return false;   // a resource with no expression is not a resource — and empty would read as "delete"

		wxString func, path;
		if (!SplitCall(text, func, path)) { func.clear(); path = text; }
		// THE NAME SURVIVES AN EDIT OF THE FIGURE. Rewriting the whole line would drop it, and a
		// person changing SUM to COUNT is not renaming their column.
		(*list)[row].m_func = func;
		(*list)[row].m_path = path;
		return true;
	}

	// `SUM(Amount)` → { SUM, Amount }. FALSE when the text is not one plain call — a formula
	// (`SUM(Qty) / COUNT(Doc)`) is kept whole, because taking it apart here would be a second,
	// worse parser beside the engine's.
	static bool SplitCall(const wxString& text, wxString& func, wxString& arg) {
		const int open = text.Find(wxT('('));
		if (open <= 0 || !text.EndsWith(wxT(")")))
			return false;
		func = text.Left(open);
		func.Trim(true).Trim(false);
		arg = text.Mid(open + 1, text.length() - open - 2);
		arg.Trim(true).Trim(false);
		// A call, and nothing but: a nested parenthesis or an operator after the closing one means
		// the text is an expression that merely starts like a call.
		if (func.IsEmpty() || arg.IsEmpty() || arg.Find(wxT('(')) != wxNOT_FOUND)
			return false;
		for (const wxUniChar ch : func)
			if (!wxIsalpha(ch) && ch != wxT('_'))
				return false;
		return true;
	}

private:
	std::function<std::vector<ibResourceDescription>*()> m_resources;   // asked every time — no copy kept here
	std::function<std::vector<wxString>()>               m_groupings;   // …and what an area may name (see the ctor)
};

// THE ROW A COMMAND ACTS ON. A virtual list keys rows by (index + 1), so nothing selected answers
// wxNOT_FOUND rather than row zero — which would quietly act on the first resource.
static int ibSelectedRow(ibDataViewCtrl* view)
{
	if (view == nullptr)
		return wxNOT_FOUND;
	const ibDataViewItem row = view->GetSelection();
	if (!row.IsOk())
		return wxNOT_FOUND;
	const size_t id = reinterpret_cast<size_t>(row.GetID());
	return id > 0 ? (int)(id - 1) : wxNOT_FOUND;
}


// THE TYPES A DECLARATION HOLDS, as the text a cell shows — the configuration's own names, joined.
// Empty declaration reads as "any": that is what it MEANS (the expression decides), and a blank cell
// would read as "not filled in yet".
wxString ibDescribeTypes(const ibTypeDescription& typeDesc, const ibMetaData* metaData)
{
	if (metaData == nullptr || typeDesc.GetClsidCount() == 0)
		return _("<any>");

	wxString described;
	for (const ibClassID& clsid : typeDesc.GetClsidList()) {
		if (!metaData->IsRegisterCtor(clsid))
			continue;
		if (!described.IsEmpty())
			described += wxT(", ");
		described += metaData->GetNameObjectFromID(clsid);
	}
	return described.IsEmpty() ? _("<any>") : described;
}
// A PLAIN TEXT CELL WITH A "..." — no drop-down.
//
// ⭐ The expression cell used the query constructor's, which is a COMBO: it exists there because a
// totals expression is nearly always one of the ready calls, and the list is the point. A parameter
// expression has no such list — the offered items were just the text already in the cell, so the
// arrow opened a menu of one (Max: "get rid of the combobox there"). What is wanted is the ordinary
// value cell: type in it, or press "..." for room to write.
class ibTextWithDotsRenderer : public ibDataViewValueRenderer, public ibControlFrame {
public:
	using Expand = std::function<bool(wxString& text)>;

	ibTextWithDotsRenderer(wxWindow* host, Expand expand)
		: ibDataViewValueRenderer(nullptr), m_host(host), m_expand(std::move(expand)) {
	}

	virtual bool HasEditorCtrl() const override { return true; }
	bool EditOnSingleClick() const override { return true; }

	virtual wxWindow* CreateEditorCtrl(wxWindow* dv, wxRect labelRect, const wxVariant& value) override {
		m_text = value.GetString();

		ibControlTextEditor* editor = new ibControlTextEditor;
		editor->SetDVCMode(true);
		editor->Show(false);
		if (!editor->Create(dv, wxID_ANY, value, labelRect.GetPosition(), labelRect.GetSize()))
			return nullptr;

		editor->ShowSelectButton(true);    // the "..." — room to write what does not fit
		editor->ShowClearButton(true);
		editor->ShowOpenButton(false);
		editor->SetTextEditMode(true);     // typing straight into the cell is the ordinary case
		editor->Bind(wxEVT_CONTROL_BUTTON_SELECT, &ibTextWithDotsRenderer::OnExpand, this);
		editor->Bind(wxEVT_CONTROL_BUTTON_CLEAR, &ibTextWithDotsRenderer::OnClear, this);
		editor->LayoutControls();
		editor->Show(true);
		return editor;
	}

	// WHAT THE CELL COMMITS is whatever the box holds — typed or written in the dialog. With the box
	// already gone (the dialog's own closing takes the editor with it), what it last held is what
	// this renderer kept, so the written text is not lost with the window that wrote it.
	virtual bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value) override {
		if (ibControlTextEditor* box = dynamic_cast<ibControlTextEditor*>(editor)) {
			m_text = box->GetValue();
			value  = m_text;
			return true;
		}
		value = m_text;
		return true;
	}


	// NO QUICK CHOICE HERE — the cell holds TEXT (an expression, a type description), and the "..."
	// is what opens the real editor for it. Saying so is what keeps the runtime from offering a
	// value picker over a piece of code.
	virtual bool HasQuickChoice() const override { return false; }
	virtual void ChoiceProcessing(ibValue&) override {}
	virtual void ControlIncrRef() override {}
	virtual void ControlDecrRef() override {}

private:
	// ⚠⚠ THE BOX MAY NOT SURVIVE THE DIALOG. `m_expand` opens a MODAL window on top of a live cell
	// editor; the editor loses focus, the grid closes it, and the pointer read before the call is
	// then a dead object — writing the result back through it is a use-after-free (two crash dumps,
	// 2026-08-21, both landing on this line).
	//
	// So the editor is asked for AGAIN afterwards, and the text is kept here as well: the cell
	// commits `m_text` when the editor is already gone, which is what makes the dialog's result
	// survive its own window closing. (The value cell beside this one learnt the same lesson from
	// the other side — it closes the editor itself before opening a picker.)
	void OnExpand(wxCommandEvent&) {
		ibControlTextEditor* box = dynamic_cast<ibControlTextEditor*>(GetEditorCtrl());
		wxString text = box != nullptr ? box->GetValue() : m_text;
		if (!m_expand || !m_expand(text))
			return;

		m_text = text;   // what the cell commits, whether or not the box is still there
		if (ibControlTextEditor* alive = dynamic_cast<ibControlTextEditor*>(GetEditorCtrl()))
			alive->SetValue(text);
	}
	void OnClear(wxCommandEvent&) {
		if (ibControlTextEditor* box = dynamic_cast<ibControlTextEditor*>(GetEditorCtrl()))
			box->SetValue(wxEmptyString);
	}

	wxWindow* m_host;
	Expand    m_expand;
	wxString  m_text;
};
// THE PARAMETERS, as a dataview model over the COMPOSITION — which owns them. Four columns, because
// a parameter answers four separate questions, and folding any two of them into one caption is how
// a settings page ends up unreadable:
//
//   Name        — what the query calls it (`&Period`). Editable only for a hand-made one: an auto
//                 parameter is named by the TEXT, and renaming it here would just be a rename the
//                 next re-parse undoes.
//   Value       — what it holds when no expression is given.
//   Expression  — evaluated BEFORE the read; its result becomes the value. `CurrentDate()`, a call
//                 into a common module — legitimate here precisely because it runs once, not per row.
//   For user    — whether the author fixes it or hands it to the person reading the report.
//
// Read straight from the composition: the window keeps no copy.
class ibParameterModel : public ibDataViewVirtualListModel {
public:
	// Column 0 is reserved by the fork, so these start at 1.
	enum { kColName = 1, kColValue, kColType, kColExpression, kColUser };

	// ⭐⭐ ON THE DESCRIPTION THIS WINDOW IS EDITING, like the resources and the structure. A parameter
	// is a line of it (`ibParameterDescription`); it used to be read and written through the live
	// composition, which held a second vector of its own — so what this window showed was not what
	// was saved (Max, 2026-08-24: "the parameters have the same illness as the resources").
	//
	// The metadata comes along because ONE column needs it: the declared type is rendered by name.
	ibParameterModel(std::function<std::vector<ibParameterDescription>*()> parameters,
	                 std::function<const ibMetaData*()> metaData)
		: m_parameters(std::move(parameters)), m_metaData(std::move(metaData)) { ResetFromList(); }

	std::vector<ibParameterDescription>* List() const { return m_parameters ? m_parameters() : nullptr; }

	void ResetFromList() {
		const std::vector<ibParameterDescription>* list = List();
		Reset(list != nullptr ? (unsigned int)list->size() : 0u);
	}

	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		const std::vector<ibParameterDescription>* list = List();
		if (list == nullptr || row >= list->size())
			return;   // BOUNDS FIRST — a queued paint can outlive the row it was queued for
		const ibParameterDescription& parameter = (*list)[row];
		switch (col) {
		case kColName:
			// AN AUTO PARAMETER SAYS SO. Not decoration: it is the difference between a row that can
			// be renamed or removed here and one that is written in the query text.
			variant = parameter.m_fromQuery
				? parameter.m_name + wxT("  (") + _("from query") + wxT(")")
				: parameter.m_name;
			break;
		case kColValue:      variant = parameter.m_value.GetString(); break;
		case kColType:       variant = ibDescribeTypes(parameter.m_type, m_metaData ? m_metaData() : nullptr); break;
		case kColExpression: variant = parameter.m_expression; break;
		case kColUser:       variant = parameter.m_userSettable; break;
		default: break;
		}
	}

	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override {
		std::vector<ibParameterDescription>* list = List();
		if (list == nullptr || row >= list->size())
			return false;
		ibParameterDescription& parameter = (*list)[row];
		switch (col) {
		case kColValue:      parameter.m_value        = ibValue(variant.GetString()); return true;
		case kColExpression: parameter.m_expression   = variant.GetString();          return true;
		case kColUser:       parameter.m_userSettable = variant.GetBool();            return true;
		default: return false;
		}
	}

	// 🛑 VIEW ONLY, AND THE ONLY DOOR THAT WORKS FOR IT. An ACTIVATABLE cell — the "For user" tick —
	// never sends the start-editing event the panel vetoes: the fork activates it straight from the
	// click and from Space, and the single gate it consults on both roads is the model's IsEnabled.
	// So a tick in a read-only tab wrote the composition until this was added (final audit,
	// 2026-08-20). Nothing else here needs a per-row answer, which is why the row is unused.
	void SetReadOnly(bool readOnly) { m_readOnly = readOnly; }
	virtual bool IsEnabledByRow(unsigned int, unsigned int) const override { return !m_readOnly; }

private:
	std::function<std::vector<ibParameterDescription>*()> m_parameters;   // asked every time — no copy here
	std::function<const ibMetaData*()>                    m_metaData;     // for the type column alone
	bool m_readOnly = false;
};
// THE VARIANTS, as a dataview model over the COMPOSITION — which is where they live. The window
// keeps no list of its own: a variant is a snapshot the composition owns, and a copy of the names
// here would be the second store that drifts the first time one is renamed.
//
// The name is EDITABLE in place: it is the only thing about a variant a person picks it by.
class ibVariantModel : public ibDataViewVirtualListModel {
public:
	enum { kColName = 0 };

	// ⭐⭐ ON THE COPY THIS WINDOW EDITS, like everything else here. The variants used to be read and
	// written through the live composition while the window held its own copy of the description —
	// so adding one wrote it THERE, and the first commit put the copy back and took it away again.
	// The list had already been told there were two rows, and the second had no variant behind it:
	// a row with an empty name that nothing could do anything with (Max, 2026-08-24).
	explicit ibVariantModel(std::function<std::vector<ibVariantDescription>*()> variants)
		: m_variants(std::move(variants)) { ResetFromList(); }

	std::vector<ibVariantDescription>* List() const { return m_variants ? m_variants() : nullptr; }

	void ResetFromList() {
		const std::vector<ibVariantDescription>* list = List();
		Reset(list != nullptr ? (unsigned int)list->size() : 0u);
	}

	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		const std::vector<ibVariantDescription>* list = List();
		if (list == nullptr || col != kColName || row >= list->size())
			return;   // BOUNDS FIRST — a queued paint can outlive the variant it was queued for
		variant = (*list)[row].m_name;
	}
	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override {
		std::vector<ibVariantDescription>* list = List();
		if (list == nullptr || col != kColName || row >= list->size())
			return false;
		const wxString name = variant.GetString();
		if (name.IsEmpty())
			return false;   // a nameless variant is unpickable — the name is how it is chosen
		(*list)[row].m_name = name;
		return true;
	}

private:
	std::function<std::vector<ibVariantDescription>*()> m_variants;   // asked every time — no copy here
};

namespace {
wxArrayString ibAggregatesForField(const ibQueryConstructorField& field)
{
	wxArrayString words;
	for (const ibQueryKeyword keyword : ibQueryLowering::AggregatesFor(field.m_type))
		words.Add(ibQueryKeywordText(keyword));
	return words;
}
} // namespace

// ===========================================================================
//  The OUTPUT STRUCTURE — Report -> level -> level, as a tree over the ladder
// ===========================================================================
//
// ONE ROW OF THE STRUCTURE. A level is identified by its POSITION, because position is what a
// level IS here: the order is the nesting, so moving a level up is not "the same level somewhere
// else", it is a different report. (The filter tree keys its rows by the value object instead —
// there a row genuinely survives being re-parented.)
//
// The rows are POOLED and never dropped: the view may still be painting one when the ladder
// changes underneath it, and a row freed mid-paint is a crash with a stack that blames the paint.
// (ibStructurePos itself lives in the header — the panel keys its per-node buffers by the same
// coordinate the tree names its rows with, and one coordinate written twice is how the two came to
// disagree about what an AXIS row is.)

class ibStructureNode : public ibDataViewObject {
public:
	ibStructureNode(const ibStructurePos& pos, ibStructureNode* parent) : m_pos(pos), m_parent(parent) {}

	const ibStructurePos& GetPos() const { return m_pos; }
	// wxNOT_FOUND on anything that is not a level — a command aimed at a level then does nothing
	// where there is none, instead of acting on level zero.
	int GetLevel() const { return m_pos.IsLevel() ? m_pos.m_level : wxNOT_FOUND; }

	// A LEVEL CONTAINS THE NEXT ONE. Whether there IS a next one follows the ladder's length and
	// is re-stated on every rebuild — the front asks the ROW, not the model, so the row has to
	// carry the answer.
	void SetHasChild(bool hasChild) { m_hasChild = hasChild; }
	virtual bool IsContainer() const override { return m_hasChild; }

	virtual ibDataViewItem GetParentItem() const override {
		return m_parent != nullptr ? ibDataViewItem(m_parent) : ibDataViewItem();
	}

private:
	ibStructurePos   m_pos;
	ibStructureNode* m_parent;    // owned by the model, outlives this row
	bool             m_hasChild = false;
};

class ibComposerStructureModel : public ibDataViewModel {
public:
	// COLUMN 0 IS RESERVED by the fork (it paints blank and does not edit), so these start at 1.
	//
	// ⭐ THREE COLUMNS, NOT TWO, AND THE REASON IS THE EXPANDER. The tree hangs off the FIRST
	// column, and the grid refuses to start an edit in the expander's column — a click there
	// belongs to open/close. Editing the field in that same column is therefore impossible (the
	// filter tab learnt this when its "Left value" column carried the tree). So the first column
	// answers WHAT THIS NODE IS — Report / Grouping — and the field and the kind get columns of
	// their own. It is not a duplicate caption: when tables and columns arrive, this is where
	// Table / Rows / Columns will read, and the field column stays exactly what it is.
	enum { kColNode = 1, kColField, kColKind };

	explicit ibComposerStructureModel(std::function<std::vector<ibOutputDescription>*()> outputs)
		: m_outputs(std::move(outputs)) {
	}

	// THE AXIS A ROW READS — its levels, or null where the coordinate points at nothing. Every
	// reader goes through here, so "is there such an output / such an axis" is answered once.
	const std::vector<ibLevelDescription>* AxisOf(const ibStructurePos& pos) const {
		std::vector<ibOutputDescription>* outputs = m_outputs ? m_outputs() : nullptr;
		if (outputs == nullptr || pos.m_output < 0 || (size_t)pos.m_output >= outputs->size())
			return nullptr;
		const ibOutputDescription& output = (*outputs)[pos.m_output];
		if (pos.m_axis == 1) return &output.m_columnGroups;
		if (pos.m_axis == 0) return &output.m_rowGroups;
		return nullptr;
	}

	size_t OutputCount() const {
		std::vector<ibOutputDescription>* outputs = m_outputs ? m_outputs() : nullptr;
		return outputs != nullptr ? outputs->size() : 0u;
	}

	size_t LevelCount(const ibStructurePos& pos) const {
		const std::vector<ibLevelDescription>* axis = AxisOf(pos);
		return axis != nullptr ? axis->size() : 0u;
	}

	// A CROSS-TABLE SHOWS ITS TWO AXES as rows of their own; a plain grouping shows its levels
	// straight under the output, because naming an axis that has no counterpart says nothing.
	//
	// 🛑 IT ASKED THE CONTENT — `LevelCount(columns) > 0` — and that answered "has a column heading
	// been added yet", which is a different question. A table is added EMPTY and its two nodes are
	// undeletable (Max, 2026-08-25), so the axes have to be there before anything is in them: asked
	// of the content, a fresh table showed as a plain grouping and there was nowhere to add the
	// first column heading. The kind is what somebody decided; it is stored, and this reads it.
	bool HasTwoAxes(int output) const {
		std::vector<ibOutputDescription>* outputs = m_outputs ? m_outputs() : nullptr;
		if (outputs == nullptr || output < 0 || (size_t)output >= outputs->size())
			return false;
		return (*outputs)[output].m_kind == ibCompositionOutputKind::Table;
	}

	// RE-READ THE STRUCTURE. Rows are pooled by coordinate, so they survive a rebuild; what changes
	// is how many the walk hands out and which of them still has a child.
	void Rebuild() {
		for (auto& entry : m_nodes)
			entry.second->SetHasChild(HasChildren(entry.first));
		Cleared();
	}

	ibDataViewItem RootItem() const { return ibDataViewItem(NodeFor(ibStructurePos(), nullptr)); }

	// The row for an output — the tree opens them, and a caller adding one selects it.
	ibDataViewItem ItemForOutput(int output) const {
		if (output < 0 || (size_t)output >= OutputCount())
			return ibDataViewItem();
		ibStructurePos pos; pos.m_output = output;
		return ibDataViewItem(NodeFor(pos, nullptr));
	}

	// THE ROW FOR ONE NODE, by its coordinate — what a caller that just added, moved or removed
	// something wants to put the cursor on. An axis or an output is named the same way, with the
	// parts it does not have left at -1.
	ibDataViewItem ItemForNode(int output, int axis, int level) const {
		if (output < 0 || (size_t)output >= OutputCount())
			return ibDataViewItem();
		ibStructurePos pos; pos.m_output = output; pos.m_axis = axis; pos.m_level = level;
		if (pos.IsLevel() && (size_t)level >= LevelCount(pos))
			return ibDataViewItem();
		return ibDataViewItem(NodeFor(pos, nullptr));
	}

	// The row for a level of the FIRST output's rows — what a caller that speaks the old flat
	// ladder means by "level N".
	ibDataViewItem ItemForLevel(int level) const {
		ibStructurePos pos; pos.m_output = 0; pos.m_axis = 0; pos.m_level = level;
		if (level < 0 || (size_t)level >= LevelCount(pos))
			return ibDataViewItem();
		return ibDataViewItem(NodeFor(pos, nullptr));
	}
	// THE LEVEL A ROW STANDS FOR, or wxNOT_FOUND on anything that is not one.
	int LevelAt(const ibDataViewItem& item) const {
		const ibStructureNode* node = static_cast<const ibStructureNode*>(item.GetID());
		return node != nullptr ? node->GetLevel() : wxNOT_FOUND;
	}
	// WHERE A ROW POINTS, whole — what the panel needs to edit the thing that was selected.
	ibStructurePos PosAt(const ibDataViewItem& item) const {
		const ibStructureNode* node = static_cast<const ibStructureNode*>(item.GetID());
		return node != nullptr ? node->GetPos() : ibStructurePos();
	}

	// ---- ibDataViewModel ----
	void GetValue(wxVariant& variant, const ibDataViewItem& item, unsigned int col) const override {
		const ibStructureNode* node = static_cast<const ibStructureNode*>(item.GetID());
		if (node == nullptr)
			return;
		const ibStructurePos pos = node->GetPos();

		if (pos.IsReport()) {
			// THE ROOT SAYS WHAT IT IS. Everything below it is an output of this one report.
			if (col == kColNode)
				variant = _("Report");
			return;
		}

		if (pos.IsOutput()) {
			if (col != kColNode)
				return;
			// WHAT THIS OUTPUT IS — read off its own fields, exactly as the engine reads it: a
			// column axis makes it a table. Its NAME is shown beside that when it has one, since
			// that is how a query package addresses it (ONTO).
			std::vector<ibOutputDescription>* outputs = m_outputs ? m_outputs() : nullptr;
			if (outputs == nullptr || (size_t)pos.m_output >= outputs->size())
				return;
			const ibOutputDescription& output = (*outputs)[pos.m_output];
			// ⚠ NOT "Grouping" — that is what its LEVELS are called, and two rows reading the same
			// word one under another is how a tree stops saying anything (Max, on the first run).
			// An output is a place: what it holds is its levels, what it is called is its own.
			const wxString kind = HasTwoAxes(pos.m_output) ? _("Table") : _("Output");
			// ⚠ ASCII IN THE SEPARATOR, deliberately. This file has no BOM, so MSVC reads a UTF-8
			// em-dash in a literal as ANSI bytes and the row came out with mojibake between the two
			// names, `Output ??? Output2` (seen
			// live once outputs started carrying names, 2026-08-27). Anything a person must READ goes
			// through _() and the message catalogue; punctuation written in code stays ASCII.
			variant = output.m_name.IsEmpty() ? kind : kind + wxT(" - ") + output.m_name;
			return;
		}

		if (pos.IsAxis()) {
			if (col == kColNode)
				variant = pos.m_axis == 1 ? _("Columns") : _("Rows");
			return;
		}

		const std::vector<ibLevelDescription>* axis = AxisOf(pos);
		if (axis == nullptr || (size_t)pos.m_level >= axis->size())
			return;   // BOUNDS FIRST — a queued paint can outlive the level it was queued for
		const ibLevelDescription& level = (*axis)[pos.m_level];

		if (col == kColNode) {
			// WHAT THIS LEVEL IS — asked of the level, not read off its emptiness. The rows are a
			// level of their own kind, and the row says so where a person looks for it.
			variant = level.IsDetailRecords() ? _("Detail records") : _("Grouping");
			return;
		}
		if (col == kColField) {
			// EVERY FIELD OF THE LEVEL, in order — a level groups by all of them together, so
			// showing only the first would describe a different report. The detail level has none
			// to show: what it prints is the rows, and the fields cell has nothing to add.
			if (level.m_settings.m_group.m_lines.empty()) {
				variant = wxString();   // the records name no field — the Structure cell above already says so
				return;
			}
			wxString fields;
			for (const auto& field : level.m_settings.m_group.m_lines) {
				if (!fields.IsEmpty()) fields += wxT(", ");
				fields += field.m_path;
			}
			variant = fields;
			return;
		}
		// (No kind here — it belongs to each FIELD, and they are shown on the Grouping page.)
	}

	// WRITTEN BY THE CELL, not through here: both columns are VALUES, and the cell hands the
	// chosen value straight to the ladder (a field through the picker, a kind through the quick
	// choice). Text arriving here would only be a string that looks like one of them.
	bool SetValue(const wxVariant&, const ibDataViewItem&, unsigned int) override { return false; }

	ibDataViewItem GetParent(const ibDataViewItem& item) const override {
		const ibStructureNode* node = static_cast<const ibStructureNode*>(item.GetID());
		return node != nullptr ? node->GetParentItem() : ibDataViewItem();
	}

	bool IsContainer(const ibDataViewItem& item) const override {
		// The invisible root is a container, or the first fetch never happens.
		if (!item.IsOk())
			return true;
		const ibStructureNode* node = static_cast<const ibStructureNode*>(item.GetID());
		return node != nullptr && node->IsContainer();
	}

	// THE REPORT NODE HAS NO KIND — it is not a grouping, and drawing an empty editable cell
	// beside it would invite a choice that means nothing.
	// ONLY A LEVEL HAS A FIELD AND A KIND. The report, an output and an axis are places, not
	// settings — drawing empty editable cells beside them would invite a choice that means nothing.
	bool HasValue(const ibDataViewItem& item, unsigned col) const override {
		const ibStructureNode* node = static_cast<const ibStructureNode*>(item.GetID());
		if (node == nullptr)
			return true;
		if (!node->GetPos().IsLevel())
			return col == kColNode;
		return true;
	}

	// ...AND THEY ARE NOT EDITED. Their captions say what the report and its outputs ARE, which is
	// read off the structure, not typed over it.
	bool IsEnabled(const ibDataViewItem& item, unsigned int) const override {
		const ibStructureNode* node = static_cast<const ibStructureNode*>(item.GetID());
		return node == nullptr || node->GetPos().IsLevel();
	}

	unsigned int GetFirstFetch(const ibDataViewItem& parent, const ibDataViewItem& /*anchor*/,
		int /*count*/, ibDataViewItemArray& out) const override {
		// THE REPORT NODE IS THE ONLY TOP-LEVEL ROW; a level hangs under the level before it, so
		// the ladder reads on screen as the nesting it actually is.
		if (!parent.IsOk()) {
			out.Add(RootItem());
			return 1;
		}
		ibStructureNode* node = static_cast<ibStructureNode*>(parent.GetID());
		if (node == nullptr)
			return 0;
		const ibStructurePos pos = node->GetPos();

		// THE REPORT'S CHILDREN ARE ITS OUTPUTS — as many as were declared, printed in this order.
		if (pos.IsReport()) {
			// ⚠ AN OUTPUT WITH NOTHING IN IT IS NOT SHOWN. A composition always HAS one — that is
			// what makes "no output at all" impossible downstream — but an empty row under the
			// report says nothing to the person looking at it, and a report that was never
			// configured should read as empty, because it is.
			unsigned int shown = 0;
			const size_t count = OutputCount();
			for (size_t i = 0; i < count; ++i) {
				ibStructurePos child; child.m_output = (int)i;
				if (!HasChildren(child))
					continue;
				out.Add(ibDataViewItem(NodeFor(child, node)));
				++shown;
			}
			return shown;
		}

		// AN OUTPUT SHOWS ITS AXES only when it HAS two — a cross-table. A plain grouping hangs its
		// levels straight underneath, because a row called "Rows" with no "Columns" beside it is a
		// heading that answers a question nobody asked.
		if (pos.IsOutput()) {
			if (HasTwoAxes(pos.m_output)) {
				for (int axis = 0; axis <= 1; ++axis) {
					ibStructurePos child; child.m_output = pos.m_output; child.m_axis = axis;
					out.Add(ibDataViewItem(NodeFor(child, node)));
				}
				return 2;
			}
			ibStructurePos first; first.m_output = pos.m_output; first.m_axis = 0; first.m_level = 0;
			if (LevelCount(first) == 0)
				return 0;
			out.Add(ibDataViewItem(NodeFor(first, node)));
			return 1;
		}

		// AN AXIS OPENS ON ITS FIRST LEVEL; a level contains the next one, so the ladder reads on
		// screen as the nesting it actually is.
		ibStructurePos child = pos;
		child.m_level = pos.IsAxis() ? 0 : pos.m_level + 1;
		if ((size_t)child.m_level >= LevelCount(child))
			return 0;
		out.Add(ibDataViewItem(NodeFor(child, node)));
		return 1;
	}

private:
	// Does this row have anything under it? Asked on every rebuild, because the front asks the ROW
	// and the row has to carry the answer.
	bool HasChildren(const ibStructurePos& pos) const {
		if (pos.IsReport()) {
			// …and it has children only if one of its outputs has something in it — see the fetch.
			for (size_t i = 0; i < OutputCount(); ++i) {
				ibStructurePos output; output.m_output = (int)i;
				if (HasChildren(output))
					return true;
			}
			return false;
		}
		if (pos.IsOutput()) {
			if (HasTwoAxes(pos.m_output))
				return true;
			ibStructurePos rows; rows.m_output = pos.m_output; rows.m_axis = 0;
			return LevelCount(rows) > 0;
		}
		ibStructurePos next = pos;
		next.m_level = pos.IsAxis() ? 0 : pos.m_level + 1;
		return (size_t)next.m_level < LevelCount(next);
	}

	// The row for a coordinate, created on the way down so every row has its parent. Pooled BY
	// COORDINATE: the tree hands out pointers and keeps them, so the same place must always be the
	// same row — rebuilding into fresh objects is how a selection ends up pointing at nothing.
	// THE PARENT OF A COORDINATE, worked out rather than passed in: a level hangs under the level
	// before it (or under its axis / its output), an axis under its output, an output under the
	// report. Without this a row created on demand — the freshly added level, asked for so the tree
	// can open on it — had NO parent, and a row with no parent cannot be expanded to: the tree
	// stayed shut over a level that was really there.
	ibStructureNode* ParentFor(const ibStructurePos& pos) const {
		if (pos.IsReport())
			return nullptr;
		if (pos.IsOutput())
			return NodeFor(ibStructurePos(), nullptr);          // the report
		ibStructurePos up = pos;
		if (pos.IsAxis()) {
			up.m_axis = -1;                                     // its output
		}
		else if (pos.m_level == 0) {
			up.m_level = -1;
			if (!HasTwoAxes(pos.m_output))
				up.m_axis = -1;                                 // a plain grouping hangs off the output
		}
		else {
			up.m_level = pos.m_level - 1;                       // the level above it
		}
		return NodeFor(up, nullptr);
	}

	ibStructureNode* NodeFor(const ibStructurePos& pos, ibStructureNode* parent) const {
		const auto found = m_nodes.find(pos);
		if (found != m_nodes.end())
			return found->second.get();
		if (parent == nullptr)
			parent = ParentFor(pos);
		wxObjectDataPtr<ibStructureNode> node(new ibStructureNode(pos, parent));
		node->SetHasChild(HasChildren(pos));
		ibStructureNode* raw = node.get();
		m_nodes[pos] = node;
		return raw;
	}

	std::function<std::vector<ibOutputDescription>*()> m_outputs;   // asked every time — no copy kept here
	mutable std::map<ibStructurePos, wxObjectDataPtr<ibStructureNode>> m_nodes;
};

// ---------------------------------------------------------------------------
// THE GROUPING FIELDS OF ONE LEVEL — "a grouping may be made of several elements" (Max).
//
// A flat list, because that is what a level's key IS: the fields it groups by TOGETHER, in order,
// each with its own unfold. The order matters to the reader (it is how the heading reads) and not
// to the engine, which groups by the tuple either way.
//
// It edits the level the structure tree has selected, asked for through a callback rather than
// held: the selection moves, and a page holding a pointer would go on editing the level that was
// selected when it was built.
// ---------------------------------------------------------------------------
// A PLAIN LIST OF PATHS, as a model — what the available-fields page shows. One column, no
// children, rows pooled by position; the list itself is asked for through a callback, so the page
// follows the selection instead of holding whichever node was selected when it was built.
// ⭐ THE SELECTED-FIELDS TABLE, and a row of it is not a string any more: it is a FIELD or the
// `Auto` row that says where everything the storey above chose lands (2026-08-28). Named for what
// it lists rather than for the C++ type it used to hold.
class ibSelectedListModel : public ibDataViewModel {
public:
	enum { kColText = 1 };

	explicit ibSelectedListModel(std::function<std::vector<ibSelectedFieldDescription>*()> list)
		: m_list(std::move(list)) {}

	std::vector<ibSelectedFieldDescription>* List() const { return m_list ? m_list() : nullptr; }
	size_t Count() const { const std::vector<ibSelectedFieldDescription>* list = List(); return list != nullptr ? list->size() : 0u; }
	void Rebuild() { Cleared(); }

	int RowAt(const ibDataViewItem& item) const;
	ibDataViewItem ItemForRow(size_t row) const;

	void GetValue(wxVariant& variant, const ibDataViewItem& item, unsigned int col) const override;
	bool SetValue(const wxVariant&, const ibDataViewItem&, unsigned int) override { return false; }
	ibDataViewItem GetParent(const ibDataViewItem&) const override { return ibDataViewItem(); }
	bool IsContainer(const ibDataViewItem& item) const override { return !item.IsOk(); }
	unsigned int GetFirstFetch(const ibDataViewItem& parent, const ibDataViewItem&,
		int, ibDataViewItemArray& out) const override;

private:
	class Row;
	Row* RowFor(size_t row) const;

	std::function<std::vector<ibSelectedFieldDescription>*()> m_list;
	mutable std::vector<wxObjectDataPtr<class ibSelectedListModel::Row>> m_rows;
};

class ibSelectedListModel::Row : public ibDataViewObject {
public:
	explicit Row(size_t row) : m_row(row) {}
	size_t GetRow() const { return m_row; }
	virtual bool IsContainer() const override { return false; }
	virtual ibDataViewItem GetParentItem() const override { return ibDataViewItem(); }
private:
	size_t m_row;
};

ibSelectedListModel::Row* ibSelectedListModel::RowFor(size_t row) const
{
	while (m_rows.size() <= row)
		m_rows.push_back(wxObjectDataPtr<Row>(new Row(m_rows.size())));
	return m_rows[row].get();
}

int ibSelectedListModel::RowAt(const ibDataViewItem& item) const
{
	const Row* row = static_cast<const Row*>(item.GetID());
	return row != nullptr ? (int)row->GetRow() : wxNOT_FOUND;
}

ibDataViewItem ibSelectedListModel::ItemForRow(size_t row) const
{
	return row < Count() ? ibDataViewItem(RowFor(row)) : ibDataViewItem();
}

void ibSelectedListModel::GetValue(wxVariant& variant, const ibDataViewItem& item, unsigned int col) const
{
	const std::vector<ibSelectedFieldDescription>* list = List();
	const int row = RowAt(item);
	if (list == nullptr || row == wxNOT_FOUND || (size_t)row >= list->size() || col != kColText)
		return;
	// ⭐ THE `Auto` ROW READS AS WHAT IT IS. It names no field — it says "everything the storey above
	// chose, here" — so it is drawn as the word rather than as a blank line, which is what an empty
	// path would look like and is a different thing entirely.
	variant = (*list)[row].IsAuto() ? _("<Auto>") : (*list)[row].m_path;
}

unsigned int ibSelectedListModel::GetFirstFetch(const ibDataViewItem& parent, const ibDataViewItem&,
	int, ibDataViewItemArray& out) const
{
	if (parent.IsOk())
		return 0;
	const size_t count = Count();
	for (size_t i = 0; i < count; ++i)
		out.Add(ibDataViewItem(RowFor(i)));
	return (unsigned int)count;
}

// ONE ROW of that list — its position, and nothing else. A grouping field has no children and no
// identity beyond where it sits, so the row is the position.
class ibGroupingFieldRow : public ibDataViewObject {
public:
	explicit ibGroupingFieldRow(size_t row) : m_row(row) {}
	size_t GetRow() const { return m_row; }
	virtual bool IsContainer() const override { return false; }
	virtual ibDataViewItem GetParentItem() const override { return ibDataViewItem(); }
private:
	size_t m_row;
};

class ibGroupingFieldsModel : public ibDataViewModel {
public:
	enum { kColField = 1, kColKind };

	explicit ibGroupingFieldsModel(std::function<ibLevelDescription*()> level)
		: m_level(std::move(level)) {
	}

	ibLevelDescription* Level() const { return m_level ? m_level() : nullptr; }
	size_t FieldCount() const {
		const ibLevelDescription* level = Level();
		return level != nullptr ? level->m_settings.m_group.m_lines.size() : 0u;
	}

	void Rebuild() { Cleared(); }

	ibDataViewItem ItemForRow(size_t row) const {
		return row < FieldCount() ? ibDataViewItem(RowFor(row)) : ibDataViewItem();
	}
	int RowAt(const ibDataViewItem& item) const {
		const ibGroupingFieldRow* row = static_cast<const ibGroupingFieldRow*>(item.GetID());
		return row != nullptr ? (int)row->GetRow() : wxNOT_FOUND;
	}

	void GetValue(wxVariant& variant, const ibDataViewItem& item, unsigned int col) const override {
		const ibLevelDescription* level = Level();
		const int row = RowAt(item);
		if (level == nullptr || row == wxNOT_FOUND || (size_t)row >= level->m_settings.m_group.m_lines.size())
			return;   // BOUNDS FIRST — a queued paint can outlive the level it was queued for
		if (col == kColField)
			variant = level->m_settings.m_group.m_lines[row].m_path;
		else if (col == kColKind)
			variant = ibValue::CreateEnumObject<ibValueEnumGroupKind>(level->m_settings.m_group.m_lines[row].m_kind).GetString();
	}

	// Written through the CELLS, which hand over values (a field through the picker, a kind through
	// the runtime's quick choice) — text arriving here would be a string that looks like one.
	bool SetValue(const wxVariant&, const ibDataViewItem&, unsigned int) override { return false; }

	ibDataViewItem GetParent(const ibDataViewItem&) const override { return ibDataViewItem(); }
	bool IsContainer(const ibDataViewItem& item) const override { return !item.IsOk(); }

	unsigned int GetFirstFetch(const ibDataViewItem& parent, const ibDataViewItem& /*anchor*/,
		int /*count*/, ibDataViewItemArray& out) const override {
		if (parent.IsOk())
			return 0;
		const size_t count = FieldCount();
		for (size_t i = 0; i < count; ++i)
			out.Add(ibDataViewItem(RowFor(i)));
		return (unsigned int)count;
	}

private:
	// Rows pooled by position — the view keeps the pointers it was handed, so the same position has
	// to stay the same row across a rebuild.
	ibGroupingFieldRow* RowFor(size_t row) const {
		while (m_rows.size() <= row)
			m_rows.push_back(wxObjectDataPtr<ibGroupingFieldRow>(new ibGroupingFieldRow(m_rows.size())));
		return m_rows[row].get();
	}

	std::function<ibLevelDescription*()> m_level;
	mutable std::vector<wxObjectDataPtr<ibGroupingFieldRow>> m_rows;
};

// The art and the command-append helper are NOT spelled here: they live in settingsStyle.h, which
// this folder was split around. Two byte-identical copies stood here under composer-flavoured
// names, which is exactly the drift the shared header exists to prevent.

// THE PICKER IS THE PANEL'S. Which fields this composition has is one question with one answer,
// and the panel below already builds it — so the structure's cells and its "Add grouping" button
// open that very tree instead of a second one assembled up here.
static ibRowValueCellRenderer::FieldChooser ibComposerFieldChooser(ibComposerSettingsPanel* panel)
{
	return [panel](wxWindow* parent, const wxString& held) -> ibValueCompositionField* {
		return panel != nullptr ? panel->ChooseStructureField(parent, held) : nullptr;
	};
}

// ⭐⭐ ADDING A GROUPING IS A FORM, NOT A FIELD PICKER (Max, 2026-08-21: adding one must open a form
// that composes the grouping, with the option of an EMPTY grouping — the detail records: a node
// with no group, but a node all the same).
//
// A grouping is not one field: it is a LIST of them, welded into one heading, each with its own
// unfold. A picker that asks for one field can only ever make a one-field level, and the rest had
// to be added afterwards on another tab — so the act of adding said less than the thing being
// added. This form is that list, at the moment the level is made.
//
// AND THE EMPTY LIST IS AN ANSWER. A node with no fields is the DETAIL RECORDS — a node all the
// same, with its own filter, sort and selected fields — so "OK with nothing in the list" is not a
// cancelled dialog: it is the level that prints the rows.
class ibComposerGroupingDialog : public wxDialog
{
public:
	ibComposerGroupingDialog(ibComposerSettingsPanel* panel, const ibLevelDescription& seed)
		: wxDialog(panel, wxID_ANY, _("Grouping"), wxDefaultPosition, wxDefaultSize,
			wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
		  m_panel(panel), m_node(seed)
	{
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

		wxToolBar* bar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
			wxTB_FLAT | wxTB_NODIVIDER | wxTB_HORIZONTAL);
		bar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
		// ⚠ ASCII ONLY IN UI LITERALS — this file has no BOM, so MSVC reads it as ANSI.
		bar->AddTool(wxID_ADD, _("Add field"), ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("Add field"));
		bar->AddTool(wxID_DELETE, _("Delete"), ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete"));
		bar->AddSeparator();
		bar->AddTool(wxID_UP, _("Move up"), ibSettingsArt(wxASCII_STR(wxART_GO_UP), this), _("Move up"));
		bar->AddTool(wxID_DOWN, _("Move down"), ibSettingsArt(wxASCII_STR(wxART_GO_DOWN), this), _("Move down"));
		bar->Realize();
		sizer->Add(bar, 0, wxEXPAND | wxALL, FromDIP(4));

		m_view = new ibDataViewCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(430, 220)),
			wxDV_ROW_LINES | wxDV_SINGLE);
		ibStyleSettingsGrid(m_view);
		// THE SAME MODEL THE GROUPING TAB USES, over the node being MADE rather than the one
		// selected. It asks for its level through a callback, so handing it another level is the
		// whole of what this dialog had to do differently.
		m_model = new ibGroupingFieldsModel([this]() -> ibLevelDescription* { return &m_node; });
		m_view->AssociateModel(m_model);

		m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Field"),
			new ibRowValueCellRenderer(this, ibComposerFieldChooser(m_panel),
				[this](const ibDataViewItem& row) -> ibValue {
					const int at = m_model->RowAt(row);
					return (at != wxNOT_FOUND && (size_t)at < m_node.m_settings.m_group.m_lines.size())
						? ibValue(new ibValueCompositionField(m_node.m_settings.m_group.m_lines[at].m_path)) : ibValue();
				},
				[this](const ibDataViewItem& row, const ibValue& value) {
					const int at = m_model->RowAt(row);
					if (at == wxNOT_FOUND || (size_t)at >= m_node.m_settings.m_group.m_lines.size())
						return;
					ibValueCompositionField* field = nullptr;
					if (value.ConvertToValue(field) && field != nullptr)
						m_node.m_settings.m_group.m_lines[at].m_path = field->GetPath();
					else
						m_node.m_settings.m_group.m_lines.erase(m_node.m_settings.m_group.m_lines.begin() + at);   // cleared = out of the key
					Refresh();
				}),
			ibGroupingFieldsModel::kColField, FromDIP(240), wxAlignment::wxALIGN_LEFT));

		// THE UNFOLD BELONGS TO THE FIELD — same rule as on the tab, and the same refusal when a
		// level of several fields is asked to walk one field's parent chain.
		m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Kind"),
			new ibRowValueCellRenderer(this, ibComposerFieldChooser(m_panel),
				[this](const ibDataViewItem& row) -> ibValue {
					const int at = m_model->RowAt(row);
					return (at != wxNOT_FOUND && (size_t)at < m_node.m_settings.m_group.m_lines.size())
						? ibValue::CreateEnumObject<ibValueEnumGroupKind>(m_node.m_settings.m_group.m_lines[at].m_kind) : ibValue();
				},
				[this](const ibDataViewItem& row, const ibValue& value) {
					const int at = m_model->RowAt(row);
					if (at == wxNOT_FOUND || (size_t)at >= m_node.m_settings.m_group.m_lines.size())
						return;
					const ibQueryDimUnfold kind = value.ConvertToEnumValue<ibQueryDimUnfold>();
					if (kind != ibQueryDimUnfold::Elements && m_node.m_settings.m_group.m_lines.size() > 1) {
						wxMessageBox(_("This grouping is made of several fields, and a hierarchy unfolds "
						               "one field's parent chain.\n\nGive the hierarchy field a grouping "
						               "of its own."),
							GetTitle(), wxOK | wxICON_WARNING, this);
						return;
					}
					m_node.m_settings.m_group.m_lines[at].m_kind = kind;
					Refresh();
				}),
			ibGroupingFieldsModel::kColKind, FromDIP(150), wxAlignment::wxALIGN_LEFT));

		sizer->Add(m_view, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));

		// ⭐⭐ BY PERIODS — the strip the query constructor's Totals tab already has, on the form that
		// makes a level here. Same three parts (`PERIODS(unit, from, to)`), same gate: it is HIDDEN,
		// not greyed, when the selected line is not a date (Max, 2026-08-25: "if the row is not a
		// date, the whole strip is not shown at all"). Greying it would say "there is something here
		// for you, but not now", which for a field that can never be a period is not true.
		m_periodPane = new wxPanel(this, wxID_ANY);
		{
			wxBoxSizer* strip = new wxBoxSizer(wxHORIZONTAL);
			m_byPeriods = new wxCheckBox(m_periodPane, wxID_ANY, _("By periods"));
			strip->Add(m_byPeriods, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

			// THE UNITS ARE THE ENGINE'S OWN LIST — `ibPeriodUnits()` (query/queryable.h), the same
			// vocabulary the lowering reads and the query constructor offers. A list typed out here
			// would be a second copy of the words, and it would be right until one of them changed.
			m_periodUnit = new wxChoice(m_periodPane, wxID_ANY);
			for (const std::pair<ibTotalsPeriod, wxString>& unit : ibPeriodUnits())
				m_periodUnit->Append(unit.second);
			strip->Add(m_periodUnit, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

			// THE BOUNDS ARE OPTIONAL and are typed as they are meant: `&From` — a parameter — or a
			// literal. Left empty they mean "from the data", which is what the renderer writes back.
			//
			// 🛑 wxTE_PROCESS_ENTER IS NOT DECORATION — it is what makes Enter reach this control at
			// all, and wx checks it: binding wxEVT_TEXT_ENTER to a field without the style trips an
			// assert inside wxTextCtrlBase::OnDynamicBind, which in a Debug build is an int 3 in the
			// dialog's CONSTRUCTOR. Dropped on the way over from the query constructor's strip (which
			// has it), the whole "Add grouping" command died with the window (dump 2026-08-25 22:18).
			const auto addBound = [&](const wxString& label, wxTextCtrl*& field) {
				strip->Add(new wxStaticText(m_periodPane, wxID_ANY, label), 0,
					wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
				field = new wxTextCtrl(m_periodPane, wxID_ANY, wxEmptyString,
					wxDefaultPosition, FromDIP(wxSize(90, -1)), wxTE_PROCESS_ENTER);
				field->SetToolTip(_("A parameter (&From) or a literal date. Empty = from the data.\n"
				                    "A bound does not filter rows - it says which periods to show."));
				strip->Add(field, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
			};
			addBound(_("From:"), m_periodFrom);
			addBound(_("To:"), m_periodTo);

			m_periodPane->SetSizer(strip);
		}
		sizer->Add(m_periodPane, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(4));
		m_periodPane->Hide();

		m_byPeriods->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { ApplyPeriods(); });
		m_periodUnit->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { ApplyPeriods(); });
		// ⚠ COMMITTED ON LEAVING THE FIELD, not on every keystroke: a bound is half-typed most of the
		// time it is being typed. (`ChangeValue` in FillPeriods raises no event, so filling the strip
		// never reads back as an edit — the fault that made the constructor's strip fight the user.)
		for (wxTextCtrl* bound : { m_periodFrom, m_periodTo }) {
			bound->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { ApplyPeriods(); e.Skip(); });
			bound->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { ApplyPeriods(); });
		}
		m_view->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](ibDataViewEvent&) { FillPeriods(); });

		// WHAT AN EMPTY LIST MEANS, said where the list is empty. Not a warning — it is one of the
		// two things this form makes, and a person should not have to know it in advance.
		m_hint = new wxStaticText(this, wxID_ANY, wxEmptyString);
		sizer->Add(m_hint, 0, wxEXPAND | wxALL, FromDIP(6));

		sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(6));
		SetSizerAndFit(sizer);

		Bind(wxEVT_TOOL, [this](wxCommandEvent&) { AddField(); }, wxID_ADD);
		Bind(wxEVT_TOOL, [this](wxCommandEvent&) { RemoveField(); }, wxID_DELETE);
		Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveField(-1); }, wxID_UP);
		Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveField(+1); }, wxID_DOWN);
		Refresh();
	}

	// WHAT WAS MADE — a grouping when fields were chosen, the detail records when none were. The
	// KIND is decided here, once, so nothing downstream has to read it off the emptiness.
	ibLevelDescription Node() const
	{
		ibLevelDescription node = m_node;
		node.m_kind = node.m_settings.m_group.m_lines.empty()
			? ibCompositionLevelKind::Details : ibCompositionLevelKind::Grouping;
		return node;
	}

private:
	void AddField()
	{
		ibValueCompositionField* field = m_panel != nullptr ? m_panel->ChooseStructureField(this) : nullptr;
		if (field == nullptr)
			return;   // closed without picking
		m_node.m_settings.m_group.m_lines.push_back({ field->GetPath(), ibQueryDimUnfold::Elements });
		Refresh();
	}

	void RemoveField()
	{
		const int at = m_model->RowAt(m_view->GetSelection());
		if (at == wxNOT_FOUND || (size_t)at >= m_node.m_settings.m_group.m_lines.size())
			return;
		m_node.m_settings.m_group.m_lines.erase(m_node.m_settings.m_group.m_lines.begin() + at);
		Refresh();
	}

	// The order of the fields is the order they are PRINTED in, side by side on the heading — so it
	// is worth moving, and moving it is a swap.
	void MoveField(int delta)
	{
		const int at = m_model->RowAt(m_view->GetSelection());
		const int to = at + delta;
		if (at == wxNOT_FOUND || to < 0 || (size_t)at >= m_node.m_settings.m_group.m_lines.size() || (size_t)to >= m_node.m_settings.m_group.m_lines.size())
			return;
		std::swap(m_node.m_settings.m_group.m_lines[at], m_node.m_settings.m_group.m_lines[to]);
		Refresh();
		const ibDataViewItem row = m_model->ItemForRow((size_t)to);
		if (row.IsOk())
			m_view->Select(row);
	}

	void Refresh()
	{
		m_model->Rebuild();
		m_hint->SetLabel(m_node.m_settings.m_group.m_lines.empty()
			// ⚠ ASCII ONLY IN A UI LITERAL — this file has no BOM, so MSVC reads it as ANSI and an em
			// dash comes out as three bytes of mojibake on screen (seen live 2026-08-21). The rule is
			// stated at the top of the toolbars above; this line is what happens when it is forgotten.
			? _("No fields: this node prints the DETAIL RECORDS - the rows under the grouping above it.")
			: _("The fields of one grouping are printed side by side, on one heading."));
		FillPeriods();
		Layout();
	}

	// THE STRIP, FILLED FROM THE SELECTED LINE — or hidden, when that line is not a date and never
	// could be a period. Shown/hidden rather than enabled/disabled: see where it is built.
	void FillPeriods()
	{
		const int at = m_model != nullptr ? m_model->RowAt(m_view->GetSelection()) : wxNOT_FOUND;
		std::vector<ibGroupLineDescription>& lines = m_node.m_settings.m_group.m_lines;
		const bool onLine = at != wxNOT_FOUND && (size_t)at < lines.size();
		const bool applies = onLine && m_panel != nullptr
			&& m_panel->StructureFieldIsDated(lines[(size_t)at].m_path);

		if (m_periodPane->IsShown() != applies) {
			m_periodPane->Show(applies);
			// ⚠ LAYOUT MOVES WINDOWS, IT DOES NOT PAINT. Without the repaint the pixels the strip
			// appears over keep whatever was drawn there — which came out as the labels of this strip
			// reading the text of the control that used to sit at those coordinates (seen live in the
			// query constructor, 2026-08-25).
			Layout();
			// ⚠ QUALIFIED. This class has a `Refresh()` of its own — rebuilding the field list — so
			// the unqualified name is THAT one, and asking it to repaint would rebuild, refill and
			// land back here: a loop, not a redraw. (The hidden-name trap: a member of this name
			// hides every wxWindow overload.)
			wxDialog::Refresh(true);
			wxDialog::Update();
		}
		if (!applies)
			return;

		const ibGroupPeriodsDescription& periods = lines[(size_t)at].m_periods;
		m_byPeriods->SetValue(periods.IsOk());
		const int unitAt = periods.IsOk() ? m_periodUnit->FindString(periods.m_unit) : wxNOT_FOUND;
		m_periodUnit->SetSelection(unitAt != wxNOT_FOUND ? unitAt : 0);
		// ChangeValue, not SetValue: filling the strip is not an edit of it.
		m_periodFrom->ChangeValue(periods.m_from);
		m_periodTo->ChangeValue(periods.m_to);

		m_periodUnit->Enable(periods.IsOk());
		m_periodFrom->Enable(periods.IsOk());
		m_periodTo->Enable(periods.IsOk());
	}

	// …AND BACK ONTO THE LINE. Unchecking clears all three: "not by periods" is the absence of the
	// answer, not a unit remembered in case it comes back.
	void ApplyPeriods()
	{
		const int at = m_model != nullptr ? m_model->RowAt(m_view->GetSelection()) : wxNOT_FOUND;
		std::vector<ibGroupLineDescription>& lines = m_node.m_settings.m_group.m_lines;
		if (at == wxNOT_FOUND || (size_t)at >= lines.size() || !m_periodPane->IsShown())
			return;

		ibGroupPeriodsDescription& periods = lines[(size_t)at].m_periods;
		if (!m_byPeriods->GetValue()) {
			periods.Clear();
		}
		else {
			const int unitAt = m_periodUnit->GetSelection();
			periods.m_unit = unitAt != wxNOT_FOUND
				? m_periodUnit->GetString(unitAt) : ibPeriodUnits().front().second;
			periods.m_from = m_periodFrom->GetValue().Trim(true).Trim(false);
			periods.m_to   = m_periodTo->GetValue().Trim(true).Trim(false);
		}
		m_periodUnit->Enable(periods.IsOk());
		m_periodFrom->Enable(periods.IsOk());
		m_periodTo->Enable(periods.IsOk());
	}

	ibComposerSettingsPanel*  m_panel = nullptr;
	ibLevelDescription m_node;
	ibGroupingFieldsModel*    m_model = nullptr;
	ibDataViewCtrl*           m_view  = nullptr;
	wxStaticText*             m_hint  = nullptr;
	// The BY PERIODS strip — one panel, so it is shown and hidden as one thing.
	wxPanel*                  m_periodPane = nullptr;
	wxCheckBox*               m_byPeriods  = nullptr;
	wxChoice*                 m_periodUnit = nullptr;
	wxTextCtrl*               m_periodFrom = nullptr;
	wxTextCtrl*               m_periodTo   = nullptr;
};

// ⭐ THE SETTINGS ARE A PANEL, AND THE DIALOG IS ONE OF ITS HOSTS. A composer declared in the
// metadata is edited on a TAB of its own (the designer opens it like a form or a template — see
// docViewComposer), while a composition held by a form is edited modally from the gridbox. The
// same shape the list settings already took: content in a panel, the modal window a thin wrapper
// around it, so the two hosts cannot drift about what a setting is.
// THE DESIGNER'S — it edits the description's own variants, starting on the zeroth.
ibComposerSettingsPanel::ibComposerSettingsPanel(wxWindow* parent, ibCompositionDescription& edited,
	const ibMetaData* metaData)
	: wxPanel(parent, wxID_ANY),
	  m_edited(edited), m_readerRoad(false),
	  m_settings(&edited.m_variants.front().m_settings),
	  m_metaData(metaData),
	  m_fieldSource(new ibSettingsFieldTree())
{
	BuildPanel();
}

// THE READER'S — it edits the setting it was handed, and knows nothing of variants.
ibComposerSettingsPanel::ibComposerSettingsPanel(wxWindow* parent, ibCompositionDescription& edited,
	const ibMetaData* metaData, ibSettingsDescription& settings)
	: wxPanel(parent, wxID_ANY),
	  m_edited(edited), m_readerRoad(true),
	  m_settings(&settings),
	  m_metaData(metaData),
	  m_fieldSource(new ibSettingsFieldTree())
{
	BuildPanel();
}

void ibComposerSettingsPanel::BuildPanel()
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	// ⭐ THE WINDOW OPENS ON WHAT THE TEXT SAYS TODAY. Every field list here — the trees below, the
	// picker, the resources page — is the parse of the query text as it stands, so the state is the
	// same however the window was reached and costs one parse to get.
	//
	// (THERE IS NOTHING TO "APPLY" ANY MORE. This used to call ApplySource on a live composition to
	//  make it rebuild a column schema it kept; the fields are now read straight from the text, so
	//  the schema and the moment it was built stopped being things this window has to think about.)
	//
	// Only the READ happens here: the trees do not exist yet, and filling them is what
	// PopulateFieldTrees does once they do.
	RefreshQueryFields();

	// ⭐ THE NAMES A PARAMETER EXPRESSION MAY CALL, made to exist BEFORE anything can be edited.
	//
	// An expression is checked against the module MANAGER (see CheckExpression), and a manager that
	// has never been compiled carries an empty bytecode — so `CurrentDate()` came back as "procedure
	// or function not detected", a true statement about an empty world. Compiling it is what fills
	// it, and it happens HERE, once, because compiling rebuilds modules and refreshes what is bound
	// to them: from inside a cell editor that same rebuild destroys the renderer mid-call.
	PrepareModuleContext();

	// (NOTHING IS COPIED HERE ANY MORE. The description this window edits was handed to it BY
	//  REFERENCE and is edited in place — the shape the designer's other editors have, where the grid
	//  editor is given the metaobject and writes its description directly. A host that wants a
	//  transaction holds the copy itself and hands a reference to it, which is where Cancel belongs:
	//  it is the one who knows whether there is a Cancel at all.)


	// THE ORDER IS THE ORDER OF THE DECISIONS (Max, 2026-08-18): first WHAT IS READ — for a
	// composition that is the query, and it has no other source — then what is FOLDED, and last
	// how it is LAID OUT.
	//
	// 🛑 THE FIRST THREE PAGES ARE THE AUTHOR'S, THE LAST IS EVERYBODY'S (Max, 2026-08-19: "the
	// parameters and resources tabs are not available to the user — the user sees only the last
	// tab, the settings").
	//
	// What is READ (the query), what is FOLDED (the resources) and what the query ASKS FOR (the
	// parameters) are decisions taken when the report is written: they define what the report IS.
	// The person running it configures its OUTPUT — which groupings, which filter, which order —
	// and nothing they could do to the other three would survive as their setting.
	//
	// THE BUFFER AND THE FIELDS FIRST — every pane below is built over them. The settings arrived
	// with the copy above; there is nothing to load out of the composer, because the description is
	// where they live and the composer is only ever told.
	BindFieldSource();
	// ⭐⭐ AND THERE IS ALWAYS ONE OUTPUT — the engine's own rule (`ibDataComposer::Outputs`: "a
	// composition that has been told nothing still produces its rows"), stated here because the
	// DESCRIPTION does not carry that one until something is written into it.
	//
	// 🛑 WITHOUT IT EVERY STRUCTURE COMMAND REFUSED. "Add grouping" asks AxisForCommand, which answers
	// null on an empty structure, and the window said "this composition has no output to add a
	// grouping to" — with a variant named and selected right beside the message. A composer opened
	// fresh could never be given its FIRST grouping, on either road (Max, 2026-08-24).
	if (Structure().empty())
		AddOutput();

	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);
	if (appData->DesignerMode()) {
		notebook->AddPage(BuildQueryPage(notebook), _("Query"), true);
		notebook->AddPage(BuildResourcePage(notebook), _("Resources"), false);
		// PARAMETERS between what is READ and what is FOLDED: they are part of the reading — the query
		// asks for them — but they are filled in, not written, so they get a page of their own.
		notebook->AddPage(BuildParameterPage(notebook), _("Parameters"), false);
	}
	notebook->AddPage(BuildOutputPage(notebook), _("Output"), notebook->GetPageCount() == 0);
	mainSizer->Add(notebook, 1, wxALL | wxEXPAND, FromDIP(6));

	// LEAVING THE QUERY PAGE APPLIES IT. The query IS the composition's source: everything on every
	// other page - the fields to group by, the resources, the parameters - is read out of it. While
	// the text lived in the editor until somebody pressed "Apply", walking to the Output page showed
	// a composition with no fields, and closing the window dropped the query altogether - the report
	// then saved with an EMPTY source and the runtime opened on nothing (Max, 2026-08-19: "the data
	// is not saved").
	//
	// One rule now: what is on screen IS the composition — and since the text lands as it is typed,
	// leaving the page only has to make sure the SOURCE has been re-read for it.
	notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
		ApplyPendingQueryText();
		e.Skip();
	});

	// (Filter and Sort used to be buttons along the bottom of the window. They are SECTIONS under
	// the output tree now — they are about the result the tree describes, so that is where they
	// belong, and a button at the far end of the dialog said they were something else.)
	//
	// OK / Cancel belong to the HOST, not here: a modal window has them, a designer tab has Save
	// and Close instead. What they DO is Commit() / RestoreOnOpenState() below — one pair of rules
	// for both hosts.
	SetSizer(mainSizer);

	Bind(wxEVT_BUTTON, &ibComposerSettingsPanel::OnBuildQuery, this, ID_QUERY_BUILD);
	// THE QUERY IS RE-READ WHEN THE TYPING STOPS — see OnIdleApplyQuery.
	Bind(wxEVT_IDLE, &ibComposerSettingsPanel::OnIdleApplyQuery, this);
	Bind(wxEVT_TOOL, &ibComposerSettingsPanel::OnAddResource, this, ID_RESOURCE_ADD);
	Bind(wxEVT_TOOL, &ibComposerSettingsPanel::OnRemoveResource, this, ID_RESOURCE_REMOVE);
	Bind(wxEVT_TOOL, &ibComposerSettingsPanel::OnResourceExpression, this, ID_RESOURCE_EXPR);

	// THE STRUCTURE VERBS. Raised from its toolbar; each is a no-op when the cursor is on the
	// Report node, because a level command has nothing to act on there.
	Bind(wxEVT_TOOL, &ibComposerSettingsPanel::OnStructureAdd, this, ID_LEVEL_ADD);
	Bind(wxEVT_TOOL, &ibComposerSettingsPanel::OnStructureAddTable, this, ID_TABLE_ADD);
	Bind(wxEVT_TOOL, &ibComposerSettingsPanel::OnStructureRemove, this, ID_LEVEL_REMOVE);
	Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveStructureLevel(-1); }, ID_LEVEL_UP);
	Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveStructureLevel(+1); }, ID_LEVEL_DOWN);

	// THE VARIANT VERBS — add / copy / delete a snapshot of the settings.
	Bind(wxEVT_TOOL, &ibComposerSettingsPanel::OnVariantAdd, this, ID_VARIANT_ADD);
	Bind(wxEVT_TOOL, &ibComposerSettingsPanel::OnVariantCopy, this, ID_VARIANT_COPY);
	Bind(wxEVT_TOOL, &ibComposerSettingsPanel::OnVariantRemove, this, ID_VARIANT_REMOVE);

	// THE PARAMETER VERBS — a hand-made parameter is added and removed here; an auto one is not.
	Bind(wxEVT_TOOL, &ibComposerSettingsPanel::OnParameterAdd, this, ID_PARAM_ADD);
	Bind(wxEVT_TOOL, &ibComposerSettingsPanel::OnParameterRemove, this, ID_PARAM_REMOVE);

	// ⭐ EVERYTHING MOVES BY MOUSE (Max: "make it so a report can be made with the mouse"). A drag
	// from the field tree and a press of the ">" button are the SAME VERB reached two ways, which
	// is why every drop target below raises the handler the button raises — the drag carries no
	// payload of its own, because the thing being moved is what the tree has selected.
	//
	// ⚠ The begin-drag event is deliberately NOT Allow()ed: allowing it starts wxTreeCtrl's own
	// native drag beside ours, and MSW then refuses the second BeginDrag with an assert. Same shape
	// the query constructor uses.
	// ⚠ GUARDED, because a page that is not built leaves its controls null — the Query page is
	// Designer-only, and a rearrangement of the pages once left this binding pointing at a tree
	// nobody had created any more. It crashed in the constructor, before the window ever appeared
	// (designer_25192, 2026-08-18), and the stack said `Bind` rather than anything about layout.
	if (m_resourceFieldTree != nullptr) {
		m_resourceFieldTree->Bind(wxEVT_TREE_BEGIN_DRAG, [this](wxTreeEvent& e) {
			m_resourceFieldTree->SelectItem(e.GetItem());
			wxTextDataObject payload(wxT("field"));
			wxDropSource drag(payload, m_resourceFieldTree);
			drag.DoDragDrop(wxDrag_CopyOnly);
		});
		// THE AGGREGATE LIST FOLLOWS THE FIELD, and a double-click adds the resource. Without
		// these two the chooser stayed empty forever and ">" answered nothing: its handler asks
		// the chooser for a selection, and an unfilled chooser has none.
		m_resourceFieldTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibComposerSettingsPanel::OnResourceFieldActivated, this);
	}
	if (m_resourceView != nullptr)
		m_resourceView->SetDropTarget(new ibCallbackDropTarget([this] { wxCommandEvent e; OnAddResource(e); }));
	if (m_resourceView != nullptr)
		m_resourceView->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibComposerSettingsPanel::OnResourceContextMenu, this);

	// VIEW ONLY, the cell half — every grid this window owns, in ONE place. Bound after all four
	// exist so none is missed, and listed here rather than beside each creation for the same reason:
	// a fifth grid is added to this line, not remembered about.
	for (ibDataViewCtrl* view : { m_variantView, m_structureView, m_resourceView, m_parameterView }) {
		if (view != nullptr)
			view->Bind(wxEVT_DATAVIEW_ITEM_START_EDITING, &ibComposerSettingsPanel::OnStartEditing, this);
	}

	PopulateFieldTrees();
	ReloadResources();
	ReloadStructure();
	ReloadVariants();
	ReloadParameters();   // the page is built before the first Apply — start it on what the text already asks for
	UpdateSettingsHeader();

	// ⭐ AND THE QUERY SAYS AT ONCE WHETHER IT STILL COMPILES. The parse happened in this
	// constructor; showing its verdict only after the first EDIT meant a composition whose stored
	// text had gone stale — a renamed field, a table dropped — opened looking healthy and admitted
	// nothing until somebody typed. The window knew from the start; now it says so from the start.
	ShowQueryFault();
}

// ⭐⭐ VIEW ONLY — every verb closed, everything still READABLE (Max, 2026-08-20: "in view mode you
// can only look: no copying, no adding, nothing").
//
// Three roads reach a verb in this window and all three are shut here, because leaving one open is
// the whole failure: the TOOLBARS (disabled — they also stop looking clickable, which is the honest
// signal), the CONTEXT MENUS (they ask m_readOnly and simply do not appear — a disabled menu item
// still invites the click), and the CELLS (the edit is vetoed as it starts, the same door the table
// control uses). The query text goes read-only rather than disabled, so it can still be read and
// copied out.
//
// ⭐ And the two SHARED editors are forwarded to, not re-implemented: filter and sort are the pieces
// this world holds in common with the list's, so the list gets the identical answer from the
// identical code — which is why they live one folder up.
void ibComposerSettingsPanel::SetReadOnly(bool readOnly)
{
	m_readOnly = readOnly;

	wxToolBar* const bars[] = { m_variantBar, m_structureBar, m_resourceBar, m_parameterBar };
	for (wxToolBar* bar : bars) {
		if (bar != nullptr)
			bar->Enable(!readOnly);
	}

	if (m_queryText != nullptr)
		m_queryText->SetReadOnly(readOnly);
	// The Query page's two buttons, by id rather than by a member each: they are already NAMED — the
	// ids are what their handlers are bound to — so holding a second name for the same button would
	// be one more thing to keep in step.
	if (wxWindow* build = FindWindow(ID_QUERY_BUILD))
		build->Enable(!readOnly);

	if (m_filterEditor != nullptr)
		m_filterEditor->SetReadOnly(readOnly);
	if (m_sortEditor != nullptr)
		m_sortEditor->SetReadOnly(readOnly);

	// …AND THE ACTIVATABLE CELL, which no veto reaches — the model is the one gate the fork asks on
	// both the click and the Space road. See ibParameterModel::IsEnabledByRow.
	if (m_parameterModel != nullptr)
		m_parameterModel->SetReadOnly(readOnly);
}

// The cell half of the rule above — one handler for all four grids, so a fifth grid added later is
// covered by binding it rather than by remembering a rule.
void ibComposerSettingsPanel::OnStartEditing(ibDataViewEvent& event)
{
	if (m_readOnly)
		event.Veto();
	else
		event.Skip();
}

// ⭐ THE BUFFER WAS EDITED — one sentence said in one place. It carries BOTH consequences and they
// are different: the change is announced NOW (the value changed, so it changed — no OK required),
// and the fact is remembered so that accepting the window knows there is a buffer worth landing.
// Keeping them together is what stops the two from drifting apart.
void ibComposerSettingsPanel::MarkSettingsTouched()
{
	m_settingsDirty = true;
	MarkModified();
}

// --- what a HOST answers ------------------------------------------------------------------------
//
// The metadata mode's answers, and they are the ones this panel had inline everywhere before the
// document mode existed. Overridden whole by ibComposerEditor, which has a document to ask instead.

const ibMetaData* ibComposerSettingsPanel::GetEditedMetaData() const
{
	return m_metaData;
}

void ibComposerSettingsPanel::MarkModified()
{
	// ⭐ NOTHING, ON THIS ROAD, AND THAT IS THE POINT OF THE HOOK. The metadata mode edits a
	// description somebody else owns — the modal host's own copy, which OK writes and Cancel drops —
	// so there is nobody here to tell. The DOCUMENT mode has a dirty bit and a Save behind it, and
	// overrides this to set it (Max, 2026-08-24: "you set modified straight on the doc, and give
	// whoever has none a way to override it").
	//
	// 🛑 IT USED TO POKE THE LIVE COMPOSITION'S OnChildChanged and hope the attach chain carried it
	// somewhere. That is the mechanism it replaces, not a second one beside it.
}

// ⭐ WHAT "ACCEPT" MEANS, wherever the panel is hosted. Everything on screen is edited in a
// TRANSACTIONAL BUFFER, so committing is what puts it onto the composition — and a half-written
// condition is objected to here, with nothing written.
//
// Returns false when the host must stay open on what was objected to.
bool ibComposerSettingsPanel::Commit()
{
	// 🛑 VIEW ONLY WRITES NOTHING — and answers YES, because there is nothing to object to. Closing
	// a tab is accepting it (ibComposerEditView::OnClose), so without this a composer opened purely
	// to LOOK at would write itself back on the way out and raise the change signal doing it: the
	// configuration would come out modified from a session that changed none of it.
	if (m_readOnly)
		return true;

	// THE TEXT FIRST: the filter and sort about to be committed are expressed over the query's
	// fields, so the query has to be the current one before they land.
	ApplyPendingQueryText();

	// Gated on the same fact as the capture below: with nothing edited in the buffer there is
	// nothing to land, and writing it back anyway is how an untouched tab looked like a changed one.
	if (m_settingsDirty && !CommitSettings())
		return false;

	// ⭐ AN EXPRESSION CAN BE TYPED STRAIGHT INTO THE CELL, without ever opening the editor — so the
	// settings are checked on the way out (Max, 2026-08-19). It ASKS rather than refuses: a
	// half-written expression is a legitimate state to leave behind (the query may not be finished
	// either), and losing the rest of the settings over it would be worse.
	const wxString complaints = CheckAllExpressions();
	if (!complaints.IsEmpty()) {
		const int answer = wxMessageBox(
			_("Some parameter expressions do not compile:") + wxT("\n\n") + complaints + wxT("\n")
				+ _("Close anyway?"),
			_("Data composer settings"), wxYES_NO | wxICON_WARNING, this);
		if (answer != wxYES)
			return false;   // stay open, on the settings that are still being written
	}

	// 🛑 ONLY IF THE BUFFER WAS ACTUALLY EDITED. Closing a designer tab IS accepting it, so this runs
	// every time a composer is merely LOOKED at — and announcing unconditionally meant an untouched
	// tab came back as "the configuration changed", asterisk and all (found by the final audit,
	// 2026-08-20).
	//
	// (What used to happen here — capturing the composition into its active variant — is done inside
	//  CommitSettings now, over the COPY: the variant a person edits is a variant of the copy, and
	//  asking the live composition to capture itself would have captured what it held before OK.)
	m_settingsDirty = false;
	// The signal was already raised where the edit happened (see the editors' SetOnChanged), so it is
	// deliberately NOT raised again here.

	return true;
}

// ⭐ AND WHAT "CANCEL" MEANS. Switching variants inside this panel WRITES — the composer holds one
// set of settings at a time, so activating another one is not a preview. The whole set is
// snapshotted when the panel opens, and this puts it back.
// ⭐⭐ THE WINDOW EDITS A COPY, AND ON SUCCESS THE COPY REPLACES THE ORIGINAL (Max, 2026-08-23).
// That is the whole of accept and cancel: OK assigns the edited description over the composition's
// own and lets it fill its live objects from it; Cancel simply drops the copy. There is nothing to
// restore, because nothing was changed until the assignment — which is why this is a transaction
// and the old snapshot-and-put-back was only an imitation of one.
// (CommitDescription REMOVED — CommitSettings does exactly this on the designer's road, and a second
//  method saying the same thing is a second place to keep in step.)

// (RestoreOpenState DELETED — CANCEL is the copy being dropped, and that is all of it. What this
//  window edits is a description somebody else holds, and whoever holds it decided what Cancel
//  means: the modal host keeps a COPY and lets it die with the window, the designer's tab hands
//  over the metaobject's own and has no Cancel at all. So the body was empty, and an empty method
//  wired to a button is a promise that something happens there.)

// ---------------------------------------------------------------------------
//  ibDialogComposerSettings — the MODAL host: the panel plus OK / Cancel.
// ---------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
// THE COMPOSER EDITOR — the document mode. Everything it adds is the pair of answers below; the
// content, the tabs and the commit are the panel's, unchanged.
// ------------------------------------------------------------------------------------------------

// ⭐ THE CAST IS THE EDITOR'S, exactly as ibGridEditor does it: the document is what this editor was
// given, and `ConvertMetaObjectToType` is how a document is asked what it was opened ON. Nothing is
// handed down from the view but the document itself (Max, 2026-08-24).
//
// Free functions rather than methods because the BASE CONSTRUCTOR needs both answers, and a virtual
// call during base construction dispatches to the base.
static ibValueMetaObjectComposer* ComposerOf(ibMetaDocument* document)
{
	return document != nullptr ? document->ConvertMetaObjectToType<ibValueMetaObjectComposer>() : nullptr;
}

static const ibMetaData* MetaDataOf(ibMetaDocument* document)
{
	const ibValueMetaObjectComposer* metaComposer = ComposerOf(document);
	return metaComposer != nullptr ? metaComposer->GetMetaData() : nullptr;
}

// …AND THE DESCRIPTION IS REACHED THE SAME WAY, and edited in place — the shape ibGridEditor has,
// where a cell write goes straight into `creator->GetSpreadsheetDesc()`. No copy travels, and the
// view above stores nothing.
//
// ⚠ THE STAND-IN. A composer tab with no composer metaobject cannot happen through the document
// manager — the template is registered against the composer metatype — but a reference has to bind
// to something, so it binds here. Nothing written into it is ever saved, which is the truth about a
// tab that is editing nothing.
static ibCompositionDescription& DescOf(ibMetaDocument* document)
{
	static ibCompositionDescription s_noComposer;
	ibValueMetaObjectComposer* metaComposer = ComposerOf(document);
	return metaComposer != nullptr ? metaComposer->GetCompositionDesc() : s_noComposer;
}

// THE DOCUMENT IS THE ONLY INPUT. Both of the other two — which description is edited, and which
// configuration it means — are reached from it here, so the view above hands over nothing else and
// stores nothing itself.
ibComposerEditor::ibComposerEditor(wxWindow* parent, ibMetaDocument* document)
	: ibComposerSettingsPanel(parent, DescOf(document), MetaDataOf(document)),
	  m_document(document)
{
}

const ibMetaData* ibComposerEditor::GetEditedMetaData() const
{
	// An absent configuration is a legitimate answer, not a broken one — see the base's constructor:
	// with nothing to ask, the primitive types are what a field can be. What it must never do is
	// reach for the ACTIVE configuration, which in the designer is somebody else's.
	return MetaDataOf(m_document);
}

void ibComposerEditor::MarkModified()
{
	// STRAIGHT ONTO THE DOCUMENT. It is the thing with a dirty bit, and the tab's Save reads it —
	// there is no chain to walk and nothing above it to hope for.
	if (m_document != nullptr)
		m_document->Modify(true);
}


// THE AUTHOR'S ROAD, OVER A DESCRIPTION — edited in place. The caller decides whose description it
// is (its own value, or a clone of it that Cancel simply drops), so nothing live is reached here.
// THE READER'S ROAD — see the header. A copy of the setting in force, the window over it, and the
// copy set back on OK. The description it stands over is the model's own, so the field lists offer
// what this report actually reads.
// THE DOOR — a model goes in, its composer answers. See the header for why it is a REPORT's model.
bool ibDialogComposerSettings::ShowVariantPicker(wxWindow* parent, ibValueSpreadsheetModel* model)
{
	if (model == nullptr)
		return false;

	// 🛑 THE VARIANTS REACH THE COMPOSER LAZILY, and asking without saying so read an empty list.
	// `LoadVariants` is called from `RebuildSource` / `RefreshComposerSettings` — that is, when the
	// source is rebuilt or a setting changes — so a report that has just been opened has its
	// variants in the DESCRIPTION and not yet in the composer. The first press showed nothing and
	// the second one worked, which is what "you have to click twice" means (Max, 2026-08-26).
	//
	// ⭐ Restated rather than reached around: the model has a verb for exactly this — the same one
	// the settings window relies on — so the picker asks it to bring the composer up to date and
	// then reads the composer, as it should. Reading the description directly here would be a
	// second road to the same fact, and the two would drift.
	if (ibValueDataComposition* composition = dynamic_cast<ibValueDataComposition*>(model))
		composition->RefreshComposerSettings();

	return PickVariant(parent, model->GetModelComposer());
}

// ⭐⭐ THE VARIANT PICKER — the menu, and picking IS setting a setting. See the header for why this
// needs nothing of its own.
bool ibDialogComposerSettings::PickVariant(wxWindow* parent, ibDataComposer& composer)
{
	const std::vector<ibVariantDescription>& variants = composer.GetVariants();

	// 🛑 A COMMAND THAT ANSWERS NOTHING IS WORSE THAN ONE THAT IS GREYED OUT. This refused to open at
	// all when there was "nothing to pick between" — one unnamed variant, which is what a report
	// nobody has structured has — so the button was there, it was live, and pressing it did nothing
	// whatsoever (Max, 2026-08-26: "the button does not work"). Whether the list is worth choosing
	// from is the READER's judgement, and the menu is where they make it: one entry, ticked, says
	// "this is all there is" — which is an answer.
	if (variants.empty())
		return false;   // …and this cannot happen: the vector is born with one element

	wxWindow* over = parent != nullptr ? parent
		: ((wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr);
	if (over == nullptr)
		return false;

	const int base = wxID_HIGHEST + 1;
	wxMenu menu;
	for (size_t i = 0; i < variants.size(); ++i) {
		// WHAT THE PICKER SHOWS — the synonym, else the name, else its place in the list. The last is
		// not a caption anybody wrote; it is what an unnamed variant HAS, and a blank line in a menu
		// cannot be clicked with any confidence.
		wxString caption = variants[i].m_synonym;
		if (caption.IsEmpty()) caption = variants[i].m_name;
		if (caption.IsEmpty()) caption = wxString::Format(_("Variant %u"), static_cast<unsigned>(i + 1));

		// ⭐ AND THE ONE IN FORCE IS TICKED — by COMPARING the settings, because there is no stored
		// "active variant" to read. That is not a gap: at runtime there is only the setting that
		// composes, and a variant is where it may have come from. Compared, the tick says the truth
		// even after the reader edited the setting themselves — it simply stops matching.
		//
		// 🛑 AGAINST WHAT COMPOSES, not against the READER's section. Asked of the user's setting,
		// nothing was ticked until they had picked something — and what composes at that moment is
		// variant ZERO, by the rule that an empty section reads as the zeroth's. So the menu opened
		// claiming no variant was in force while one plainly was (Max, 2026-08-26: "why is the flag
		// not lit on the main variant to begin with?").
		wxMenuItem* item = menu.AppendCheckItem(base + static_cast<int>(i), caption);
		if (item != nullptr && composer.GetCurrentSettingsDesc() == variants[i].m_settings)
			item->Check(true);
	}

	const int picked = over->GetPopupMenuSelectionFromUser(menu);
	if (picked == wxID_NONE)
		return false;   // closed without choosing — nothing changes

	const size_t at = static_cast<size_t>(picked - base);
	if (at >= variants.size())
		return false;

	// …AND THAT IS THE WHOLE ACT. The same call the settings window makes on OK.
	composer.SetUserSettingsDesc(variants[at].m_settings);
	return true;
}

// ⭐⭐ SAVE — the question is WHERE TO PUT what is in force. The shelf itself, plus a place that is
// not on it yet: saving over an entry is an ordinary act once you have saved before, so it is one
// gesture rather than a name typed again exactly as before.
bool ibDialogComposerSettings::ShowSavedSettings(wxWindow* parent, ibDataComposer& composer,
                                                 ibSettingsCategory category, const ibGuid& objectKey,
                                                 const ibMetaData* metaData)
{
	// ⭐ THE SAME SHELF, opened for the other act. It used to be a MENU here and a window there —
	// two surfaces over one set of entries, and the menu had no room for the mark a person wants to
	// set while they are saving (Max, 2026-08-26: *"beside it there is a button, set as the main
	// one"*). One window, and the mode decides which button is the main one.
	return ibDialogSavedSettings::Show(parent, composer, ibDialogSavedSettings::Mode::Save,
		category, objectKey, metaData);
}

// ⭐⭐ THE OTHER BUTTON — WHICH ONE TO PUT ON. A WINDOW rather than a menu, because this is where
// the shelf is kept as well as read: the entries with the default one in BOLD, and the verbs that
// act on whichever is selected (restore, mark as the one to restore on open, rename, delete).
// Picking an entry IS SetUserSettingsDesc — the same act as picking a variant.
bool ibDialogComposerSettings::ShowRestoreSettings(wxWindow* parent, ibDataComposer& composer,
                                                   ibSettingsCategory category, const ibGuid& objectKey,
                                                   const ibMetaData* metaData)
{
	return ibDialogSavedSettings::Show(parent, composer, ibDialogSavedSettings::Mode::Restore,
		category, objectKey, metaData);
}

bool ibDialogComposerSettings::ShowUserSettings(wxWindow* parent, ibValueSpreadsheetModel* model)
{
	if (model == nullptr)
		return false;

	ibValueDataComposition* composition = dynamic_cast<ibValueDataComposition*>(model);
	if (composition == nullptr)
		return false;   // a drawn document describes nothing — there is no setting to arrange

	// THE COPY — what Cancel drops, and what OK becomes.
	// ⭐ WHAT IS IN FORCE — the reader's where they set one, the author's where they did not, asked
	// per part. The composer answers it now: this used to reach for the description itself when the
	// user's section was empty, which is the same question asked in a second place.
	ibSettingsDescription edited = model->GetModelComposer().GetCurrentSettingsDesc();

	wxWindow* top = parent != nullptr ? parent
		: ((wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr);

	// 🛑 AND THE SCHEMA IS NOT HANDED OVER TO BE EDITED. It used to go in as a NON-CONST reference on
	// this road, and the panel edits a description in place — so a reader switching, renaming, adding
	// or deleting a VARIANT was rewriting the live composition, and Cancel undid none of it. The
	// list's window has been const-schema since the same day; this is the mirror of it
	// (audit, 2026-08-24).
	//
	// A copy, because the panel's ctor takes a mutable reference and its structure pane genuinely
	// edits one. The composition is never written on this road — what the reader changed leaves
	// through the two halves below.
	// (The variants in this copy are NOT what the window edits and are never read back from: on a
	//  reader's road the panel stands over `edited` and nothing else. The copy is here because the
	//  panel's other pages — the query text, the resources — take a description.)
	ibCompositionDescription shown = composition->GetCompositionDesc();

	ibDialogComposerSettings dlg(top, shown, composition->GetMetaData(), edited);
	if (dlg.ShowModal() != wxID_OK)
		return false;

	// ⭐⭐ WHAT THE WINDOW EDITED IS WHAT IS SAVED — `edited`, the very object the panel stood over.
	// A node's settings live in its OUTPUTS, the outputs are part of a setting, and the setting is
	// what goes back; nothing is forwarded separately and nothing is re-derived.
	//
	// 🛑 THIS LINE READ A SNAPSHOT TAKEN BEFORE THE DIALOG. The copy `edited` was assigned into the
	// shown description's zeroth variant on the way IN, and the way OUT read that variant back — so
	// every edit made in the window was thrown away and the pre-dialog state was saved over it.
	// Seen live as *"a filter or a sort set on a NODE is never kept"* and as the structure snapping
	// back to `<detail records>`: the nodes were not written wrong, the wrong object was written
	// (Max, 2026-08-24).
	model->GetModelComposer().SetUserSettingsDesc(edited);

	// ⭐ …AND IT RUNS AGAIN. Setting is not showing: the composer now says something different, and
	// the sheet on screen was built before it did. The list's window has said this since it was
	// written (RefetchAll); this one only assigned and left, so a person accepted their settings and
	// nothing moved until they pressed Compose (Max, 2026-08-24).
	//
	// Compose is the spreadsheet model's own re-read — it builds into a document of its own and
	// publishes it, and the control is already subscribed to that sheet. A refused query raises, and
	// it is raised on THROUGH: whoever ran this command reports engine failures the way it reports
	// every other one.
	model->Compose();
	return true;
}

bool ibDialogComposerSettings::ShowComposerSettings(wxWindow* parent, ibCompositionDescription& desc,
	const ibMetaData* metaData)
{
	wxWindow* top = parent != nullptr ? parent
		: ((wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr);

	// WHAT IT WAS — the other half of the comparison at the close.
	const ibCompositionDescription before = desc;

	ibDialogComposerSettings dlg(top, desc, metaData);
	if (dlg.ShowModal() != wxID_OK)
		return false;

	// Compared at the CLOSE, not per keystroke: every character typed into the query would otherwise
	// be a version of its own.
	return before != desc;
}

// THE SAME WINDOW OVER A BARE DESCRIPTION — edited IN PLACE, so this host keeps no copy of its own:
// whoever handed the description in decided whether it is a clone (and therefore what Cancel drops).
// ⭐⭐ ONE CONSTRUCTOR, OVER A SNAPSHOT — see the header. A description and the configuration its
// names mean, edited IN PLACE, so this host keeps no copy of its own: whoever handed the description
// in decided whether it is a clone, and that decides what Cancel drops.
ibDialogComposerSettings::ibDialogComposerSettings(wxWindow* parent, ibCompositionDescription& desc,
	const ibMetaData* metaData, ibSettingsDescription& settings)
	: wxDialog(parent, wxID_ANY, _("Data composer settings"), wxDefaultPosition, wxSize(900, 620),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	m_panel = new ibComposerSettingsPanel(this, desc, metaData, settings);
	BuildAround();
}

// …AND THE AUTHOR'S, over the description itself.
ibDialogComposerSettings::ibDialogComposerSettings(wxWindow* parent, ibCompositionDescription& desc,
	const ibMetaData* metaData)
	: wxDialog(parent, wxID_ANY, _("Data composer settings"), wxDefaultPosition, wxSize(900, 620),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	m_panel = new ibComposerSettingsPanel(this, desc, metaData);
	BuildAround();
}

// The frame around whichever panel was built — the two ctors differ in the panel and in nothing else.
void ibDialogComposerSettings::BuildAround()
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	mainSizer->Add(m_panel, 1, wxALL | wxEXPAND, FromDIP(6));
	mainSizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxALIGN_RIGHT, FromDIP(6));
	SetSizer(mainSizer);

	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (m_panel != nullptr && !m_panel->Commit())
			return;   // the panel objected and said so — stay on it
		EndModal(wxID_OK);
	}, wxID_OK);

	// (NO CANCEL HANDLER. Dropping the copy IS the cancel — see the note where RestoreOpenState was.)
}

// ---------------------------------------------------------------------------
//  Pages
// ---------------------------------------------------------------------------

// THE OUTPUT PAGE, laid out the way the decisions nest (Max, 2026-08-19, from a DCS screenshot):
//
//   ┌──────────┬───────────────────────────────────────┐
//   │ Variants │  toolbar + the OUTPUT STRUCTURE        │
//   │          ├───────────── splitter ────────────────┤
//   │          │  the selected node's settings, with    │
//   │          │  THEIR field list on the left          │
//   └──────────┴───────────────────────────────────────┘
//
// Variants stand on the LEFT because choosing one reloads everything to its right — its own
// structure, its own filter, its own sort. The structure stands ABOVE the settings because the
// settings are about the result it describes. Every boundary is a SPLITTER: how much room a
// filter needs is the user's business, not the layout's.
//
// 🛑 NO FIELD LIST IN THE UPPER PANE. The lower area already has one, shared by its sections, and
// a second one up here is the same question answered twice side by side (which is exactly how this
// page read on 2026-08-19 before Max saw it). A field for a new grouping is picked in a DIALOG,
// raised from the structure toolbar — the panel's own picker, so there is still one answer to
// "which fields does this composition have".
wxWindow* ibComposerSettingsPanel::BuildOutputPage(wxWindow* parent)
{
	// ⭐⭐ VARIANTS ARE THE DESIGNER'S, AND A READER MUST NOT SEE THEM (Max, 2026-08-24: "variants
	// are shown in the designer only; a run always takes the zeroth").
	//
	// 🛑 THE COLUMN WAS BUILT UNCONDITIONALLY, so Enterprise showed a reader the list of variants
	// with Add / Copy / Delete over the author's templates. Seen live on an external report.
	//
	// 🛑🛑 AND THE FIRST CURE CRASHED — a dump the same evening, `wxSplitterWindow::DoSplit` on an
	// assert. Dropping the PANE while keeping the SPLITTER left `SplitVertically(nullptr, inner)`:
	// a splitter with one half is not a splitter. So the outer one is not created at all on a
	// reader's road, and the structure with its settings band IS the page.
	// ⭐ THE LESSON, since it is the second of its kind today: removing a thing means removing what
	// held it, not passing null where it stood.
	wxSplitterWindow* outer = nullptr;
	wxWindow* variantPane = nullptr;
	if (!m_readerRoad) {
		outer = new wxSplitterWindow(parent, wxID_ANY,
			wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
		outer->SetMinimumPaneSize(FromDIP(120));
		variantPane = BuildVariantPane(outer);
	}

	wxSplitterWindow* inner = new wxSplitterWindow(outer != nullptr ? (wxWindow*)outer : parent, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	inner->SetMinimumPaneSize(FromDIP(90));

	// ⚠ THE SETTINGS PANE IS BUILT FIRST. The structure tree edits the PANEL's transactional
	// buffer (see Levels()), so the panel — and the buffer it loads from the composition — has
	// to exist before the tree reads a single level.
	wxWindow* settingsPane = BuildSettingsPane(inner);
	wxWindow* structurePane = BuildStructurePane(inner);

	// ⭐ THE STRUCTURE IS THE CENTRAL AREA (Max, 2026-08-19). This window is ABOUT the output, so
	// the tree takes the room and the settings sit UNDER it as a band — not two equal halves.
	//
	// The sash is set from the BOTTOM (a negative position) because the height the settings need is
	// a known quantity and the window's own height is not one yet at this point; and the gravity is
	// 1.0, so every pixel a resize adds goes to the CENTRE. Stretching the window grows the report's
	// structure; the band keeps the height the user gave it.
	inner->SetSashGravity(1.0);
	inner->SplitHorizontally(structurePane, settingsPane, -FromDIP(230));

	// A READER'S PAGE IS THE INNER ONE — no variants column, so no column to split off.
	if (outer == nullptr)
		return inner;

	// Variants are a fixed-width column: nothing about a list of names wants more room when the
	// window grows, so gravity 0.0 hands all of it to the centre as well.
	outer->SetSashGravity(0.0);
	outer->SplitVertically(variantPane, inner, FromDIP(170));
	return outer;
}

// VARIANTS — a toolbar over a dataview, and the reason the pane is on the LEFT: picking one
// reloads everything to its right. Add / Copy / Delete, exactly the three verbs a snapshot needs
// ("you can add one, you can copy an existing one — it copies the groupings, filters, sorts and so
// on", Max). Delete is greyed on the last one, because a composition without a variant has no
// settings at all.
wxWindow* ibComposerSettingsPanel::BuildVariantPane(wxWindow* parent)
{
	wxPanel* variantPane = new wxPanel(parent);
	wxBoxSizer* variantSizer = new wxBoxSizer(wxVERTICAL);

	m_variantBar = new wxToolBar(variantPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
	m_variantBar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
	m_variantBar->AddTool(ID_VARIANT_ADD, _("Add"),
		ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("Add variant"));
	m_variantBar->AddTool(ID_VARIANT_COPY, _("Copy"),
		ibSettingsArt(wxASCII_STR(wxART_COPY), this), _("Copy variant"));
	m_variantBar->AddTool(ID_VARIANT_REMOVE, _("Delete"),
		ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete variant"));
	m_variantBar->Realize();
	variantSizer->Add(m_variantBar, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(4));

	m_variantView = new ibDataViewCtrl(variantPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE | wxDV_NO_HEADER);
	ibStyleSettingsGrid(m_variantView);
	m_variantModel = new ibVariantModel([this] { return &m_edited.m_variants; });
	m_variantView->AssociateModel(m_variantModel);
	// THE NAME IS EDITABLE IN PLACE — it is the whole of a variant a person sees, and renaming it
	// somewhere else would be a dialog for one string.
	m_variantView->GetRootColumnGroup()->AppendTextColumn(_("Variant"), ibVariantModel::kColName,
		wxDATAVIEW_CELL_EDITABLE, FromDIP(160), wxAlignment::wxALIGN_LEFT);
	variantSizer->Add(m_variantView, 1, wxALL | wxEXPAND, FromDIP(4));
	variantPane->SetSizer(variantSizer);

	// PICKING ONE SWITCHES EVERYTHING. The row that is now selected is the variant the rest of the
	// window is about — see ActivateVariant for what "switching" costs.
	m_variantView->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibComposerSettingsPanel::OnVariantContextMenu, this);
	m_variantView->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](ibDataViewEvent& e) {
		const int picked = ibSelectedRow(m_variantView);
		if (picked != wxNOT_FOUND)
			ActivateVariant((size_t)picked);
		e.Skip();
	});

	return variantPane;
}

// THE STRUCTURE — the one editor this window owns, because it is the one thing only a composition
// has. A toolbar of verbs over a TREE: Report at the root, a level under each level, so what is on
// screen is the nesting the report will actually produce.
//
// Both cells are VALUES, edited through the shared row-value cell: the field opens the panel's
// picker, the kind opens the runtime's quick choice over the GroupKind enumeration. Neither is a
// drop-down assembled here — a hand-built list beside a registered type is a second enumeration.
wxWindow* ibComposerSettingsPanel::BuildStructurePane(wxWindow* parent)
{
	wxPanel* pane = new wxPanel(parent);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxToolBar* bar = m_structureBar = new wxToolBar(pane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
	bar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
	// ⚠ ASCII ONLY IN UI LITERALS — this file has no BOM (see the query page below).
	bar->AddTool(ID_LEVEL_ADD, _("Add grouping"),
		ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("Add grouping"));
	// …AND THE OTHER SHAPE, beside it: a table, which arrives with its two axes already there.
	bar->AddTool(ID_TABLE_ADD, _("Add table"),
		ibSettingsArt(wxASCII_STR(wxART_LIST_VIEW), this), _("Add table (rows and columns)"));
	bar->AddTool(ID_LEVEL_REMOVE, _("Delete"),
		ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete"));
	bar->AddSeparator();
	bar->AddTool(ID_LEVEL_UP, _("Move up"), ibSettingsArt(wxASCII_STR(wxART_GO_UP), this), _("Move up"));
	bar->AddTool(ID_LEVEL_DOWN, _("Move down"), ibSettingsArt(wxASCII_STR(wxART_GO_DOWN), this), _("Move down"));
	bar->Realize();
	// THE BAR KEEPS THE VIEW.S MARGINS, or its left border reads as unfinished where the two meet.
	sizer->Add(bar, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(4));

	m_structureView = new ibDataViewCtrl(pane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(m_structureView);

	// ⚠ COLUMN 0 IS RESERVED by the ibDataViewCtrl fork (a model column 0 paints blank and does
	// not edit) — the filter tree starts at 1 for exactly this reason, and so does this one.
	// ⚠ AppendTextColumn does not exist in the fork: a column belongs to a column GROUP.
	//
	// WHAT THE NODE IS — and the column the TREE hangs off, so it is deliberately not editable:
	// a click in the expander column belongs to open/close, and the grid refuses to start an edit
	// there. That is why the field is a column of its own rather than this one's caption.
	m_structureView->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Structure"),
		new ibDataViewTextRenderer(), ibComposerStructureModel::kColNode,
		FromDIP(160), wxAlignment::wxALIGN_LEFT));

	// ⭐ THE FIELDS THIS LEVEL GROUPS BY — a LIST, so the cell's "…" opens the GROUPING FORM (Max,
	// 2026-08-21: "the three dots call the picker today, and they should open the window"). A picker chooses ONE
	// field, and a level is not one field: opening it here could only ever edit the head one, and
	// the row plainly shows several. Same button, and it still means "open what edits this cell".
	ibRowValueCellRenderer* fieldCell = new ibRowValueCellRenderer(this, ibComposerFieldChooser(this),
			[this](const ibDataViewItem& row) -> ibValue {
				// THE HEAD FIELD is what this cell edits; a level's full set is shown by the model
				// and composed on the level's own tab, where a list of fields has room to be a list.
				const ibLevelDescription* level = LevelAtRow(row);
				return (level != nullptr && !level->m_settings.m_group.m_lines.empty())
					? ibValue(new ibValueCompositionField(level->m_settings.m_group.m_lines.front().m_path)) : ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibLevelDescription* level = LevelAtRow(row);
				if (level == nullptr)
					return;
				// ⚠ THE DETAIL LEVEL GROUPS BY NOTHING, and that is what it IS. A field written here
				// would turn the rows into a heading without anybody asking for one, so the cell
				// stays silent on it — deleting the level is how it stops being there.
				if (level->IsDetailRecords())
					return;
				// An empty value CLEARS the head field — which leaves the level with none, and a
				// level with no fields IS the detail records. The rest of its fields stay where
				// they are: this cell speaks for one of them.
				ibValueCompositionField* field = nullptr;
				const bool chosen = value.ConvertToValue(field) && field != nullptr;
				if (!chosen) {
					if (!level->m_settings.m_group.m_lines.empty())
						level->m_settings.m_group.m_lines.erase(level->m_settings.m_group.m_lines.begin());
				}
				else if (level->m_settings.m_group.m_lines.empty()) {
					level->m_settings.m_group.m_lines.push_back({ field->GetPath(), ibQueryDimUnfold::Elements });
				}
				else {
					level->m_settings.m_group.m_lines.front().m_path = field->GetPath();
				}
				MarkSettingsTouched();
				ReloadStructure(m_structureModel != nullptr ? m_structureModel->LevelAt(row) : wxNOT_FOUND);
			});
	// …and THAT is what the button opens: the level, whole, in the same form that made it.
	fieldCell->SetExpand([this](const ibDataViewItem& row) { EditLevelInForm(row); });
	m_structureView->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Field"),
		fieldCell, ibComposerStructureModel::kColField, FromDIP(220), wxAlignment::wxALIGN_LEFT));

	// ⚠ NO "KIND" COLUMN HERE. How a field unfolds is the FIELD's, and a grouping may be made of
	// several of them (Max) — so one cell on the level could only ever speak for one of them, and
	// would read as the level's own answer. The kind is set inside the group, on the Grouping page,
	// where every element has its own row.


	// ⭐ A TREE, NOT A LIST — said out loud, because the control defaults to ibDataViewList and the
	// list build walks children into the SAME flat level, inserting each one at its parent's index.
	// The nesting then reads inside out: the level came first and "Report" sat under it (seen live
	// 2026-08-19). The filter tab carries the same line for the same reason.
	m_structureView->SetViewMode(ibDataViewTree);
	// THE EXPANDER STAYS ON THE FIRST COLUMN, which is why that column is the node's KIND and not
	// its field: the grid refuses to start an edit in the expander's column.
	if (m_structureView->GetColumnCount() > 0)
		m_structureView->SetExpanderColumn(m_structureView->GetColumn(0));
	sizer->Add(m_structureView, 1, wxALL | wxEXPAND, FromDIP(4));
	pane->SetSizer(sizer);

	// THE LADDER IS READ THROUGH A CALLBACK, not captured: the panel's buffer is what it edits,
	// and asking for it every time is what keeps this window from holding a second copy.
	// THE MODEL READS THE STRUCTURE BUFFER — the outputs themselves, not a flattened ladder. That is
	// what lets the tree show a second output, a column axis, and a level made of several fields.
	m_structureModel = new ibComposerStructureModel([this] { return &Structure(); });
	m_structureView->AssociateModel(m_structureModel);

	// A DOUBLE-CLICK EDITS THE CELL under the cursor — the same gesture the settings grids use.
	m_structureView->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](ibDataViewEvent& e) {
		if (m_structureView != nullptr)
			m_structureView->EditItem(e.GetItem(), e.GetDataViewColumn());
		e.Skip();
	});
	// WHICH NODE IS SELECTED is what the settings below are about — and, until the engine holds
	// settings per node, the header is also where that limitation is stated out loud.
	// RIGHT-CLICK RAISES THE SAME VERBS the toolbar does — a list you can only command from a bar
	// above it makes you travel for every action.
	m_structureView->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibComposerSettingsPanel::OnStructureContextMenu, this);
	m_structureView->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](ibDataViewEvent& e) {
		// ⚠ THE ITEM COMES FROM THE EVENT. Asking the control for its selection HERE walks a
		// selection it is still rebuilding, and it fails the assertion that says so.
		if (m_structureModel != nullptr) {
			m_currentNode = m_structureModel->PosAt(e.GetItem());   // one coordinate, handed over whole
		}
		// THE PANELS FOLLOW THE SELECTION — a level's filter and sort are ITS OWN, and the editors
		// below are re-pointed at its buffer. The report and an output keep the composition-wide
		// one, which is the pair that stands above every output.
		BindNodeEditors();
		ReloadGrouping();   // the Grouping page follows the selection too — it edits THAT level
		ReloadFieldSets();  // both field-set pages follow the selection — they are per node too
		// ⭐ AND SO DO THE FIELD TREES: what may be used is the NODE's available set, so the panes
		// that offer fields are re-filled for the node now selected. Without this the narrowing is
		// a setting that only takes effect the next time the window opens.
		ReloadFieldTrees();
		UpdateSettingsHeader();
		e.Skip();
	});

	return pane;
}

// THE GROUPING PAGE — the fields ONE level groups by, with the available fields beside them.
//
// Same shape as the shared editors next to it (a splitter, the field tree on the left, a toolbar
// over the list on the right), because it answers the same kind of question and a page that looks
// different for no reason reads as a page that works differently.
wxWindow* ibComposerSettingsPanel::BuildGroupingPage(wxWindow* parent)
{
	wxSplitterWindow* splitter = new wxSplitterWindow(parent, wxID_ANY, wxDefaultPosition,
		wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);

	// LEFT — WHAT THIS NODE MAY SEE. The available fields are inherited: what the report offers is
	// what its outputs offer, and what an output offers is what its levels offer. The tree is the
	// shared one, so this page cannot come to a different answer than the filter tab beside it.
	wxPanel* left = new wxPanel(splitter);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
	leftSizer->Add(new wxStaticText(left, wxID_ANY, _("Available fields")), 0, wxALL, FromDIP(4));
	m_groupingFieldTree = new wxTreeCtrl(left, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_SINGLE);
	leftSizer->Add(m_groupingFieldTree, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
	left->SetSizer(leftSizer);
	// ⚠ ATTACH WIRES THE BEHAVIOUR, POPULATE PUTS THE FIELDS IN. Attaching alone leaves an empty
	// pane that unfolds and drags perfectly — which is exactly how this page first shipped, with
	// "no available fields" as the only visible symptom.
	if (m_fieldSource) {
		m_fieldSource->Attach(m_groupingFieldTree);
		m_fieldSource->Populate(m_groupingFieldTree);
	}
	// ⭐ FROM THE PICKER INTO THE GROUPING, the same gesture the filter and the sort answer to:
	// double-click a field on the left and it joins this level's elements. A reference row unfolds
	// instead — it is a road, not a field.
	m_groupingFieldTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& e) {
		AddGroupingFieldFromTree(e.GetItem());
		e.Skip();
	});
	// RIGHT — the level's own elements, in order.
	wxPanel* right = new wxPanel(splitter);
	// ⭐ AND BY DRAGGING, the way the filter and the sort already take a field: drop it on this
	// pane and it joins the level's elements. The payload is nothing — what moved is what the tree
	// has selected, which the source knows (ibCallbackDropTarget).
	right->SetDropTarget(new ibCallbackDropTarget([this] {
		if (m_fieldSource)
			AddGroupingFieldFromTree(m_fieldSource->GetDragItem());
	}));
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);

	wxToolBar* bar = new wxToolBar(right, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_FLAT | wxTB_NODIVIDER | wxTB_HORIZONTAL);
	bar->AddTool(ID_GROUPFIELD_ADD, _("Add field"), ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("Add field"));
	bar->AddTool(ID_GROUPFIELD_REMOVE, _("Delete"), ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete"));
	bar->AddSeparator();
	bar->AddTool(ID_GROUPFIELD_UP, _("Move up"), ibSettingsArt(wxASCII_STR(wxART_GO_UP), this), _("Move up"));
	bar->AddTool(ID_GROUPFIELD_DOWN, _("Move down"), ibSettingsArt(wxASCII_STR(wxART_GO_DOWN), this), _("Move down"));
	bar->Realize();
	rightSizer->Add(bar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(4));

	m_groupingView = new ibDataViewCtrl(right, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(m_groupingView);
	m_groupingModel = new ibGroupingFieldsModel([this]() -> ibLevelDescription* {
		return CurrentLevel();   // the remembered node — this model is read DURING a selection change
	});
	m_groupingView->AssociateModel(m_groupingModel);

	// THE FIELD, as a VALUE — the same cell the structure tree uses, opening the same picker.
	m_groupingView->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Field"),
		new ibRowValueCellRenderer(this, ibComposerFieldChooser(this),
			[this](const ibDataViewItem& row) -> ibValue {
				ibLevelDescription* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
				const int at = m_groupingModel != nullptr ? m_groupingModel->RowAt(row) : wxNOT_FOUND;
				return (level != nullptr && at != wxNOT_FOUND && (size_t)at < level->m_settings.m_group.m_lines.size())
					? ibValue(new ibValueCompositionField(level->m_settings.m_group.m_lines[at].m_path)) : ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibLevelDescription* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
				const int at = m_groupingModel != nullptr ? m_groupingModel->RowAt(row) : wxNOT_FOUND;
				if (level == nullptr || at == wxNOT_FOUND || (size_t)at >= level->m_settings.m_group.m_lines.size())
					return;
				ibValueCompositionField* field = nullptr;
				if (value.ConvertToValue(field) && field != nullptr)
					level->m_settings.m_group.m_lines[at].m_path = field->GetPath();
				else
					level->m_settings.m_group.m_lines.erase(level->m_settings.m_group.m_lines.begin() + at);   // cleared = removed from the key
				MarkSettingsTouched();
				ReloadGrouping();
				RefreshStructureText();
			}),
		ibGroupingFieldsModel::kColField, FromDIP(240), wxAlignment::wxALIGN_LEFT));

	// THE UNFOLD BELONGS TO THE FIELD — a level may take one field through a hierarchy and the next
	// one flat, which is why this cell sits on the field and not on the level.
	m_groupingView->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Kind"),
		new ibRowValueCellRenderer(this, ibComposerFieldChooser(this),
			[this](const ibDataViewItem& row) -> ibValue {
				ibLevelDescription* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
				const int at = m_groupingModel != nullptr ? m_groupingModel->RowAt(row) : wxNOT_FOUND;
				return (level != nullptr && at != wxNOT_FOUND && (size_t)at < level->m_settings.m_group.m_lines.size())
					? ibValue::CreateEnumObject<ibValueEnumGroupKind>(level->m_settings.m_group.m_lines[at].m_kind) : ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibLevelDescription* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
				const int at = m_groupingModel != nullptr ? m_groupingModel->RowAt(row) : wxNOT_FOUND;
				if (level == nullptr || at == wxNOT_FOUND || (size_t)at >= level->m_settings.m_group.m_lines.size())
					return;
				const ibQueryDimUnfold kind = value.ConvertToEnumValue<ibQueryDimUnfold>();
				if (kind != ibQueryDimUnfold::Elements && level->m_settings.m_group.m_lines.size() > 1) {
					wxMessageBox(_("This grouping is made of several fields, and a hierarchy unfolds "
					               "one field's parent chain.\n\nGive the hierarchy field a grouping "
					               "of its own."),
						_("Data composer settings"), wxOK | wxICON_WARNING, this);
					return;
				}
				level->m_settings.m_group.m_lines[at].m_kind = kind;
				MarkSettingsTouched();
				ReloadGrouping();
				RefreshStructureText();
			}),
		ibGroupingFieldsModel::kColKind, FromDIP(150), wxAlignment::wxALIGN_LEFT));

	rightSizer->Add(m_groupingView, 1, wxEXPAND | wxALL, FromDIP(4));
	right->SetSizer(rightSizer);

	splitter->SplitVertically(left, right, FromDIP(220));
	splitter->SetMinimumPaneSize(FromDIP(120));

	Bind(wxEVT_TOOL, &ibComposerSettingsPanel::OnGroupingFieldAdd, this, ID_GROUPFIELD_ADD);
	Bind(wxEVT_TOOL, &ibComposerSettingsPanel::OnGroupingFieldRemove, this, ID_GROUPFIELD_REMOVE);
	Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveGroupingField(-1); }, ID_GROUPFIELD_UP);
	Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveGroupingField(+1); }, ID_GROUPFIELD_DOWN);
	return splitter;
}

// THE AVAILABLE-FIELDS PAGE — what the selected node MAY see.
//
// A set of its own, or the one above it ("Auto"). Set on the report it reaches every output; set on
// an output it reaches its levels — that is the inheritance Max described, and the reason this is a
// page of its own rather than a pane beside the others: it is a SETTING, not a catalogue.
wxWindow* ibComposerSettingsPanel::BuildFieldSetPage(wxWindow* parent)
{
	ibFieldSetPage& page = m_selectedPage;

	wxSplitterWindow* splitter = new wxSplitterWindow(parent, wxID_ANY, wxDefaultPosition,
		wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);

	// LEFT — everything the source can give, which is where a set is picked FROM.
	wxPanel* left = new wxPanel(splitter);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
	leftSizer->Add(new wxStaticText(left, wxID_ANY, _("Available fields")), 0, wxALL, FromDIP(4));
	page.m_sourceTree = new wxTreeCtrl(left, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_SINGLE);
	leftSizer->Add(page.m_sourceTree, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
	left->SetSizer(leftSizer);
	if (m_fieldSource) {
		m_fieldSource->Attach(page.m_sourceTree);
		m_fieldSource->Populate(page.m_sourceTree);
	}

	// RIGHT — what this node shows.
	//
	// ⭐ THE `Auto` ROW IS BACK, AND IT IS A ROW (2026-08-28). It was a SWITCH once, removed when a
	// node started adding instead of replacing — a flag that could only mean "do not replace" has
	// nothing to say once nothing replaces. What returns is the choice itself, in the table: it
	// stands where the inherited fields land, and taking it out is how a node states its whole
	// composition by hand. A flag could not say WHERE.
	wxPanel* right = new wxPanel(splitter);
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);

	const int idAdd    = ID_SELECTED_ADD;
	const int idRemove = ID_SELECTED_REMOVE;
	const int idCopy   = ID_SELECTED_COPY;
	const int idUp     = ID_SELECTED_UP;
	const int idDown   = ID_SELECTED_DOWN;

	wxToolBar* bar = new wxToolBar(right, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_FLAT | wxTB_NODIVIDER | wxTB_HORIZONTAL);
	bar->AddTool(idAdd, _("Add field"), ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("Add field"));
	bar->AddTool(ID_SELECTED_AUTO, _("Add «Auto»"), ibSettingsArt(wxASCII_STR(wxART_GO_DIR_UP), this),
		_("Everything the level above shows, in this place"));
	bar->AddTool(idRemove, _("Delete"), ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete"));
	bar->AddTool(idCopy, _("Copy"), ibSettingsArt(wxASCII_STR(wxART_COPY), this), _("Copy"));
	bar->AddSeparator();
	// THE ORDER OF THE SET IS THE ORDER IT READS IN, so it is moved, not re-entered.
	bar->AddTool(idUp, _("Move up"), ibSettingsArt(wxASCII_STR(wxART_GO_UP), this), _("Move up"));
	bar->AddTool(idDown, _("Move down"), ibSettingsArt(wxASCII_STR(wxART_GO_DOWN), this), _("Move down"));
	bar->Realize();
	rightSizer->Add(bar, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));

	page.m_view = new ibDataViewCtrl(right, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(page.m_view);
	page.m_model = new ibSelectedListModel([this]() -> std::vector<ibSelectedFieldDescription>* {
		return CurrentFieldSet();
	});
	page.m_view->AssociateModel(page.m_model);
	// ⭐ A LINE OF THIS LIST IS A FIELD, so it is edited the way every other field is: the shared
	// row-value cell, which opens the SAME picker the sort and the grouping open (Max, 2026-08-21:
	// there was no way to open the picker here as one can in the sort). Drawn as text it could only
	// be re-made — delete the line, add another — which is a different verb for "I picked the wrong
	// one", and one this window offers nowhere else.
	page.m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Field"),
		new ibRowValueCellRenderer(this, ibComposerFieldChooser(this),
			[this](const ibDataViewItem& row) -> ibValue {
				const std::vector<ibSelectedFieldDescription>* list = CurrentFieldSet();
				const int at = m_selectedPage.m_model != nullptr ? m_selectedPage.m_model->RowAt(row) : wxNOT_FOUND;
				// ⚠ THE `Auto` ROW IS NOT A FIELD and opens no picker: it names nothing, so there is
				// nothing to pick. It is added and removed as a row, which is the whole of editing it.
				return (list != nullptr && at != wxNOT_FOUND && (size_t)at < list->size()
				        && !(*list)[at].IsAuto())
					? ibValue(new ibValueCompositionField((*list)[at].m_path)) : ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				std::vector<ibSelectedFieldDescription>* list = CurrentFieldSet();
				const int at = m_selectedPage.m_model != nullptr ? m_selectedPage.m_model->RowAt(row) : wxNOT_FOUND;
				if (list == nullptr || at == wxNOT_FOUND || (size_t)at >= list->size())
					return;
				// CLEARED MEANS REMOVED — an empty line in a set of fields is not a field, and the
				// "×" beside the picker is the only place that says so.
				ibValueCompositionField* field = nullptr;
				if (value.ConvertToValue(field) && field != nullptr)
					(*list)[at] = ibSelectedFieldDescription::Field(field->GetPath());
				else
					list->erase(list->begin() + at);
				MarkSettingsTouched();
				ReloadFieldSets();
			}),
		ibSelectedListModel::kColText, FromDIP(260), wxAlignment::wxALIGN_LEFT));
	rightSizer->Add(page.m_view, 1, wxEXPAND | wxALL, FromDIP(4));
	right->SetSizer(rightSizer);

	splitter->SplitVertically(left, right, FromDIP(220));
	splitter->SetMinimumPaneSize(FromDIP(120));

	Bind(wxEVT_TOOL, [this](wxCommandEvent&) { OnFieldSetAdd(); }, idAdd);
	Bind(wxEVT_TOOL, [this](wxCommandEvent&) { OnFieldSetAuto(); }, ID_SELECTED_AUTO);
	Bind(wxEVT_TOOL, [this](wxCommandEvent&) { OnFieldSetRemove(); }, idRemove);
	Bind(wxEVT_TOOL, [this](wxCommandEvent&) { OnFieldSetCopy(); }, idCopy);
	Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveFieldSetRow(-1); }, idUp);
	Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveFieldSetRow(+1); }, idDown);

	// ⭐ FROM THE TREE INTO THE LIST, by hand. Double-clicking a field on the left puts it on the
	// right — the shortest gesture, and the one people try first; the toolbar's Add opens the same
	// picker for whoever reaches for a button instead.
	page.m_sourceTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& e) {
		AddFieldFromTree(e.GetItem());
		e.Skip();
	});
	// ...and by dragging onto the list, the same gesture the filter and the sort answer to.
	right->SetDropTarget(new ibCallbackDropTarget([this] {
		if (m_fieldSource)
			AddFieldFromTree(m_fieldSource->GetDragItem());
	}));
	return splitter;
}

// WHAT THE SELECTED NODE ADDS — its OWN list, never the pile. A node states what it contributes;
// what it ends up showing is the composer's answer (SelectedFor), and showing the pile here would
// mean a person deleting an inherited line and nothing happening.
std::vector<ibSelectedFieldDescription>* ibComposerSettingsPanel::CurrentFieldSet()
{
	if (ibLevelDescription* level = CurrentLevel())
		return &level->m_selected;

	const int output = m_currentNode.m_output;
	if (output >= 0 && (size_t)output < Structure().size())
		return &Structure()[output].m_selected;

	// ⭐⭐ THE REPORT ROW — and WHOSE table it is depends on which road this window is on.
	//
	// 🛑 IT WAS ALWAYS THE COMPOSITION'S, and on the READER road that is an object about to be
	// dropped: the caller keeps only the SETTING, so a person chose their columns, pressed OK and
	// lost them — no error, nothing written, nothing said (Max, 2026-08-28, live: "I set the selected
	// fields, pressed OK, they were not saved").
	//
	// The reader edits the SETTING's own table, which is saved with the rest of what they set; the
	// author edits the composition's, which is the report itself. The same division the filter and
	// the sort have had all along — this one part simply never got it.
	if (m_readerRoad)
		return &EditedSettings().m_selected;
	return &m_edited.m_selected;
}

void ibComposerSettingsPanel::OnFieldSetAdd()
{
	std::vector<ibSelectedFieldDescription>* fields = CurrentFieldSet();
	if (fields == nullptr)
		return;
	ibValueCompositionField* field = ChooseStructureField(this);
	if (field == nullptr)
		return;   // closed without picking

	// ADDING IS ADDING — nothing is cleared first. A node contributes this field on top of what its
	// output and the composition already show; it never takes over the list by naming one line.
	fields->push_back(ibSelectedFieldDescription::Field(field->GetPath()));
	MarkSettingsTouched();
	ReloadFieldSets();
}

// ⭐⭐ THE `Auto` ROW — added like any other row, and there is at most ONE of it: two would say
// "everything from above, twice", and the second one contributes nothing the first did not.
//
// It goes in at the CURSOR when there is one, because its position is its whole meaning: above the
// node's own fields the inherited ones come first, below them they come last.
void ibComposerSettingsPanel::OnFieldSetAuto()
{
	std::vector<ibSelectedFieldDescription>* fields = CurrentFieldSet();
	if (fields == nullptr)
		return;
	if (ibSelectedInherits(*fields))
		return;   // it is already there, and one is all it can mean

	const int at = SelectedFieldSetRow();
	const size_t where = (at == wxNOT_FOUND || (size_t)at > fields->size())
		? fields->size() : (size_t)at;
	fields->insert(fields->begin() + where, ibSelectedFieldDescription::Auto());
	MarkSettingsTouched();
	ReloadFieldSets();
}

void ibComposerSettingsPanel::OnFieldSetRemove()
{
	std::vector<ibSelectedFieldDescription>* fields = CurrentFieldSet();
	const int at = SelectedFieldSetRow();
	if (fields == nullptr || at == wxNOT_FOUND || (size_t)at >= fields->size())
		return;
	fields->erase(fields->begin() + at);
	MarkSettingsTouched();
	ReloadFieldSets();
}

// COPY A LINE — the third verb of the toolbar (Max: add, delete, copy). A field listed twice is
// not a mistake to refuse here: the same field can be wanted under two names, and what a duplicate
// means is the engine's answer, not this list's.
void ibComposerSettingsPanel::OnFieldSetCopy()
{
	std::vector<ibSelectedFieldDescription>* fields = CurrentFieldSet();
	const int at = SelectedFieldSetRow();
	if (fields == nullptr || at == wxNOT_FOUND || (size_t)at >= fields->size())
		return;
	fields->insert(fields->begin() + at + 1, (*fields)[at]);
	MarkSettingsTouched();
	ReloadFieldSets();
}

// MOVE A LINE — the set is read in order, so its order is a setting like any other.
void ibComposerSettingsPanel::MoveFieldSetRow(int delta)
{
	std::vector<ibSelectedFieldDescription>* fields = CurrentFieldSet();
	const int at = SelectedFieldSetRow();
	if (fields == nullptr || at == wxNOT_FOUND || (size_t)at >= fields->size())
		return;
	const int target = at + delta;
	if (target < 0 || (size_t)target >= fields->size())
		return;
	std::swap((*fields)[at], (*fields)[target]);
	MarkSettingsTouched();
	ReloadFieldSets();
	// The cursor travels with the line — a move whose result you have to go and find again reads
	// as a move that did nothing.
	ibFieldSetPage& page = m_selectedPage;
	if (page.m_view != nullptr && page.m_model != nullptr) {
		const ibDataViewItem row = page.m_model->ItemForRow((size_t)target);
		if (row.IsOk())
			page.m_view->Select(row);
	}
}

// A FIELD PICKED IN THE TREE goes into the set — the same act as Add, from the other side.
void ibComposerSettingsPanel::AddFieldFromTree(const wxTreeItemId& item)
{
	ibFieldSetPage& page = m_selectedPage;
	if (m_readOnly || page.m_sourceTree == nullptr || !item.IsOk())
		return;
	ibValueCompositionField* field = ibSettingsFieldTree::FieldAt(page.m_sourceTree, item);
	if (field == nullptr)
		return;   // a reference row is a road, not a field — double-clicking it unfolds instead

	std::vector<ibSelectedFieldDescription>* fields = CurrentFieldSet();
	if (fields == nullptr)
		return;
	fields->push_back(ibSelectedFieldDescription::Field(field->GetPath()));
	MarkSettingsTouched();
	ReloadFieldSets();
}

int ibComposerSettingsPanel::SelectedFieldSetRow()
{
	ibFieldSetPage& page = m_selectedPage;
	if (page.m_view == nullptr || page.m_model == nullptr)
		return wxNOT_FOUND;
	const ibDataViewItem row = page.m_view->GetSelection();
	return row.IsOk() ? page.m_model->RowAt(row) : wxNOT_FOUND;
}

// (⭐ NO "AUTO" HANDLER. The switch it served said "take the fields from above" — and under adding,
//  what a node states is ALWAYS on top of what is above it, so there is no other mode to switch to.
//  Its old body had to guess what "turning it off" should start from, which is a question that no
//  longer exists.)

void ibComposerSettingsPanel::ReloadFieldSets()
{
	if (m_selectedPage.m_model != nullptr)
		m_selectedPage.m_model->Rebuild();
}

void ibComposerSettingsPanel::OnGroupingFieldAdd(wxCommandEvent&)
{
	ibLevelDescription* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
	if (level == nullptr) {
		// SAID, NOT IGNORED: the page is only about a grouping, and pressing its button on the
		// report is a reasonable thing to try.
		wxMessageBox(_("Select a grouping in the structure above: these are the fields IT groups by."),
			_("Data composer settings"), wxOK | wxICON_INFORMATION, this);
		return;
	}
	ibValueCompositionField* field = ChooseStructureField(this);
	if (field == nullptr)
		return;   // closed without picking
	level->m_settings.m_group.m_lines.push_back({ field->GetPath(), ibQueryDimUnfold::Elements });
	MarkSettingsTouched();
	RefreshStructureText();
	ReloadGrouping();
}

void ibComposerSettingsPanel::AddGroupingFieldFromTree(const wxTreeItemId& item)
{
	ibLevelDescription* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
	if (m_readOnly || level == nullptr || m_groupingFieldTree == nullptr || !item.IsOk())
		return;
	ibValueCompositionField* field = ibSettingsFieldTree::FieldAt(m_groupingFieldTree, item);
	if (field == nullptr)
		return;   // a reference row unfolds instead of being taken
	level->m_settings.m_group.m_lines.push_back({ field->GetPath(), ibQueryDimUnfold::Elements });
	MarkSettingsTouched();
	RefreshStructureText();
	ReloadGrouping((int)level->m_settings.m_group.m_lines.size() - 1);
}

void ibComposerSettingsPanel::OnGroupingFieldRemove(wxCommandEvent&)
{
	ibLevelDescription* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
	const int at = SelectedGroupingField();
	if (level == nullptr || at == wxNOT_FOUND || (size_t)at >= level->m_settings.m_group.m_lines.size())
		return;
	level->m_settings.m_group.m_lines.erase(level->m_settings.m_group.m_lines.begin() + at);
	MarkSettingsTouched();
	RefreshStructureText();
	ReloadGrouping();
}

void ibComposerSettingsPanel::MoveGroupingField(int delta)
{
	ibLevelDescription* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
	const int at = SelectedGroupingField();
	if (level == nullptr || at == wxNOT_FOUND || (size_t)at >= level->m_settings.m_group.m_lines.size())
		return;
	const int target = at + delta;
	if (target < 0 || (size_t)target >= level->m_settings.m_group.m_lines.size())
		return;
	std::swap(level->m_settings.m_group.m_lines[at], level->m_settings.m_group.m_lines[target]);
	MarkSettingsTouched();
	RefreshStructureText();
	ReloadGrouping(target);
}

int ibComposerSettingsPanel::SelectedGroupingField() const
{
	if (m_groupingView == nullptr || m_groupingModel == nullptr)
		return wxNOT_FOUND;
	const ibDataViewItem row = m_groupingView->GetSelection();
	return row.IsOk() ? m_groupingModel->RowAt(row) : wxNOT_FOUND;
}

// THE TREE'S "Field" COLUMN SHOWS A LEVEL'S ELEMENTS, so it is refreshed when they change — and
// only then. Refreshing it costs the whole view its expanded state, so it is not something to do
// on the way past.
void ibComposerSettingsPanel::RefreshStructureText()
{
	if (m_structureModel == nullptr || m_structureView == nullptr)
		return;
	m_structureView->Refresh();
}

// SHOW THE GROUPING PAGE ONLY WHERE THERE IS A GROUPING. Taken off the notebook and put back at
// the front, rather than greyed: an empty page invites a click that can do nothing.
void ibComposerSettingsPanel::SyncGroupingPage()
{
	if (m_settingsTabs == nullptr || m_groupingPage == nullptr)
		return;

	// …AND THE DETAIL RECORDS ARE NOT ONE. That level groups by nothing by construction — a page
	// listing the fields it groups by would be a page that can only ever be empty, and the one verb
	// on it (add a field) would turn the rows back into a heading behind the author's back.
	// ⚠ AND THIS ONE ASKS THE KIND, not `IsDetailRecords()` — the ONE place where the two differ on
	// purpose. A level somebody just added has no fields yet, so it IS the records to the engine;
	// hiding its grouping page would leave nowhere to type the first field, and the level could
	// never stop being the records. The page goes only for a node DECLARED as records, which is a
	// decision, not an empty list.
	const ibLevelDescription* level = CurrentLevel();
	const bool wanted = level != nullptr && level->m_kind != ibCompositionLevelKind::Details;
	size_t at = m_settingsTabs->GetPageCount();
	for (size_t i = 0; i < m_settingsTabs->GetPageCount(); ++i)
		if (m_settingsTabs->GetPage(i) == m_groupingPage) { at = i; break; }
	const bool shown = at < m_settingsTabs->GetPageCount();

	if (wanted == shown)
		return;
	if (wanted) {
		m_settingsTabs->InsertPage(0, m_groupingPage, _("Grouping"), true);
		m_groupingPage->Show();
	}
	else {
		m_settingsTabs->RemovePage(at);   // REMOVE, not Delete — the page is ours and comes back
		m_groupingPage->Hide();
	}
}

void ibComposerSettingsPanel::ReloadGrouping(int select)
{
	SyncGroupingPage();
	if (m_groupingModel == nullptr || m_groupingView == nullptr)
		return;
	m_groupingModel->Rebuild();
	// ⚠ AND NOT THE STRUCTURE TREE. Rebuilding it clears the view — every node collapses and the
	// row that was just clicked stops responding, which is what "clicking a grouping folds it and
	// nothing works" was. The tree is rebuilt where its CONTENT changed (a field added, removed or
	// renamed), not on the way through a selection.
	if (select == wxNOT_FOUND)
		return;
	const ibDataViewItem row = m_groupingModel->ItemForRow((size_t)select);
	if (row.IsOk())
		m_groupingView->Select(row);
}

// THE SETTINGS OF THE SELECTED NODE — the platform's own editors, embedded. Selected fields /
// Filter / Sort, each with the field list on ITS left, shared by all of them: switching Sort to
// Filter changes what is on the right, never the list of fields on the left.
//
// Grouping is NOT among the pages asked for: the structure tree above is the composition's
// grouping, and a second ladder inside the panel would be the same setting edited in two places
// on one screen.
wxWindow* ibComposerSettingsPanel::BuildSettingsPane(wxWindow* parent)
{
	wxPanel* pane = new wxPanel(parent);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	m_settingsHeader = new wxStaticText(pane, wxID_ANY, wxEmptyString);
	sizer->Add(m_settingsHeader, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(4));

	// ⭐⭐ THE TWO SHARED EDITORS, over THIS window's buffer. Not the dynamic list's settings
	// window with some tabs switched off — that made a composition a case of a table model, which
	// it is not, and the cast that got it in there was the proof (Max, 2026-08-20: "two different
	// worlds").
	wxNotebook* tabs = m_settingsTabs = new wxNotebook(pane, wxID_ANY);

	// ⭐ THE GROUPING'S OWN FIELDS — first, because it is what a grouping IS before it is anything
	// else: it may be made of SEVERAL elements (Max), grouped by their tuple, and this is where
	// that list is composed.
	//
	// ⚠ AND IT IS SHOWN ONLY ON A GROUPING. On the report and on an output the page has nothing to
	// hold — "what grouping could there be here" (Max) — so it is taken off the notebook rather
	// than left standing empty, which reads as a page that is broken.
	m_groupingPage = BuildGroupingPage(tabs);
	tabs->AddPage(m_groupingPage, _("Grouping"), true);

	// ⭐⭐ WHAT THIS NODE SHOWS — ONE PAGE, AND THE ONLY ONE THERE EVER SHOULD HAVE BEEN.
	//
	// There were two, "Available" and "Selected", and the objection to them was right the first time:
	// two nearly identical lists, and the person has to work out which they meant. It was answered by
	// explaining the difference instead of removing it. The difference does not survive the question
	// "what does a person want to say here" (Max, 2026-08-24: "available tells us nothing, it is the
	// same thing understood in a harder way") — they want to say which fields they want to see, and
	// that is this list. `ibDataComposer::SelectedFor(output[, level])` builds the query's SELECT out
	// of it, piling the composition's, the output's and the node's own together.
	//
	// The other page said what MAY be reached, had no reader on the run path at all, and cost a
	// parameter on every verb of this window to tell the two apart.
	tabs->AddPage(BuildFieldSetPage(tabs), _("Selected fields"), false);

	m_filterEditor = new ibFilterEditor(tabs, &EditedSettings().m_filter, m_fieldSource.get());
	tabs->AddPage(m_filterEditor, _("Filter"), false);
	m_sortEditor = new ibSortEditor(tabs, &EditedSettings().m_sort, m_fieldSource.get());
	tabs->AddPage(m_sortEditor, _("Sort"), false);

	// ⭐ THESE TWO EDIT A BUFFER, so nothing they do reaches the composition until this window is
	// accepted — but the CHANGE is a fact the moment it is made (Max, 2026-08-20: "we changed the
	// value, we do not have to press OK"). So they say so as it happens, and the buffer landing at
	// commit is gated on the same fact: a tab opened and closed without touching anything writes
	// nothing and announces nothing.
	//
	// ⚠ Wired from HERE, not inside the editors: they are shared with the list's world, which is
	// left exactly as it was — an editor nobody wired behaves as it always did.
	m_filterEditor->SetOnChanged([this] { MarkSettingsTouched(); });
	// ⭐ WHOSE WINDOW THIS IS — the same question the list's panel asks, and the same answer: a
	// setting handed in means a reader, and an inaccessible line is hidden from them.
	m_filterEditor->SetAuthoring(!m_readerRoad);
	m_sortEditor->SetOnChanged([this] { MarkSettingsTouched(); });
	sizer->Add(tabs, 1, wxEXPAND);

	pane->SetSizer(sizer);
	return pane;
}

// WHAT THE LEVELS FOLD. A level says "break here"; a resource says "and add this up". The two are
// separate pages because they are separate decisions — the same table folded by warehouse can carry
// a sum of quantity, a count of documents, or both, and changing one has nothing to do with the other.
wxWindow* ibComposerSettingsPanel::BuildResourcePage(wxWindow* parent)
{
	// ⭐ THE SAME LANGUAGE AS THE OUTPUT PAGE (2026-08-19): a splitter between the two halves, and a
	// TOOLBAR over the list that is being built. The column of ">" / "<" / "..." buttons floating in
	// the middle was the odd one out in this window — it read as three unlabelled arrows between two
	// panes, and it fixed the proportion of those panes at exactly half and half.
	wxSplitterWindow* split = new wxSplitterWindow(parent, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	split->SetMinimumPaneSize(FromDIP(140));

	// ---- LEFT: the fields this composition offers ----------------------------
	// Resources carry their own field list because they are their own screen; the Output page's
	// fields live in ITS lower area, shared by its sections.
	wxPanel* leftPane = new wxPanel(split);
	wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);
	left->Add(new wxStaticText(leftPane, wxID_ANY, _("Available fields")), 0, wxALL, FromDIP(4));
	m_resourceFieldTree = CreateFieldTree(leftPane);
	left->Add(m_resourceFieldTree, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(4));
	leftPane->SetSizer(left);

	// ---- RIGHT: what is folded, and the verbs that build it ------------------
	wxPanel* rightPane = new wxPanel(split);
	wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);

	wxToolBar* bar = m_resourceBar = new wxToolBar(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
	bar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
	// ⭐ NO AGGREGATE CHOOSER ON THE BAR ANY MORE. Adding a field now lands the FIRST aggregate the
	// engine admits for its type — `SUM(Amount)`, `COUNT(Description)` — and the choice of which one
	// lives in the row's Expression cell, where the answer is about that row. A chooser on the
	// toolbar was a second place stating what a type may be folded by, one gesture away from the
	// cell that states it properly.
	bar->AddTool(ID_RESOURCE_ADD, _("Add"),
		ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("Add resource"));
	// The way out of the ready list, for the row under the cursor — the same editor the cell's "..."
	// opens, reachable without entering the cell first.
	bar->AddTool(ID_RESOURCE_EXPR, _("Expression"),
		ibSettingsArt(wxASCII_STR(wxART_EDIT), this), _("Expression..."));
	bar->AddTool(ID_RESOURCE_REMOVE, _("Delete"),
		ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete resource"));
	bar->Realize();
	right->Add(bar, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(4));

	m_resourceView = new ibDataViewCtrl(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(m_resourceView);
	m_resourceModel = new ibResourceModel([this] { return &m_edited.m_resources; },
	                                      [this] { return ResourceScopeNames(); });
	m_resourceView->AssociateModel(m_resourceModel);
	m_resourceView->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Field"),
		new ibDataViewTextRenderer(), ibResourceModel::kColField, FromDIP(150), wxAlignment::wxALIGN_LEFT));

	// ⭐ THE SAME CELL THE QUERY CONSTRUCTOR'S TOTALS TAB USES (Max, 2026-08-19: "look at the query
	// constructor"). A resource expression is nearly always one of the calls the engine admits over
	// THIS row's field, so the cell offers those whole — `SUM(Amount)`, `COUNT(Amount)` — and the
	// "..." beside the list opens the full expression editor for everything else: a ratio, a
	// restricted measure, anything the ready calls do not cover. Quick choice and free text side by
	// side, and neither takes the other away.
	//
	// It is the SAME class, not a lookalike: which aggregates fit a type is one answer
	// (`ibQueryLowering::AggregatesFor`), and a second cell over it would drift from this one.
	m_resourceView->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Expression"),
		new queryctor::ibExpressionCellRenderer(
			[this]() -> wxArrayString { return ResourceChoices(); },
			[this](wxString& text) -> bool { return EditResourceExpression(text); },
			wxDATAVIEW_CELL_EDITABLE),
		ibResourceModel::kColExpression, FromDIP(260), wxAlignment::wxALIGN_LEFT));
	// ⭐ AND WHAT THE FIGURE IS CALLED ON THE REPORT. Empty means "after its argument", which is what
	// `SUM(Amount)` reads as — and the cell is where a person overrules that, instead of discovering
	// a name the engine invented to dodge a collision with a grouping (Max, 2026-08-26).
	// ⛔ NO "ALIAS" COLUMN. A column of the result is NAMED WHERE COLUMNS ARE NAMED — in the selection
	// — and the figure is read back under the name of the field it is a reading of. A second name
	// written beside the resource was a duplicate of that one, and duplicates drift the day somebody
	// renames the field (Max, 2026-08-27, on the same column in the query constructor and then here).
	//
	// ⚠ The stored alias is untouched: `ibResourceDescription::m_alias` still travels as `AS name`,
	// so a report that has one keeps it and keeps working. What is gone is the second door to it.
	// ⭐⭐ …AND OVER WHICH GROUPING IT IS COMPUTED — the reason all of this was built. A resource
	// normally folds by the ladder and means one figure per heading; named a grouping, it is computed
	// there and stays constant inside it, which is how a share gets its denominator and how a group
	// reads its neighbour's total.
	//
	// The choices are THIS composition's own groupings, so nothing can be named that the report does
	// not declare — and picking is one gesture, as everywhere else in this window.
	// THE SAME PICKER THE CONSTRUCTOR USES — the ticked tree, where a separator is a node and its
	// levels hang inside it. A dropdown was the first shape of this cell and it was wrong for the
	// question: choosing an area is choosing PLACES, possibly several, and a flat list can neither
	// show where a level lives nor let two be marked (Max, 2026-08-27).
	m_resourceView->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Computed over"),
		new queryctor::ibExpressionCellRenderer(
			[this]() -> wxArrayString { return ResourceScopeChoices(); },
			[this](wxString& text) -> bool {
				return ibPickGroupingScope(this, ResourceScopeNames(), ResourceScopeSeparators(), text);
			},
			wxDATAVIEW_CELL_EDITABLE),
		ibResourceModel::kColScope, FromDIP(160), wxAlignment::wxALIGN_LEFT));
	right->Add(m_resourceView, 1, wxALL | wxEXPAND, FromDIP(4));
	rightPane->SetSizer(right);

	// The list of fields is long and the list of resources is short — but which of them wants the
	// room is the user's business, so the sash is only a starting point.
	split->SplitVertically(leftPane, rightPane, FromDIP(260));
	return split;
}


// THE PARAMETERS PAGE — what the query asks for, and how each one behaves.
//
// The list follows the TEXT: writing `&Period` puts Period here the moment the query is applied.
// A parameter can also be added by hand — for a common module to read, or because the text is still
// being written — and only a hand-made one can be renamed or removed here (an auto one is named by
// the query, and removing it would last exactly until the next re-parse).
wxWindow* ibComposerSettingsPanel::BuildParameterPage(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxToolBar* bar = m_parameterBar = new wxToolBar(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
	bar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
	bar->AddTool(ID_PARAM_ADD, _("Add"),
		ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("Add parameter"));
	bar->AddTool(ID_PARAM_REMOVE, _("Delete"),
		ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete parameter"));
	bar->Realize();
	sizer->Add(bar, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(4));

	m_parameterView = new ibDataViewCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(m_parameterView);
	m_parameterModel = new ibParameterModel(
		[this] { return &m_edited.m_parameters; },
		[this]() -> const ibMetaData* { return GetEditedMetaData(); });
	m_parameterView->AssociateModel(m_parameterModel);

	m_parameterView->GetRootColumnGroup()->AppendTextColumn(_("Parameter"), ibParameterModel::kColName,
		wxDATAVIEW_CELL_INERT, FromDIP(170), wxAlignment::wxALIGN_LEFT);
	m_parameterView->GetRootColumnGroup()->AppendTextColumn(_("Value"), ibParameterModel::kColValue,
		wxDATAVIEW_CELL_EDITABLE, FromDIP(160), wxAlignment::wxALIGN_LEFT);
	// THE EXPRESSION IS EVALUATED BEFORE THE READ and its result becomes the value — which is why a
	// call into a common module is legitimate here and a scripted FIELD is not: this runs once.
	// One line in the cell, and "..." opens the same text with room to write it.
	m_parameterView->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Expression"),
		new ibTextWithDotsRenderer(this,
			[this](wxString& text) -> bool { return EditParameterExpression(text); }),
		ibParameterModel::kColExpression, FromDIP(200), wxAlignment::wxALIGN_LEFT));
	// THE DECLARED TYPE — what the value is adjusted to. Empty means "whatever the expression
	// produces". Edited through the PRODUCT'S type picker (the same dialog an attribute's Type
	// opens), because "which types may this hold" has one answer and one window.
	m_parameterView->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Type"),
		new ibTextWithDotsRenderer(this,
			[this](wxString& text) -> bool { return EditParameterType(text); }),
		ibParameterModel::kColType, FromDIP(150), wxAlignment::wxALIGN_LEFT));
	// WHO FILLS IT IN — the author, or the person reading the report. Kept as its own column because
	// it is a different question from what the parameter holds.
	m_parameterView->GetRootColumnGroup()->AppendToggleColumn(_("For user"), ibParameterModel::kColUser,
		wxDATAVIEW_CELL_ACTIVATABLE, FromDIP(80), wxAlignment::wxALIGN_CENTER);

	sizer->Add(m_parameterView, 1, wxALL | wxEXPAND, FromDIP(4));
	panel->SetSizer(sizer);

	// RIGHT-CLICK OFFERS WHAT THE TOOLBAR OFFERS — every list in this window answers the right hand.
	m_parameterView->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibComposerSettingsPanel::OnParameterContextMenu, this);
	m_parameterView->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](ibDataViewEvent& e) {
		if (m_parameterView != nullptr)
			m_parameterView->EditItem(e.GetItem(), e.GetDataViewColumn());
		e.Skip();
	});

	return panel;
}

// ---------------------------------------------------------------------------
//  Parameter commands
// ---------------------------------------------------------------------------

int ibComposerSettingsPanel::SelectedParameter() const
{
	return ibSelectedRow(m_parameterView);
}

void ibComposerSettingsPanel::ReloadParameters()
{
	if (m_parameterModel != nullptr)
		m_parameterModel->ResetFromList();
}



// THE "..." BEHIND THE EXPRESSION CELL — the same expression, with room to write it.
//
// A parameter is usually one line (`CurrentDate()`, a call into a common module), and one line is
// what the cell shows. What does not fit is written here: the SAME text, in a box big enough to read
// it. No field tree, no palette — it is the LANGUAGE, not a query, and the value it produces is what
// the parameter takes (see EvaluateParameters).

// THE TYPE PICKER — the product's own, the same dialog an attribute's Type opens. The cell hands us

// ⭐ THE WINDOW'S OPENING ACT: make sure the names an expression may call EXIST.
//
// They live on the module MANAGER of the configuration this composition belongs to — the built-in
// globals are methods of scope-context values bound there (BindScopeVariable), and a compile context
// resolves a call by walking off the end of its own chain into the parent's BYTECODE. A manager that
// was never compiled has none, so nothing resolves.
//
// ⚠ AND IT IS DONE HERE, NOT DURING A CHECK. Compiling recompiles the module chain and refreshes
// what is bound to it; run from inside a cell editor's "…", that rebuild destroys the renderer whose
// button is still on the stack (crash dump, 2026-08-21). Once, at the start, with nothing open.
//
// A configuration whose own modules do not compile is a separate complaint and not this window's to
// make — whatever names did get in are what expressions are then checked against.
void ibComposerSettingsPanel::PrepareModuleContext()
{
	const ibMetaData* metaData = GetEditedMetaData();
	if (metaData == nullptr)
		return;
	if (ibValueModuleManager* manager = ibSession::EditModuleManagerFor(metaData))
		if (ibCompileModule* context = manager->GetCompileModule()) {
			// SWALLOWED FOR THE USER, WRITTEN DOWN FOR US. A configuration whose own modules do not
			// compile is not this window's complaint to make — but "the expression check knows fewer
			// names than it should" is invisible otherwise, and the reason is exactly this line.
			try { context->Compile(); }
			catch (const ibBackendException& error) {
				ibJournalInfo(wxT("ui"), wxT("composer settings: module manager did not compile - %s"),
					error.GetErrorDescription());
			}
		}
}

// COMPILE AN EXPRESSION AND SAY WHY NOT — the ONE check, used by the editor's OK and by the window's.
//
// Compiling, never running: the translator needs no runtime, and in the Designer there is none (a
// session that runs no scripts has no root module). An evaluation-based check silently passed
// everything there — `ibProcUnit::Evaluate` returns false without a context and throws nothing.
//
// The text is compiled as the BODY OF A FUNCTION so it is judged as an expression; compiled loose,
// a stray assignment would pass for a module statement.
bool ibComposerSettingsPanel::CheckExpression(const wxString& expression, wxString& complaint,
	const ibMetaData* metaData)
{
	complaint.clear();
	if (expression.IsEmpty())
		return true;   // no expression is not an error

	try {
		const bool braces = ibCompileCode::GetCodeStyle() == CODE_CES;
		const wxString wrapped = braces
			? wxT("function __checkExpression() { return ") + expression + wxT("; }")
			: wxT("Function __checkExpression() Return ") + expression + wxT("; EndFunction");

		ibCompileCode check;

		// ⭐ THE PARENT COMES FROM THE COMPILE CACHE OF THE ROOT DESCRIPTOR (Max, 2026-08-19: "you
		// have to pull the module cache out of the root descriptor and attach to it") — the same road
		// the code editor's own intellisense takes: metaData → GetCompileCache() →
		// FindCompileModule(metaObject) → GetCompileModule().
		//
		// `SetParent` wires the BYTECODE chain, and that chain is what name resolution walks; with no
		// parent not one name exists, so `CurrentDate()` reads back as "procedure or function not
		// detected" — a true statement about an empty world and a useless one about this expression.
		// ⭐ THE CONCRETE OBJECT'S OWN METADATA, and nothing else (Max, 2026-08-21). The composition
		// hands in the container it belongs to; whatever configuration happens to be open globally is
		// a different world, and compiling against it would pass or fail for reasons invisible from
		// here. Null means "no container was named" — the session road below still answers.
		const ibMetaData* against = metaData;
		// ⭐⭐ THE MODULE MANAGER IS THE PARENT — not the configuration's root module.
		//
		// `CurrentDate()` is not a function of any module: it is a METHOD of a scope-context value
		// (`SystemManager`, bound by BindScopeVariable), and those values live on the MODULE MANAGER.
		// Parenting to the configuration's own module therefore compiled against a world where no
		// built-in exists — "procedure or function not detected (currentdate)" about a function every
		// module can call (seen live 2026-08-21).
		//
		// `GetEditModuleManager` is the one seam for this, with the two roads already inside it: in
		// the Designer the lightweight manager held in this metadata's compile cache, at run time the
		// session's root. Asking it means this window neither branches on the mode nor names either.
		// ⚠⚠ THE PARENT IS ONLY ATTACHED HERE — COMPILING IT IS THE WINDOW'S OPENING ACT
		// (PrepareModuleContext), and it must NOT happen from inside this call.
		//
		// It did for one build, and the Designer crashed: this check runs from the "…" of a cell
		// EDITOR, compiling the manager rebuilds the modules and refreshes what is bound to them,
		// the parameter grid's columns are rebuilt — and the renderer whose button is mid-call is
		// destroyed under itself. The dump lands in ibTextWithDotsRenderer::OnExpand, calling a
		// std::function whose target is gone.
		//
		// So the expensive, side-effecting half happens once when the window opens, and the check
		// itself only reads what is already there.
		if (ibValueModuleManager* manager = ibSession::EditModuleManagerFor(against))
			if (ibCompileModule* context = manager->GetCompileModule())
				check.SetParent(context);
		// ⚠ AND NO ROAD THROUGH THE GLOBAL ACTIVE METADATA. The container the expression belongs to is
		// the authority on which names it may call; reaching past it for whatever configuration
		// happens to be open would compile against a world this expression does not live in — and
		// would pass or fail for reasons nobody can see from here.

		check.Compile(wrapped);
	}
	catch (const ibBackendException& error) {
		complaint = error.GetErrorDescription();
		return false;
	}
	return true;
}

// EVERY PARAMETER'S EXPRESSION, checked at once — what the window asks before it closes.
//
// ⭐ BECAUSE A CELL CAN BE EDITED WITHOUT OPENING THE EDITOR (Max, 2026-08-19: "if I change the
// expression field by hand, without going into the editor, it should say on the way out that there
// are errors"). The editor's OK guards one expression; this guards the window.
wxString ibComposerSettingsPanel::CheckAllExpressions()
{
	wxString complaints;

	for (size_t i = 0; i < m_edited.m_parameters.size(); ++i) {
		wxString complaint;
		// ⭐ THE OBJECT'S OWN CONFIG, through the attach chain — NOT the query's SOURCE config. A
		// parameter expression is SCRIPT, and the names it may call are the ones the configuration
		// this composition lives in declares; where the query reads its rows from is a different
		// question that happens to have the same answer most of the time (Max, 2026-08-21).
		if (CheckExpression(m_edited.m_parameters[i].m_expression, complaint, GetEditedMetaData()))
			continue;
		complaints += m_edited.m_parameters[i].m_name + wxT(": ") + complaint + wxT("\n");
	}
	return complaints;
}
// text only because that is what a cell carries; what is edited is the DECLARATION on the parameter.
bool ibComposerSettingsPanel::EditParameterType(wxString& text)
{
	const int idx = SelectedParameter();
	if (idx == wxNOT_FOUND)
		return false;

	if ((size_t)idx >= m_edited.m_parameters.size())
		return false;
	ibTypeDescription declared = m_edited.m_parameters[idx].m_type;
	if (!ibShowTypeSelector(this, ibSelectorDataType::ibSelectorDataType_any, std::vector<ibClassID>(),
			declared, GetEditedMetaData(), /*allowEdit*/true, /*single*/false))
		return false;

	m_edited.m_parameters[idx].m_type = declared;
	MarkSettingsTouched();
	ReloadParameters();
	text = ibDescribeTypes(declared, GetEditedMetaData());
	return true;
}
bool ibComposerSettingsPanel::EditParameterExpression(wxString& text)
{
	wxDialog dlg(this, wxID_ANY, _("Expression editor"), wxDefaultPosition, FromDIP(wxSize(620, 380)),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

	// THE PRODUCT'S OWN CODE EDITOR, SET UP THE WAY EVERY OTHER ONE IS — the shared font and colour
	// settings and the shared editor settings (line numbers among them), taken from the main frame.
	//
	// ⚠ WITHOUT THOSE TWO CALLS THE EDITOR IS PLAIN TEXT. The highlighting is not built into the
	// control: it styles what it lexes using the colours it was GIVEN, so a bare `new ibCodeEditor`
	// shows black text on white and no line numbers — which is exactly what the first version did
	// (Max: "there is no highlighting here"). Every other place that shows code calls this pair.
	//
	// The module DOCUMENT stays null: it is needed for load / save / syntax-check against a module,
	// none of which a parameter expression has — highlighting needs none of it.
	ibCodeEditor* editor = new ibCodeEditor(nullptr, &dlg, wxID_ANY);
	if (mainFrame != nullptr) {
		editor->SetEditorSettings(mainFrame->GetEditorSettings());
		editor->SetFontColorSettings(mainFrame->GetFontColorSettings());
	}
	editor->SetText(text);

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(editor, 1, wxALL | wxEXPAND, FromDIP(6));
	sizer->Add(dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, FromDIP(6));
	dlg.SetSizer(sizer);

	dlg.Bind(wxEVT_INIT_DIALOG, [editor](wxInitDialogEvent& e) { editor->SetFocus(); e.Skip(); });
	// OK COMPILES THE EXPRESSION — the same check the window runs on the way out, so the two cannot
	// disagree about what is valid. A failure keeps this window open on the text that failed.
	// The object's own config — see CheckAllExpressions: a parameter expression is script.
	const ibMetaData* metaData = GetEditedMetaData();
	dlg.Bind(wxEVT_BUTTON, [&dlg, editor, metaData](wxCommandEvent& e) {
		wxString complaint;
		if (!CheckExpression(editor->GetText(), complaint, metaData)) {
			wxMessageBox(complaint, _("Expression editor"), wxOK | wxICON_WARNING, &dlg);
			return;
		}
		dlg.EndModal(wxID_OK);
	}, wxID_OK);


	if (dlg.ShowModal() != wxID_OK)
		return false;
	text = editor->GetText();

	// ⭐⭐ AND THE VALUE IS WRITTEN HERE, not left for the cell to commit (Max, 2026-08-21: "I press
	// OK and the value is not written into the expression").
	//
	// The modal takes the focus, the grid closes the cell editor behind it, and there is then no
	// editor left for the grid to read a value out of — so the text made it back to the caller and
	// no further. The TYPE cell beside this one never had the problem because it writes the
	// declaration itself and only hands the caption back; this one now does the same.
	const int idx = SelectedParameter();
	if (idx != wxNOT_FOUND && (size_t)idx < m_edited.m_parameters.size()) {
		m_edited.m_parameters[idx].m_expression = text;
		MarkSettingsTouched();
		ReloadParameters();
	}
	return true;
}
void ibComposerSettingsPanel::OnParameterContextMenu(ibDataViewEvent& event)
{
	// VIEW ONLY — no menu at all rather than a menu of greyed items: this is the SECOND road to the
	// verbs the toolbar above already refuses, and offering it half-open only invites the click.
	if (m_readOnly)
		return;
	if (m_parameterView != nullptr && event.GetItem().IsOk())
		m_parameterView->Select(event.GetItem());

	const int idx = SelectedParameter();
	// An AUTO parameter cannot be removed here — greyed rather than refusing after the click.
	const bool removable = idx != wxNOT_FOUND && (size_t)idx < m_edited.m_parameters.size()
	                    && !m_edited.m_parameters[idx].m_fromQuery;

	wxMenu menu;
	ibAppendCmd(menu, ID_PARAM_ADD, _("Add parameter"), wxASCII_STR(wxART_NEW), this);
	ibAppendCmd(menu, ID_PARAM_REMOVE, _("Delete parameter"), wxASCII_STR(wxART_DELETE), this)
		->Enable(removable);

	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnParameterAdd(e); }, ID_PARAM_ADD);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnParameterRemove(e); }, ID_PARAM_REMOVE);
	PopupMenu(&menu);
}
// A HAND-MADE PARAMETER IS NAMED WHEN IT IS ADDED — the name is what the query will refer to, so an
// unnamed row would be a row nothing can use.
void ibComposerSettingsPanel::OnParameterAdd(wxCommandEvent&)
{
	const wxString name = wxGetTextFromUser(_("Parameter name"), _("Add parameter"), wxEmptyString, this);
	if (name.IsEmpty())
		return;

	// A NAME IS TAKEN OR IT IS NOT — the same name twice would be two rows the query cannot tell
	// apart. An existing one is simply selected: that IS what the person asked for.
	std::vector<ibParameterDescription>& parameters = m_edited.m_parameters;
	size_t added = parameters.size();
	for (size_t i = 0; i < parameters.size(); ++i)
		if (parameters[i].m_name.IsSameAs(name, false)) { added = i; break; }
	if (added == parameters.size()) {
		ibParameterDescription parameter;
		parameter.m_name = name;   // hand-made: m_fromQuery stays false, which is what makes it removable
		parameters.push_back(std::move(parameter));
		MarkSettingsTouched();
	}

	ReloadParameters();
	if (m_parameterView != nullptr)
		m_parameterView->Select(ibDataViewItem(reinterpret_cast<void*>(added + 1)));
}

void ibComposerSettingsPanel::OnParameterRemove(wxCommandEvent&)
{
	const int idx = SelectedParameter();
	if (idx == wxNOT_FOUND || (size_t)idx >= m_edited.m_parameters.size())
		return;
	// 🛑 AN AUTO PARAMETER IS NOT REMOVED HERE — it is in the query text. Said out loud rather than
	// silently doing nothing, because the row looks exactly like a removable one.
	if (m_edited.m_parameters[idx].m_fromQuery) {
		wxMessageBox(_("This parameter comes from the query text. Remove it from the query instead."),
			_("Parameters"), wxOK | wxICON_INFORMATION, this);
		return;
	}
	m_edited.m_parameters.erase(m_edited.m_parameters.begin() + idx);
	MarkSettingsTouched();
	ReloadParameters();
}
wxWindow* ibComposerSettingsPanel::BuildQueryPage(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	// ⚠ ASCII ONLY IN UI LITERALS. The file has no BOM, so MSVC reads it as ANSI and a UTF-8 dash
	// arrives on screen as three mojibake characters. Seen live 2026-08-18 on this very label.
	wxBoxSizer* head = new wxBoxSizer(wxHORIZONTAL);
	head->Add(new wxStaticText(panel, wxID_ANY, _("Query text - what the composition reads")),
		0, wxALIGN_CENTER_VERTICAL);
	head->AddStretchSpacer();
	// THE CONSTRUCTOR EDITS THIS VERY TEXT and writes it back — the same door the dynamic list's
	// query tab opens, so a composition is authored with the tool that already exists.
	head->Add(new wxButton(panel, ID_QUERY_BUILD, _("Query constructor")), 0);
	sizer->Add(head, 0, wxALL | wxEXPAND, FromDIP(4));
	// THE SAME STYLED EDITOR the query constructor and the list settings use — SQL lexer, the
	// language's own keyword set, line numbers. A query is query text wherever it is shown, and
	// showing it as grey characters HERE while it is highlighted THERE is one language wearing
	// two faces.
	m_queryText = new wxStyledTextCtrl(panel, wxID_ANY);
	ibStyleQueryText(m_queryText);
	// THE TEXT IS THE DESCRIPTION'S, and there is no second copy of it to fall out of step with.
	m_queryText->SetText(m_edited.m_query);
	// ⭐ BOUND AFTER THE FILL, deliberately: the initial SetText is not somebody typing, and a
	// handler bound before it would report the composition as modified the instant it was opened.
	// No flag is needed to tell the two apart — the order does it.
	m_queryText->Bind(wxEVT_STC_MODIFIED, &ibComposerSettingsPanel::OnQueryTextChanged, this);
	sizer->Add(m_queryText, 1, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(4));

	// THE ENGINE'S OWN WORDS, verbatim — a query that cannot be described cannot be run, and
	// hearing that here, from its author's chair, is the whole point of describing it early.
	m_queryError = new wxStaticText(panel, wxID_ANY, wxEmptyString);
	m_queryError->SetForegroundColour(*wxRED);
	m_queryError->Hide();
	sizer->Add(m_queryError, 0, wxALL | wxEXPAND, FromDIP(4));

	// ⭐ NO "APPLY" BUTTON. It was the second place a query lived: what the editor held and what the
	// composition held were two facts that a button reconciled, and until it was pressed a save wrote
	// the older one. The text now lands as it is typed and is re-read when the typing stops, so there
	// is nothing left for the button to do (Max, 2026-08-20).

	panel->SetSizer(sizer);
	return panel;
}

// ---------------------------------------------------------------------------
//  Fields and levels
// ---------------------------------------------------------------------------

// THE FIELD PICKER, forwarded to the one thing that knows which fields this composition offers.
// The structure pane deliberately has no field tree of its own, so this modal picker IS the way a
// field gets in.
ibValueCompositionField* ibComposerSettingsPanel::ChooseStructureField(wxWindow* parent, const wxString& held)
{
	return m_fieldSource != nullptr ? m_fieldSource->ChooseField(parent != nullptr ? parent : this, held) : nullptr;
}

// See the header for why this is a question and not a list, and why "contains" rather than "is".
bool ibComposerSettingsPanel::StructureFieldIsDated(const wxString& path) const
{
	if (path.IsEmpty())
		return false;   // no field named yet — there is nothing to offer periodicity ON
	for (const ibQueryConstructorField& field : m_fieldList)
		if (field.m_name.IsSameAs(path, false))
			return !field.m_type.IsOk() || field.m_type.ContainType(g_valueDateCLSID);
	// A dot-walk, or a field of a source this window has not read: unknown, so not refused.
	return true;
}

// WHICH FIELDS THIS COMPOSITION OFFERS — its own explorer, which is what its query resolved to.
void ibComposerSettingsPanel::BindFieldSource()
{
	// ⚠ BOTH STATEMENTS ARE GUARDED. The second one used to sit outside the `if` for want of a pair
	// of braces — a null field tree took the source check and then dereferenced anyway.
	if (m_fieldSource == nullptr)
		return;

	// ⭐ THE FIELDS AS THE QUERY GIVES THEM — a FLAT list, which is what a parsed text has to offer.
	// A reference among them still unfolds: what a value may BE is a question about its TYPE, and the
	// tree asks the configuration that (see SetPlainFields). With no configuration to ask, the
	// primitive types are all anything can be — which is a smaller window, not a broken one.
	std::vector<ibSettingsPlainField> plain;
	plain.reserve(m_fieldList.size());
	for (const ibQueryConstructorField& field : m_fieldList)
		// NO metaID — a field of a PARSED TEXT stands behind no metaobject attribute, and the type is
		// what says whether it unfolds anyway. wxNOT_FOUND is what "there is no such id" means here.
		plain.push_back({ field.m_name, wxNOT_FOUND, field.m_type });
	m_fieldSource->SetPlainFields(std::move(plain), GetEditedMetaData());
	// ⭐ AND WHICH OF THOSE FIELDS ARE RESOURCES — asked of the composition every time the tree
	// draws, never copied into it. Being a resource is a DECLARATION this window makes on the
	// Resources tab; a list handed over once would be a second copy, and it would still say
	// "attribute" the moment somebody adds one (Max, 2026-08-22).
	m_fieldSource->SetResourceTest([this](const wxString& path) {
		// FROM THE DESCRIPTION — where the resources are declared and saved from. They were read off
		// the running composer, which is filled from the description at a RUN, so before the first
		// one every field answered "not a resource" however many had just been added.
		for (const ibResourceDescription& resource : m_edited.m_resources)
			if (resource.m_path.IsSameAs(path, false))
				return true;
		return false;
	});
	// (⭐ AND NOTHING NARROWS WHAT THE PICKERS OFFER. There used to be a second test here, over the
	//  "available" set: what this node MAY be shown at all. It is gone with the set — the source
	//  offers what it has, and choosing among that is what SELECTED says. A narrowing that only
	//  decided which names a person could type was a rule to maintain, not an answer to anything.)
}

// ACCEPTING WHAT IS ON SCREEN. There is no description to assign anywhere: this window edits one BY
// REFERENCE and has been writing into it all along. What is left is the two things that only exist
// while the window is open — the structure buffer and "which variant is in force" — and landing them
// is what makes the description whole. FALSE = objected to, stay where you are.
bool ibComposerSettingsPanel::CommitSettings()
{
	if (!ValidateEditedSettings())
		return false;

	// ⭐⭐ A SETTING WAS HANDED IN: somebody is configuring THIS run, not writing the report. It is
	// copied into the setting they gave us and the caller decides what that means — the report is
	// left exactly as its author wrote it, and the whole session is undone later by dropping that one
	// setting (Max, 2026-08-23: the gridbox is the other host).
	if (m_readerRoad)
		return true;   // …edited in place all along, and nothing about the report was touched

	// (No node buffers to commit: a level's filter and sort are ON the level, inside the structure
	//  buffer, and they landed there as they were typed.)
	CaptureIntoActiveVariant();
	return true;
}

// THE SAME CHECK THE RUNTIME MAKES. A half-written line raises there; here that exception becomes a
// warning and the window stays open on the offending setting, instead of closing and quietly
// dropping it. Its own function because TWO moments ask it: accepting the window, and LEAVING a
// variant — the second writes nothing to the composition and still must not carry a broken line
// into a variant nobody is looking at any more.
bool ibComposerSettingsPanel::ValidateEditedSettings()
{
	try {
		ibValidateSettings(EditedSettings());
	}
	catch (const ibBackendException& err) {
		wxMessageBox(err.GetErrorDescription(), _("Data composer settings"), wxOK | wxICON_WARNING, this);
		return false;
	}
	return true;
}

// ⭐⭐ WHAT IS ON SCREEN BELONGS TO THE VARIANT IT WAS WRITTEN IN — over the COPY, which is the only
// place this window edits. A variant holds the settings AND the structure, and both are in force
// outside it while it is the active one (that is what "active" means), so landing them is what makes
// them the variant's own.
//
// 🛑 IT USED TO BE ASKED OF THE LIVE COMPOSITION (`CaptureActiveVariant`), which holds what it was
// last told — not what a person has just typed here. And the variants themselves lived there too,
// so adding one wrote it into the composition while this window's copy knew nothing about it: the
// next commit assigned the copy over the composition and the new variant vanished, leaving the list
// showing a row with no variant behind it (Max, 2026-08-24: "a variant with an empty name appears").
void ibComposerSettingsPanel::CaptureIntoActiveVariant()
{
	// ⭐ NOTHING TO CAPTURE. The settings ARE the active variant's and so is the structure — both are
	// handed out by reference (`GetCompositionSettingsDesc`, `Structure`), so the editors and the
	// structure pane have been writing into the variant all along. What stood here copied two things
	// onto themselves (2026-08-24).
	//
	// The function stays because callers speak it — "keep what I edited" is a real thing to say at a
	// variant switch — and because a future variant that is loaded lazily would land exactly here.
}

// RE-READ THE SETTINGS — another variant was activated, so the composition now holds a different
// set and the editors have to start over on it. The SAME m_edited object, so the editors' pointers
// into it stay valid; only its contents are replaced.
void ibComposerSettingsPanel::ReloadSettings()
{
	// 🛑 THE COPY IS NOT RE-READ FROM THE COMPOSITION HERE. It used to be — `m_edited` was assigned
	// the composition's description again — which was right while the composition was the store this
	// window wrote through: a variant switch committed first, so re-reading picked the switch up.
	//
	// It is the copy that holds the variants now, and switching happens IN it (ActivateVariant), so
	// re-reading would throw away everything the person has done since the window opened. What is
	// needed is what this function is FOR: the editors and the structure buffer start again on
	// whatever the copy now has in force.
	// (Nothing to re-read for the structure: it is the cursor's variant's, so moving the cursor
	//  moved it. The editors below are RE-POINTED, because a variant is a separate object: Reload
	//  alone re-read the pointer taken when the window was built, so switching variants left the
	//  filter and sort pages showing the FIRST one's while everything else had moved.)
	if (Structure().empty())
		AddOutput();   // …and a variant nobody authored still gets its output
	if (m_filterEditor != nullptr) m_filterEditor->SetFilter(&EditedSettings().m_filter);
	if (m_sortEditor   != nullptr) m_sortEditor->SetSort(&EditedSettings().m_sort);
	ReloadResources();
	ReloadParameters();
}

// THE LEVEL THE REMEMBERED SELECTION POINTS AT — the one door every panel reads through, so none of
// them has to touch the tree control while it is telling us the selection changed.
ibLevelDescription* ibComposerSettingsPanel::CurrentLevel()
{
	const int output = m_currentNode.m_output, axis = m_currentNode.m_axis,
	          level  = m_currentNode.m_level;
	if (output < 0 || axis < 0 || level < 0 || (size_t)output >= Structure().size())
		return nullptr;
	std::vector<ibLevelDescription>& ladder = axis == 1
		? Structure()[output].m_columnGroups : Structure()[output].m_rowGroups;
	return (size_t)level < ladder.size() ? &ladder[level] : nullptr;
}

ibLevelDescription* ibComposerSettingsPanel::LevelAtRow(const ibDataViewItem& row)
{
	if (m_structureModel == nullptr)
		return nullptr;
	const ibStructurePos pos = m_structureModel->PosAt(row);
	if (!pos.IsLevel() || (size_t)pos.m_output >= Structure().size())
		return nullptr;
	ibOutputDescription& output = Structure()[pos.m_output];
	std::vector<ibLevelDescription>& axis = pos.m_axis == 1 ? output.m_columnGroups : output.m_rowGroups;
	return (size_t)pos.m_level < axis.size() ? &axis[pos.m_level] : nullptr;
}

// (THE PER-NODE BUFFER IS GONE, and with it the map that held it. A node's settings ARE on the node
//  — ibLevelDescription::m_settings, the same whole the composition has — so there was nothing for a
//  buffer to hold that the structure buffer did not already hold. It cost three keeping-in-step
//  chores: copy in on first selection, write back at commit, and re-key every entry whenever a level
//  was added, removed or moved. Settings travel WITH their node now, because they are part of it.)

// POINT THE SHARED EDITORS AT WHAT IS SELECTED. A LEVEL is edited in place; the report and an output
// keep the composition-wide one, which is what stands above every output (Max: the topmost filter
// and sort admit what all of them may see).
void ibComposerSettingsPanel::BindNodeEditors()
{
	if (m_filterEditor == nullptr || m_sortEditor == nullptr)
		return;

	// READ THE REMEMBERED NODE, not the control — see m_currentNode.
	ibLevelDescription* level = CurrentLevel();
	ibSettingsDescription& target = level != nullptr
		? level->m_settings : EditedSettings();
	m_filterEditor->SetFilter(&target.m_filter);
	m_sortEditor->SetSort(&target.m_sort);
}

// (CommitNodeSettings REMOVED. There is nothing to write back: the editors work on the node's own
//  settings inside the structure buffer, so a level's filter and sort land there as they are typed
//  and travel with the structure on accept. What it also used to do — build the level's condition
//  into a cached expression — belongs where the condition is USED, not where it is edited.)

// THE SNAPSHOT, taken whole. Copying the outputs — rather than reading them through some flattened
// view — is what lets this window edit a level made of several fields, a second output beside the
// first, and a column axis: all three are things a flat ladder cannot say.

// RE-READ WHICH FIELDS EXIST — the query was edited on this window's Query tab, so what may be
// filtered, sorted and grouped by is something else now.
void ibComposerSettingsPanel::ReloadFields()
{
	BindFieldSource();
	if (m_filterEditor != nullptr) m_filterEditor->ReloadFields();
	if (m_sortEditor   != nullptr) m_sortEditor->ReloadFields();
	// The pages this window owns are filled here too — the query changed, so which fields exist
	// changed, and a pane that is not re-filled goes on offering the ones that are gone.
	ReloadFieldTrees();
}

// THE PANES THAT OFFER FIELDS, re-filled. Two things change what they should show: the QUERY (other
// fields exist now) and the SELECTED NODE (the same fields, narrowed by what that node may use) —
// so the filling is one call and both callers reach it.
void ibComposerSettingsPanel::ReloadFieldTrees()
{
	if (m_fieldSource == nullptr)
		return;
	if (m_groupingFieldTree != nullptr)
		m_fieldSource->Populate(m_groupingFieldTree);
	if (m_selectedPage.m_sourceTree != nullptr)
		m_fieldSource->Populate(m_selectedPage.m_sourceTree);
}

// (Out of line, so the header can forward-declare the field tree it holds by pointer.)
ibComposerSettingsPanel::~ibComposerSettingsPanel() = default;

wxTreeCtrl* ibComposerSettingsPanel::CreateFieldTree(wxWindow* parent)
{
	return new wxTreeCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
}

// EVERY TREE, ONE WALK. Which fields the composition has is one question, so a query change
// refreshes all of them together — a page still offering a field another page has dropped is the
// same defect as a setting pointing at a field that is gone.
// ⭐ THE ONE PLACE THE QUERY IS READ. Text plus configuration in, fields plus the parser's complaint
// out — no composition, no column schema, nothing kept between calls but the answer.
void ibComposerSettingsPanel::RefreshQueryFields()
{
	m_fieldList = ibQueryFieldsOfText(m_edited.m_query, GetEditedMetaData(), &m_queryFault);
}

// EVERY TREE, ONE WALK. Which fields the composition has is one question, so a query change
// refreshes all of them together — a page still offering a field another page has dropped is the
// same defect as a setting pointing at a field that is gone.
void ibComposerSettingsPanel::PopulateFieldTrees()
{
	// ONE READ, then every tree shows it. Parsing once per tree would answer differently mid-edit.
	RefreshQueryFields();

	PopulateFieldTree(m_resourceFieldTree);
}

// The field a tree's cursor stands on, or null on the root / an empty tree.
const ibQueryConstructorField* ibComposerSettingsPanel::SelectedField(wxTreeCtrl* tree) const
{
	if (tree == nullptr)
		return nullptr;
	const wxTreeItemId sel = tree->GetSelection();
	if (!sel.IsOk())
		return nullptr;
	const ibFieldItemData* data = dynamic_cast<ibFieldItemData*>(tree->GetItemData(sel));
	if (data == nullptr || data->GetIndex() >= m_fieldList.size())
		return nullptr;
	return &m_fieldList[data->GetIndex()];
}

void ibComposerSettingsPanel::PopulateFieldTree(wxTreeCtrl* tree)
{
	if (tree == nullptr)
		return;

	wxTreeItemId root = tree->GetRootItem();
	if (root.IsOk())
		tree->DeleteChildren(root);
	else
		root = tree->AddRoot(wxEmptyString);

	// THE SAME ATTRIBUTE ICON the list settings put on every field. A field list without them reads
	// as text; with them it reads as the same thing a person sees everywhere else in the designer.
	//
	// ⭐ …AND A RESOURCE WEARS THE RESOURCE METATYPE'S OWN PICTURE (Max, 2026-08-22: highlight them
	// in the available fields too). Index 0 is the attribute, index 1 the resource — the same two
	// answers the settings field tree gives, so a field looks the same on every page of this window.
	wxImageList* icons = new wxImageList(16, 16);
	icons->Add(ibValue::GetIconGroup());
	const wxIcon resourceIcon = ibValueMetaObjectResource::GetIconGroup();
	icons->Add(resourceIcon.IsOk() ? resourceIcon : ibValue::GetIconGroup());
	tree->AssignImageList(icons);

	// WHICH PATHS ARE RESOURCES — asked of the DESCRIPTION, never copied: adding one changes the
	// picture on the next fill without anything else being told.
	auto isResource = [this](const wxString& path) {
		for (const ibResourceDescription& resource : m_edited.m_resources)
			if (resource.m_path.IsSameAs(path, false))
				return true;
		return false;
	};

	// THE FIELDS THE ENGINE SAYS THIS QUERY OFFERS, carrying their TYPE — which is what lets the
	// resources page ask "which aggregates fit this one" instead of guessing. The same list the
	// query constructor builds its own trees from.
	//
	// The row carries the field's INDEX, so a later question about the selected row (its type, its
	// path) is answered from the field itself rather than by parsing the label back.
	for (size_t i = 0; i < m_fieldList.size(); ++i) {
		const ibQueryConstructorField& field = m_fieldList[i];
		const wxString label = field.m_presentation.IsEmpty() ? field.m_name : field.m_presentation;
		tree->AppendItem(root, label, 0, 0, new ibFieldItemData(i));   // icon 0 = attribute
	}
}

// THE RESOURCES, READ BACK FROM THE COMPOSER. They live there and nowhere else, which is why this
// window needed the composer to grow a reading for them — a settings page that keeps its own copy
// of what it is editing is the second store that drifts.
void ibComposerSettingsPanel::ReloadResources()
{
	// THE MODEL IS THE COMPOSER — reset re-reads it. Nothing is copied into the window.
	if (m_resourceModel != nullptr)
		m_resourceModel->ResetFromList();
}



// ---------------------------------------------------------------------------
//  Context menus — the same verbs the toolbars raise, reached with the right hand
// ---------------------------------------------------------------------------
//
// ⭐ ONE IMPLEMENTATION PER VERB. The menu items carry the toolbar's own ids and end in the toolbar's
// own handlers, so a rule about what is possible where (a level command does nothing on the Report
// node, the last variant cannot be removed) is written once and both roads obey it. A menu with its
// own copy of the logic is the pair that disagrees the first time one of them changes.

void ibComposerSettingsPanel::OnStructureContextMenu(ibDataViewEvent& event)
{
	if (m_readOnly)   // view only — see OnParameterContextMenu
		return;
	// The row under the cursor becomes the selection first — a menu that acts on something other
	// than what was right-clicked is worse than no menu.
	if (m_structureView != nullptr && event.GetItem().IsOk())
		m_structureView->Select(event.GetItem());

	const bool onLevel = SelectedLevel() != wxNOT_FOUND;
	// An OUTPUT can be moved and deleted as well — its position is the order it prints in.
	//
	// 🛑 AND AN AXIS IS NOT AN OUTPUT. "No level" alone is true on the Rows and Columns rows too, so
	// Delete stood ENABLED there and did nothing when pressed — OnStructureRemove asks IsOutput()
	// and correctly refused. A command that is offered and then silently declines is worse than one
	// that is greyed: the axes of a table are UNDELETABLE (Max, 2026-08-25), and the menu says so.
	const bool onOutput = m_currentNode.IsOutput();

	wxMenu menu;
	ibAppendCmd(menu, ID_LEVEL_ADD, _("Add grouping"), wxASCII_STR(wxART_NEW), this);
	ibAppendCmd(menu, ID_TABLE_ADD, _("Add table"), wxASCII_STR(wxART_LIST_VIEW), this);
	ibAppendCmd(menu, ID_LEVEL_REMOVE, _("Delete"), wxASCII_STR(wxART_DELETE), this)
		->Enable(onLevel || onOutput);
	menu.AppendSeparator();
	ibAppendCmd(menu, ID_LEVEL_UP, _("Move up"), wxASCII_STR(wxART_GO_UP), this)
		->Enable(onLevel || onOutput);
	ibAppendCmd(menu, ID_LEVEL_DOWN, _("Move down"), wxASCII_STR(wxART_GO_DOWN), this)
		->Enable(onLevel || onOutput);

	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnStructureAdd(e); }, ID_LEVEL_ADD);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnStructureAddTable(e); }, ID_TABLE_ADD);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnStructureRemove(e); }, ID_LEVEL_REMOVE);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent&)   { MoveStructureLevel(-1); }, ID_LEVEL_UP);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent&)   { MoveStructureLevel(+1); }, ID_LEVEL_DOWN);
	PopupMenu(&menu);
}

void ibComposerSettingsPanel::OnVariantContextMenu(ibDataViewEvent& event)
{
	if (m_readOnly)   // view only — see OnParameterContextMenu
		return;
	if (m_variantView != nullptr && event.GetItem().IsOk())
		m_variantView->Select(event.GetItem());

	wxMenu menu;
	ibAppendCmd(menu, ID_VARIANT_ADD, _("Add variant"), wxASCII_STR(wxART_NEW), this);
	ibAppendCmd(menu, ID_VARIANT_COPY, _("Copy variant"), wxASCII_STR(wxART_COPY), this);
	// 🛑 The last variant stays — greyed here for the same reason it is greyed on the toolbar.
	ibAppendCmd(menu, ID_VARIANT_REMOVE, _("Delete variant"), wxASCII_STR(wxART_DELETE), this)
		->Enable(m_edited.m_variants.size() > 1);

	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnVariantAdd(e); }, ID_VARIANT_ADD);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnVariantCopy(e); }, ID_VARIANT_COPY);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnVariantRemove(e); }, ID_VARIANT_REMOVE);
	PopupMenu(&menu);
}

// THE RESOURCES LIST answers the right hand too — same two verbs its buttons carry.
void ibComposerSettingsPanel::OnResourceContextMenu(ibDataViewEvent& event)
{
	if (m_readOnly)   // view only — see OnParameterContextMenu
		return;
	if (m_resourceView != nullptr && event.GetItem().IsOk())
		m_resourceView->Select(event.GetItem());

	wxMenu menu;
	ibAppendCmd(menu, ID_RESOURCE_ADD, _("Add"), wxASCII_STR(wxART_NEW), this);
	ibAppendCmd(menu, ID_RESOURCE_EXPR, _("Expression..."), wxASCII_STR(wxART_EDIT), this);
	ibAppendCmd(menu, ID_RESOURCE_REMOVE, _("Delete"), wxASCII_STR(wxART_DELETE), this)
		->Enable(SelectedResourceIndex() != wxNOT_FOUND);

	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnAddResource(e); }, ID_RESOURCE_ADD);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnResourceExpression(e); }, ID_RESOURCE_EXPR);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnRemoveResource(e); }, ID_RESOURCE_REMOVE);
	PopupMenu(&menu);
}
// ---------------------------------------------------------------------------
//  Variants — add / copy / delete, and what a switch costs
// ---------------------------------------------------------------------------

// WHICH VARIANT THE CURSOR STANDS ON, or wxNOT_FOUND when nothing is selected — never row zero.
int ibComposerSettingsPanel::SelectedVariant() const
{
	return ibSelectedRow(m_variantView);
}

// RE-READ THE VARIANT LIST and land the cursor on `select`. Delete is greyed when only one is left:
// the store refuses anyway, and a button that refuses silently reads as a broken button.
void ibComposerSettingsPanel::ReloadVariants(int select)
{
	if (m_variantModel == nullptr || m_variantView == nullptr)
		return;
	m_variantModel->ResetFromList();
	const size_t count = m_edited.m_variants.size();
	if (m_variantBar != nullptr)
		m_variantBar->EnableTool(ID_VARIANT_REMOVE, count > 1);

	const size_t row = (select >= 0 && (size_t)select < count) ? (size_t)select : 0;
	// A VIRTUAL LIST KEYS ITS ROWS BY (index + 1) — the same off-by-one every list here obeys.
	m_variantView->Select(ibDataViewItem(reinterpret_cast<void*>(row + 1)));
}

// SWITCHING IS: keep what is on screen, then load the other one.
//
// ⭐ WHAT IS ON SCREEN BELONGS TO THE VARIANT BEING LEFT, so it is captured INTO that variant before
// another becomes the one in force. Miss the capture and switching away silently discards the edit —
// the defect a person only finds when they come back.
//
// ⭐⭐ ALL OF IT ON THE COPY. A variant is part of the description, and this window edits a copy of
// the description; asking the live composition to switch would switch something the copy is about to
// be assigned over. Nothing is written to the composition until OK.
void ibComposerSettingsPanel::ActivateVariant(size_t idx)
{
	// 🛑 VIEW ONLY — and this one WRITES, which is exactly why it needs saying. Switching variants
	// captures the buffer into the variant being left; the grid stays selectable in a read-only tab,
	// so without this a look-only session could rewrite the composition by clicking a row (found by
	// the final audit, 2026-08-20).
	if (m_readOnly)
		return;
	if (idx >= m_edited.m_variants.size())
		return;
	// ⭐ ALREADY THERE — asked of the POINTER, not of the index. It read `idx == 0` for as long as
	// this window always edited the zeroth; the moment a click began re-pointing it, that line meant
	// "you may never go BACK to variant zero" — the list would highlight row 0 while the pages went
	// on editing variant 1. A guard that outlived the thing it guarded (found by reading, 2026-08-24).
	if (m_settings == &m_edited.m_variants[idx].m_settings)
		return;

	// A HALF-WRITTEN SETTING KEEPS US HERE, and the cursor goes back to the variant it belongs to:
	// leaving the list pointing at one variant while another is active is the lie that follows.
	if (!ValidateEditedSettings()) {
		ReloadVariants(wxNOT_FOUND);
		return;
	}
	CaptureIntoActiveVariant();

	// ⭐⭐ …AND THE PAGES ARE RE-POINTED AT THAT VARIANT'S SETTING. That is the whole of the switch:
	// what this window edits is a SETTING, so going into another variant is being handed another
	// one. The outputs come with it — they are part of a setting — so the structure follows without
	// being carried.
	//
	// ⭐ AND NOTHING IS MARKED MODIFIED. Looking at another row is NAVIGATION, not an edit.
	//
	// 🛑 THIS IS THE DESIGNER'S ROAD ONLY. A reader has no variant list at all (Max, 2026-08-24:
	// *"there are no selected variants on the runtime side; they exist only in the designer"*), and
	// the guard above already returned for them — the pointer they were handed is their own setting
	// and nothing here may move it.
	m_settings = &m_edited.m_variants[idx].m_settings;

	ReloadSettings();
	ReloadStructure();
	UpdateSettingsHeader();
}

// ⭐ ADD / COPY / DELETE ARE EDITS OF THE COPY, like everything else on this window. They used to go
// through the live composition, which this window's copy is assigned over on OK — so a variant added
// here was gone at the first commit and the list was left showing a row with nothing behind it.
void ibComposerSettingsPanel::OnVariantAdd(wxCommandEvent&)
{
	// WHAT IS ON SCREEN BELONGS TO THE VARIANT IT WAS WRITTEN IN — captured before another becomes
	// the one in force, exactly as a switch does it.
	if (!ValidateEditedSettings())
		return;
	CaptureIntoActiveVariant();

	// A NEW VARIANT IS EMPTY, and it is named so it can be told apart before anything is in it.
	ibVariantDescription added;
	added.m_name = wxString::Format(wxT("%s %u"), _("Variant"),
		(unsigned)(m_edited.m_variants.size() + 1));
	m_edited.m_variants.push_back(std::move(added));
	MarkSettingsTouched();

	const size_t at = m_edited.m_variants.size() - 1;
	ReloadVariants((int)at);
	ActivateVariant(at);
}

// COPY — "it copies the groupings, filters, sorts and so on" (Max), and it is ONE ASSIGNMENT of the
// variant's description: the settings, the structure, the parameter values. (It used to be made by
// the store through the node the settings serialise into — serialisation standing in for
// assignment, free to differ from a real save the moment the two paths part.)
void ibComposerSettingsPanel::OnVariantCopy(wxCommandEvent&)
{
	// THE ACTIVE VARIANT'S EDITS ARE PART OF WHAT IS COPIED, so they are captured first — copying a
	// variant and getting the state it had when the window opened is the surprise worth avoiding.
	if (!ValidateEditedSettings())
		return;
	CaptureIntoActiveVariant();

	// COPY THE SELECTED ROW — the list's own selection is what "copy this one" means; there is no
	// cursor beside it to disagree with.
	const int selected = SelectedVariant();
	const size_t source = (selected != wxNOT_FOUND) ? (size_t)selected : 0;
	if (source >= m_edited.m_variants.size())
		return;
	ibVariantDescription added = m_edited.m_variants[source];
	added.m_name = m_edited.m_variants[source].m_name + wxT(" ") + _("(copy)");
	m_edited.m_variants.push_back(std::move(added));
	MarkSettingsTouched();

	const size_t at = m_edited.m_variants.size() - 1;
	ReloadVariants((int)at);
	ActivateVariant(at);
}

void ibComposerSettingsPanel::OnVariantRemove(wxCommandEvent&)
{
	const int idx = SelectedVariant();
	if (idx == wxNOT_FOUND || (size_t)idx >= m_edited.m_variants.size())
		return;
	// 🛑 THE LAST ONE STAYS. A composition with no variant has no settings at all — the button is
	// greyed to say so before the click, and this is the answer if it is reached anyway.
	if (m_edited.m_variants.size() <= 1)
		return;

	m_edited.m_variants.erase(m_edited.m_variants.begin() + idx);
	// 🛑 AND THE POINTER IS PUT BACK ON SOMETHING THAT EXISTS. An erase moves every element after it,
	// so the setting the pages stand over would silently become a DIFFERENT variant's — or the one
	// just deleted. The zeroth always survives (the guard above), so it is what the window falls back
	// to, exactly as it opened.
	//
	// The comment that stood here said this window always edits the zeroth. That was true until the
	// same hour's change taught a click to re-point it — a claim that outlived its premise, which is
	// the second one of those found today (found by reading, 2026-08-24).
	m_settings = &m_edited.m_variants.front().m_settings;
	MarkSettingsTouched();

	ReloadSettings();
	ReloadStructure();
	ReloadVariants(wxNOT_FOUND);
	UpdateSettingsHeader();
}
// ---------------------------------------------------------------------------
//  The structure's commands — its toolbar, and nothing else raises them
// ---------------------------------------------------------------------------

// THE LEVEL THE CURSOR STANDS ON, or wxNOT_FOUND on the Report node and on nothing-selected —
// so a command aimed at a level does nothing there instead of acting on level zero.
int ibComposerSettingsPanel::SelectedLevel()
{
	// ASKED OF THE REMEMBERED NODE, and checked against the buffer: a coordinate the structure no
	// longer has is not a level, however recently it was one.
	return CurrentLevel() != nullptr ? m_currentNode.m_level : wxNOT_FOUND;
}

// RE-READ THE LADDER and land the cursor on the level the user just acted on — a command whose
// result you have to go and find again reads as a command that did nothing.
void ibComposerSettingsPanel::ReloadStructure(int selectLevel)
{
	if (m_structureModel == nullptr || m_structureView == nullptr)
		return;
	// ⚠ DROP THE SELECTION FIRST. A rebuild clears the view's internal state, and a selection left
	// pointing into what was there is what the control's own assertion calls "invalid item in
	// selection - bad internal state". The cursor is put back below, on purpose and on a row that
	// exists by then.
	m_structureView->UnselectAll();
	m_structureModel->Rebuild();

	// ⚠ EXPANDING RUNS ON THE NEXT TURN of the event loop: expanding a row the view has not
	// fetched yet does nothing at all, which is how a freshly added level ends up invisible under
	// a collapsed parent. (The filter tree learnt this the same way.)
	// WHERE THE CURSOR SHOULD LAND — the node that was just acted on, by its own coordinate. The
	// old spelling ("level N") could only ever name the first output's ladder, so adding a level to
	// the second output left the cursor on the first one's — which reads as the command having done
	// nothing.
	const ibNodeKey landOn = selectLevel == wxNOT_FOUND
		? m_currentNode
		: ibNodeKey(std::max(0, m_currentNode.m_output), std::max(0, m_currentNode.m_axis), selectLevel);

	CallAfter([this, landOn] {
		if (m_structureModel == nullptr || m_structureView == nullptr)
			return;

		// EVERY NODE IS OPENED, not just the first output's ladder. A rebuild collapses the tree,
		// and a level that stays hidden under a collapsed parent reads as a level that was not
		// added — which is what "the nodes fold up" was.
		m_structureView->Expand(m_structureModel->RootItem());
		for (size_t out = 0; out < Structure().size(); ++out) {
			const ibDataViewItem output = m_structureModel->ItemForOutput((int)out);
			if (output.IsOk())
				m_structureView->Expand(output);
			for (int axis = 0; axis <= 1; ++axis) {
				const std::vector<ibLevelDescription>& ladder = axis == 1
					? Structure()[out].m_columnGroups : Structure()[out].m_rowGroups;
				for (size_t level = 0; level < ladder.size(); ++level) {
					const ibDataViewItem item = m_structureModel->ItemForNode((int)out, axis, (int)level);
					if (item.IsOk())
						m_structureView->Expand(item);
				}
			}
		}

		const ibDataViewItem row = m_structureModel->ItemForNode(
			landOn.m_output, landOn.m_axis, landOn.m_level);
		m_structureView->Select(row.IsOk() ? row : m_structureModel->RootItem());
		if (row.IsOk()) {
			m_structureView->EnsureVisible(row);
			m_currentNode = landOn;   // the panels below follow the cursor we just placed
		}
		BindNodeEditors();
		ReloadGrouping();
		ReloadFieldSets();
		UpdateSettingsHeader();
	});
}

// WHICH NODE THE SETTINGS BELOW BELONG TO.
//
// ⚠ AND THAT THEY ARE STILL THE WHOLE COMPOSITION'S. The engine holds one filter, one sort and one
// set of totals for the composer as a whole; the layout asks the per-node question already, and
// saying so here is cheaper than a window that quietly answers it wrong. When totals move to the
// node, this line loses its second half.
void ibComposerSettingsPanel::UpdateSettingsHeader()
{
	if (m_settingsHeader == nullptr)
		return;

	wxString node = _("Report");
	if (const ibLevelDescription* level = CurrentLevel()) {
		// EVERY FIELD OF THE LEVEL — it groups by all of them together, and a header naming only
		// the first would describe a narrower heading than the one being edited.
		wxString fields;
		for (const auto& field : level->m_settings.m_group.m_lines) {
			if (!fields.IsEmpty()) fields += wxT(", ");
			fields += field.m_path;
		}
		node = fields.IsEmpty() ? _("Detail records") : _("Grouping") + wxT(": ") + fields;
	}

	// WHAT THESE SETTINGS REACH — no longer one answer. A level's filter and sort are its own; on
	// the report and on an output they are the pair that stands above every output.
	const bool onLevel = CurrentLevel() != nullptr;
	// ⚠ ASCII ONLY IN UI LITERALS — this file has no BOM, so MSVC reads it as ANSI.
	m_settingsHeader->SetLabel(_("Settings of") + wxT(": ") + node + wxT("  ")
		+ (onLevel ? _("(this grouping only)") : _("(applies to every output)")));
	if (m_settingsHeader->GetParent() != nullptr)
		m_settingsHeader->GetParent()->Layout();
}

// A NEW GROUPING IS A FIELD, PICKED IN A DIALOG. There is no field list in this pane on purpose —
// the settings below already have one, and two of them side by side is the same question answered
// twice. The kind is not asked for here: it is a VALUE on the level's own row, chosen through the
// runtime's quick choice like every other registered type.
//
// THE NEW LEVEL GOES INNERMOST. "By warehouse, then by item" is what appending means, and moving it
// is one keystroke — a rule about where a level lands relative to the cursor would be a second rule
// about an order the user can already see.
std::vector<ibLevelDescription>* ibComposerSettingsPanel::AxisForCommand(int& at)
{
	at = wxNOT_FOUND;
	if (Structure().empty())
		return nullptr;

	// FROM THE REMEMBERED NODE — the same one every other panel reads, so a command and the panels
	// can never disagree about what is selected.
	ibStructurePos pos;
	pos.m_output = m_currentNode.m_output;
	pos.m_axis   = m_currentNode.m_axis;
	pos.m_level  = m_currentNode.m_level;

	// ⭐ ON THE REPORT ITSELF, ADDING STARTS A NEW OUTPUT (Max): levels added one under another
	// belong to one output; going back to the root and adding again is exactly how a SECOND one is
	// asked for. The only exception is an output that has nothing in it yet — starting a second
	// empty one beside it would produce two headings for one act.
	if (pos.IsReport()) {
		if (!Structure().back().m_rowGroups.empty() || !Structure().back().m_columnGroups.empty())
			AddOutput();
		// THE COMMAND MOVES THE CURSOR WITH IT — what is added lands in the output the command
		// just chose, and the cursor has to be looking at that one, not at where it started.
		m_currentNode = ibNodeKey((int)Structure().size() - 1, 0, -1);
		return &Structure().back().m_rowGroups;
	}

	const size_t output = pos.m_output >= 0 && (size_t)pos.m_output < Structure().size()
		? (size_t)pos.m_output : 0u;
	const bool columns = pos.m_axis == 1;
	if (pos.IsLevel())
		at = pos.m_level;
	// The axis this command works on is where the cursor belongs afterwards — an output row and an
	// axis row both mean "the rows of this output" here, and the reload lands accordingly.
	m_currentNode = ibNodeKey((int)output, columns ? 1 : 0, at);
	return columns ? &Structure()[output].m_columnGroups : &Structure()[output].m_rowGroups;
}

void ibComposerSettingsPanel::OnStructureAdd(wxCommandEvent&)
{
	int at = wxNOT_FOUND;
	std::vector<ibLevelDescription>* axis = AxisForCommand(at);
	if (axis == nullptr) {
		// SAID, NOT SWALLOWED. A command that does nothing and explains nothing reads as broken,
		// and this is the one state where it can happen: the composition has no output to add to.
		wxMessageBox(_("This composition has no output to add a grouping to."),
			_("Data composer settings"), wxOK | wxICON_INFORMATION, this);
		return;
	}
	// ⭐ A FORM, NOT A FIELD PICKER (Max). A grouping is a LIST of fields welded into one heading —
	// asking for one field could only ever make a one-field level — and an EMPTY list is the second
	// thing this form makes: the detail records, a node with no group but a node all the same.
	ibComposerGroupingDialog dialog(this, ibLevelDescription());
	if (dialog.ShowModal() != wxID_OK)
		return;
	axis->push_back(dialog.Node());

	MarkSettingsTouched();
	ReloadStructure((int)axis->size() - 1);
}

// ⭐⭐ ADD A TABLE — an output of the other shape, which is not a level and never lands inside one.
//
// A table opens with BOTH ITS AXES, and they are undeletable: "table (output) — rows (undeletable),
// columns (undeletable), and the groupings hang off each" (Max, 2026-08-25). Nothing is created for
// them here, because an axis is not a thing to create — it is the pair of level lists an output of
// this KIND has. Saying the kind is the whole act.
//
// ⚠ AND IT IS ALWAYS A NEW OUTPUT — the one exception being a last output nobody has put anything
// in, exactly as adding a grouping on the report node reuses it. Turning a FILLED grouping into a
// table would silently move its levels onto the rows axis of a shape the person did not ask for.
void ibComposerSettingsPanel::OnStructureAddTable(wxCommandEvent&)
{
	if (m_readOnly)
		return;

	const bool lastIsUntouched = !Structure().empty()
		&& Structure().back().m_kind == ibCompositionOutputKind::Grouping
		&& Structure().back().m_rowGroups.empty() && Structure().back().m_columnGroups.empty();
	if (!lastIsUntouched)
		AddOutput();

	Structure().back().m_kind = ibCompositionOutputKind::Table;

	// THE CURSOR FOLLOWS THE COMMAND, onto the ROWS of what was just made — the next thing a person
	// does is add a heading, and that is where the first one goes.
	m_currentNode = ibNodeKey((int)Structure().size() - 1, 0, -1);
	MarkSettingsTouched();
	ReloadStructure();
}

// ⭐ EDIT AN EXISTING LEVEL IN THE SAME FORM THAT MADE IT — what the "..." on its Field cell opens.
// One window for a grouping, whether it is being made or being changed: a second, different way to
// edit the same node is how the two start to disagree about what a grouping is.
//
// Emptying the field list here turns the level into the DETAIL RECORDS, and filling it turns it
// back — the form decides the kind from what it holds, so nothing else has to.
void ibComposerSettingsPanel::EditLevelInForm(const ibDataViewItem& row)
{
	if (m_readOnly)
		return;
	ibLevelDescription* level = LevelAtRow(row);
	if (level == nullptr)
		return;

	ibComposerGroupingDialog dialog(this, *level);
	if (dialog.ShowModal() != wxID_OK)
		return;

	const ibLevelDescription edited = dialog.Node();
	// ITS OWN SETTINGS STAY ITS OWN — the form edits the fields and the kind; the filter, the sort
	// and the selected fields of this level are not its business.
	level->m_kind   = edited.m_kind;
	level->m_settings.m_group.m_lines = edited.m_settings.m_group.m_lines;
	MarkSettingsTouched();
	ReloadStructure(m_structureModel != nullptr ? m_structureModel->LevelAt(row) : wxNOT_FOUND);
	ReloadGrouping();
}

void ibComposerSettingsPanel::OnStructureRemove(wxCommandEvent&)
{
	// AN OUTPUT IS REMOVED WHOLE — with its nodes, both its axes and everything each node holds.
	// Never the last one: a composition always has at least one output, and "no outputs" is not a
	// state anything downstream handles.
	// ⚠ AN AXIS ROW IS NOT THE OUTPUT — the coordinate says which it is, and it is asked (IsOutput),
	// not re-derived from the numbers. Testing "no level" alone made Delete on the "Columns" row
	// erase the whole output, its rows included.
	const int selectedOutput = m_currentNode.m_output;
	if (m_currentNode.IsOutput()
	    && (size_t)selectedOutput < Structure().size() && Structure().size() > 1) {
		// (One erase, and that is the whole of it. A map of per-node settings used to be re-keyed here
		//  — every remaining entry shifted down by one — because the settings lived beside the nodes
		//  rather than on them. They are on them now, so they leave with what they belong to.)
		Structure().erase(Structure().begin() + selectedOutput);

		m_currentNode = ibNodeKey(std::min<int>(selectedOutput, (int)Structure().size() - 1), -1, -1);
		MarkSettingsTouched();
		ReloadStructure();
		return;
	}

	int level = wxNOT_FOUND;
	std::vector<ibLevelDescription>* axis = AxisForCommand(level);
	if (axis == nullptr || level == wxNOT_FOUND || (size_t)level >= axis->size())
		return;

	// ⭐ REMOVING A GROUPING BREAKS THE CHAIN, so everything nested under it goes with it (Max).
	// Pulling the deeper levels up instead would silently re-parent them: "by warehouse, then by
	// item" would become "by item", which is a different report nobody asked for.
	axis->erase(axis->begin() + level, axis->end());

	// (What each removed node held went with it. There used to be a second sweep here, dropping the
	//  buffers of every coordinate at or below the removal — otherwise a filter written on a level
	//  that no longer exists would land on whatever level later took that coordinate.)
	const int output = m_currentNode.m_output, axisIndex = m_currentNode.m_axis;
	MarkSettingsTouched();
	// The chain now ends where the removal started, so the cursor lands on its new last level —
	// or on the output itself when nothing is left to stand on.
	const int count = (int)axis->size();
	if (count == 0)
		m_currentNode = ibNodeKey(output, axisIndex, -1);
	ReloadStructure(count == 0 ? wxNOT_FOUND : count - 1);
}

// ORDER IS THE NESTING — "by warehouse, then by item" is a different report from the other way
// round — so moving a level is a real edit and not a view preference.
void ibComposerSettingsPanel::MoveStructureLevel(int delta)
{
	// ⭐ AN OUTPUT MOVES TOO. The order of the outputs is the order they are PRINTED in, so it is a
	// setting like any other — and the arrows were dead on an output row, which reads as a command
	// that does not work rather than one that does not apply.
	// An AXIS row is not the output — see OnStructureRemove. Moving one would move the output it
	// hangs under, which on a cross-table is every level of both axes.
	const int selectedOutput = m_currentNode.m_output;
	if (m_currentNode.IsOutput() && (size_t)selectedOutput < Structure().size()) {
		const int target = selectedOutput + delta;
		if (target < 0 || (size_t)target >= Structure().size())
			return;
		// ONE SWAP, and everything the two outputs hold swaps with them — their nodes, and each node's
		// own settings. (A parallel map used to be re-keyed here for the same reason it was re-keyed
		// on a removal: a filter written on one output would otherwise stay behind and land on
		// whatever moved into its place.)
		std::swap(Structure()[selectedOutput], Structure()[target]);

		m_currentNode = ibNodeKey(target, m_currentNode.m_axis, m_currentNode.m_level);
		MarkSettingsTouched();
		ReloadStructure();
		return;
	}

	int level = wxNOT_FOUND;
	std::vector<ibLevelDescription>* axis = AxisForCommand(level);
	if (axis == nullptr || level == wxNOT_FOUND || (size_t)level >= axis->size())
		return;
	const int target = level + delta;
	if (target < 0 || (size_t)target >= axis->size())
		return;   // already at the end it was moved towards
	// THE ORDER IS THE NESTING, so moving a level is moving a heading up or down the report —
	// swap keeps every other level exactly where it was.
	std::swap((*axis)[level], (*axis)[target]);
	MarkSettingsTouched();
	ReloadStructure(target);
}




// WHAT THE CELL OFFERS FOR THE ROW UNDER THE CURSOR — the calls the ENGINE admits over that row's
// field, whole and ready to pick. Empty when the row's expression is not a plain call over a field
// (a ratio has no "which aggregate" to ask about), and the cell then behaves as a free text box
// with the editor behind "...".
// ⭐⭐ THE GROUPINGS THIS COMPOSITION DECLARES — what a resource's area may name, and nothing else.
//
// Read off the structure itself, so the list is the report's own: add a grouping and it is offered
// at once, remove one and it stops being. The empty line comes first and is not a placeholder — no
// area IS the ordinary answer, and it is the way back to it.
//
// ⚠ THE SAME NAMES THE ENGINE RESOLVES BY. A level is addressed by its PATH here (that is what the
// composition stores and what the rendered `TOTALS … BY` writes), so what is picked is what the
// lowering will find (docs/query-language-arc.md §27).
std::vector<wxString> ibComposerSettingsPanel::ResourceScopeNames() const
{
	std::vector<wxString> names;
	for (const ibOutputDescription& output : Structure())
		for (const std::vector<ibLevelDescription>* axis : { &output.m_rowGroups, &output.m_columnGroups })
			for (const ibLevelDescription& level : *axis)
				for (const ibGroupLineDescription& line : level.m_settings.m_group.m_lines)
					if (!line.m_path.IsEmpty()
					    && std::find(names.begin(), names.end(), line.m_path) == names.end())
						names.push_back(line.m_path);
	return names;
}

// ⚠ A COMPOSITION HAS NO SEPARATORS OF ITS OWN — `SPLIT` is a shape of the QUERY, and a composition
// says the same thing by having several OUTPUTS. So nothing here is a node, and the tree comes out
// flat; the picker draws whatever it is given.
std::vector<wxString> ibComposerSettingsPanel::ResourceScopeSeparators() const
{
	return {};
}

wxArrayString ibComposerSettingsPanel::ResourceScopeChoices() const
{
	wxArrayString words;
	words.Add(wxEmptyString);   // the ladder — the ordinary case, and the way back to it
	for (const wxString& name : ResourceScopeNames())
		words.Add(name);
	return words;
}

wxArrayString ibComposerSettingsPanel::ResourceChoices() const
{
	wxArrayString words;
	const int row = SelectedResourceIndex();
	if (row == wxNOT_FOUND || (size_t)row >= m_edited.m_resources.size())
		return words;

	// FROM THE DESCRIPTION — the resources are declared there, and the running composer only ever
	// held a copy of them taken at a run.
	const wxString path = m_edited.m_resources[(size_t)row].m_path;
	if (path.IsEmpty())
		return words;

	// THE FIELD'S TYPE is what decides, so the path is looked up among the fields this composition
	// offers. A path that is not one of them (a hand-written expression) has no type to ask about.
	for (const ibQueryConstructorField& field : m_fieldList) {
		if (!field.m_name.IsSameAs(path, false))
			continue;
		// THE ENGINE COMPOSES THE OFFERS: the calls this type admits over this field, DISTINCT twin
		// included where it asks a different question. One door, so the constructor and this window
		// never offer different things about the same field.
		for (const wxString& call : ibQueryLowering::AggregateCallsFor(field.m_type, path))
			words.Add(call);
		break;
	}
	return words;
}

// THE FULL EDITOR, over the text the cell holds. Returns true when the author changed it — the
// same contract the query constructor's own cells use.
bool ibComposerSettingsPanel::EditResourceExpression(wxString& text)
{

	ibDialogQueryExpression editor(this, _("Resource"), m_fieldList, nullptr,
		GetEditedMetaData(), /*readOnly*/false);
	editor.SetText(text);
	if (editor.ShowModal() != wxID_OK)
		return false;
	text = editor.GetText();
	return true;
}

// A RESOURCE IS A FIELD AND WHAT IS DONE TO IT. The aggregate is the FIRST one the engine admits
// for that field's type — a number arrives as a sum, a string as a count — and which one it should
// really be is answered in the row's own Expression cell. One gesture to add, one place to change.
void ibComposerSettingsPanel::OnAddResource(wxCommandEvent&)
{
	// VIEW ONLY — guarded in the HANDLER, not on each road to it: the toolbar, the context menu, a
	// double-click on the field tree and a drop on the pane all end here, and guarding roads is how
	// the fourth one gets forgotten (the double-click and the drop were, until the final audit).
	if (m_readOnly)
		return;
	const ibQueryConstructorField* field = SelectedField(m_resourceFieldTree);
	if (field == nullptr || field->m_name.IsEmpty())
		return;

	const wxArrayString aggregates = ibAggregatesForField(*field);
	// NOTHING THE ENGINE ADMITS means the field cannot be folded at all — added as a bare
	// expression rather than wrapped in a call it would refuse.
	m_edited.m_resources.push_back({ aggregates.IsEmpty() ? wxString() : aggregates[0], field->m_name, wxString() });
	MarkSettingsTouched();
	ReloadResources();
	// THE CURSOR FOLLOWS WHAT WAS ADDED — a row you have to go and find reads as a command that did
	// nothing, and the Expression cell is the next thing a person reaches for.
	if (m_resourceView != nullptr && !m_edited.m_resources.empty())
		m_resourceView->Select(ibDataViewItem(
			reinterpret_cast<void*>(m_edited.m_resources.size())));
}

// THE EXPRESSION EDITOR FOR THE ROW UNDER THE CURSOR — the same door the cell's "..." opens, on the
// toolbar for reach. With no row selected it adds one: writing an expression is a way to CREATE a
// resource too, not only to change one.
void ibComposerSettingsPanel::OnResourceExpression(wxCommandEvent&)
{
	if (m_readOnly)   // view only — see OnAddResource
		return;

	const int row = SelectedResourceIndex();
	wxString text;
	if (row != wxNOT_FOUND && (size_t)row < m_edited.m_resources.size()) {
		const ibResourceDescription& resource = m_edited.m_resources[(size_t)row];
		text = resource.m_func.IsEmpty() ? resource.m_path
		                                 : resource.m_func + wxT("(") + resource.m_path + wxT(")");
	}
	else if (const ibQueryConstructorField* field = SelectedField(m_resourceFieldTree)) {
		// A head start rather than a default: the field the cursor stands on, with the aggregate the
		// engine put first for its type.
		const wxArrayString aggregates = ibAggregatesForField(*field);
		text = aggregates.IsEmpty() ? field->m_name : aggregates[0] + wxT("(") + field->m_name + wxT(")");
	}

	if (!EditResourceExpression(text) || text.IsEmpty())
		return;

	// THE SAME SPLIT THE CELL MAKES — a plain call keeps its field and its function apart, anything
	// else is kept whole as an expression. Written once, in the model.
	wxString func, path;
	if (!ibResourceModel::SplitCall(text, func, path)) { func.clear(); path = text; }

	// INTO THE COPY THIS WINDOW EDITS — the description, where a resource lives.
	if (row != wxNOT_FOUND && (size_t)row < m_edited.m_resources.size()) {
		// THE FIGURE IS EDITED, THE NAME STAYS — this editor is about the expression.
		m_edited.m_resources[row].m_func = func;
		m_edited.m_resources[row].m_path = path;
	}
	else
		m_edited.m_resources.push_back({ func, path, wxString() });
	MarkSettingsTouched();
	ReloadResources();
}
// AND THE WAY OUT OF THE READY LIST: an expression. `SUM(Amount) / COUNT(DISTINCT Doc)` is a
// resource too, and it is written in the SAME editor the query constructor uses — with the field
// tree, the language palette and the engine as the only judge of what parses.

void ibComposerSettingsPanel::OnResourceFieldActivated(wxTreeEvent&)
{
	wxCommandEvent unused;
	OnAddResource(unused);
}

void ibComposerSettingsPanel::OnRemoveResource(wxCommandEvent&)
{
	if (m_readOnly)   // view only — see OnAddResource
		return;
	const int idx = SelectedResourceIndex();
	if (idx == wxNOT_FOUND || (size_t)idx >= m_edited.m_resources.size())
		return;
	m_edited.m_resources.erase(m_edited.m_resources.begin() + idx);
	MarkSettingsTouched();
	ReloadResources();


}

// THE COMPOSITION IS AUTHORED HERE. It has no main table to fall back on — the query IS its
// source — so the constructor edits the one text everything else hangs off, and what it renders
// comes straight back into the box. The names resolve against the composition's own config, never
// a quietly-defaulted active one.
void ibComposerSettingsPanel::OnBuildQuery(wxCommandEvent&)
{
	if (m_queryText == nullptr)
		return;

	wxString text = m_queryText->GetText();
	// EXCLUDING TOTALS: a composition folds through its RESOURCES and its levels are its GROUPINGS,
	// so a TOTALS clause in this text would be the same setting written where no window can show it.
	if (!ibShowQueryConstructor(this, text, GetEditedMetaData(), /*readOnly*/false,
			ibQueryExclude_Totals))
		return;   // cancelled — the box keeps what the author had

	// SetText fires the change handler, which stores the text and marks the source for a re-read —
	// the same road a typed character takes. Doing it here as well would be the second answer.
	m_queryText->SetText(text);
}

// THE PENDING RE-READ, FORCED. The text itself is never pending any more — a keystroke stores it —
// so what can still be outstanding is the SOURCE not having been re-read for it yet. Leaving the
// page and accepting the window both come through here, so neither can act on a stale source.
//
// (It also catches the text the editor holds when something wrote it without going through the
// change handler, which is why it still compares before storing.)
void ibComposerSettingsPanel::ApplyPendingQueryText()
{
	if (m_queryText == nullptr)
		return;
	// ⭐ THE TEXT LANDS IN THE DESCRIPTION THIS WINDOW EDITS, and that is the only place it lands.
	// It used to be written into a live composition as well, so the ENGINE could read it — but a
	// composition is a FACADE over a description (Max, 2026-08-24), and the fields, the parameters
	// and the parser's complaint all come off the text itself now. One store, so nothing can hold
	// the old one and put it back at commit.
	if (m_queryText->GetText() != m_edited.m_query) {
		m_edited.m_query = m_queryText->GetText();
		m_queryDirty = true;
		// 🛑 AND THE BUFFER NOW HAS SOMETHING TO LAND. Commit only writes the copy back when the
		// buffer was touched, and "touched" was raised by the filter / sort / structure editors
		// alone — so a session that edited nothing but the QUERY closed without writing it (Max,
		// 2026-08-24: "saving the query does not work"). The text is part of the copy like
		// everything else on this window.
		MarkSettingsTouched();
	}
	if (!m_queryDirty)
		return;
	m_queryDirty = false;
	RefreshFromQueryText();
}

// ⭐ A KEYSTROKE STORES THE TEXT — and nothing more. Cheap on purpose: this runs per character, so it
// writes the text and marks the source as needing a re-read, and that is all. Working out what the
// text MEANS costs a describe, a parameter sync and a settings prune; ANNOUNCING it can cost a whole
// form-editor re-render at the listener. Both wait for the pause — see RefreshFromQueryText.
//
// ⚠ INSERT / DELETE ONLY — wxSTC_MOD_* also reports styling, folding and selection, which are not
// changes to the text. The code editor filters on exactly this pair, for exactly this reason.
void ibComposerSettingsPanel::OnQueryTextChanged(wxStyledTextEvent& event)
{
	event.Skip();

	const int modFlags = event.GetModificationType();
	if ((modFlags & (wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT)) == 0)
		return;
	if (m_queryText == nullptr || m_readOnly)
		return;

	// ONLY THE FLAG. The text is carried into the description by ApplyPendingQueryText, once per
	// pause — a keystroke marks that there is something to carry, and nothing more.
	m_queryDirty = true;
}

// …AND THE TYPING STOPPING IS WHEN IT IS READ. Idle, not a timer: it fires once the queue is empty,
// which IS "the user paused", and it needs nothing to start, stop or cancel. The flag is what keeps
// it from doing the work on every idle turn.
void ibComposerSettingsPanel::OnIdleApplyQuery(wxIdleEvent& event)
{
	event.Skip();

	if (!m_queryDirty)
		return;

	// ⭐ THE TEXT FIRST, THEN WHAT FOLLOWS FROM IT — through the one door that does both, so the
	// pause cannot refresh over a text it has not stored yet.
	//
	// 🛑 IT CALLED RefreshFromQueryText DIRECTLY. That was right only while a keystroke ALSO wrote
	// the text into a live composition, which is what the refresh then read; with the text living in
	// the description alone, refreshing without carrying it there first re-reads the PREVIOUS text —
	// so the fields and the red line described the query as it was before the edit (Max, 2026-08-24:
	// "it cannot refresh when the text changes").
	ApplyPendingQueryText();
}

// EVERYTHING THAT FOLLOWS FROM THE QUERY, re-read in one place — so the idle pass, leaving the page
// and accepting the window cannot disagree about what "the query changed" entails.
void ibComposerSettingsPanel::RefreshFromQueryText()
{
	// (NOTHING TO REBUILD FIRST. This began with ApplySource on a live composition, which parsed the
	//  text into a column schema it kept; PopulateFieldTrees below parses it straight, so the schema
	//  and the question of whether it was up to date both stopped existing.)

	// ⭐ AND THIS IS WHERE THE TEXT EDIT IS ANNOUNCED — once per pause, not once per character.
	// SetQueryText deliberately stays silent (see it): whoever hears the signal may re-render a
	// whole form editor, and doing that per keystroke made typing a query unusable.
	MarkModified();

	// The fields follow the text: a field added to the query is there to group by without closing
	// anything.
	PopulateFieldTrees();
	// ...AND SO DO THE EDITORS. Their trees are the SAME question asked in the lower half of this
	// window; leaving them behind is how one screen ends up with two answers about one query.
	ReloadFields();

	// AND THE PARAMETERS: a new &Name in the text is a new row here, and one the text stopped asking
	// for is gone. The list follows the query, so it is re-read where the query is applied — over
	// THIS window's copy, through the one spelling of that rule (valueDataComposition.h).
	ibSyncParametersWithQuery(m_edited.m_parameters, m_edited.m_query);
	ReloadParameters();

	// WHAT THE PARSER SAID, taken at the same moment the fields were — PopulateFieldTrees above is
	// what wrote it, so the line and the trees can never be describing two different texts.
	ShowQueryFault();
}

// ⭐ THE LINE UNDER THE TEXT, said in one place because TWO moments say it: the text changing, and
// the window OPENING.
//
// 🛑 IT USED TO BE SAID ONLY ON A CHANGE. So a composition whose stored query no longer compiles —
// a field renamed in the configuration, a table gone — opened looking perfectly fine, and only
// admitted it after somebody typed into the text or pressed OK. The window has already parsed by
// then (the constructor reads the fields), so it KNEW and was not saying (Max, 2026-08-24).
void ibComposerSettingsPanel::ShowQueryFault()
{
	if (m_queryError == nullptr)
		return;
	m_queryError->SetLabel(m_queryFault);
	m_queryError->Show(!m_queryFault.IsEmpty());
	if (m_queryError->GetParent() != nullptr)
		m_queryError->GetParent()->Layout();
}

int ibComposerSettingsPanel::SelectedResourceIndex() const
{
	return ibSelectedRow(m_resourceView);
}

// ⭐⭐ AN OUTPUT IS BORN WITH A NAME — see the declaration for why it is not decoration: the composer
// addresses a branch of the shared read by exactly this name, and a nameless output falls out of
// that read and costs a query of its own.
ibOutputDescription& ibComposerSettingsPanel::AddOutput()
{
	wxString name;
	for (size_t at = Structure().size() + 1; ; ++at) {
		name = wxString::Format(wxT("Output%u"), static_cast<unsigned>(at));
		bool taken = false;
		for (const ibOutputDescription& output : Structure())
			if (output.m_name.IsSameAs(name, false)) { taken = true; break; }
		if (!taken)
			break;
	}

	ibOutputDescription added;
	added.m_name = name;
	Structure().push_back(std::move(added));
	return Structure().back();
}
