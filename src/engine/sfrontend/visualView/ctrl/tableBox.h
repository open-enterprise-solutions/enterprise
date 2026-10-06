#ifndef __TABLE_BOX_H__
#define __TABLE_BOX_H__

#include "backend/tabularModel.h"   // ibValueModel, and through it ibDataViewItem and wxDVC_DEFAULT_WIDTH
#include "backend/compiler/enumUnit.h"   // ibValueEnumeration template + AddEnumeration

#include "sfrontend/visualView/ctrl/window.h"
#include "sfrontend/visualView/ctrl/typeControl.h"
#include "backend/sourceDescription.h"   // ibSourceDescription (full [head, field] binding path)

#include <deque>
#include <map>
#include <vector>

// The two choices a table's own properties make — what the cursor highlights and how a source with
// folders or parents is shown. Values of the PROPERTIES, so they are the model's; a client reads them
// off the frame and draws accordingly.
enum ibDataViewSelectionMode {
	ibDataViewSelectCell = 0,
	ibDataViewSelectRow  = 1,
};
enum ibDataViewViewMode {
	ibDataViewTree,
	ibDataViewHierarchical,
	ibDataViewList,
};

// Column groups: the orientation (ibColumnGroupKind) — a property of the group control; the geometry it
// decides is the client's.
#include "sfrontend/visualView/ctrl/columnLayout.h"

struct ibSettingsDescription;   // compositionDescription.h — the reader's setting a list is narrowed by

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

//COMMON TABLE & COLUMN
constexpr ibClassID g_controlTableBoxCLSID = control_to_clsid("CT_TABL");
constexpr ibClassID g_controlTableBoxColumnCLSID = control_to_clsid("CT_TBLC");
constexpr ibClassID g_controlTableBoxColumnGroupCLSID = control_to_clsid("CT_TBCG");

//********************************************************************************************
//*                                 Value TableBox                                           *
//********************************************************************************************

class ibValueEnumTableBoxSelectionMode :
	public ibValueEnumeration<ibDataViewSelectionMode> {
	public:
	ibValueEnumTableBoxSelectionMode() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibDataViewSelectionMode::ibDataViewSelectCell, wxT("SelectCell"), _("Select cell"));
		AddEnumeration(ibDataViewSelectionMode::ibDataViewSelectRow, wxT("SelectRow"), _("Select row"));
	}
private:
};

class ibValueEnumTableBoxViewMode :
	public ibValueEnumeration<ibDataViewViewMode> {
	public:
	ibValueEnumTableBoxViewMode() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibDataViewViewMode::ibDataViewHierarchical, wxT("Hierarchical"), _("Hierarchical"));
		AddEnumeration(ibDataViewViewMode::ibDataViewTree, wxT("Tree"), _("Tree"));
		AddEnumeration(ibDataViewViewMode::ibDataViewList, wxT("List"), _("List"));
	}
private:
};

class ibValueModelTableBox : public ibValueWindowComposite,
	public ibTypeControlFactory, public ibSourceObject {
	public:

	////////////////////////////////////////////////////////////////////////////////////////
	void SetSource(const ibMetaID& id) { m_propertySource->SetValue(id); ibValueModelTableBox::RefreshModel(true); }
	// Full binding path [headAttrId, tableSection, ...] — the resolve walks the attribute.
	void SetSource(const std::vector<ibSourceId>& path) { m_propertySource->SetValue(ibSourceDescription(path)); ibValueModelTableBox::RefreshModel(true); }
	ibMetaID GetSource() const { return m_propertySource->GetValueAsSource(); }
	// This tablebox's own bound path ([headAttr, tableSection] or [headAttr]) — a child column's
	// path is this prefix + its own field id(s); the row-relative tail is what the resolve walks.
	const std::vector<ibSourceHop>& GetSourcePath() const { return m_propertySource->GetValueAsPath(); }
	////////////////////////////////////////////////////////////////////////////////////////

	ibValueModelTableBox();

	//get model
	ibValueModel* GetTableModel() const { return m_tableModel; }

	// Choice mode = this table is a VALUE PICKER (opened to return a selection to a caller). The TableBox owns
	// this affordance (like a form owns Close / Update): when on, GetStandardCommands composes Select FIRST.
	// The front-owned property is the SOLE source of truth — set at form-build from the source explorer's choice
	// flag (autobuild) or by the runtime open-as-choice path (SetChoiceMode). The dumb model carries no choice.
	bool IsChoiceMode() const { return m_propertyChoiceMode->GetValueAsBoolean(); }
	// Set at FORM-BUILD time from the source explorer (a picker source stamps its main table node — see
	// ibSourceExplorer::IsChoiceMode); this is the source-of-truth for the runtime open-as-choice path.
	void SetChoiceMode(bool on = true) { m_propertyChoiceMode->SetValue(on); }

	// Single point: resolve a dot-path column's value for ONE row (asked per shown cell by the column's
	// UpdateCell). First hop via the DUMB model (GetValueByMetaID), deeper hops walk the reference.
	// Returns false for a plain column (model resolves it).
	bool ResolveCellValue(const ibDataViewItem& item, const class ibValueModelTableBoxColumn* column, wxVariant& out) const;

	// A dot-path column — its binding reaches PAST the tablebox prefix + one row column. Such a
	// column is resolved through the dot: read-only, so its cells take no editing (the column's ReadOnly).
	bool IsPathColumn(const class ibValueModelTableBoxColumn* column) const;

	// A FOREIGN-root column — its path is NOT under this tablebox's own bound prefix, so it is rooted
	// at a different form source: the object ABOVE the table (its header). Such a column lives in the
	// tablebox alongside the real columns but reads from the form, CONSTANT across every row of the
	// tabular section (the header doesn't vary per line). Read-only, like a dot-path column. (Mode 2)
	bool IsForeignColumn(const class ibValueModelTableBoxColumn* column) const;

	//methods & attributes
	void FillControlMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	//support icons
	static wxIcon GetIconGroup();

	//other
	void AddColumn();
	// Add a GROUP of columns — the columns then move into it.
	void AddColumnGroup();

	// ⭐ WHERE A COLUMN WITH THIS GROUP NAME HANGS — this table for an empty name, else the
	// group of that name, made on the first ask and found again on every later one.
	//
	// THE one description of "when a group opens", shared by both builders — the auto-built
	// form (ibValueForm::BuildForm) and the designer's refill-from-source. Written twice,
	// they would drift; and finding the group BY NAME rather than by remembering a run also
	// means the source may hand its columns out in any order.
	//
	// `created` (optional) says a group control was just made, which the designer needs in
	// order to tell its editor about it.
	ibValueFrame* GetColumnGroupHolder(const wxString& group, bool* created = nullptr);

	// THE COLUMNS OF A MODEL THAT NAMES ITS OWN (AutoCreateColumn) — a value handed to the table from
	// code: the column controls are rebuilt from the model's column collection. Not in the designer.
	void CreateColumnCollection();

	void CreateTable(bool recreateModel = false);

	void CreateModel(bool recreateModel = false);
	void RefreshModel(bool recreateModel = false);

	// get current line if exist
	ibValueModel::ibValueModelReturnLine* GetCurrentLine() const { return m_tableCurrentLine; }

	// Single source of truth for programmatic current-line mutation. Every path that puts the
	// table on a row — the client moving its cursor (OnCurrentRowChanged), a created value found among the
	// rows (OnUpdate) or the owner control's (OnCreated), a row the model just added, the script's
	// CurrentRow — routes through here. Pass nullptr for `line` to clear. The client is told by the
	// next frame (CurrentRow), and by the next fetch that hands the row out.
	void ApplyCurrentLine(ibValueModel::ibValueModelReturnLine* line);

public:

	virtual ~ibValueModelTableBox();

	// Designer: rebuild the DEFAULT columns from the bound source explorer (walks the path to the leaf
	// section / list). Shared by the drag-to-create drop and the inspector's Source-change refill.
	virtual void RefillFromSource() override;

	// Available sources = the owning form's attributes of THIS control's kind (table).
	virtual bool GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const override;

	//Get source attribute
	virtual const class ibBackendSourceColumn* GetSourceAttributeObject() const { return m_propertySource->GetSourceAttributeObject(); }
	// Unbound (no source picked) -> the table is not shown (ibValueWindow::Update writes Visible off).
	// Ask the PROPERTY (IsEmptyProperty), NOT GetSourceDesc — the latter walks the source and can be
	// broken; the property flag is cheap and safe.
	virtual bool IsUnbound() const override { return m_propertySource->IsEmptyProperty(); }
	// On a field or a TABULAR SECTION this base does not use -> not available, and not shown by the same
	// gate. Body in tableBox.cpp — a section is no column.
	virtual bool IsAvailable() const override;
	virtual ibSelectorDataType GetFilterDataType() const { return ibSelectorDataType::ibSelectorDataType_table; }
	virtual ibSourceDataType GetFilterSourceDataType() const { return ibSourceDataType::ibSourceDataType_table; }

	//Get source object
	virtual ibSourceObject* GetSourceObject() const;

	// This tablebox's bound path ([headAttr, tableSection] or [headAttr] for a list) — MUTABLE ref (like
	// GetTypeDesc): read it, or assign to bind (GetSourceDesc() = desc). A child column composes its own
	// path as THIS path + its column id.
	virtual ibSourceDescription& GetSourceDesc() const override { return m_propertySource->GetValueAsSourceDesc(); }

#pragma region _source_data_

	//get metaData from object
	virtual const ibValueMetaObjectCompositeData* GetSourceMetaObject() const;
	// ibSourceObject's source metadata — same context this control already exposes via GetMetaData.
	virtual const ibMetaData* GetSourceMetaData() const override { return GetMetaData(); }
	//get ref class
	virtual ibClassID GetSourceClassType() const;
	//Get presentation
	virtual wxString GetSourceCaption() const { return GetString(); }

#pragma endregion

	//get form owner
	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }

	//get metaData
	virtual const ibMetaData* GetMetaData() const;

	//get type description
	virtual ibTypeDescription& GetTypeDesc() const {
		return m_propertySource->GetValueAsTypeDesc();
	}

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	// before run — the model
	virtual bool InitializeControl() { CreateModel(); return true; }

	// ⭐ CREATED — the form has opened: the model taken again, the reader's own setting marked "restore on open"
	// put on it, and a choice form's list standing on the value it was opened for. The window's OnCreated.
	virtual void OnCreated(ibVisualHost* host) override;
	// …and the form closed: the table lets go of its model's view (the window's Cleanup).
	virtual void OnCleanup(ibVisualHost* host) override;

	//get title
	virtual wxString GetControlTitle() const {

		if (!m_propertySource->IsEmptyProperty()) {
			ibValue pvarPropVal;
			if (m_propertySource->GetDataValue(pvarPropVal))
				return _("TableBox") + wxT(": ") + stringUtils::GenerateSynonym(pvarPropVal.GetString());
		}

		return _("TableBox") + wxT(": ") + _("<empty source>");
	}

	// The window's state and the command bar's, then what the table decides at run time: the
	// current row as a handle the client was given (CurrentRow, absent while that row has not been
	// handed out), the column the cursor stands on (CurrentColumn), the cell whose editor is open
	// (Edit {Row, Column} — the client opens its editor there, and closes one the table did not take),
	// ChoiceMode and ViewMode. The model's view is attached here, and a value just created from this
	// list is looked for among the rows — what the window used to do whenever it was redrawn.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	// The table's own: Row — the cursor moved (Row: a handle; Column — across to it); Open without a
	// Column — a row activated; Sort, Move and Resize — a column's header (Column). A CELL'S are its
	// column's, the field's events on a row (OnCellEvent). Everything else is the composite's (the
	// command bar).
	virtual bool OnClientEvent(ibClientEvent event, const ibDataNode& args) override;

	// ⭐ THE ROWS A CLIENT SHOWS, written by the table itself. `request`: Direction (s32: 0 reset,
	// 1 forward, -1 backward), Anchor (s32 row handle; 0 none), Count (s32, 1..500), Parent (s32 handle
	// of the container whose children are asked for; 0 the top level). `response`: one child per row
	// (metaId = its handle) with Container, Group (a group row's caption), ReadOnly (no cell of it can be
	// edited) and one child per SHOWN column (metaId = the column's control id) carrying the cell (the
	// column's UpdateCell); End when that side is exhausted; CurrentRow when the current row is among them.
	virtual bool Fetch(const ibDataNode& request, ibDataNode& response) override;

	//get component type
	virtual int GetComponentType() const { return COMPONENT_TYPE_WINDOW; }

	// A table bound to the form's MAIN attribute doesn't carry its own command bar — the form's
	// toolbar already serves those commands, so a table bar would just duplicate them (Add / Mark
	// as delete twice). Suppress it: no toolbar layer, no "Command interface" node, no AutoFill.
	virtual bool HasCommandBar() const override;

	// This table IS the form's main source — its WHOLE binding path is the main attribute (a single hop). THE
	// authoritative "am I the main view" fact: HasCommandBar reads it (a main view shows no bar of its own —
	// the form toolbar serves it), and the form's command-provider resolve finds it by this (formAction.cpp).
	// A nested source (a tabular section, path [mainAttr, section]) has the main attribute only as its HEAD —
	// not main-bound; it keeps its own bar.
	virtual bool IsMainSourceBound() const override;

	//support icons
	virtual wxIcon GetIcon() const;

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

	/**
	* Override actionData
	*/

	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType);
	// The command bar calls this (generic id, form). The TableBox reads the rows a command runs against — its
	// current row plus the create ANCHOR (resolved per view mode, see CallAsAction) — and either runs a view-state
	// command DIRECTLY against the table (Command_*), or forwards the OBJECT command to the model as
	// CallAsCommand(id, {selection, anchor, column}, form) — the ibStandardCommandTabular contract.
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	//contol value
	virtual bool HasValueInControl() const {
		return m_propertySource->IsEmptyProperty();
	}

	virtual bool GetControlValue(ibValue& pvarControlVal) const;
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue());

private:

	// WHAT THE CLIENT DID — the table's model methods its events map onto (OnClientEvent); each keeps
	// the model half of what the window's handler did.

	// The cursor moved to a row: the script's Selection event, and unless it refuses (standard
	// processing off), the row becomes the current line. True at once when it already is. False — an
	// unknown handle, or refused.
	bool OnCurrentRowChanged(s32 rowHandle);
	// ⭐ A CELL'S EVENT — Input, Change, Select, Open, Clear naming Row and Column: the field's events,
	// and the column's. Its editor and its buttons work on the current line's value, as the window's
	// did, so the row named becomes the current line first (unless the script refuses it).
	bool OnCellEvent(ibClientEvent event, const ibDataNode& args);
	// …and the column it stands on across — what the by-column verbs and the Edit command read.
	void OnCurrentColumnChanged(const ibFormID& columnId);
	// A header clicked: sort by that column's field — flipped when it already is the sole sort — and
	// re-read. False when the model does not sort or the column has no field to sort by.
	bool OnColumnClick(const ibFormID& columnId);
	// A column dragged to another place, possibly into another group (`holderId` 0 = the table itself):
	// the control tree is brought to the same story.
	bool OnColumnMoved(const ibFormID& columnId, const ibFormID& holderId, unsigned int position);
	// A column's header edge dragged: the width the column ASKS for is recorded.
	void OnColumnResized(const ibFormID& columnId, int width);
	// The person switched list / tree / hierarchy.
	void OnViewModeChanged(ibDataViewViewMode viewMode);
	// Double-click / Enter on a row, which becomes the current line: choice → the row is returned; a row
	// with an editable cell → that cell's editor opens (Edit); a read-only row → the model opens the row's
	// value (a list opens its object form). Then the script's OnActivateRow.
	void OnRowActivated(s32 rowHandle);
	// May the client open its inline editor on this cell?
	bool OnCellEditStarting(s32 rowHandle, const ibFormID& columnId) const;

	// What the model said happened to its rows — the model's view (ibTableModelView) calls these.
	// A row appended or inserted becomes the current line and the script hears OnAddRow; a row deleted
	// is heard by BeforeDeleteRow / OnDeleteRow and is no longer handed out.
	void OnRowAdded(const ibDataViewItem& item);
	void OnRowDeleted(const ibDataViewItem& item);

	// The commands of the view-state band the TableBox composes — run DIRECTLY against the table and its
	// model (no model → notifier shim).
	void Command_Choose(ibBackendValueForm* srcForm);
	void Command_FilterByCurrentColumn();
	void Command_ClearFilter();
	// ⭐ WHAT IS ON THE SCREEN, AS A DOCUMENT: the composition in force, printed by the report's own sheet
	// driver into a spreadsheet document, which is then shown. `chosen` — the columns to print (control
	// ids); empty = every shown column. See the body.
	void Command_OutputList(const std::vector<ibFormID>& chosen = std::vector<ibFormID>());

	// What the settings window applied on OK: the reader's setting, put on the composer, and the rows
	// re-read.
	void ApplyUserSettings(const ibSettingsDescription& settings);
	// The reader's own shelf, under the List category, the saving half: what is in force, under a name
	// (`id` empty = a new entry) — what the person picks or types arrives here. The restoring half is a
	// choice (ibChooseSavedSettings); the listing, rename, remove and the "restore on open" mark are the
	// backend's own (settingsComposer.h) at SettingsObjectKey().
	bool SaveSettings(const ibGuid& id, const wxString& name);

	// ⭐ WHERE THIS LIST'S SAVED SETTINGS LIVE — the guid of the METAOBJECT behind the attribute this
	// box is bound to, asked of the attribute's own source (Max, 2026-08-26: *"the unique identifier
	// of the attribute it is bound to, from the source — it is there"*). Not the control's guid: the
	// settings belong to what is shown, so the box may be redrawn without losing them. Empty when
	// nothing is bound, and then there is no shelf.
	ibGuid SettingsObjectKey() const;

	// The double-click's decision (see OnRowActivated); the column id to edit, 0 when none.
	ibFormID ActivateRow(const ibDataViewItem& item);
	// The cell the inline editor opens on for `item`: the current column when its cell is editable, else
	// the first shown column whose cell is. Null when no cell of the row is (a list row).
	class ibValueModelTableBoxColumn* FindEditableColumn(const ibDataViewItem& item) const;
	// This table's column with that control id, wherever it hangs (groups included); null when none.
	class ibValueModelTableBoxColumn* FindColumn(const ibFormID& columnId) const;

	// WHERE A NEW ELEMENT IS CREATED — the anchor a create command runs against, per view mode: the folder
	// drilled into (Hierarchical), the folder the current row is or stands in (Tree), none (List).
	ibDataViewItem GetCreateAnchor() const;

	// THE MODEL'S VIEW — added to the model's data view (what the window's AssociateModel did), once per
	// model: a model handed over since the last time takes it over, and a real model swap forgets the
	// rows and the current line of the old one.
	void AttachModelView();
	void DetachModelView();

	// THE ROWS HANDED OUT — a handle per row, from a counter, never reused.
	s32 HandRow(const ibDataViewItem& item, s32 parentHandle, bool& added);
	ibDataViewItem FindRow(s32 handle) const;
	// The handle of a row the client was given — by its object, else by its identity (a row re-read as a
	// new object, a restore stub). 0 when it was not handed out.
	s32 FindRowHandle(const ibDataViewItem& item) const;
	void ForgetRow(s32 handle);           // …the row and the rows handed out under it
	void ForgetChildRows(s32 handle);
	void ForgetAllRows();
	// Over the limit, the rows furthest from where the client is reading go: the end opposite to the
	// direction just fetched (`fromBack` after a backward fetch). `keep` — the top-level row the fetch was
	// under, never forgotten by it.
	void TrimRows(bool fromBack, s32 keep);

	// The model's view, defined in tableBox.cpp.
	class ibTableModelView;

	// One row handed out: the row, the container it was fetched under (0 = the top level) and the rows
	// fetched under it.
	struct ibFetchedRow {
		ibDataViewItem m_item;
		s32 m_parent = 0;
		std::vector<s32> m_children;
	};

#pragma region __property_define_h__

	ibPropertyCategory* m_categoryInfo = ibPropertyObject::CreatePropertyCategory(wxT("Info"), _("Info"));
	ibPropertyBoolean* m_propertyHeader = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("Header"), _("Header"),
		_("Whether the column headers are shown above the rows. On by default; clicking a header sorts by that column."), true);
	ibPropertyUInteger* m_propertyHeaderHeight = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryInfo, wxT("HeaderHeight"), _("Header height"),
		_("The header's height in text lines. At least 1, and never less than the column groups need, so a grouped header always fits."), 1);
	ibPropertyBoolean* m_propertyFooter = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("Footer"), _("Footer"),
		_("Whether a footer row is shown under the rows, carrying each column's footer text (a total, for example). Off by default."), false);
	ibPropertyUInteger* m_propertyFooterHeight = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryInfo, wxT("FooterHeight"), _("Footer height"),
		_("The footer's height in text lines, used when the footer is shown. Default 1."), 1);
	ibPropertyUInteger* m_propertyFreezeRow = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryInfo, wxT("FrezeeRow"), _("Frezee row"),
		_("How many leading rows stay in place while the rest scroll vertically. 0 (the default): none."), 0);
	ibPropertyUInteger* m_propertyFreezeCol = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryInfo, wxT("FrezeeCol"), _("Frezee column"),
		_("How many leading columns stay in place while the rest scroll horizontally - keeps a name column visible in a wide table. 0 (the default): none."), 0);
	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertySource* m_propertySource = ibPropertyObject::CreateProperty<ibPropertySource>(m_categoryData, wxT("Source"), _("Source"),
		_("The form attribute the table shows: a dynamic list, a value table or tree, or a tabular section of the form's object. The table's columns are bound to its fields, and its command bar gets the source's commands."));
	ibPropertyEnum<ibValueEnumTableBoxSelectionMode>* m_propertyRowSelectionMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTableBoxSelectionMode>>(m_categoryData, wxT("RowSelectionMode"), _("Row selection mode"),
		_("What the cursor highlights. Select cell (the default): one cell, moved cell by cell - suits a table edited in place. Select row: the whole row - suits a list rows are picked or opened from."),
		ibDataViewSelectionMode::ibDataViewSelectCell);
	ibPropertyEnum<ibValueEnumTableBoxViewMode>* m_propertyViewMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTableBoxViewMode>>(m_categoryData, wxT("ViewMode"), _("View mode"),
		_("How a source with folders or parents is shown. Hierarchical (the default): one level at a time, entering a folder to see its contents. Tree: an expandable tree. List: every row flat, hierarchy ignored. The user can switch it at run time."),
		ibDataViewViewMode::ibDataViewHierarchical);
	// Value-picker flag: when set (or when the bound list-model is a picker), the Select command is composed FIRST.
	ibPropertyBoolean* m_propertyChoiceMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("ChoiceMode"), _("Choice mode"),
		_("Whether the table works as a picker: double-click or Enter hands the current row back to whoever opened the form, instead of opening or editing it, and a Select command leads its command bar. Set automatically on a choice form's list."),
		false);
	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibEventControl* m_eventSelection = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("Selection"), _("Selection"), _("On double mouse click or pressing of Enter."), wxArrayString{ wxT("Control"), wxT("RowSelected"), wxT("StandardProcessing") });
	ibEventControl* m_eventOnActivateRow = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("OnActivateRow"), _("Activate row"), _("When row is activated"), wxArrayString{ {wxT("Control")} });
	ibEventControl* m_eventBeforeAddRow = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("BeforeAddRow"), _("Before add row"), _("When row addition mode is called"), wxArrayString{ wxT("Control"), wxT("Cancel"), wxT("Clone") });
	ibEventControl* m_eventBeforeDeleteRow = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("BeforeDeleteRow"), _("Before delete row"), _("When row deletion is called"), wxArrayString{ wxT("Control"), wxT("Cancel") });
	// After-add / after-delete pair — fires when the MODEL says a row was added / deleted (the model's
	// view, ibTableModelView: whatever added it — a command, the script), so script observes creation
	// regardless of source.
	ibEventControl* m_eventOnAddRow = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("OnAddRow"), _("On add row"), _("When a new row has been inserted"), wxArrayString{ wxT("Control"), wxT("RowAdded") });
	ibEventControl* m_eventOnDeleteRow = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("OnDeleteRow"), _("On delete row"), _("When a row has been deleted"), wxArrayString{ wxT("Control"), wxT("RowDeleted") });

#pragma endregion

	ibValuePtr<ibValueModel> m_tableModel;
	ibValuePtr<ibValueModel::ibValueModelReturnLine> m_tableCurrentLine;

	// THE MODEL'S VIEW and the model it was added to. The model owns the view once it is added (and deletes
	// it when it goes); it is HELD here so that it cannot go while the view is on it.
	ibTableModelView* m_modelView;
	ibValuePtr<ibValueModel> m_viewModel;

	// THE ROWS HANDED OUT — by handle, by row object (the reverse lookup), and the top-level ones in display
	// order (the ends a trim takes from). At most s_maxFetchedRows of them.
	std::map<s32, ibFetchedRow> m_fetchedRows;
	std::map<const ibDataViewObject*, s32> m_fetchedHandles;
	std::deque<s32> m_topRows;
	s32 m_nextRowHandle;

	// The folder the client has drilled into in the Hierarchical view (the Parent of its last reset) —
	// where a new element is created, and what Output list is delimited by. Empty at the top.
	ibDataViewItem m_drillItem;
	// The column the cursor stands on across (OnCurrentColumnChanged); 0 = none said.
	ibFormID m_currentColumn;
	// ⭐ THE CELL WHOSE EDITOR IS OPEN — what the window's editor was: opened by a row activated, the Edit
	// command or the client's own Input the table took; over with the text committed (Change) or the cursor
	// moved off it (Row). 0 = none.
	s32 m_editRow;
	ibFormID m_editColumn;
	// A sort changed the order: the next reset reads from the top instead of around the current row,
	// whose position was a position in the OLD order.
	bool m_fetchFromTop;

	// (NO SETTING KEPT HERE — see the gridbox: the ACTIVE one lives on the model, in its composer.)
};

//********************************************************************************************
//*                            TableBox column GROUP                                         *
//********************************************************************************************

// The orientation, as the user picks it. A TYPE rather than a flag: the grid asks
// what KIND a group is, and a third kind (say, one that wraps) is a member here and
// a case in the layout, not a second boolean somewhere.
class ibValueEnumTableBoxColumnGrouping :
	public ibValueEnumeration<ibColumnGroupKind> {
	public:
	ibValueEnumTableBoxColumnGrouping() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibColumnGroupHorizontal, wxT("Horizontal"), _("Horizontal"));
		AddEnumeration(ibColumnGroupVertical, wxT("Vertical"), _("Vertical"));
		AddEnumeration(ibColumnGroupInCell, wxT("InCell"), _("In cell"));
	}
private:
};

// A GROUP OF COLUMNS — a header that owns columns, plus the direction they run in.
//
// It renders NOTHING of its own in the rows: it is a title in the header and a
// decision about where its columns go. Horizontal keeps them side by side under one
// title (the classic look); Vertical stacks them inside one width, so the row grows
// taller instead of wider — twelve account-dimension columns become three, and the
// titles stop being cut to "Account di...".
//
// Groups nest, so "Amount (Qty Dr / Qty Cr / currency amount)" beside
// "Account Dr (its dimensions under it)" is just two of them side by side.
class ibValueModelTableBoxColumnGroup : public ibValueControl {
public:

	ibValueModelTableBoxColumnGroup();

	// The table this group serves — groups nest, so it is looked for up the tree.
	ibValueModelTableBox* GetOwner() const;

	// The same lines a notebook writes to add a page.
	void AddColumn();
	void AddColumnGroup();

	// Building a group from code takes exactly two things: what it is called and
	// which way its columns run. (The title is READ through GetControlTitle, like
	// every other control's — no second reader for it here.)
	void SetCaption(const wxString& caption) { m_propertyTitle->SetValue(caption); }
	void SetGrouping(ibColumnGroupKind kind) { m_propertyGrouping->SetValue(kind); }
	ibColumnGroupKind GetGrouping() const { return m_propertyGrouping->GetValueAsEnum(); }

	// Shown — its own Visible and the functional options; what it holds goes with it.
	bool IsGroupShown() const;

	//support icons
	static wxIcon GetIconGroup();

public:

	//get metaData
	virtual const ibMetaData* GetMetaData() const;

	//get title
	virtual wxString GetControlTitle() const;

	// Caption (its title, else its name) and Visible (IsGroupShown). Its kind, alignment and whether
	// the title is shown are the schema's: the client reads them off the properties as saved.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	//get component type — a header, not a window
	virtual int GetComponentType() const { return COMPONENT_TYPE_ABSTRACT; }

	//support icons
	virtual wxIcon GetIcon() const;

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:

	ibPropertyCategory* m_categoryInfo = ibPropertyObject::CreatePropertyCategory(wxT("Info"), _("Info"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryInfo, wxT("Title"), _("Title"),
		_("The group's title in the header, shown when Show title is on (an in-cell group always shows it as its one header cell). Can be written per language."), wxT(""));
	// VERTICAL by default — stacking the columns is what a group is added FOR. Side by
	// side is what they already do without one.
	ibPropertyEnum<ibValueEnumTableBoxColumnGrouping>* m_propertyGrouping = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTableBoxColumnGrouping>>(m_categoryInfo, wxT("Grouping"), _("Grouping"),
		_("Where the group's columns go. Vertical (the default): stacked within one width, so the row grows taller instead of wider. Horizontal: side by side under the group's title. In cell: side by side but merged under one header cell, reading as one field. Groups nest."),
		ibColumnGroupVertical);
	// OFF by default: the plain use of a group is to STACK or MERGE its columns, and
	// that needs no title. Turned on, the group takes a band of the header above them
	// and they read as one thing under it.
	ibPropertyBoolean* m_propertyShowTitle = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("ShowTitle"), _("Show title"),
		_("Whether the group's title takes a band of the header above its columns. Off (the default): the group only arranges its columns and costs the header no height."),
		false);

	ibPropertyCategory* m_categoryStyle = ibPropertyObject::CreatePropertyCategory(wxT("Style"), _("Style"));
	ibPropertyEnum<ibValueEnumHorizontalAlignment>* m_propertyHeaderAlign = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumHorizontalAlignment>>(m_categoryStyle, wxT("HeaderAlign"), _("Header align"),
		_("How the group's title is aligned in its header cell: left, center (the default) or right."), wxALIGN_CENTER);
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStyle, wxT("Visible"), _("Visible"),
		_("Whether the group and all its columns are shown. Hidden columns keep their data and can be shown again from code."), true);
};

class ibValueModelTableBoxColumn : public ibValueControl,
	public ibTypeControlFactory {
	public:
protected:

	bool GetChoiceForm(ibPropertyList* property);

public:

	////////////////////////////////////////////////////////////////////////////////////////

	ibFormID GetModelColumn() const {
		const ibFormID& id = m_model_id != wxNOT_FOUND ? m_model_id : GetSource();
		return id != wxNOT_FOUND ? id : m_controlId;
	}
	void SetModelColumn(const ibFormID& id) { m_model_id = id; }

	////////////////////////////////////////////////////////////////////////////////////////

	void SetSource(const ibSourceId& id) { m_propertySource->SetValue(id); }
	// Full binding path [headAttrId, tableSection, column, ...] — the resolve walks the attribute.
	void SetSource(const std::vector<ibSourceId>& path) { m_propertySource->SetValue(ibSourceDescription(path)); }
	// Type-carrying entry: the hops (with any pinned composite types) go in as-is — no id-stripping.
	void SetSource(const ibSourceDescription& desc) { m_propertySource->SetValue(desc); }
	ibMetaID GetSource() const { return m_propertySource->GetValueAsSource(); }
	// The column's FULL binding path (tablebox prefix + the column's own field id(s)); the
	// tablebox strips its own prefix to get the row-relative tail it walks per row.
	const std::vector<ibSourceHop>& GetSourcePath() const { return m_propertySource->GetValueAsPath(); }
	// (THE BINDING WHOLE — `GetSourceDesc()`, and it is NOT declared here: the column already has it, as
	//  ibBackendTypeSourceFactory's own pure virtual. A second one differing only in return type is not an
	//  override, it is a redefinition — the verb was already there under the name I was about to give it.)

	// The column's bound source as a composer FIELD — its dotted NAME (e.g. "Product.SKU"), row-relative to
	// the bound table. Universal: whatever addresses a column by field (sort, filter, group) uses it. Straight
	// off m_propertySource. MakeString renders the FULL form-rooted path; the composer's queryable is the bound
	// TABLE, so drop the tablebox's own prefix (one name segment per prefix id) to make it row-relative — the
	// same seam ResolveCellValue uses. prefix 0 (model bound directly) drops nothing.
	wxString GetSourceFieldName() const {
		wxString name = m_propertySource->GetValueAsString();
		const ibValueModelTableBox* owner = GetOwner();
		for (size_t prefix = owner != nullptr ? owner->GetSourcePath().size() : 0; prefix > 0; --prefix)
			name = name.AfterFirst(wxT('.'));
		return name;
	}

	////////////////////////////////////////////////////////////////////////////////////////

	void SetCaption(const wxString& caption) { return m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	void SetPasswordMode(bool caption) { return m_propertyPasswordMode->SetValue(caption); }
	bool GetPasswordMode() const { return m_propertyPasswordMode->GetValueAsBoolean(); }

	void SetMultilineMode(bool caption) { return m_propertyMultilineMode->SetValue(caption); }
	bool GetMultilineMode() const { return m_propertyMultilineMode->GetValueAsBoolean(); }

	void SetTexteditMode(bool caption) { return m_propertyTexteditMode->SetValue(caption); }
	bool GetTextEditMode() const { return m_propertyTexteditMode->GetValueAsBoolean(); }

	void SetSelectButton(bool caption) { return m_propertySelectButton->SetValue(caption); }
	bool GetSelectButton() const { return m_propertySelectButton->GetValueAsBoolean(); }

	void SetOpenButton(bool caption) { return m_propertyOpenButton->SetValue(caption); }
	bool GetOpenButton() const { return m_propertyOpenButton->GetValueAsBoolean(); }

	void SetClearButton(bool caption) { return m_propertyClearButton->SetValue(caption); }
	bool GetClearButton() const { return m_propertyClearButton->GetValueAsBoolean(); }

	void SetVisibleColumn(bool visible = true) const { m_propertyVisible->SetValue(visible); }
	bool GetVisibleColumn() const { return m_propertyVisible->GetValueAsBoolean(); }

	// SHOWN — its own Visible, a binding to show (a source, or a model column it stands on), the
	// functional options, and every group above it shown. A column that is not shown has no cells
	// in a fetch.
	bool IsColumnShown() const;

	void SetWidthColumn(int width) const { m_propertyWidth->SetValue(width); }
	int GetWidthColumn() const { return m_propertyWidth->GetValueAsUInteger(); }

	///////////////////////////////////////////////////////////////////////

	const ibTranslateString& GetFormat() const { return m_propertyFormat->GetValueAsFormatString(); }


	// The tablebox this column belongs to. NOT simply the parent any more: a column
	// may sit inside a column GROUP (and groups nest), so the table is looked for UP
	// the tree. Everything that resolves a cell goes through here, which is why
	// grouping a column must not change the answer.
	ibValueModelTableBox* GetOwner() const;

	ibValueModel::ibValueModelReturnLine* GetCurrentLine() const {
		const ibValueModelTableBox* tableBox = GetOwner();
		return tableBox != nullptr ?
			tableBox->GetCurrentLine() : nullptr;
	}

	///////////////////////////////////////////////////////////////////////

	ibValueModelTableBoxColumn();

	// THE FORMAT THE COLUMN SHOWS ITS CELLS WITH — its own where it has one, else the one its model column
	// gives (the attribute's, else its type's). Null while the column stands in no table model.
	const ibFormatString* GetCellFormat() const;

	// ⭐ ONE CELL OF THIS COLUMN, AS THE CLIENT SHOWS IT — the value of `item` in this column: resolved
	// through the dot or the form (ibValueModelTableBox::ResolveCellValue) when the column reaches past
	// the row, else the model's own. `Text` — through the column's format (GetCellFormat); `Checked` —
	// a boolean, drawn as a tick; `Number` — a number, aligned right. A fetch passes the format it found
	// once for the column (`format`); the short form finds it itself.
	void UpdateCell(const ibDataViewItem& item, ibDataNode& cell) const;
	void UpdateCell(const ibDataViewItem& item, ibDataNode& cell, const ibFormatString* format) const;

	// May this column's cell of `item` be edited — the column is not read-only (IsReadOnly) and the model
	// lets the row's cell be (a group heading, a list row never are).
	bool IsCellEditable(const ibDataViewItem& item) const;

	//support icons
	static wxIcon GetIconGroup();

public:

	//Get source object
	virtual ibSourceObject* GetSourceObject() const { return GetOwner(); }

	// Own bound source path ([headAttr, table, column]) — its leaf is this column. MUTABLE ref (like
	// GetTypeDesc): read it, or assign to bind (GetSourceDesc() = desc). No separate setter.
	virtual ibSourceDescription& GetSourceDesc() const override { return m_propertySource->GetValueAsSourceDesc(); }

	//Get source attribute
	virtual const class ibBackendSourceColumn* GetSourceAttributeObject() const { return m_propertySource->GetSourceAttributeObject(); }
	virtual ibSelectorDataType GetFilterDataType() const { return ibSelectorDataType::ibSelectorDataType_reference; }
	virtual ibSourceDataType GetFilterSourceDataType() const { return ibSourceDataType::ibSourceDataType_tableColumn; }

	// Available sources = the owning form's attributes of THIS control's kind (table).
	virtual bool GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const override;

	//get form owner 
	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }

	//get metaData
	virtual const ibMetaData* GetMetaData() const;

	//get type description 
	virtual ibTypeDescription& GetTypeDesc() const { return m_propertySource->GetValueAsTypeDesc(); }

	//get title
	virtual wxString GetControlTitle() const;

	// A column on a field this base does not use is not available — asked of the column's field (ibPropertySource).
	// An empty binding is not a reason here: a column may stand on the model rather than a source (IsColumnShown).
	virtual bool IsAvailable() const override { return m_propertySource->IsAvailable() && ibValueControl::IsAvailable(); }

	// READ-ONLY — its cells take no editing: the form is view-only, the column reaches past the row (a
	// dot-path or a foreign-root column — resolved, not stored), or the table's own binding cannot be
	// written (an object this person may not change, a section read through a reference).
	virtual bool IsReadOnly() const override;

	// The header as it shows now: Caption (its title, else the bound field's synonym, else its name),
	// FooterText, Representation (Auto resolved: picture and text) with HeaderPicture / FooterPicture where
	// pictures are shown, Visible (its own Visible, a binding, the functional options), ReadOnly, Sortable
	// (the model sorts and the column has a field to sort by) and Sort (1 ascending / -1 descending, when
	// the composer's active sort is on this column's field). Width and alignment are the schema's.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	virtual bool CanDeleteControl() const;

	//get component type
	virtual int GetComponentType() const { return COMPONENT_TYPE_ABSTRACT; }

	//support icons
	virtual wxIcon GetIcon() const;

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual void OnPropertyRefresh() override;
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

	//get control value
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue());
	virtual bool GetControlValue(ibValue& pvarControlVal) const;

	// A column stands on the ROW being edited: its neighbours are the other cells of that row.
	virtual ibChoiceHolder GetChoiceHolder() const override;

	//choice processing
	virtual void ChoiceProcessing(ibValue& vSelected);

private:

	// WHAT THE PERSON DID IN A CELL of this column — on the table's current row, which the client made
	// current first. Reached through the table, which receives the events (a column gets none of its own).
	// Typed text arrives as it stands and is parsed by the value's type; a read-only column takes none.
	void OnTextEnter(const wxString& text);
	// …the edit is over: the form refreshes and the rows are read again (a row edited out of the filter,
	// or into another group, moves).
	void OnEditingDone();

	void OnSelectButtonPressed();
	void OnOpenButtonPressed();
	void OnClearButtonPressed();

	// text processing
	bool TextProcessing(const wxString& strData);

	ibFormID m_model_id;

	ibPropertyCategory* m_categoryInfo = ibPropertyObject::CreatePropertyCategory(wxT("Info"), _("Info"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryInfo, wxT("Title"), _("Title"),
		_("The column's header text. Empty: the synonym of the bound field, or the column's name when nothing is bound. Can be written per language."), wxT(""));
	ibPropertyBoolean* m_propertyPasswordMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("PasswordMode"), _("Password mode"), _("Whether the cell editor hides what is typed behind placeholder characters, for secrets such as passwords. Off by default."), false);
	ibPropertyBoolean* m_propertyMultilineMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("MultilineMode"), _("Multiline mode"), _("Whether the cell editor accepts several lines of text (Enter starts a new line). Off by default: Enter finishes the edit."), false);
	ibPropertyBoolean* m_propertyTexteditMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("TexteditMode"), _("Textedit mode"), _("Whether the value can be typed into the cell. Off: it can only be picked with the Select button or cleared. On by default; a read-only column never takes typing."), true);
	ibPropertyFormat* m_propertyFormat = ibPropertyObject::CreateProperty<ibPropertyFormat>(m_categoryInfo, wxT("Format"), _("Format"),
		_("How the column shows its cells, written per language: digits after the point, separators, a date pattern. Empty: the bound attribute's format, and without one a number shows as many digits after the point as its type keeps."), wxT(""));

	ibPropertyTString* m_propertyFooterText = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryInfo, wxT("FooterText"), _("Footer text"),
		_("The text in this column's footer cell, shown when the table's footer is on - a label or a total set from code. Can be written per language."), wxT(""));

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertySource* m_propertySource = ibPropertyObject::CreateProperty<ibPropertySource>(m_categoryData, wxT("Source"), _("Source"),
		_("The field of the table's source this column shows and edits. A path through a reference (such as Owner.Description) is shown read-only."),
		ibValueTypes::TYPE_STRING);
	ibPropertyList* m_propertyChoiceForm = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryData, wxT("ChoiceForm"), _("Choice form"),
		_("Which form opens when the user presses Select in the cell: one of the forms of the value's type. Empty: the type's default choice form."),
		&ibValueModelTableBoxColumn::GetChoiceForm);

	ibPropertyCategory* m_categoryButton = ibPropertyObject::CreatePropertyCategory(wxT("Button"), _("Button"));
	ibPropertyBoolean* m_propertySelectButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonSelect"), _("Select button"),
		_("Whether the cell editor shows the Select button (...), which opens a choice form or list to pick the value from. On by default."), true);
	ibPropertyBoolean* m_propertyClearButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonClear"), _("Clear button"),
		_("Whether the cell editor shows the Clear button (X), which empties the value. On by default."), true);
	ibPropertyBoolean* m_propertyOpenButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonOpen"), _("Open button"),
		_("Whether the cell editor shows the Open button, which opens the referenced object's form. Off by default."), false);

	ibPropertyCategory* m_categoryStyle = ibPropertyObject::CreatePropertyCategory(wxT("Style"), _("Style"));
	ibPropertyUInteger* m_propertyWidth = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryStyle, wxT("Width"), _("Width"),
		_("The column's starting width, in pixels. The user can change it when the column is resizable."), wxDVC_DEFAULT_WIDTH);
	ibPropertyEnum<ibValueEnumHorizontalAlignment>* m_propertyHeaderAlign = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumHorizontalAlignment>>(m_categoryStyle, wxT("HeaderAlign"), _("Header align"),
		_("How the header text is aligned: left (the default), center or right."), wxALIGN_LEFT);
	ibPropertyEnum<ibValueEnumHorizontalAlignment>* m_propertyFooterAlign = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumHorizontalAlignment>>(m_categoryStyle, wxT("FooterAlign"), _("Footer align"),
		_("How the footer text is aligned: left (the default), center or right. Right suits a total under a numeric column."), wxALIGN_LEFT);
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_categoryStyle, wxT("Representation"), _("Representation"),
		_("What the header and footer cells show: text, picture, or both. Auto: the title and the picture, whichever are set."),
		ibRepresentation::ibRepresentation_Auto);
	ibPropertyPicture* m_propertyHeaderPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryStyle, wxT("HeaderPicture"), _("Header picture"),
		_("An icon in the column's header cell, beside or instead of the title (see Representation). A narrow flag column often shows only a picture."));
	ibPropertyPicture* m_propertyFooterPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryStyle, wxT("FooterPicture"), _("Footer picture"),
		_("An icon in the column's footer cell, beside or instead of the footer text (see Representation)."));

	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStyle, wxT("Visible"), _("Visible"),
		_("Whether the column is shown. A hidden column keeps its data and can be shown again from code."), true);
	ibPropertyBoolean* m_propertyResizable = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStyle, wxT("Resizable"), _("Resizable"),
		_("Whether the user can change the column's width by dragging its header edge. On by default."), true);
	//ibPropertyBoolean* m_propertySortable = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStyle, wxT("Sortable"), _("Sortable"), false);
	ibPropertyBoolean* m_propertyReorderable = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStyle, wxT("Reorderable"), _("Reorderable"),
		_("Whether the user can move the column to another place by dragging its header. On by default."), true);

	ibPropertyCategory* m_propertyEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibEventControl* m_eventOnChange = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("OnChange"), _("Change"), wxArrayString{ wxT("Control") });
	ibEventControl* m_eventStartChoice = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("StartChoice"), _("Start choice"), wxArrayString{ wxT("Control"), wxT("StandardProcessing") });
	ibEventControl* m_eventStartListChoice = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("StartListChoice"), _("Start list choice"), wxArrayString{ wxT("Control"), wxT("StandardProcessing") });
	ibEventControl* m_eventClearing = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("Clearing"), _("Clearing"), wxArrayString{ wxT("Control"), wxT("StandardProcessing") });
	ibEventControl* m_eventOpening = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("Opening"), _("Opening"), wxArrayString{ wxT("Control"), wxT("StandardProcessing") });
	ibEventControl* m_eventChoiceProcessing = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("ChoiceProcessing"), _("Choice processing"), wxArrayString{ wxT("Control"), wxT("ValueSelected"), wxT("StandardProcessing") });

	// The table receives the events of its cells and hands each to the column it was in.
	friend class ibValueModelTableBox;
};

#endif 
