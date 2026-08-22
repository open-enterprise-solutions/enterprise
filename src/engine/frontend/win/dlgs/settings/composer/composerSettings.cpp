#include "frontend/win/dlgs/settings/composer/composerSettings.h"

#include "backend/srcDataObject.h"                 // ibSourceExplorer — the available fields
#include "frontend/win/dlgs/settings/settingsFieldTree.h"      // which fields this composition offers — one answer
#include "frontend/win/dlgs/settings/settingsFilterEditor.h"   // SHARED with the list's world
#include "frontend/win/dlgs/settings/settingsSortEditor.h"     // …and so is this one
#include "frontend/win/dlgs/settings/settingsStyle.h"          // how a settings surface LOOKS — said once, for both worlds
#include "frontend/win/dlgs/callbackDropTarget.h"              // dropping a field onto a list — the same-process drag
#include "frontend/win/dlgs/queryConstructor/queryConstructor.h" // the Query tab's constructor button
#include "frontend/win/dlgs/queryConstructor/queryExpressionDialog.h" // the resource expression editor
#include "frontend/win/dlgs/typeSelector.h"              // the product.s type picker — a parameter declares its type
#include "frontend/win/dlgs/queryConstructor/queryConstructorInternal.h" // ibExpressionCellRenderer — the Totals tab's own cell
#include "frontend/win/editor/codeEditor/codeEditor.h"  // the script editor behind a parameter expression
#include "frontend/mainFrame/mainFrame.h"                // the shared editor / font-colour settings
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
	// WHAT A NODE MAY SEE — its own set, or the one above it.
	ID_AVAILABLE_ADD,
	ID_AVAILABLE_REMOVE,
	ID_AVAILABLE_COPY,
	ID_AVAILABLE_AUTO,
	ID_AVAILABLE_UP,
	ID_AVAILABLE_DOWN,
	ID_SELECTED_ADD,
	ID_SELECTED_REMOVE,
	ID_SELECTED_COPY,
	ID_SELECTED_AUTO,
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
	enum { kColField = 0, kColExpression };

	explicit ibResourceModel(ibValueDataComposition* composition) : m_composition(composition) { ResetFromList(); }

	void ResetFromList() {
		Reset(m_composition != nullptr ? (unsigned int)m_composition->GetModelComposer().TotalCount() : 0u);
	}

	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		if (m_composition == nullptr)
			return;
		const ibDataComposer& composer = m_composition->GetModelComposer();
		wxString func, path;
		if (row >= composer.TotalCount() || !composer.GetTotalAt(row, func, path))
			return;   // BOUNDS FIRST — a queued paint can outlive the line it was queued for

		if (col == kColField)
			variant = path;
		else if (col == kColExpression)
			// No function means the text IS the expression — the same rule the renderer follows.
			variant = func.IsEmpty() ? path : func + wxT("(") + path + wxT(")");
	}

	// ⭐ THE EXPRESSION IS EDITED IN THE CELL, the way the query constructor's Totals tab edits its
	// own (Max, 2026-08-19: "look at the query constructor"). What arrives is either one of the ready
	// calls the cell offered — `SUM(Amount)` — or anything a person wrote in the editor behind "...".
	// Both land here as text, and the split is the same one the store already speaks: a FUNC and its
	// argument, or an empty func meaning "the text is the expression".
	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override {
		if (m_composition == nullptr || col != kColExpression)
			return false;
		ibDataComposer& composer = m_composition->GetModelComposer();
		if (row >= composer.TotalCount())
			return false;

		wxString text = variant.GetString();
		text.Trim(true).Trim(false);
		if (text.IsEmpty())
			return false;   // a resource with no expression is not a resource — and empty would read as "delete"

		wxString func, path;
		if (!SplitCall(text, func, path)) { func.clear(); path = text; }
		// Through the COMPOSITION, not the store it holds: a cell edit is a change like any other,
		// and reaching past the composition is what kept it from being announced.
		return m_composition->SetTotal(row, func, path);
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
	ibValueDataComposition* m_composition;
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
// a parameter answers four separate questions and folding any two of them into one caption is how
// 1C ends up with a page nobody can read:
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

	explicit ibParameterModel(ibValueDataComposition* composition) : m_composition(composition) { ResetFromList(); }

	void ResetFromList() {
		Reset(m_composition != nullptr ? (unsigned int)m_composition->ParameterCount() : 0u);
	}

	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		if (m_composition == nullptr || row >= m_composition->ParameterCount())
			return;   // BOUNDS FIRST — a queued paint can outlive the row it was queued for
		switch (col) {
		case kColName:
			// AN AUTO PARAMETER SAYS SO. Not decoration: it is the difference between a row that can
			// be renamed or removed here and one that is written in the query text.
			variant = m_composition->IsParameterFromQuery(row)
				? m_composition->GetParameterName(row) + wxT("  (") + _("from query") + wxT(")")
				: m_composition->GetParameterName(row);
			break;
		case kColValue:      variant = m_composition->GetParameterValue(row).GetString(); break;
		case kColType:       variant = ibDescribeTypes(m_composition->GetParameterType(row), m_composition->GetSourceMetaData()); break;
		case kColExpression: variant = m_composition->GetParameterExpression(row); break;
		case kColUser:       variant = m_composition->IsParameterUserSettable(row); break;
		default: break;
		}
	}

	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override {
		if (m_composition == nullptr || row >= m_composition->ParameterCount())
			return false;
		switch (col) {
		case kColValue:      return m_composition->SetParameterValue(row, ibValue(variant.GetString()));
		case kColExpression: return m_composition->SetParameterExpression(row, variant.GetString());
		case kColUser:       return m_composition->SetParameterUserSettable(row, variant.GetBool());
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
	ibValueDataComposition* m_composition;
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

	explicit ibVariantModel(ibValueDataComposition* composition) : m_composition(composition) { ResetFromList(); }

	void ResetFromList() {
		Reset(m_composition != nullptr ? (unsigned int)m_composition->VariantCount() : 0u);
	}

	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		if (m_composition == nullptr || col != kColName)
			return;
		if (row >= m_composition->VariantCount())
			return;   // BOUNDS FIRST — a queued paint can outlive the variant it was queued for
		variant = m_composition->GetVariantName(row);
	}
	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override {
		if (m_composition == nullptr || col != kColName)
			return false;
		return m_composition->SetVariantName(row, variant.GetString());
	}

private:
	ibValueDataComposition* m_composition;
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

	explicit ibComposerStructureModel(std::function<std::vector<ibDataComposer::Output>*()> outputs)
		: m_outputs(std::move(outputs)) {
	}

	// THE AXIS A ROW READS — its levels, or null where the coordinate points at nothing. Every
	// reader goes through here, so "is there such an output / such an axis" is answered once.
	const std::vector<ibDataComposer::GroupNode>* AxisOf(const ibStructurePos& pos) const {
		std::vector<ibDataComposer::Output>* outputs = m_outputs ? m_outputs() : nullptr;
		if (outputs == nullptr || pos.m_output < 0 || (size_t)pos.m_output >= outputs->size())
			return nullptr;
		const ibDataComposer::Output& output = (*outputs)[pos.m_output];
		if (pos.m_axis == 1) return &output.m_columnGroups;
		if (pos.m_axis == 0) return &output.m_rowGroups;
		return nullptr;
	}

	size_t OutputCount() const {
		std::vector<ibDataComposer::Output>* outputs = m_outputs ? m_outputs() : nullptr;
		return outputs != nullptr ? outputs->size() : 0u;
	}

	size_t LevelCount(const ibStructurePos& pos) const {
		const std::vector<ibDataComposer::GroupNode>* axis = AxisOf(pos);
		return axis != nullptr ? axis->size() : 0u;
	}

	// A CROSS-TABLE SHOWS ITS TWO AXES as rows of their own; a plain grouping shows its levels
	// straight under the output, because naming an axis that has no counterpart says nothing.
	bool HasTwoAxes(int output) const {
		ibStructurePos columns; columns.m_output = output; columns.m_axis = 1;
		return LevelCount(columns) > 0;
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
			std::vector<ibDataComposer::Output>* outputs = m_outputs ? m_outputs() : nullptr;
			if (outputs == nullptr || (size_t)pos.m_output >= outputs->size())
				return;
			const ibDataComposer::Output& output = (*outputs)[pos.m_output];
			// ⚠ NOT "Grouping" — that is what its LEVELS are called, and two rows reading the same
			// word one under another is how a tree stops saying anything (Max, on the first run).
			// An output is a place: what it holds is its levels, what it is called is its own.
			const wxString kind = HasTwoAxes(pos.m_output) ? _("Table") : _("Output");
			variant = output.m_name.IsEmpty() ? kind : kind + wxT(" — ") + output.m_name;
			return;
		}

		if (pos.IsAxis()) {
			if (col == kColNode)
				variant = pos.m_axis == 1 ? _("Columns") : _("Rows");
			return;
		}

		const std::vector<ibDataComposer::GroupNode>* axis = AxisOf(pos);
		if (axis == nullptr || (size_t)pos.m_level >= axis->size())
			return;   // BOUNDS FIRST — a queued paint can outlive the level it was queued for
		const ibDataComposer::GroupNode& level = (*axis)[pos.m_level];

		if (col == kColNode) {
			// WHAT THIS LEVEL IS — asked of the level, not read off its emptiness. The rows are a
			// level of their own kind, and the row says so where a person looks for it.
			variant = level.m_kind == ibCompositionLevelKind::Details ? _("Detail records") : _("Grouping");
			return;
		}
		if (col == kColField) {
			// EVERY FIELD OF THE LEVEL, in order — a level groups by all of them together, so
			// showing only the first would describe a different report. The detail level has none
			// to show: what it prints is the rows, and the fields cell has nothing to add.
			if (level.m_fields.empty()) {
				variant = level.m_kind == ibCompositionLevelKind::Details ? wxString() : _("<detail records>");
				return;
			}
			wxString fields;
			for (const auto& field : level.m_fields) {
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

	std::function<std::vector<ibDataComposer::Output>*()> m_outputs;   // asked every time — no copy kept here
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
class ibStringListModel : public ibDataViewModel {
public:
	enum { kColText = 1 };

	explicit ibStringListModel(std::function<std::vector<wxString>*()> list) : m_list(std::move(list)) {}

	std::vector<wxString>* List() const { return m_list ? m_list() : nullptr; }
	size_t Count() const { const std::vector<wxString>* list = List(); return list != nullptr ? list->size() : 0u; }
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

	std::function<std::vector<wxString>*()> m_list;
	mutable std::vector<wxObjectDataPtr<class ibStringListModel::Row>> m_rows;
};

class ibStringListModel::Row : public ibDataViewObject {
public:
	explicit Row(size_t row) : m_row(row) {}
	size_t GetRow() const { return m_row; }
	virtual bool IsContainer() const override { return false; }
	virtual ibDataViewItem GetParentItem() const override { return ibDataViewItem(); }
private:
	size_t m_row;
};

ibStringListModel::Row* ibStringListModel::RowFor(size_t row) const
{
	while (m_rows.size() <= row)
		m_rows.push_back(wxObjectDataPtr<Row>(new Row(m_rows.size())));
	return m_rows[row].get();
}

int ibStringListModel::RowAt(const ibDataViewItem& item) const
{
	const Row* row = static_cast<const Row*>(item.GetID());
	return row != nullptr ? (int)row->GetRow() : wxNOT_FOUND;
}

ibDataViewItem ibStringListModel::ItemForRow(size_t row) const
{
	return row < Count() ? ibDataViewItem(RowFor(row)) : ibDataViewItem();
}

void ibStringListModel::GetValue(wxVariant& variant, const ibDataViewItem& item, unsigned int col) const
{
	const std::vector<wxString>* list = List();
	const int row = RowAt(item);
	if (list == nullptr || row == wxNOT_FOUND || (size_t)row >= list->size() || col != kColText)
		return;
	variant = (*list)[row];
}

unsigned int ibStringListModel::GetFirstFetch(const ibDataViewItem& parent, const ibDataViewItem&,
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

	explicit ibGroupingFieldsModel(std::function<ibDataComposer::GroupNode*()> level)
		: m_level(std::move(level)) {
	}

	ibDataComposer::GroupNode* Level() const { return m_level ? m_level() : nullptr; }
	size_t FieldCount() const {
		const ibDataComposer::GroupNode* level = Level();
		return level != nullptr ? level->m_fields.size() : 0u;
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
		const ibDataComposer::GroupNode* level = Level();
		const int row = RowAt(item);
		if (level == nullptr || row == wxNOT_FOUND || (size_t)row >= level->m_fields.size())
			return;   // BOUNDS FIRST — a queued paint can outlive the level it was queued for
		if (col == kColField)
			variant = level->m_fields[row].m_path;
		else if (col == kColKind)
			variant = ibValue::CreateEnumObject<ibValueEnumGroupKind>(level->m_fields[row].m_kind).GetString();
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

	std::function<ibDataComposer::GroupNode*()> m_level;
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
	ibComposerGroupingDialog(ibComposerSettingsPanel* panel, const ibDataComposer::GroupNode& seed)
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
		m_model = new ibGroupingFieldsModel([this]() -> ibDataComposer::GroupNode* { return &m_node; });
		m_view->AssociateModel(m_model);

		m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Field"),
			new ibRowValueCellRenderer(this, ibComposerFieldChooser(m_panel),
				[this](const ibDataViewItem& row) -> ibValue {
					const int at = m_model->RowAt(row);
					return (at != wxNOT_FOUND && (size_t)at < m_node.m_fields.size())
						? ibValue(new ibValueCompositionField(m_node.m_fields[at].m_path)) : ibValue();
				},
				[this](const ibDataViewItem& row, const ibValue& value) {
					const int at = m_model->RowAt(row);
					if (at == wxNOT_FOUND || (size_t)at >= m_node.m_fields.size())
						return;
					ibValueCompositionField* field = nullptr;
					if (value.ConvertToValue(field) && field != nullptr)
						m_node.m_fields[at].m_path = field->GetPath();
					else
						m_node.m_fields.erase(m_node.m_fields.begin() + at);   // cleared = out of the key
					Refresh();
				}),
			ibGroupingFieldsModel::kColField, FromDIP(240), wxAlignment::wxALIGN_LEFT));

		// THE UNFOLD BELONGS TO THE FIELD — same rule as on the tab, and the same refusal when a
		// level of several fields is asked to walk one field's parent chain.
		m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Kind"),
			new ibRowValueCellRenderer(this, ibComposerFieldChooser(m_panel),
				[this](const ibDataViewItem& row) -> ibValue {
					const int at = m_model->RowAt(row);
					return (at != wxNOT_FOUND && (size_t)at < m_node.m_fields.size())
						? ibValue::CreateEnumObject<ibValueEnumGroupKind>(m_node.m_fields[at].m_kind) : ibValue();
				},
				[this](const ibDataViewItem& row, const ibValue& value) {
					const int at = m_model->RowAt(row);
					if (at == wxNOT_FOUND || (size_t)at >= m_node.m_fields.size())
						return;
					const ibQueryDimUnfold kind = value.ConvertToEnumValue<ibQueryDimUnfold>();
					if (kind != ibQueryDimUnfold::Elements && m_node.m_fields.size() > 1) {
						wxMessageBox(_("This grouping is made of several fields, and a hierarchy unfolds "
						               "one field's parent chain.\n\nGive the hierarchy field a grouping "
						               "of its own."),
							GetTitle(), wxOK | wxICON_WARNING, this);
						return;
					}
					m_node.m_fields[at].m_kind = kind;
					Refresh();
				}),
			ibGroupingFieldsModel::kColKind, FromDIP(150), wxAlignment::wxALIGN_LEFT));

		sizer->Add(m_view, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));

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
	ibDataComposer::GroupNode Node() const
	{
		ibDataComposer::GroupNode node = m_node;
		node.m_kind = node.m_fields.empty()
			? ibCompositionLevelKind::Details : ibCompositionLevelKind::Grouping;
		return node;
	}

private:
	void AddField()
	{
		ibValueCompositionField* field = m_panel != nullptr ? m_panel->ChooseStructureField(this) : nullptr;
		if (field == nullptr)
			return;   // closed without picking
		m_node.m_fields.push_back({ field->GetPath(), ibQueryDimUnfold::Elements });
		Refresh();
	}

	void RemoveField()
	{
		const int at = m_model->RowAt(m_view->GetSelection());
		if (at == wxNOT_FOUND || (size_t)at >= m_node.m_fields.size())
			return;
		m_node.m_fields.erase(m_node.m_fields.begin() + at);
		Refresh();
	}

	// The order of the fields is the order they are PRINTED in, side by side on the heading — so it
	// is worth moving, and moving it is a swap.
	void MoveField(int delta)
	{
		const int at = m_model->RowAt(m_view->GetSelection());
		const int to = at + delta;
		if (at == wxNOT_FOUND || to < 0 || (size_t)at >= m_node.m_fields.size() || (size_t)to >= m_node.m_fields.size())
			return;
		std::swap(m_node.m_fields[at], m_node.m_fields[to]);
		Refresh();
		const ibDataViewItem row = m_model->ItemForRow((size_t)to);
		if (row.IsOk())
			m_view->Select(row);
	}

	void Refresh()
	{
		m_model->Rebuild();
		m_hint->SetLabel(m_node.m_fields.empty()
			// ⚠ ASCII ONLY IN A UI LITERAL — this file has no BOM, so MSVC reads it as ANSI and an em
			// dash comes out as three bytes of mojibake on screen (seen live 2026-08-21). The rule is
			// stated at the top of the toolbars above; this line is what happens when it is forgotten.
			? _("No fields: this node prints the DETAIL RECORDS - the rows under the grouping above it.")
			: _("The fields of one grouping are printed side by side, on one heading."));
		Layout();
	}

	ibComposerSettingsPanel*  m_panel = nullptr;
	ibDataComposer::GroupNode m_node;
	ibGroupingFieldsModel*    m_model = nullptr;
	ibDataViewCtrl*           m_view  = nullptr;
	wxStaticText*             m_hint  = nullptr;
};

// ⭐ THE SETTINGS ARE A PANEL, AND THE DIALOG IS ONE OF ITS HOSTS. A composer declared in the
// metadata is edited on a TAB of its own (the designer opens it like a form or a template — see
// docViewComposer), while a composition held by a form is edited modally from the gridbox. The
// same shape the list settings already took: content in a panel, the modal window a thin wrapper
// around it, so the two hosts cannot drift about what a setting is.
ibComposerSettingsPanel::ibComposerSettingsPanel(wxWindow* parent, ibValueDataComposition* composer)
	: wxPanel(parent, wxID_ANY),
	  m_composer(composer),
	  m_settings(new ibValueListSettings()),
	  m_fieldTree(new ibSettingsFieldTree())
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	// ⭐ THE WINDOW OPENS ON WHAT THE TEXT SAYS TODAY. Describing the query is what fills the
	// composition's column schema, and every field list in this window — the panel's trees below,
	// the picker, the resources page — reads that schema. It is built by RebuildSource, which runs
	// when a property changes or the attribute is read back; a composition whose text was typed and
	// not applied has none, and the whole window then offers nothing to filter, sort or group by.
	// Asking for it here costs one parse and makes the state the same however the window was reached.
	if (m_composer != nullptr)
		m_composer->ApplySource();

	// ⭐ THE NAMES A PARAMETER EXPRESSION MAY CALL, made to exist BEFORE anything can be edited.
	//
	// An expression is checked against the module MANAGER (see CheckExpression), and a manager that
	// has never been compiled carries an empty bytecode — so `CurrentDate()` came back as "procedure
	// or function not detected", a true statement about an empty world. Compiling it is what fills
	// it, and it happens HERE, once, because compiling rebuilds modules and refreshes what is bound
	// to them: from inside a cell editor that same rebuild destroys the renderer mid-call.
	PrepareModuleContext();

	// THE VARIANTS AS THEY STAND, kept so Cancel can put them back — see the Cancel binding below.
	if (m_composer != nullptr)
		m_composer->WriteVariants(m_openState);


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
	// THE BUFFER AND THE FIELDS FIRST — every pane below is built over them.
	BindFieldSource();
	if (m_composer != nullptr && m_settings != nullptr)
		ibLoadSettingsFromComposer(m_settings, m_composer->GetModelComposer(), m_composer->GetListSettings());
	// ⚠ AND THE STRUCTURE, which is a buffer of its own. Loading one and not the other left the
	// window with no outputs at all: the tree showed the report and nothing under it, and "Add
	// grouping" had nowhere to add — it looked like a dead command rather than an empty buffer.
	LoadStructure();

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
	if (m_composer != nullptr)
		m_composer->OnChildChanged();
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
	// every time a composer is merely LOOKED at — and capturing + announcing unconditionally meant an
	// untouched tab came back as "the configuration changed", asterisk and all (found by the final
	// audit, 2026-08-20). Every other door announces itself where it writes; the only change this
	// function makes on its own is landing the filter / sort / structure buffer.
	if (m_composer != nullptr && m_settingsDirty) {
		m_composer->CaptureActiveVariant();   // what was edited belongs to the variant it was edited in
		m_settingsDirty = false;
		// The signal was already raised where the edit happened (see the editors' SetOnChanged), so
		// it is deliberately NOT raised again here.
	}

	return true;
}

// ⭐ AND WHAT "CANCEL" MEANS. Switching variants inside this panel WRITES — the composer holds one
// set of settings at a time, so activating another one is not a preview. The whole set is
// snapshotted when the panel opens, and this puts it back.
void ibComposerSettingsPanel::RestoreOpenState()
{
	if (m_composer != nullptr)
		m_composer->ReadVariants(m_openState);
}

// ---------------------------------------------------------------------------
//  ibDialogComposerSettings — the MODAL host: the panel plus OK / Cancel.
// ---------------------------------------------------------------------------

bool ibDialogComposerSettings::ShowComposerSettings(ibValueDataComposition* composer)
{
	if (composer == nullptr)
		return false;
	wxWindow* top = (wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr;
	ibDialogComposerSettings dlg(top, composer);
	return dlg.ShowModal() == wxID_OK;
}

ibDialogComposerSettings::ibDialogComposerSettings(wxWindow* parent, ibValueDataComposition* composer)
	: wxDialog(parent, wxID_ANY, _("Data composer settings"), wxDefaultPosition, wxSize(900, 620),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	m_panel = new ibComposerSettingsPanel(this, composer);
	mainSizer->Add(m_panel, 1, wxALL | wxEXPAND, FromDIP(6));
	mainSizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxALIGN_RIGHT, FromDIP(6));
	SetSizer(mainSizer);

	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (m_panel != nullptr && !m_panel->Commit())
			return;   // the panel objected and said so — stay on it
		EndModal(wxID_OK);
	}, wxID_OK);

	Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
		if (m_panel != nullptr)
			m_panel->RestoreOpenState();
		e.Skip();   // the dialog closes the way it always did
	}, wxID_CANCEL);
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
	wxSplitterWindow* outer = new wxSplitterWindow(parent, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	outer->SetMinimumPaneSize(FromDIP(120));

	wxWindow* variantPane = BuildVariantPane(outer);

	wxSplitterWindow* inner = new wxSplitterWindow(outer, wxID_ANY,
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
	m_variantModel = new ibVariantModel(m_composer);
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
				const ibDataComposer::GroupNode* level = LevelAtRow(row);
				return (level != nullptr && !level->m_fields.empty())
					? ibValue(new ibValueCompositionField(level->m_fields.front().m_path)) : ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibDataComposer::GroupNode* level = LevelAtRow(row);
				if (level == nullptr)
					return;
				// ⚠ THE DETAIL LEVEL GROUPS BY NOTHING, and that is what it IS. A field written here
				// would turn the rows into a heading without anybody asking for one, so the cell
				// stays silent on it — deleting the level is how it stops being there.
				if (level->m_kind == ibCompositionLevelKind::Details)
					return;
				// An empty value CLEARS the head field — which leaves the level with none, and a
				// level with no fields IS the detail records. The rest of its fields stay where
				// they are: this cell speaks for one of them.
				ibValueCompositionField* field = nullptr;
				const bool chosen = value.ConvertToValue(field) && field != nullptr;
				if (!chosen) {
					if (!level->m_fields.empty())
						level->m_fields.erase(level->m_fields.begin());
				}
				else if (level->m_fields.empty()) {
					level->m_fields.push_back({ field->GetPath(), ibQueryDimUnfold::Elements });
				}
				else {
					level->m_fields.front().m_path = field->GetPath();
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
	m_structureModel = new ibComposerStructureModel([this] { return &m_structure; });
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
	if (m_fieldTree) {
		m_fieldTree->Attach(m_groupingFieldTree);
		m_fieldTree->Populate(m_groupingFieldTree);
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
		if (m_fieldTree)
			AddGroupingFieldFromTree(m_fieldTree->GetDragItem());
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
	m_groupingModel = new ibGroupingFieldsModel([this]() -> ibDataComposer::GroupNode* {
		return CurrentLevel();   // the remembered node — this model is read DURING a selection change
	});
	m_groupingView->AssociateModel(m_groupingModel);

	// THE FIELD, as a VALUE — the same cell the structure tree uses, opening the same picker.
	m_groupingView->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Field"),
		new ibRowValueCellRenderer(this, ibComposerFieldChooser(this),
			[this](const ibDataViewItem& row) -> ibValue {
				ibDataComposer::GroupNode* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
				const int at = m_groupingModel != nullptr ? m_groupingModel->RowAt(row) : wxNOT_FOUND;
				return (level != nullptr && at != wxNOT_FOUND && (size_t)at < level->m_fields.size())
					? ibValue(new ibValueCompositionField(level->m_fields[at].m_path)) : ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibDataComposer::GroupNode* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
				const int at = m_groupingModel != nullptr ? m_groupingModel->RowAt(row) : wxNOT_FOUND;
				if (level == nullptr || at == wxNOT_FOUND || (size_t)at >= level->m_fields.size())
					return;
				ibValueCompositionField* field = nullptr;
				if (value.ConvertToValue(field) && field != nullptr)
					level->m_fields[at].m_path = field->GetPath();
				else
					level->m_fields.erase(level->m_fields.begin() + at);   // cleared = removed from the key
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
				ibDataComposer::GroupNode* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
				const int at = m_groupingModel != nullptr ? m_groupingModel->RowAt(row) : wxNOT_FOUND;
				return (level != nullptr && at != wxNOT_FOUND && (size_t)at < level->m_fields.size())
					? ibValue::CreateEnumObject<ibValueEnumGroupKind>(level->m_fields[at].m_kind) : ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibDataComposer::GroupNode* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
				const int at = m_groupingModel != nullptr ? m_groupingModel->RowAt(row) : wxNOT_FOUND;
				if (level == nullptr || at == wxNOT_FOUND || (size_t)at >= level->m_fields.size())
					return;
				const ibQueryDimUnfold kind = value.ConvertToEnumValue<ibQueryDimUnfold>();
				if (kind != ibQueryDimUnfold::Elements && level->m_fields.size() > 1) {
					wxMessageBox(_("This grouping is made of several fields, and a hierarchy unfolds "
					               "one field's parent chain.\n\nGive the hierarchy field a grouping "
					               "of its own."),
						_("Data composer settings"), wxOK | wxICON_WARNING, this);
					return;
				}
				level->m_fields[at].m_kind = kind;
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
wxWindow* ibComposerSettingsPanel::BuildFieldSetPage(wxWindow* parent, ibFieldSet set)
{
	wxSplitterWindow* splitter = new wxSplitterWindow(parent, wxID_ANY, wxDefaultPosition,
		wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);

	// LEFT — everything the source can give, which is where a set is picked FROM.
	wxPanel* left = new wxPanel(splitter);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
	leftSizer->Add(new wxStaticText(left, wxID_ANY, _("Available fields")), 0, wxALL, FromDIP(4));
	PageOf(set).m_sourceTree = new wxTreeCtrl(left, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_SINGLE);
	leftSizer->Add(PageOf(set).m_sourceTree, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
	left->SetSizer(leftSizer);
	if (m_fieldTree) {
		m_fieldTree->Attach(PageOf(set).m_sourceTree);
		m_fieldTree->Populate(PageOf(set).m_sourceTree);
	}

	// RIGHT — this node's own set, with the inheritance switch over it.
	wxPanel* right = new wxPanel(splitter);
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);

	// ⚠ IDS ARE PER PAGE. Two pages of the same shape live in one window, and a command id they
	// share would reach whichever of them was bound last — so the available page takes one triple
	// and the selected page the next.
	const bool available = set == ibFieldSet::Available;
	const int idAdd    = available ? ID_AVAILABLE_ADD    : ID_SELECTED_ADD;
	const int idRemove = available ? ID_AVAILABLE_REMOVE : ID_SELECTED_REMOVE;
	const int idCopy   = available ? ID_AVAILABLE_COPY   : ID_SELECTED_COPY;
	const int idAuto   = available ? ID_AVAILABLE_AUTO   : ID_SELECTED_AUTO;
	const int idUp     = available ? ID_AVAILABLE_UP     : ID_SELECTED_UP;
	const int idDown   = available ? ID_AVAILABLE_DOWN   : ID_SELECTED_DOWN;

	PageOf(set).m_autoBox = new wxCheckBox(right, idAuto, _("Auto - take the fields from above"));
	rightSizer->Add(PageOf(set).m_autoBox, 0, wxALL, FromDIP(4));

	wxToolBar* bar = new wxToolBar(right, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_FLAT | wxTB_NODIVIDER | wxTB_HORIZONTAL);
	bar->AddTool(idAdd, _("Add field"), ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("Add field"));
	bar->AddTool(idRemove, _("Delete"), ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete"));
	bar->AddTool(idCopy, _("Copy"), ibSettingsArt(wxASCII_STR(wxART_COPY), this), _("Copy"));
	bar->AddSeparator();
	// THE ORDER OF THE SET IS THE ORDER IT READS IN, so it is moved, not re-entered.
	bar->AddTool(idUp, _("Move up"), ibSettingsArt(wxASCII_STR(wxART_GO_UP), this), _("Move up"));
	bar->AddTool(idDown, _("Move down"), ibSettingsArt(wxASCII_STR(wxART_GO_DOWN), this), _("Move down"));
	bar->Realize();
	rightSizer->Add(bar, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));

	PageOf(set).m_view = new ibDataViewCtrl(right, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(PageOf(set).m_view);
	PageOf(set).m_model = new ibStringListModel([this, set]() -> std::vector<wxString>* {
		return CurrentFieldSet(set);
	});
	PageOf(set).m_view->AssociateModel(PageOf(set).m_model);
	// ⭐ A LINE OF THIS LIST IS A FIELD, so it is edited the way every other field is: the shared
	// row-value cell, which opens the SAME picker the sort and the grouping open (Max, 2026-08-21:
	// there was no way to open the picker here as one can in the sort). Drawn as text it could only
	// be re-made — delete the line, add another — which is a different verb for "I picked the wrong
	// one", and one this window offers nowhere else.
	PageOf(set).m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Field"),
		new ibRowValueCellRenderer(this, ibComposerFieldChooser(this),
			[this, set](const ibDataViewItem& row) -> ibValue {
				const std::vector<wxString>* list = CurrentFieldSet(set);
				const int at = PageOf(set).m_model != nullptr ? PageOf(set).m_model->RowAt(row) : wxNOT_FOUND;
				return (list != nullptr && at != wxNOT_FOUND && (size_t)at < list->size())
					? ibValue(new ibValueCompositionField((*list)[at])) : ibValue();
			},
			[this, set](const ibDataViewItem& row, const ibValue& value) {
				std::vector<wxString>* list = CurrentFieldSet(set);
				const int at = PageOf(set).m_model != nullptr ? PageOf(set).m_model->RowAt(row) : wxNOT_FOUND;
				if (list == nullptr || at == wxNOT_FOUND || (size_t)at >= list->size())
					return;
				// CLEARED MEANS REMOVED — an empty line in a set of fields is not a field, and the
				// "×" beside the picker is the only place that says so.
				ibValueCompositionField* field = nullptr;
				if (value.ConvertToValue(field) && field != nullptr)
					(*list)[at] = field->GetPath();
				else
					list->erase(list->begin() + at);
				MarkSettingsTouched();
				ReloadFieldSets();
			}),
		ibStringListModel::kColText, FromDIP(260), wxAlignment::wxALIGN_LEFT));
	rightSizer->Add(PageOf(set).m_view, 1, wxEXPAND | wxALL, FromDIP(4));
	right->SetSizer(rightSizer);

	splitter->SplitVertically(left, right, FromDIP(220));
	splitter->SetMinimumPaneSize(FromDIP(120));

	Bind(wxEVT_TOOL, [this, set](wxCommandEvent&) { OnFieldSetAdd(set); }, idAdd);
	Bind(wxEVT_TOOL, [this, set](wxCommandEvent&) { OnFieldSetRemove(set); }, idRemove);
	Bind(wxEVT_TOOL, [this, set](wxCommandEvent&) { OnFieldSetCopy(set); }, idCopy);
	Bind(wxEVT_TOOL, [this, set](wxCommandEvent&) { MoveFieldSetRow(set, -1); }, idUp);
	Bind(wxEVT_TOOL, [this, set](wxCommandEvent&) { MoveFieldSetRow(set, +1); }, idDown);
	Bind(wxEVT_CHECKBOX, [this, set](wxCommandEvent& e) { OnFieldSetAuto(set, e.IsChecked()); }, idAuto);

	// ⭐ FROM THE TREE INTO THE LIST, by hand. Double-clicking a field on the left puts it on the
	// right — the shortest gesture, and the one people try first; the toolbar's Add opens the same
	// picker for whoever reaches for a button instead.
	PageOf(set).m_sourceTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this, set](wxTreeEvent& e) {
		AddFieldFromTree(set, e.GetItem());
		e.Skip();
	});
	// ...and by dragging onto the list, the same gesture the filter and the sort answer to.
	right->SetDropTarget(new ibCallbackDropTarget([this, set] {
		if (m_fieldTree)
			AddFieldFromTree(set, m_fieldTree->GetDragItem());
	}));
	return splitter;
}

// THE SET THAT IS BEING EDITED, and the flag that says whether it is used at all. The report has
// no flag — it is the top, and there is nothing above it to inherit from.
std::vector<wxString>* ibComposerSettingsPanel::CurrentFieldSet(ibFieldSet set, bool** autoFlag)
{
	if (autoFlag != nullptr)
		*autoFlag = nullptr;

	const bool available = set == ibFieldSet::Available;

	if (ibDataComposer::GroupNode* level = CurrentLevel()) {
		if (autoFlag != nullptr)
			*autoFlag = available ? &level->m_availableAuto : &level->m_selectedAuto;
		return available ? &level->m_available : &level->m_selected;
	}

	const int output = m_currentNode.m_output;
	if (output >= 0 && (size_t)output < m_structure.size()) {
		ibDataComposer::Output& node = m_structure[output];
		if (autoFlag != nullptr)
			*autoFlag = available ? &node.m_availableAuto : &node.m_selectedAuto;
		return available ? &node.m_available : &node.m_selected;
	}
	// THE REPORT ITSELF — the top of the inheritance, so it has a set and no switch.
	return available ? &m_commonAvailableBuffer : &m_commonSelectedBuffer;
}

void ibComposerSettingsPanel::OnFieldSetAdd(ibFieldSet set)
{
	bool* autoFlag = nullptr;
	std::vector<wxString>* fields = CurrentFieldSet(set, &autoFlag);
	if (fields == nullptr)
		return;
	ibValueCompositionField* field = ChooseStructureField(this);
	if (field == nullptr)
		return;   // closed without picking

	// ADDING A FIELD MEANS THIS NODE HAS A SET OF ITS OWN — inheriting and adding at the same time
	// would leave two answers about where its fields come from.
	if (autoFlag != nullptr && *autoFlag) {
		*autoFlag = false;
		fields->clear();
	}
	fields->push_back(field->GetPath());
	MarkSettingsTouched();
	ReloadFieldSet(set);
}

void ibComposerSettingsPanel::OnFieldSetRemove(ibFieldSet set)
{
	std::vector<wxString>* fields = CurrentFieldSet(set);
	const int at = SelectedFieldSetRow(set);
	if (fields == nullptr || at == wxNOT_FOUND || (size_t)at >= fields->size())
		return;
	fields->erase(fields->begin() + at);
	MarkSettingsTouched();
	ReloadFieldSet(set);
}

// COPY A LINE — the third verb of the toolbar (Max: add, delete, copy). A field listed twice is
// not a mistake to refuse here: the same field can be wanted under two names, and what a duplicate
// means is the engine's answer, not this list's.
void ibComposerSettingsPanel::OnFieldSetCopy(ibFieldSet set)
{
	bool* autoFlag = nullptr;
	std::vector<wxString>* fields = CurrentFieldSet(set, &autoFlag);
	const int at = SelectedFieldSetRow(set);
	if (fields == nullptr || at == wxNOT_FOUND || (size_t)at >= fields->size())
		return;
	if (autoFlag != nullptr && *autoFlag)
		*autoFlag = false;   // editing the list is what makes it this node's own
	fields->insert(fields->begin() + at + 1, (*fields)[at]);
	MarkSettingsTouched();
	ReloadFieldSet(set);
}

// MOVE A LINE — the set is read in order, so its order is a setting like any other.
void ibComposerSettingsPanel::MoveFieldSetRow(ibFieldSet set, int delta)
{
	std::vector<wxString>* fields = CurrentFieldSet(set);
	const int at = SelectedFieldSetRow(set);
	if (fields == nullptr || at == wxNOT_FOUND || (size_t)at >= fields->size())
		return;
	const int target = at + delta;
	if (target < 0 || (size_t)target >= fields->size())
		return;
	std::swap((*fields)[at], (*fields)[target]);
	MarkSettingsTouched();
	ReloadFieldSet(set);
	// The cursor travels with the line — a move whose result you have to go and find again reads
	// as a move that did nothing.
	ibFieldSetPage& page = PageOf(set);
	if (page.m_view != nullptr && page.m_model != nullptr) {
		const ibDataViewItem row = page.m_model->ItemForRow((size_t)target);
		if (row.IsOk())
			page.m_view->Select(row);
	}
}

// A FIELD PICKED IN THE TREE goes into the set — the same act as Add, from the other side.
void ibComposerSettingsPanel::AddFieldFromTree(ibFieldSet set, const wxTreeItemId& item)
{
	ibFieldSetPage& page = PageOf(set);
	if (m_readOnly || page.m_sourceTree == nullptr || !item.IsOk())
		return;
	ibValueCompositionField* field = ibSettingsFieldTree::FieldAt(page.m_sourceTree, item);
	if (field == nullptr)
		return;   // a reference row is a road, not a field — double-clicking it unfolds instead

	bool* autoFlag = nullptr;
	std::vector<wxString>* fields = CurrentFieldSet(set, &autoFlag);
	if (fields == nullptr)
		return;
	if (autoFlag != nullptr && *autoFlag) {
		*autoFlag = false;      // editing the list is what makes it this node's own
		fields->clear();
	}
	fields->push_back(field->GetPath());
	MarkSettingsTouched();
	ReloadFieldSet(set);
}

int ibComposerSettingsPanel::SelectedFieldSetRow(ibFieldSet set)
{
	ibFieldSetPage& page = PageOf(set);
	if (page.m_view == nullptr || page.m_model == nullptr)
		return wxNOT_FOUND;
	const ibDataViewItem row = page.m_view->GetSelection();
	return row.IsOk() ? page.m_model->RowAt(row) : wxNOT_FOUND;
}

void ibComposerSettingsPanel::OnFieldSetAuto(ibFieldSet set, bool checked)
{
	bool* autoFlag = nullptr;
	std::vector<wxString>* fields = CurrentFieldSet(set, &autoFlag);
	if (autoFlag == nullptr) {
		// The report has nothing above it — the box is not offered there, and a click that got
		// here anyway changes nothing.
		ReloadFieldSet(set);
		return;
	}
	*autoFlag = checked;
	// TURNING AUTO OFF STARTS FROM WHAT WAS INHERITED, not from an empty list: a person who
	// narrows a set expects to narrow the fields they were just looking at.
	if (!*autoFlag && fields != nullptr && fields->empty()) {
		const int output = m_currentNode.m_output;
		const bool available = set == ibFieldSet::Available;
		const std::vector<wxString>& fromAbove =
			(CurrentLevel() != nullptr && output >= 0 && (size_t)output < m_structure.size())
				? (available ? m_structure[output].m_available : m_structure[output].m_selected)
				: (available ? m_commonAvailableBuffer : m_commonSelectedBuffer);
		*fields = fromAbove;
	}
	MarkSettingsTouched();
	ReloadFieldSet(set);
}

void ibComposerSettingsPanel::ReloadFieldSets()
{
	ReloadFieldSet(ibFieldSet::Available);
	ReloadFieldSet(ibFieldSet::Selected);
}

void ibComposerSettingsPanel::ReloadFieldSet(ibFieldSet set)
{
	if (PageOf(set).m_model != nullptr)
		PageOf(set).m_model->Rebuild();
	if (PageOf(set).m_autoBox == nullptr)
		return;

	bool* autoFlag = nullptr;
	CurrentFieldSet(set, &autoFlag);
	// THE REPORT IS THE TOP: no inheritance switch, because there is nothing above it.
	PageOf(set).m_autoBox->Enable(autoFlag != nullptr && !m_readOnly);
	PageOf(set).m_autoBox->SetValue(autoFlag != nullptr && *autoFlag);
}

void ibComposerSettingsPanel::OnGroupingFieldAdd(wxCommandEvent&)
{
	ibDataComposer::GroupNode* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
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
	level->m_fields.push_back({ field->GetPath(), ibQueryDimUnfold::Elements });
	MarkSettingsTouched();
	RefreshStructureText();
	ReloadGrouping();
}

void ibComposerSettingsPanel::AddGroupingFieldFromTree(const wxTreeItemId& item)
{
	ibDataComposer::GroupNode* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
	if (m_readOnly || level == nullptr || m_groupingFieldTree == nullptr || !item.IsOk())
		return;
	ibValueCompositionField* field = ibSettingsFieldTree::FieldAt(m_groupingFieldTree, item);
	if (field == nullptr)
		return;   // a reference row unfolds instead of being taken
	level->m_fields.push_back({ field->GetPath(), ibQueryDimUnfold::Elements });
	MarkSettingsTouched();
	RefreshStructureText();
	ReloadGrouping((int)level->m_fields.size() - 1);
}

void ibComposerSettingsPanel::OnGroupingFieldRemove(wxCommandEvent&)
{
	ibDataComposer::GroupNode* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
	const int at = SelectedGroupingField();
	if (level == nullptr || at == wxNOT_FOUND || (size_t)at >= level->m_fields.size())
		return;
	level->m_fields.erase(level->m_fields.begin() + at);
	MarkSettingsTouched();
	RefreshStructureText();
	ReloadGrouping();
}

void ibComposerSettingsPanel::MoveGroupingField(int delta)
{
	ibDataComposer::GroupNode* level = m_groupingModel != nullptr ? m_groupingModel->Level() : nullptr;
	const int at = SelectedGroupingField();
	if (level == nullptr || at == wxNOT_FOUND || (size_t)at >= level->m_fields.size())
		return;
	const int target = at + delta;
	if (target < 0 || (size_t)target >= level->m_fields.size())
		return;
	std::swap(level->m_fields[at], level->m_fields[target]);
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
	const ibDataComposer::GroupNode* level = CurrentLevel();
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

	// WHAT THIS NODE MAY SEE — its own page, because it is a setting and not a list of hints: set
	// on the report it reaches every output, set on an output it reaches its levels.
	tabs->AddPage(BuildFieldSetPage(tabs, ibFieldSet::Available), _("Available fields"), false);

	// ⭐ …AND WHAT IT READS. This page was taken off as "two nearly identical lists, and the person
	// has to work out which one they meant". The objection was right and it is answered by saying
	// what each one ASKS, not by hiding one of them:
	//
	//   Available — what may be REACHED here. A field it excludes cannot be grouped, filtered or
	//               sorted by, and it does not appear in any picker (ibSettingsFieldTree).
	//   Selected  — what is actually READ. `ibDataComposer::SelectedFor(output[, level])` composes
	//               the query's SELECT list out of it, with the same report → output → level
	//               inheritance and the same Auto flag.
	//
	// And that is the question only this page can answer — one the window could not answer at all.
	// The engine has read this set from the beginning; nothing but a script could write it, so
	// every report composed from the window projected "every column the source has": forty
	// attributes read per row to print three.
	tabs->AddPage(BuildFieldSetPage(tabs, ibFieldSet::Selected), _("Selected fields"), false);

	m_filterEditor = new ibFilterEditor(tabs, m_settings, m_fieldTree.get());
	tabs->AddPage(m_filterEditor, _("Filter"), false);
	m_sortEditor = new ibSortEditor(tabs, m_settings, m_fieldTree.get());
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
	m_resourceModel = new ibResourceModel(m_composer);
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
	m_parameterModel = new ibParameterModel(m_composer);
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
	const ibMetaData* metaData = m_composer != nullptr ? m_composer->GetMetaData() : nullptr;
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
	if (m_composer == nullptr)
		return complaints;

	for (size_t i = 0; i < m_composer->ParameterCount(); ++i) {
		wxString complaint;
		// ⭐ THE OBJECT'S OWN CONFIG, through the attach chain — NOT the query's SOURCE config. A
		// parameter expression is SCRIPT, and the names it may call are the ones the configuration
		// this composition lives in declares; where the query reads its rows from is a different
		// question that happens to have the same answer most of the time (Max, 2026-08-21).
		if (CheckExpression(m_composer->GetParameterExpression(i), complaint, m_composer->GetMetaData()))
			continue;
		complaints += m_composer->GetParameterName(i) + wxT(": ") + complaint + wxT("\n");
	}
	return complaints;
}
// text only because that is what a cell carries; what is edited is the DECLARATION on the parameter.
bool ibComposerSettingsPanel::EditParameterType(wxString& text)
{
	const int idx = SelectedParameter();
	if (m_composer == nullptr || idx == wxNOT_FOUND)
		return false;

	ibTypeDescription declared = m_composer->GetParameterType((size_t)idx);
	if (!ibShowTypeSelector(this, ibSelectorDataType::ibSelectorDataType_any, std::vector<ibClassID>(),
			declared, m_composer->GetSourceMetaData(), /*allowEdit*/true, /*single*/false))
		return false;

	m_composer->SetParameterType((size_t)idx, declared);
	ReloadParameters();
	text = ibDescribeTypes(declared, m_composer->GetSourceMetaData());
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
	const ibMetaData* metaData = m_composer != nullptr ? m_composer->GetMetaData() : nullptr;
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
	if (m_composer != nullptr && idx != wxNOT_FOUND) {
		m_composer->SetParameterExpression(static_cast<size_t>(idx), text);
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
	const bool removable = idx != wxNOT_FOUND && m_composer != nullptr && !m_composer->IsParameterFromQuery(idx);

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
	if (m_composer == nullptr)
		return;
	const wxString name = wxGetTextFromUser(_("Parameter name"), _("Add parameter"), wxEmptyString, this);
	if (name.IsEmpty())
		return;
	const size_t added = m_composer->AddParameter(name);
	ReloadParameters();
	if (m_parameterView != nullptr)
		m_parameterView->Select(ibDataViewItem(reinterpret_cast<void*>(added + 1)));
}

void ibComposerSettingsPanel::OnParameterRemove(wxCommandEvent&)
{
	const int idx = SelectedParameter();
	if (m_composer == nullptr || idx == wxNOT_FOUND)
		return;
	// 🛑 THE STORE REFUSES AN AUTO PARAMETER — it is in the query text. Said out loud rather than
	// silently doing nothing, because the row looks exactly like a removable one.
	if (!m_composer->RemoveParameter((size_t)idx)) {
		wxMessageBox(_("This parameter comes from the query text. Remove it from the query instead."),
			_("Parameters"), wxOK | wxICON_INFORMATION, this);
		return;
	}
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
	if (m_composer != nullptr)
		m_queryText->SetText(m_composer->GetQueryText());
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

// THE LADDER IS THE PANEL'S BUFFER, not the composition's live settings.
//
// ⭐ ONE BUFFER, ONE COMMIT (2026-08-19). The settings panel below opens transactionally: it loads
// Filter / Sort / Group from the composer, the user edits the copy, and OK CLEARS the composer's
// settings and re-applies the copy. A level written straight onto the live settings would therefore
// be wiped by that very commit — the panel would put back the ladder as it stood when the window
// opened. Editing the same buffer is what makes the whole window one transaction, and Cancel
// discard all of it.
ibValueGroupList* ibComposerSettingsPanel::Levels() const
{
	return m_settings != nullptr ? m_settings->GetGroup() : nullptr;
}

// THE FIELD PICKER, forwarded to the one thing that knows which fields this composition offers.
// The structure pane deliberately has no field tree of its own, so this modal picker IS the way a
// field gets in.
ibValueCompositionField* ibComposerSettingsPanel::ChooseStructureField(wxWindow* parent, const wxString& held)
{
	return m_fieldTree != nullptr ? m_fieldTree->ChooseField(parent != nullptr ? parent : this, held) : nullptr;
}

// WHICH FIELDS THIS COMPOSITION OFFERS — its own explorer, which is what its query resolved to.
void ibComposerSettingsPanel::BindFieldSource()
{
	// ⚠ BOTH STATEMENTS ARE GUARDED. The second one used to sit outside the `if` for want of a pair
	// of braces — a null field tree took the source check and then dereferenced anyway.
	if (m_fieldTree == nullptr)
		return;

	m_fieldTree->SetSource(m_composer, m_composer != nullptr ? m_composer->GetSourceMetaData() : nullptr);
	// ⭐ AND WHICH OF THOSE FIELDS ARE RESOURCES — asked of the composition every time the tree
	// draws, never copied into it. Being a resource is a DECLARATION this window makes on the
	// Resources tab; a list handed over once would be a second copy, and it would still say
	// "attribute" the moment somebody adds one (Max, 2026-08-22).
	m_fieldTree->SetResourceTest([this](const wxString& path) {
		if (m_composer == nullptr)
			return false;
		const ibDataComposer& composer = m_composer->GetModelComposer();
		for (size_t i = 0; i < composer.TotalCount(); ++i) {
			wxString func, resource;
			if (composer.GetTotalAt(i, func, resource) && resource.IsSameAs(path, false))
				return true;
		}
		return false;
	});
	// ⭐⭐ …AND WHICH OF THEM THE SELECTED NODE MAY USE AT ALL. The available set is a real
	// narrowing — it decides what a person is offered to group, filter, sort and show by — and it
	// is INHERITED: a level answers for itself, an output for its levels, the report for everything
	// (ibDataComposer::AvailableFor). Asked of the composition per draw, so editing the set on the
	// report changes what every pane below offers without anything being copied.
	//
	// EMPTY MEANS EVERYTHING, as it does throughout: a composition that narrows nothing offers the
	// whole source, which is the ordinary case.
	m_fieldTree->SetVisibleTest([this](const wxString& path) {
		const std::vector<wxString>* allowed = AvailableForCurrentNode();
		if (allowed == nullptr || allowed->empty())
			return true;
		for (const wxString& name : *allowed)
			if (name.IsSameAs(path, false))
				return true;
		return false;
	});
}

// WHAT THE SELECTED NODE MAY SEE — the buffer this window is editing, not the composition's live
// copy: the two differ until Apply, and offering the live one would ignore a narrowing just made.
// Null when there is no source of an answer at all (which reads as "everything").
const std::vector<wxString>* ibComposerSettingsPanel::AvailableForCurrentNode() const
{
	const int output = m_currentNode.m_output;
	if (output < 0 || (size_t)output >= m_structure.size())
		return &m_commonAvailableBuffer;              // the report itself — the top of the inheritance

	const ibDataComposer::Output& node = m_structure[output];
	const int axis = m_currentNode.m_axis, level = m_currentNode.m_level;
	const std::vector<ibDataComposer::GroupNode>& ladder = axis == 1 ? node.m_columnGroups : node.m_rowGroups;
	if (level >= 0 && (size_t)level < ladder.size() && !ladder[level].m_availableAuto)
		return &ladder[level].m_available;
	return node.m_availableAuto ? &m_commonAvailableBuffer : &node.m_available;
}

// THE BUFFER ONTO THE COMPOSITION — the settings half of "accept", on its own because a VARIANT
// SWITCH does exactly this much and no more: what was edited has to land before the store is asked
// to hold a different set. FALSE = objected to, nothing written, stay where you are.
bool ibComposerSettingsPanel::CommitSettings()
{
	if (m_composer == nullptr || m_settings == nullptr)
		return true;

	// THE SAME CHECK THE RUNTIME MAKES. A half-written line raises there; here that exception
	// becomes a warning and the window stays open on the offending setting, instead of closing and
	// quietly dropping it.
	try {
		ibValidateSettings(m_settings);
	}
	catch (const ibBackendException& err) {
		wxMessageBox(err.GetErrorDescription(), _("Data composer settings"), wxOK | wxICON_WARNING, this);
		return false;
	}

	// THE TREE GOES BACK TO THE COMPOSITION, not only to the store. The composer takes the filter
	// as ONE expression, which cannot be read back out of it — so a tree committed only there is
	// applied but invisible: the next open shows an empty Filter tab over a report that is very
	// obviously filtered.
	if (ibValueListSettings* live = m_composer->GetListSettings())
		live->SetFilterRoot(m_settings->GetFilterRoot());
	ibCommitSettingsToComposer(m_composer->GetModelComposer(), m_settings);

	// THE STRUCTURE LANDS LAST — it owns the levels, and the commit above rebuilds the ladder from
	// the flat buffer. Applied afterwards, the outputs the window edited are the final word, and a
	// level of several fields survives a trip that the flat ladder cannot describe.
	//
	// The NODE buffers go into the structure first, since they are part of what it holds: a level's
	// own filter and sort belong to the level, not to the composition.
	CommitNodeSettings();
	ApplyStructure();
	return true;
}

// RE-READ THE SETTINGS — another variant was activated, so the composition now holds a different
// set and the editors have to start over on it. Same buffer object, same editors bound to it.
void ibComposerSettingsPanel::ReloadSettings()
{
	if (m_composer == nullptr || m_settings == nullptr)
		return;
	ibLoadSettingsFromComposer(m_settings, m_composer->GetModelComposer(), m_composer->GetListSettings());
	LoadStructure();   // the structure is a buffer of its own and is re-read with the rest
	if (m_filterEditor != nullptr) m_filterEditor->Reload();
	if (m_sortEditor   != nullptr) m_sortEditor->Reload();
}

// THE LEVEL THE REMEMBERED SELECTION POINTS AT — the one door every panel reads through, so none of
// them has to touch the tree control while it is telling us the selection changed.
ibDataComposer::GroupNode* ibComposerSettingsPanel::CurrentLevel()
{
	const int output = m_currentNode.m_output, axis = m_currentNode.m_axis,
	          level  = m_currentNode.m_level;
	if (output < 0 || axis < 0 || level < 0 || (size_t)output >= m_structure.size())
		return nullptr;
	std::vector<ibDataComposer::GroupNode>& ladder = axis == 1
		? m_structure[output].m_columnGroups : m_structure[output].m_rowGroups;
	return (size_t)level < ladder.size() ? &ladder[level] : nullptr;
}

ibDataComposer::GroupNode* ibComposerSettingsPanel::LevelAtRow(const ibDataViewItem& row)
{
	if (m_structureModel == nullptr)
		return nullptr;
	const ibStructurePos pos = m_structureModel->PosAt(row);
	if (!pos.IsLevel() || (size_t)pos.m_output >= m_structure.size())
		return nullptr;
	ibDataComposer::Output& output = m_structure[pos.m_output];
	std::vector<ibDataComposer::GroupNode>& axis = pos.m_axis == 1 ? output.m_columnGroups : output.m_rowGroups;
	return (size_t)pos.m_level < axis.size() ? &axis[pos.m_level] : nullptr;
}

// THE BUFFER OF ONE NODE. Created on first selection and kept until the window is accepted, so
// clicking away from a level and back finds what was written there.
//
// Its SORT starts from what the level holds; its FILTER starts empty, because a level's condition
// lives in the structure as an expression and this editor speaks the value TREE — the two meet at
// commit, where the tree is built into an expression (ibBuildFilterAst), the same way the
// composition-wide filter has always travelled.
ibValueListSettings* ibComposerSettingsPanel::NodeSettings(const ibNodeKey& key)
{
	const auto found = m_nodeSettings.find(key);
	if (found != m_nodeSettings.end())
		return found->second;

	ibValuePtr<ibValueListSettings> buffer(new ibValueListSettings());
	const int output = key.m_output, axis = key.m_axis, level = key.m_level;
	if ((size_t)output < m_structure.size()) {
		std::vector<ibDataComposer::GroupNode>& ladder = axis == 1
			? m_structure[output].m_columnGroups : m_structure[output].m_rowGroups;
		if ((size_t)level < ladder.size() && buffer->GetOrder() != nullptr)
			for (const auto& sort : ladder[level].m_sorts)
				buffer->GetOrder()->Add(sort.m_path,
					sort.m_ascending ? ibSortDirection_Ascending : ibSortDirection_Descending);

		// ⭐ …AND THE FILTER OPENS ON WHAT WAS WRITTEN. The level keeps the TREE (that is what is
		// saved and what an editor can edit); the expression beside it is derived and cannot be
		// taken apart back into lines. Starting empty is what made a level's condition look lost
		// every time the window was reopened.
		if ((size_t)level < ladder.size()) {
			ibValueFilterGroup* root = nullptr;
			if (ladder[level].m_filterTree.ConvertToValue(root) && root != nullptr)
				buffer->SetFilterRoot(root);
		}
	}
	m_nodeSettings[key] = buffer;
	return buffer;
}

// POINT THE SHARED EDITORS AT WHAT IS SELECTED. A LEVEL gets its own buffer; the report and an
// output keep the composition-wide one, which is what stands above every output (Max: the topmost
// filter and sort admit what all of them may see).
void ibComposerSettingsPanel::BindNodeEditors()
{
	if (m_filterEditor == nullptr || m_sortEditor == nullptr)
		return;

	// READ THE REMEMBERED NODE, not the control — see m_currentNode.
	ibValueListSettings* target = CurrentLevel() != nullptr ? NodeSettings(m_currentNode) : m_settings;
	m_filterEditor->SetSettings(target);
	m_sortEditor->SetSettings(target);
}

// EVERY NODE BUFFER BACK INTO THE STRUCTURE, before the structure itself is applied. A buffer that
// was never opened writes nothing: it does not exist.
void ibComposerSettingsPanel::CommitNodeSettings()
{
	if (m_composer == nullptr)
		return;

	for (const auto& entry : m_nodeSettings) {
		const int output = entry.first.m_output, axis = entry.first.m_axis, level = entry.first.m_level;
		if ((size_t)output >= m_structure.size())
			continue;
		std::vector<ibDataComposer::GroupNode>& ladder = axis == 1
			? m_structure[output].m_columnGroups : m_structure[output].m_rowGroups;
		if ((size_t)level >= ladder.size())
			continue;
		ibDataComposer::GroupNode& node = ladder[level];

		node.m_sorts.clear();
		if (ibValueSortList* order = entry.second->GetOrder())
			for (size_t i = 0; i < order->Count(); ++i)
				node.m_sorts.push_back({ order->GetField(i),
					order->GetDirection(i) == ibSortDirection_Ascending });

		// The tree becomes the expression the engine reads. A level's condition HIDES headings and
		// never reaches the WHERE — that is decided where it is applied, not here.
		node.m_filterAst = ibBuildFilterAst(m_composer->GetModelComposer(), entry.second->GetFilterRoot());
		++node.m_filterAstVersion;
		// ⭐ AND THE TREE ITSELF STAYS ON THE LEVEL — that is what is SAVED and what this window
		// reopens on. Committing only the expression is what made a level's filter live until the
		// window closed and no further: an expression runs, but nobody can edit it back into lines.
		node.m_filterTree = entry.second->GetFilterRoot() != nullptr
			? ibValue(static_cast<ibValueFilterGroup*>(entry.second->GetFilterRoot())) : ibValue();
	}
}

// THE SNAPSHOT, taken whole. Copying the outputs — rather than reading them through some flattened
// view — is what lets this window edit a level made of several fields, a second output beside the
// first, and a column axis: all three are things a flat ladder cannot say.
void ibComposerSettingsPanel::LoadStructure()
{
	m_structure.clear();
	m_commonSelectedBuffer.clear();
	m_commonAvailableBuffer.clear();
	if (m_composer == nullptr)
		return;
	m_structure = m_composer->GetModelComposer().Outputs();
	// ⚠ BOTH COMPOSITION-WIDE SETS travel with the structure: they are the top of the same
	// inheritance, and editing them on the REPORT row is editing these. Only the selected one was
	// carried, and the Available fields page is the one this window actually shows — so everything
	// a person narrowed on the report was read from an empty buffer and written back nowhere.
	m_commonSelectedBuffer  = m_composer->GetModelComposer().CommonSelected();
	m_commonAvailableBuffer = m_composer->GetModelComposer().CommonAvailable();
}

// ...AND PUT BACK WHOLE, on accept. The outputs ARE the structure, so there is nothing to merge:
// what the window holds is what the composition should have.
//
// ⚠ Applied AFTER the filter / sort buffer (see Commit), because that one still writes the first
// output's own filter and sort — the two touch different fields of the same output, and the order
// is what keeps them from overwriting each other.
void ibComposerSettingsPanel::ApplyStructure()
{
	if (m_composer == nullptr || m_structure.empty())
		return;

	std::vector<ibDataComposer::Output>& live = m_composer->GetModelComposer().Outputs();
	// The first output KEEPS the filter and sort just committed into it; everything the structure
	// owns is replaced. A whole-vector assignment would put the buffer's (stale) filter back.
	for (size_t i = 0; i < m_structure.size(); ++i) {
		if (i >= live.size())
			live.push_back(ibDataComposer::Output());
		ibDataComposer::Output& target = live[i];
		target.m_name          = m_structure[i].m_name;
		target.m_rowGroups     = m_structure[i].m_rowGroups;
		target.m_columnGroups  = m_structure[i].m_columnGroups;
		target.m_selected      = m_structure[i].m_selected;
		target.m_selectedAuto  = m_structure[i].m_selectedAuto;
		target.m_available     = m_structure[i].m_available;
		target.m_availableAuto = m_structure[i].m_availableAuto;
	}
	// The composition-wide sets — the top of the inheritance, edited on the report row.
	m_composer->GetModelComposer().CommonSelected()  = m_commonSelectedBuffer;
	m_composer->GetModelComposer().CommonAvailable() = m_commonAvailableBuffer;

	// An output the window removed is removed here too — but never the first one: a composition
	// always has at least one output, and "no outputs" is not a state anything downstream handles.
	if (live.size() > m_structure.size())
		live.resize(std::max<size_t>(1, m_structure.size()));
}

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
	if (m_fieldTree == nullptr)
		return;
	if (m_groupingFieldTree != nullptr)
		m_fieldTree->Populate(m_groupingFieldTree);
	if (m_availablePage.m_sourceTree != nullptr)
		m_fieldTree->Populate(m_availablePage.m_sourceTree);
	if (m_selectedPage.m_sourceTree != nullptr)
		m_fieldTree->Populate(m_selectedPage.m_sourceTree);
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
void ibComposerSettingsPanel::PopulateFieldTrees()
{
	// ONE READ, then every tree shows it. Asking the engine once per tree would parse the query
	// twice and could answer differently mid-edit.
	m_fields = (m_composer != nullptr) ? m_composer->GetConstructorFields()
	                                   : std::vector<ibQueryConstructorField>();

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
	if (data == nullptr || data->GetIndex() >= m_fields.size())
		return nullptr;
	return &m_fields[data->GetIndex()];
}

void ibComposerSettingsPanel::PopulateFieldTree(wxTreeCtrl* tree)
{
	if (tree == nullptr || m_composer == nullptr)
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

	// WHICH PATHS ARE RESOURCES — asked of the composition, never copied: adding one changes the
	// picture on the next fill without anything else being told.
	auto isResource = [this](const wxString& path) {
		if (m_composer == nullptr)
			return false;
		const ibDataComposer& composer = m_composer->GetModelComposer();
		for (size_t i = 0; i < composer.TotalCount(); ++i) {
			wxString func, resource;
			if (composer.GetTotalAt(i, func, resource) && resource.IsSameAs(path, false))
				return true;
		}
		return false;
	};

	// THE FIELDS THE ENGINE SAYS THIS QUERY OFFERS, carrying their TYPE — which is what lets the
	// resources page ask "which aggregates fit this one" instead of guessing. The same list the
	// query constructor builds its own trees from.
	//
	// The row carries the field's INDEX, so a later question about the selected row (its type, its
	// path) is answered from the field itself rather than by parsing the label back.
	for (size_t i = 0; i < m_fields.size(); ++i) {
		const ibQueryConstructorField& field = m_fields[i];
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
	const bool onOutput = m_currentNode.m_output >= 0 && m_currentNode.m_level < 0;

	wxMenu menu;
	ibAppendCmd(menu, ID_LEVEL_ADD, _("Add grouping"), wxASCII_STR(wxART_NEW), this);
	ibAppendCmd(menu, ID_LEVEL_REMOVE, _("Delete"), wxASCII_STR(wxART_DELETE), this)
		->Enable(onLevel || onOutput);
	menu.AppendSeparator();
	ibAppendCmd(menu, ID_LEVEL_UP, _("Move up"), wxASCII_STR(wxART_GO_UP), this)
		->Enable(onLevel || onOutput);
	ibAppendCmd(menu, ID_LEVEL_DOWN, _("Move down"), wxASCII_STR(wxART_GO_DOWN), this)
		->Enable(onLevel || onOutput);

	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnStructureAdd(e); }, ID_LEVEL_ADD);
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
		->Enable(m_composer != nullptr && m_composer->VariantCount() > 1);

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
	if (m_variantModel == nullptr || m_variantView == nullptr || m_composer == nullptr)
		return;
	m_variantModel->ResetFromList();
	if (m_variantBar != nullptr)
		m_variantBar->EnableTool(ID_VARIANT_REMOVE, m_composer->VariantCount() > 1);

	const size_t count = m_composer->VariantCount();
	const size_t row = (select >= 0 && (size_t)select < count) ? (size_t)select : m_composer->GetActiveVariant();
	// A VIRTUAL LIST KEYS ITS ROWS BY (index + 1) — the same off-by-one every list here obeys.
	m_variantView->Select(ibDataViewItem(reinterpret_cast<void*>(row + 1)));
}

// SWITCHING IS: keep what is on screen, then load the other snapshot.
//
// ⭐ WHAT IS ON SCREEN BELONGS TO THE VARIANT BEING LEFT. The panel edits a buffer over the
// composer, so it is committed first and the composition captures the result INTO the variant;
// only then does the other snapshot become the composer's settings and everything re-read it.
// Miss the capture and switching away silently discards the edit — the defect a person only finds
// when they come back.
void ibComposerSettingsPanel::ActivateVariant(size_t idx)
{
	// 🛑 VIEW ONLY — and this one WRITES, which is exactly why it needs saying. Switching variants
	// commits the buffer, captures it into the variant being left and makes another the composer's
	// settings; the grid stays selectable in a read-only tab, so without this a look-only session
	// could rewrite the composition by clicking a row (found by the final audit, 2026-08-20).
	if (m_readOnly)
		return;
	if (m_composer == nullptr || idx >= m_composer->VariantCount())
		return;
	if (idx == m_composer->GetActiveVariant())
		return;   // already there — a selection event fires for the row we just selected ourselves

	// A HALF-WRITTEN SETTING KEEPS US HERE, and the cursor goes back to the variant it belongs to:
	// leaving the list pointing at one variant while another is active is the lie that follows.
	if (!CommitSettings()) {
		ReloadVariants(wxNOT_FOUND);
		return;
	}
	m_composer->CaptureActiveVariant();
	m_composer->SetActiveVariant(idx);

	ReloadSettings();
	ReloadStructure();
	UpdateSettingsHeader();
}

void ibComposerSettingsPanel::OnVariantAdd(wxCommandEvent&)
{
	if (m_composer == nullptr)
		return;
	// A NEW VARIANT IS EMPTY, and it is named so it can be told apart before anything is in it.
	const size_t added = m_composer->AddVariant(wxString::Format(wxT("%s %u"),
		_("Variant"), (unsigned)(m_composer->VariantCount() + 1)));
	ReloadVariants((int)added);
	ActivateVariant(added);
}

// COPY — "it copies the groupings, filters, sorts and so on" (Max). The copy is made by the store,
// through the node the settings serialise into, so it copies whatever a settings object consists of
// today and whatever is added to it later.
void ibComposerSettingsPanel::OnVariantCopy(wxCommandEvent&)
{
	if (m_composer == nullptr)
		return;
	// THE ACTIVE VARIANT'S EDITS ARE PART OF WHAT IS COPIED, so they are captured first — copying a
	// variant and getting the state it had when the window opened is the surprise worth avoiding.
	if (!CommitSettings())
		return;
	m_composer->CaptureActiveVariant();

	const size_t source = m_composer->GetActiveVariant();
	const size_t added = m_composer->AddVariant(
		m_composer->GetVariantName(source) + wxT(" ") + _("(copy)"), (int)source);
	ReloadVariants((int)added);
	ActivateVariant(added);
}

void ibComposerSettingsPanel::OnVariantRemove(wxCommandEvent&)
{
	const int idx = SelectedVariant();
	if (m_composer == nullptr || idx == wxNOT_FOUND)
		return;
	// 🛑 THE LAST ONE STAYS — the store says no, and the button is greyed to say so first.
	if (!m_composer->RemoveVariant((size_t)idx))
		return;

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
		for (size_t out = 0; out < m_structure.size(); ++out) {
			const ibDataViewItem output = m_structureModel->ItemForOutput((int)out);
			if (output.IsOk())
				m_structureView->Expand(output);
			for (int axis = 0; axis <= 1; ++axis) {
				const std::vector<ibDataComposer::GroupNode>& ladder = axis == 1
					? m_structure[out].m_columnGroups : m_structure[out].m_rowGroups;
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
	if (const ibDataComposer::GroupNode* level = CurrentLevel()) {
		// EVERY FIELD OF THE LEVEL — it groups by all of them together, and a header naming only
		// the first would describe a narrower heading than the one being edited.
		wxString fields;
		for (const auto& field : level->m_fields) {
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
std::vector<ibDataComposer::GroupNode>* ibComposerSettingsPanel::AxisForCommand(int& at)
{
	at = wxNOT_FOUND;
	if (m_structure.empty())
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
		if (!m_structure.back().m_rowGroups.empty() || !m_structure.back().m_columnGroups.empty())
			m_structure.push_back(ibDataComposer::Output());
		// THE COMMAND MOVES THE CURSOR WITH IT — what is added lands in the output the command
		// just chose, and the cursor has to be looking at that one, not at where it started.
		m_currentNode = ibNodeKey((int)m_structure.size() - 1, 0, -1);
		return &m_structure.back().m_rowGroups;
	}

	const size_t output = pos.m_output >= 0 && (size_t)pos.m_output < m_structure.size()
		? (size_t)pos.m_output : 0u;
	const bool columns = pos.m_axis == 1;
	if (pos.IsLevel())
		at = pos.m_level;
	// The axis this command works on is where the cursor belongs afterwards — an output row and an
	// axis row both mean "the rows of this output" here, and the reload lands accordingly.
	m_currentNode = ibNodeKey((int)output, columns ? 1 : 0, at);
	return columns ? &m_structure[output].m_columnGroups : &m_structure[output].m_rowGroups;
}

void ibComposerSettingsPanel::OnStructureAdd(wxCommandEvent&)
{
	int at = wxNOT_FOUND;
	std::vector<ibDataComposer::GroupNode>* axis = AxisForCommand(at);
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
	ibComposerGroupingDialog dialog(this, ibDataComposer::GroupNode());
	if (dialog.ShowModal() != wxID_OK)
		return;
	axis->push_back(dialog.Node());
	MarkSettingsTouched();
	ReloadStructure((int)axis->size() - 1);
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
	ibDataComposer::GroupNode* level = LevelAtRow(row);
	if (level == nullptr)
		return;

	ibComposerGroupingDialog dialog(this, *level);
	if (dialog.ShowModal() != wxID_OK)
		return;

	const ibDataComposer::GroupNode edited = dialog.Node();
	// ITS OWN SETTINGS STAY ITS OWN — the form edits the fields and the kind; the filter, the sort
	// and the selected fields of this level are not its business.
	level->m_kind   = edited.m_kind;
	level->m_fields = edited.m_fields;
	MarkSettingsTouched();
	ReloadStructure(m_structureModel != nullptr ? m_structureModel->LevelAt(row) : wxNOT_FOUND);
	ReloadGrouping();
}

void ibComposerSettingsPanel::OnStructureRemove(wxCommandEvent&)
{
	// AN OUTPUT IS REMOVED WHOLE — with its levels, its axes and its node buffers. Never the last
	// one: a composition always has at least one output, and "no outputs" is not a state anything
	// downstream handles.
	// ⚠ AN AXIS ROW IS NOT THE OUTPUT — the coordinate says which it is, and it is asked (IsOutput),
	// not re-derived from the numbers. Testing "no level" alone made Delete on the "Columns" row
	// erase the whole output, its rows included.
	const int selectedOutput = m_currentNode.m_output;
	if (m_currentNode.IsOutput()
	    && (size_t)selectedOutput < m_structure.size() && m_structure.size() > 1) {
		m_structure.erase(m_structure.begin() + selectedOutput);
		std::map<ibNodeKey, ibValuePtr<ibValueListSettings>> kept;
		for (auto& entry : m_nodeSettings) {
			const int output = entry.first.m_output;
			if (output == selectedOutput)
				continue;                              // its buffers go with it
			const int shifted = output > selectedOutput ? output - 1 : output;
			kept[ibNodeKey(shifted, entry.first.m_axis, entry.first.m_level)] = entry.second;
		}
		m_nodeSettings = std::move(kept);

		m_currentNode = ibNodeKey(std::min<int>(selectedOutput, (int)m_structure.size() - 1), -1, -1);
		MarkSettingsTouched();
		ReloadStructure();
		return;
	}

	int level = wxNOT_FOUND;
	std::vector<ibDataComposer::GroupNode>* axis = AxisForCommand(level);
	if (axis == nullptr || level == wxNOT_FOUND || (size_t)level >= axis->size())
		return;

	// ⭐ REMOVING A GROUPING BREAKS THE CHAIN, so everything nested under it goes with it (Max).
	// Pulling the deeper levels up instead would silently re-parent them: "by warehouse, then by
	// item" would become "by item", which is a different report nobody asked for.
	const int removedFrom = level;
	axis->erase(axis->begin() + level, axis->end());

	// The node buffers of what was removed go too — otherwise a filter written on a level that no
	// longer exists would land on whatever level later takes that coordinate.
	const int output = m_currentNode.m_output, axisIndex = m_currentNode.m_axis;
	for (auto it = m_nodeSettings.begin(); it != m_nodeSettings.end(); ) {
		const bool sameAxis = it->first.m_output == output && it->first.m_axis == axisIndex;
		it = (sameAxis && it->first.m_level >= removedFrom) ? m_nodeSettings.erase(it) : ++it;
	}
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
	if (m_currentNode.IsOutput() && (size_t)selectedOutput < m_structure.size()) {
		const int target = selectedOutput + delta;
		if (target < 0 || (size_t)target >= m_structure.size())
			return;
		std::swap(m_structure[selectedOutput], m_structure[target]);
		// The node buffers travel with their outputs, or a filter written on one would stay behind
		// and land on whatever moved into its place.
		std::map<ibNodeKey, ibValuePtr<ibValueListSettings>> moved;
		for (auto& entry : m_nodeSettings) {
			const int output = entry.first.m_output;
			const int carried = output == selectedOutput ? target : (output == target ? selectedOutput : output);
			moved[ibNodeKey(carried, entry.first.m_axis, entry.first.m_level)] = entry.second;
		}
		m_nodeSettings = std::move(moved);

		m_currentNode = ibNodeKey(target, m_currentNode.m_axis, m_currentNode.m_level);
		MarkSettingsTouched();
		ReloadStructure();
		return;
	}

	int level = wxNOT_FOUND;
	std::vector<ibDataComposer::GroupNode>* axis = AxisForCommand(level);
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
wxArrayString ibComposerSettingsPanel::ResourceChoices() const
{
	wxArrayString words;
	const int row = SelectedResourceIndex();
	if (m_composer == nullptr || row == wxNOT_FOUND)
		return words;

	wxString func, path;
	if (!m_composer->GetModelComposer().GetTotalAt((size_t)row, func, path) || path.IsEmpty())
		return words;

	// THE FIELD'S TYPE is what decides, so the path is looked up among the fields this composition
	// offers. A path that is not one of them (a hand-written expression) has no type to ask about.
	for (const ibQueryConstructorField& field : m_fields) {
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
	if (m_composer == nullptr)
		return false;

	ibDialogQueryExpression editor(this, _("Resource"), m_fields, nullptr,
		m_composer->GetSourceMetaData(), /*readOnly*/false);
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
	if (field == nullptr || m_composer == nullptr || field->m_name.IsEmpty())
		return;

	const wxArrayString aggregates = ibAggregatesForField(*field);
	// NOTHING THE ENGINE ADMITS means the field cannot be folded at all — added as a bare
	// expression rather than wrapped in a call it would refuse.
	m_composer->AddTotal(aggregates.IsEmpty() ? wxString() : aggregates[0], field->m_name);
	ReloadResources();
	// THE CURSOR FOLLOWS WHAT WAS ADDED — a row you have to go and find reads as a command that did
	// nothing, and the Expression cell is the next thing a person reaches for.
	if (m_resourceView != nullptr && m_composer->GetModelComposer().TotalCount() > 0)
		m_resourceView->Select(ibDataViewItem(
			reinterpret_cast<void*>(m_composer->GetModelComposer().TotalCount())));
}

// THE EXPRESSION EDITOR FOR THE ROW UNDER THE CURSOR — the same door the cell's "..." opens, on the
// toolbar for reach. With no row selected it adds one: writing an expression is a way to CREATE a
// resource too, not only to change one.
void ibComposerSettingsPanel::OnResourceExpression(wxCommandEvent&)
{
	if (m_readOnly)   // view only — see OnAddResource
		return;
	if (m_composer == nullptr)
		return;

	const int row = SelectedResourceIndex();
	wxString text;
	if (row != wxNOT_FOUND) {
		wxString func, path;
		if (m_composer->GetModelComposer().GetTotalAt((size_t)row, func, path))
			text = func.IsEmpty() ? path : func + wxT("(") + path + wxT(")");
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

	if (row != wxNOT_FOUND)
		m_composer->SetTotal((size_t)row, func, path);   // the COMPOSITION's door, not the store's — it has to hear this
	else
		m_composer->AddTotal(func, path);
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
	if (idx == wxNOT_FOUND || m_composer == nullptr)
		return;
	m_composer->RemoveTotal(static_cast<size_t>(idx));   // through the composition, so it hears the change
	ReloadResources();


}

// THE COMPOSITION IS AUTHORED HERE. It has no main table to fall back on — the query IS its
// source — so the constructor edits the one text everything else hangs off, and what it renders
// comes straight back into the box. The names resolve against the composition's own config, never
// a quietly-defaulted active one.
void ibComposerSettingsPanel::OnBuildQuery(wxCommandEvent&)
{
	if (m_composer == nullptr || m_queryText == nullptr)
		return;

	wxString text = m_queryText->GetText();
	// EXCLUDING TOTALS: a composition folds through its RESOURCES and its levels are its GROUPINGS,
	// so a TOTALS clause in this text would be the same setting written where no window can show it.
	if (!ibShowQueryConstructor(this, text, m_composer->GetSourceMetaData(), /*readOnly*/false,
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
	if (m_composer == nullptr || m_queryText == nullptr)
		return;
	if (m_queryText->GetText() != m_composer->GetQueryText()) {
		m_composer->SetQueryText(m_queryText->GetText());
		m_queryDirty = true;
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
	if (m_composer == nullptr || m_queryText == nullptr || m_readOnly)
		return;

	m_composer->SetQueryText(m_queryText->GetText());
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
	m_queryDirty = false;
	RefreshFromQueryText();
}

// EVERYTHING THAT FOLLOWS FROM THE QUERY, re-read in one place — so the idle pass, leaving the page
// and accepting the window cannot disagree about what "the query changed" entails.
void ibComposerSettingsPanel::RefreshFromQueryText()
{
	if (m_composer == nullptr)
		return;

	m_composer->ApplySource();

	// ⭐ AND THIS IS WHERE THE TEXT EDIT IS ANNOUNCED — once per pause, not once per character.
	// SetQueryText deliberately stays silent (see it): whoever hears the signal may re-render a
	// whole form editor, and doing that per keystroke made typing a query unusable.
	m_composer->OnChildChanged();

	// The fields follow the text: a field added to the query is there to group by without closing
	// anything.
	PopulateFieldTrees();
	// ...AND SO DO THE EDITORS. Their trees are the SAME question asked in the lower half of this
	// window; leaving them behind is how one screen ends up with two answers about one query.
	ReloadFields();

	// AND THE PARAMETERS: a new &Name in the text is a new row here, and one the text stopped asking
	// for is gone. The list follows the query, so it is re-read where the query is applied.
	ReloadParameters();

	const wxString error = m_composer->GetQueryError();
	if (m_queryError != nullptr) {
		m_queryError->SetLabel(error);
		m_queryError->Show(!error.IsEmpty());
		if (m_queryError->GetParent() != nullptr)
			m_queryError->GetParent()->Layout();
	}
}

int ibComposerSettingsPanel::SelectedResourceIndex() const
{
	return ibSelectedRow(m_resourceView);
}
