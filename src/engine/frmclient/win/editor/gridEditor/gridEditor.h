#ifndef __GRID_EXT_COMMON_H__
#define __GRID_EXT_COMMON_H__

#include "frmclient/frmclient.h"

#include "core/spreadsheet/spreadsheetObject.h"
#include "frmclient/backend/propertyManager/propertyManager.h"
#include "frmclient/backend/system/value/valueSpreadsheet.h"

#include "protocol/protocol.h"   // ibProtocol::ibForm::ibSpreadsheet::ibComposeState — what the sheet is to its report

static const wxArrayString wxEmptyArrayString;

static const wxString s_strTypeTextOrString = wxT("stringText");
static const wxString s_strTypeTemplate = wxT("stringTemplate");
static const wxString s_strTypeParameter = wxT("stringParameter");

#include "deskgui/univ/grid/gridextctrl.h"
#include "deskgui/univ/grid/gridexteditors.h"

class wxFindDialogEvent;

// ⭐⭐ THE ONE PLACE THE TWO FIT MODES MEET. The description's mode is what a template is SAVED with;
// the grid's is what the control draws by. Translating between them was written out inline as
// `mode == Mode_Overflow ? Overflow() : Clip()` in EIGHT places — the document builder three times,
// the clipboard twice, the property panel, the notifier, the editor — which is a rule that holds only
// while it is two-valued. Adding a third meant editing eight sites and trusting the ninth to be
// written with it; the ternary silently answered `Clip` for anything it did not recognise, so the
// third value would have arrived as "cut it off" everywhere and looked like a drawing defect.
//
// ⚠ THE DOCUMENT'S Auto IS TWO OF THE GRID'S: Overflow where its text runs on, Auto where it breaks into
// lines — which depends on the row and the cell after it, so the look of the cell in view chooses
// (ibGridEditorAttrProvider, through IsCellWrapped); here it goes as Overflow. Both come back as Auto.
inline ibGridFitMode ibToGridFitMode(ibSpreadsheetCellDescription::ibFitMode mode)
{
	switch (mode) {
	case ibSpreadsheetCellDescription::ibFitMode::Mode_Auto:     return ibGridFitMode::Overflow();
	case ibSpreadsheetCellDescription::ibFitMode::Mode_Wrap:     return ibGridFitMode::Wrap();
	case ibSpreadsheetCellDescription::ibFitMode::Mode_Clip:     return ibGridFitMode::Clip();
	case ibSpreadsheetCellDescription::ibFitMode::Mode_Mask:     return ibGridFitMode::Mask();
	default: break;
	}

	// Unset and the three ellipsize modes: the grid takes them as they are - the two enums share
	// those values deliberately (see the note in ibGridFitMode).
	return ibGridFitMode::Ellipsize(static_cast<wxEllipsizeMode>(mode));
}

inline ibSpreadsheetCellDescription::ibFitMode ibFromGridFitMode(ibGridFitMode mode)
{
	if (mode.IsOverflow() || mode.IsAuto()) return ibSpreadsheetCellDescription::ibFitMode::Mode_Auto;
	if (mode.IsWrap())     return ibSpreadsheetCellDescription::ibFitMode::Mode_Wrap;
	if (mode.IsClip())     return ibSpreadsheetCellDescription::ibFitMode::Mode_Clip;
	if (mode.IsMask())     return ibSpreadsheetCellDescription::ibFitMode::Mode_Mask;

	return static_cast<ibSpreadsheetCellDescription::ibFitMode>(mode.GetEllipsizeMode());
}

// ⭐⭐ THE HEIGHT A ROW IS SHOWN AT — its own height when it has one, and otherwise its AUTOMATIC height
// (spreadsheetDescription.h, HasRowSize): the default, or taller wherever something written in the row does
// not fit — a larger font, more lines than one, a caption wrapped to its column. One function for everything
// that shows a sheet: the grid sizes its rows by it and the printout lays its pages out by it, so the paper
// keeps the rows the screen has. `dc` is a SCREEN's: a row height is drawn as that many screen pixels, by the
// grid and by the printout alike; the text is read in `langCode`, as the showing side reads it.
FRMCLIENT_API int ibSpreadsheetRowHeight(const ibSpreadsheetObject& doc, int row, wxDC& dc,
	const wxString& langCode = wxEmptyString);

// ⭐ THE WIDTH A CELL'S TEXT TAKES DRAWN AS IT STANDS — its widest line, plus what the cell keeps around a line
// not broken. The core weighs it against the room to the sheet's end (IsCellWrapped: Auto that does not fit there
// breaks), and a column fitted to its content is the widest of them (ibSpreadsheetColWidth). `dc` is a screen's,
// as for the row height.
FRMCLIENT_API int ibSpreadsheetTextWidth(const ibSpreadsheetObject& doc, int row, int col, wxDC& dc,
	const wxString& langCode = wxEmptyString);

// ⭐ THE WIDTH THE COLUMN'S CONTENT ASKS FOR — the widest thing written in it, plus what the cell's chrome
// takes. ASKED FOR ONLY WHEN SOMEBODY PRESSES FOR IT (Fit width to content): a column has no automatic
// width, because what does not fit is the cell's placement to answer and a width worked out at show time
// would move the page breaks about (spreadsheetDescription.h, HasColSize). Cells that wrap, cells a merge
// spans and text on its side are not asked — none of them says what one column should be.
FRMCLIENT_API int ibSpreadsheetColWidth(const ibSpreadsheetObject& doc, int col, wxDC& dc,
	const wxString& langCode = wxEmptyString);

class FRMCLIENT_API ibGridEditor : public ibGrid {

	class ibGenericSpreadsheetNotifier : public ibSpreadsheetNotifier {
	public:

		ibGenericSpreadsheetNotifier(ibGridEditor* view) : m_view(view) {}
		// ClearGrid takes the table's attribute provider with every attribute it holds (gridext.cpp): what the grid was
		// told itself goes, and the look — the document's — is read through a provider again.
		virtual void ClearSpreadsheet() {
			m_view->ClearGrid();
			if (m_view->GetTable() != nullptr)
				m_view->GetTable()->SetAttrProvider(new ibGridEditorAttrProvider(m_view));
		}
		virtual void EnableEditing(bool edit) { m_view->EnableEditing(edit); }

		//size 
		virtual void SetRowSize(int row, int height = 0) { m_view->SetRowSize(row, height); }
		virtual void SetColSize(int col, int width = 0) { m_view->SetColSize(col, width); }

		//freeze 
		virtual void SetRowFreeze(int row) { m_view->FreezeTo(row, 0); }
		virtual void SetColFreeze(int col) { m_view->FreezeTo(0, col); }

		// ------ row and col formatting
		//
		// ⭐ THE CELL IS THE DOCUMENT'S and the grid reads it there (ibGridEditorTable, ibGridEditorAttrProvider). A change
		// is told here BEFORE the document stores it, so the grid only grows to the cell and repaints it when it paints
		// next — by then the document has it. A merge is the grid's own, and is still told to it.

		virtual void SetCellBackgroundColour(int row, int col, const ibColour& colour) { RefreshCell(row, col); }
		virtual void SetCellTextColour(int row, int col, const ibColour& colour) { RefreshCell(row, col); }
		virtual void SetCellTextOrient(int row, int col, const int orient) { RefreshCell(row, col); }
		virtual void SetCellFont(int row, int col, const ibFont& font) { RefreshCell(row, col); }
		virtual void SetCellAlignment(int row, int col, const int horiz, const int vert) { RefreshCell(row, col); }
		virtual void SetCellBorderLeft(int row, int col, const ibSpreadsheetBorderDescription& desc) { RefreshCell(row, col); }
		virtual void SetCellBorderRight(int row, int col, const ibSpreadsheetBorderDescription& desc) { RefreshCell(row, col); }
		virtual void SetCellBorderTop(int row, int col, const ibSpreadsheetBorderDescription& desc) { RefreshCell(row, col); }
		virtual void SetCellBorderBottom(int row, int col, const ibSpreadsheetBorderDescription& desc) { RefreshCell(row, col); }
		virtual void SetCellSize(int row, int col, int num_rows, int num_cols) { GetOrCreateCell(row, col)->SetCellSize(row, col, num_rows, num_cols, false); m_view->RequestAutoRowHeights(row, row + wxMax(num_rows, 1) - 1); }
		virtual void SetCellFitMode(int row, int col, ibSpreadsheetCellDescription::ibFitMode fitMode) { RefreshCell(row, col); }
		virtual void SetCellReadOnly(int row, int col, bool isReadOnly = true) { RefreshCell(row, col); }

		// ------ cell brake accessors
		//
		//support printing 
		virtual void AddRowBrake(int row) { m_view->AddRowBrake(row); }
		virtual void AddColBrake(int col) { m_view->AddColBrake(col); }

		virtual void DeleteRowBrake(int row) { m_view->DeleteRowBrake(row); }
		virtual void DeleteColBrake(int col) { m_view->DeleteColBrake(col); }

		virtual void SetRowBrake(int row) { m_view->SetRowBrake(row); }
		virtual void SetColBrake(int col) { m_view->SetColBrake(col); }

		// ------ cell value accessors
		//
		virtual void SetCellValue(int row, int col, const ibString& s) { RefreshCell(row, col); }

		// ------ area value accessors
		//
		virtual void PutArea(
			const std::shared_ptr<ibSpreadsheetObject>& doc,
			unsigned int groupLevel = 0) override {
			m_view->PutDocument(doc, groupLevel);
		}

		virtual void JoinArea(
			const std::shared_ptr<ibSpreadsheetObject>& doc,
			unsigned int groupLevel = 0) override {
			m_view->JoinDocument(doc, groupLevel);
		}

		virtual void RowAreaAdded(unsigned int start, unsigned int end, unsigned int level) override {
			m_view->AppendRowOutlineGroup(start, end, level);
		}
		virtual void ColAreaAdded(unsigned int start, unsigned int end, unsigned int level) override {
			m_view->AppendColOutlineGroup(start, end, level);
		}

	private:

		ibGridEditor* GetOrCreateCell(int row, int col) const {

			if (m_view->GetTable() != nullptr) {

				if (row >= m_view->GetNumberRows())
					m_view->AppendRows(row - m_view->GetNumberRows() + 1);

				if (col >= m_view->GetNumberCols())
					m_view->AppendCols(col - m_view->GetNumberCols() + 1);
			}

			return m_view;
		}

		// A cell the document changes: the grid grown to it, the look it keeps for drawing the cell dropped, its row
		// fitted to its text again and repainted (RequestAutoRowHeights) — what a cell holds and how it is placed set
		// the height its row needs and how the cell before it places its text; a row is measured only when it is shown.
		void RefreshCell(int row, int col) const {
			GetOrCreateCell(row, col)->RefreshAttr(row, col);
			m_view->RequestAutoRowHeights(row, row);
		}

		ibGridEditor* m_view;
	};

	// the editor for string/text data
	class ibGridEditorCellTextEditor : public ibGridCellEditor
	{
	public:
		explicit ibGridEditorCellTextEditor(size_t maxChars = 0)
			: ibGridCellEditor(),
			m_maxChars(maxChars)
		{
		}

		ibGridEditorCellTextEditor(const ibGridEditorCellTextEditor& other);

		virtual void Create(wxWindow* parent,
			wxWindowID id,
			wxEvtHandler* evtHandler) override;

		virtual void SetSize(const wxRect& rect) override;

		virtual bool IsAcceptedKey(wxKeyEvent& event) override;
		virtual void BeginEdit(int row, int col, ibGrid* grid) override;
		virtual bool EndEdit(int row, int col, const ibGrid* grid,
			const wxString& oldval, wxString* newval) override;
		virtual void ApplyEdit(int row, int col, ibGrid* grid) override;

		virtual void Reset() override;
		virtual void StartingKey(wxKeyEvent& event) override;
		virtual void HandleReturn(wxKeyEvent& event) override;

		// parameters string format is "max_width"
		virtual void SetParameters(const wxString& params) override;
#if wxUSE_VALIDATORS
		virtual void SetValidator(const wxValidator& validator);
#endif

		virtual ibGridCellEditor* Clone() const override {
			return new ibGridEditorCellTextEditor(*this);
		}

		// added GetValue so we can get the value which is in the control
		virtual wxString GetValue() const override;

	protected:
		ibTextCtrl* Text() const { return (ibTextCtrl*)m_control; }

		// parts of our virtual functions reused by the derived classes
		void DoCreate(wxWindow* parent, wxWindowID id, wxEvtHandler* evtHandler,
			long style = 0);
		void DoBeginEdit(const wxString& startValue);
		void DoReset(const wxString& startValue);

	private:
		size_t                   m_maxChars;        // max number of chars allowed
#if wxUSE_VALIDATORS
		wxScopedPtr<wxValidator> m_validator;
#endif
		wxString                 m_value;
	};

	// ⭐⭐ THE SHEET'S CELLS ARE THE DOCUMENT'S — read where the document keeps them, its description, not copied into
	// the grid. The grid held the sheet a second time: a string per cell in a string table and an attribute object per
	// cell, all made at load — a 100 000-line sheet took 40 s and 575 MB to come up (23 s and 284 MB read from the
	// document), and a cell's fill type was looked for in a list of every typed cell at each question (2026-11-16,
	// load-million-rows.md § 12).
	// The table answers a cell's text from the description and writes an edit into it; the look comes from it the same
	// way (ibGridEditorAttrProvider). What the grid keeps is its own: the extent it shows — the document's cells and the
	// empty sheet grown around them.
	//
	// The text is the cell AS WRITTEN: a caption in the user's language, a template's text, a parameter's name — what a
	// template's editor edits. A document shown on a form comes with every cell's text worked out by the server
	// (ibFetchSpreadsheet), so there it is the text shown.
	class ibGridEditorTable : public ibGridTableBase {
	public:

		ibGridEditorTable(ibGridEditor* view, int numRows = 0, int numCols = 0);

		virtual int GetNumberRows() override { return m_numRows; }
		virtual int GetNumberCols() override { return m_numCols; }

		virtual bool IsEmptyCell(int row, int col) override;

		virtual bool CanGetValueAs(int row, int col, const wxString& typeName) override;
		virtual bool CanSetValueAs(int row, int col, const wxString& typeName) override;

		virtual void GetValue(int row, int col, wxString& s) override;
		virtual void SetValue(int row, int col, const wxString& s) override;

		// For user defined types
		virtual void SetValueAsCustom(int row, int col, const wxString& typeName, void* value) override;
		virtual void* GetValueAsCustom(int row, int col, const wxString& typeName) override;

		// RD: Starting the rows at zero confuses users, no matter how much it makes sense to us geeks.
		virtual wxString GetRowLabelValue(int row) override { return stringUtils::IntToStr(row + 1); }
		virtual wxString GetColLabelValue(int col) override { return stringUtils::IntToStr(col + 1); }

		// ------ the extent — the grid's rows and columns, nothing of a cell. Grown and cut at its far end only: the cells
		// are the document's and keep their places, so a line put or taken inside the sheet has nothing to move them with.
		virtual bool InsertRows(size_t pos = 0, size_t numRows = 1) override;
		virtual bool AppendRows(size_t numRows = 1) override;
		virtual bool DeleteRows(size_t pos = 0, size_t numRows = 1) override;
		virtual bool InsertCols(size_t pos = 0, size_t numCols = 1) override;
		virtual bool AppendCols(size_t numCols = 1) override;
		virtual bool DeleteCols(size_t pos = 0, size_t numCols = 1) override;

	private:

		// the cell as the document keeps it, or none
		const ibSpreadsheetCellDescription* GetCell(int row, int col) const;
		ibSpreadsheetFillType GetFillType(int row, int col) const;

		ibGridEditor* m_view;
		int m_numRows, m_numCols;
	};

	// ⭐ …AND ITS LOOK, THE SAME WAY: the font, the colours, the alignment, the borders and the placement read from the
	// cell's description when the grid draws it — an attribute made for the cell in view, none kept for every cell. What
	// the grid was told itself stands over it: a merge (the grid's own), and a look set through the grid, which the
	// editor hands on to the document as it is set (OnGridTableAttrModified).
	class ibGridEditorAttrProvider : public ibGridCellAttrProvider {
	public:

		ibGridEditorAttrProvider(ibGridEditor* view) : m_view(view) {}

		virtual ibGridCellAttr* GetAttr(int row, int col, ibGridCellAttr::wxAttrKind kind) const override;

	private:

		ibGridEditor* m_view;
	};

	// the property of grid 
	class ibPropertyGridEditorSpreadsheet :
		public ibPropertyObject {
	public:

		ibPropertyGridEditorSpreadsheet(ibGridEditor* view) : m_view(view) {
			if (m_view != nullptr) {
				m_view->Bind(ibEVT_GRID_SELECT_CELL, &ibPropertyGridEditorSpreadsheet::OnSelectCell, this);
				m_view->Bind(ibEVT_GRID_RANGE_SELECTED, &ibPropertyGridEditorSpreadsheet::OnSelectCells, this);
			}
		}

		virtual ~ibPropertyGridEditorSpreadsheet() {
			if (m_view != nullptr) m_view->DeletePendingEvents();
		}

		virtual bool IsEditable() const { return m_view->IsEditable(); }

		//system override 
		virtual ibString GetObjectTypeName() const override { return s_strPropertyClass; }
		virtual ibString GetClassName() const override { return s_strPropertyClass; }

		/// Gets the metadata object
		virtual ibMetaData* GetMetaData() const;

		/**
		* Property events
		*/
		virtual void OnPropertyCreated(ibProperty* property);
		virtual void OnPropertyRefresh() override;
		virtual void OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue);

		friend class ibGridEditor;

	protected:

		void OnSelectCell(ibGridEvent& event);
		void OnSelectCells(ibGridRangeSelectEvent& event);

		ibGridEditor* m_view;

		wxVector<ibGridBlockCoords> m_selection;

	private:

		void ShowInspector();

		void OnPropertyCreated(ibProperty* property, const ibGridBlockCoords& coords);
		void OnPropertyChanged(ibProperty* property, const ibGridBlockCoords& coords);

		const wxString s_strPropertyClass = wxT("propertySpreadsheet");

	private:

		ibPropertyCategory* m_categoryGeneral = ibPropertyObject::CreatePropertyCategory(ibT("General"), _("General"));
		ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_categoryGeneral, ibT("Name"), _("Name"),
			_("The address of the selected cell or range, in row-column form (R1C1, or R1C1:R3C4 for a range). Shown for orientation; named areas are made from rows and columns, not here."), ibEmptyString);
		ibPropertyTString* m_propertyText = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryGeneral, ibT("Text"), _("Text"),
			_("The cell's text: fixed text for a Text cell, or the pattern with [Name] placeholders for a Template cell. Can be written per language."), ibEmptyString);
		ibPropertyBoolean* m_propertyReadOnly = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryGeneral, ibT("ReadOnly"), _("Read only"),
			_("Whether the user can edit the cell when the document is shown editable. Off by default."), false);

		ibPropertyCategory* m_categoryTemplate = ibPropertyObject::CreatePropertyCategory(ibT("Template"), _("Template"));
		ibPropertyEnum<ibValueEnumSpreadsheetFillType>* m_propertyFillType = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetFillType>>(m_categoryTemplate, ibT("FillType"), _("Fill type"),
			_("What the cell holds when its area is put into a document. Text (the default): the text as written. Parameter: the value of the parameter named in Parameter. Template: the text with each [Name] replaced by that parameter's value."),
			ibSpreadsheetFillType::ibSpreadsheetFillType_StrText);
		ibPropertyUEString* m_propertyParameter = ibPropertyObject::CreateProperty<ibPropertyUEString>(m_categoryTemplate, ibT("Parameter"), _("Parameter"),
			_("For a Parameter cell: the name of the area parameter whose value fills the cell. Code sets it through the area's Parameters before putting the area into a document."), ibEmptyString);
		ibPropertyUEString* m_propertyDetailsParameter = ibPropertyObject::CreateProperty<ibPropertyUEString>(m_categoryTemplate, ibT("DetailsParameter"), _("Details parameter"),
			_("The name of an area parameter whose value the cell carries as its details: when the user opens the cell in the finished document, the details processing handler receives that value (a document, an employee) to open or drill into."), ibEmptyString);

		ibPropertyCategory* m_categoryAlignment = ibPropertyObject::CreatePropertyCategory(ibT("Alignment"), _("Alignment"));
		ibPropertyEnum<ibValueEnumSpreadsheetFitMode>* m_propertyFitMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetFitMode>>(m_categoryAlignment, ibT("Git_mode"), _("Fit mode"),
			_("What happens to text longer than the cell. Auto (the default): it runs on into the empty cell after it, and where it cannot, it breaks into lines in a row of automatic height and is cut in a row with a height of its own. Clip: it is cut at the cell's edge. Wrap: it breaks into lines within the cell. Mask: the cell is filled with # instead."),
			ibSpreadsheetFitMode::ibFitMode_Auto);
		ibPropertyEnum<ibValueEnumSpreadsheetHorizontalAlignment>* m_propertyAlignHorz = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetHorizontalAlignment>>(m_categoryAlignment, ibT("Align_horz"), _("Horizontal"),
			_("How the text is aligned across the cell: left (the default), center or right."), ibSpreadsheetAlignmentHorz::ibAlignmentHorz_Left);
		ibPropertyEnum<ibValueEnumSpreadsheetVerticalAlignment>* m_propertyAlignVert = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetVerticalAlignment>>(m_categoryAlignment, ibT("Align_vert"), _("Vertical"),
			_("How the text is aligned up and down the cell: top, center (the default) or bottom."), ibSpreadsheetAlignmentVert::ibAlignmentVert_Center);
		ibPropertyEnum<ibValueEnumSpreadsheetOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetOrient>>(m_categoryAlignment, ibT("Orient_text"), _("Orientation text"),
			_("The direction the cell's text runs: horizontal or vertical."), ibSpreadsheetOrientation::ibOrient_Vertical);

		ibPropertyCategory* m_categoryAppearance = ibPropertyObject::CreatePropertyCategory(ibT("Appearance"), _("Appearance"));
		ibPropertyFont* m_propertyFont = ibPropertyObject::CreateProperty<ibPropertyFont>(m_categoryAppearance, ibT("Font"), _("Font"),
			_("The font of the cell's text. Unset: the document's default font."), wxNullFont);
		ibPropertyColour* m_propertyBackgroundColour = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryAppearance, ibT("Background_colour"), _("Background colour"),
			_("The cell's fill colour, on screen and in print. Unset: no fill."), wxNullColour);
		ibPropertyColour* m_propertyTextColour = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryAppearance, ibT("Text_colour"), _("Text colour"),
			_("The colour of the cell's text. Unset: the default text colour."), wxNullColour);

		ibPropertyCategory* m_categoryBorder = ibPropertyObject::CreatePropertyCategory(ibT("Border"), _("Border"));
		ibPropertyEnum<ibValueEnumSpreadsheetBorder>* m_propertyLeftBorder = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetBorder>>(m_categoryBorder, ibT("Left_border"), _("Left"),
			_("The line drawn along the cell's left edge: none (the default), solid, dotted or dashed. Printed as shown."), ibSpreadsheetPenStyle::ibPenStyle_Transparent);
		ibPropertyEnum<ibValueEnumSpreadsheetBorder>* m_propertyRightBorder = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetBorder>>(m_categoryBorder, ibT("Right_border"), _("Right"),
			_("The line drawn along the cell's right edge: none (the default), solid, dotted or dashed. Printed as shown."), ibSpreadsheetPenStyle::ibPenStyle_Transparent);
		ibPropertyEnum<ibValueEnumSpreadsheetBorder>* m_propertyTopBorder = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetBorder>>(m_categoryBorder, ibT("Top_border"), _("Top"),
			_("The line drawn along the cell's top edge: none (the default), solid, dotted or dashed. Printed as shown."), ibSpreadsheetPenStyle::ibPenStyle_Transparent);
		ibPropertyEnum<ibValueEnumSpreadsheetBorder>* m_propertyBottomBorder = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetBorder>>(m_categoryBorder, ibT("Bottom_border"), _("Bottom"),
			_("The line drawn along the cell's bottom edge: none (the default), solid, dotted or dashed. Printed as shown."), ibSpreadsheetPenStyle::ibPenStyle_Transparent);
		ibPropertyColour* m_propertyColourBorder = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryBorder, ibT("Border_colour"), _("Colour"),
			_("The colour of the cell's border lines. Unset: the default line colour."), wxNullColour);
	};

public:

	void ActivateEditor();

	void SendPropertyModify() {
		ibGrid::SendEvent(ibEVT_GRID_EDITOR_HIDDEN, m_currentCellCoords);
	}

	void SendPropertyModify(const ibGridCellCoords& coords) {
		ibGrid::SendEvent(ibEVT_GRID_EDITOR_HIDDEN, coords);
	}

	bool IsPropertyEnabled() const { return m_enableProperty; }
	void EnableProperty(bool enable = true) { m_enableProperty = enable; }

	////////////////////////////////////////////////////////////

	// ctor and Create() create the grid window, as with the other controls
	ibGridEditor();
	ibGridEditor(class ibMetaDocument* document, wxWindow* parent,
		wxWindowID id, const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);

	virtual ~ibGridEditor();

#pragma region area

	void AddArea();
	void DeleteArea();

#pragma endregion

	bool GridHeaderEnabled() const { return m_rowLabelWidth > 0 || m_colLabelHeight > 0; }

	void ShowCells() { ibGrid::EnableGridLines(!m_gridLinesEnabled); }

	void ShowHeader() {
		if (m_rowLabelWidth > 0) {
			ibGrid::EnableGridArea(false);
			ibGrid::SetRowLabelSize(0);
			ibGrid::SetColLabelSize(0);
		}
		else {
			ibGrid::EnableGridArea(true);
			ibGrid::SetRowLabelSize(WXGRID_DEFAULT_ROW_LABEL_WIDTH - 42);
			ibGrid::SetColLabelSize(WXGRID_MIN_COL_WIDTH);
		}
	}

	void ShowArea() { ibGrid::EnableGridArea(!m_areaEnabled); }

	void MergeCells();
	void DockTable();

	void Copy();
	void Paste();
	// The next cell holding the text, as the Find dialog's flags say (wxFR_*): row by row from the cursor's, on past the
	// sheet's end from its other end, the cursor's own last — the cursor put on it; none — a box says so. The text
	// editor's (ibTextEditor::FindText).
	void FindText(const wxString& findString, int wxflags);

	bool AssociateDocument(const std::shared_ptr<ibSpreadsheetObject>& doc);
	bool GetActiveDocument(std::shared_ptr<ibSpreadsheetObject>& doc) const;

#pragma region file

	bool LoadDocument(const ibSpreadsheetDescription& spreadsheetDesc);
	bool LoadDocument(const std::shared_ptr<ibSpreadsheetObject>& doc);
	bool SaveDocument(ibSpreadsheetDescription& spreadsheetDesc) const;
	bool SaveDocument(std::shared_ptr<ibSpreadsheetObject>& doc) const;

#pragma endregion

	void PutDocument(const std::shared_ptr<ibSpreadsheetObject>& doc, unsigned int groupLevel = 0);
	void JoinDocument(const std::shared_ptr<ibSpreadsheetObject>& doc, unsigned int groupLevel = 0);

	// ⭐ THE ROWS TO BE FITTED TO THEIR TEXT AGAIN (FitAutoRowHeights) — whatever changes the height a row's text
	// needs: the text, a font, a placement, a merge, a column's width, the cell after it. Marked and shown again,
	// and fitted when they are drawn — by then the document has caught up with a change the notifier tells the
	// grid BEFORE the document stores it (ibSpreadsheetObject's setters).
	void RequestAutoRowHeights(int fromRow, int toRow);

	// Bridge called by the spreadsheet notifier when BeginGroup/EndGroup closes
	// a block — mirrors the new area into m_rowAreaAt / m_colAreaAt so the
	// outline pane repaints immediately.
	void AppendRowOutlineGroup(unsigned int start, unsigned int end, unsigned int level);
	void AppendColOutlineGroup(unsigned int start, unsigned int end, unsigned int level);

	class ibGridEditorPrintout* CreatePrintout() const;

	// ⭐⭐ WHERE THE SHEET'S CONTENT ENDS — the last row (column) that holds anything.
	//
	// 🛑 THE ENDLESS SHEET MUST NOT EAT DATA. Scrolling down grows the sheet and
	// scrolling back up trims what it grew; the trim used to stop at the last PAGE
	// BREAK, which is zero in a document that has none — so a file opened from disk
	// (an Excel workbook with a single sheet, say) had its rows deleted the moment
	// somebody scrolled back up. What was added for the view may be taken away;
	// what a person's file brought may not.
	//
	// The DOCUMENT answers it: its cells end where its description does — a cell holding a border
	// and no text is the document's too.
	int GetLastContentRow() const;
	int GetLastContentCol() const;

protected:

	// ⭐ AUTOMATIC ROW HEIGHT, APPLIED WHEN THE ROWS ARE SHOWN — the rows without a height of their own, sized to their
	// text (ibSpreadsheetRowHeight) from the document this editor shows, each once until it is marked again
	// (m_rowsFitted, RequestAutoRowHeights). Asked by the grid for the rows it is about to draw: a sheet comes up
	// without measuring the rows nobody sees (Max, 2026-11-26) — a million-row report measured every one before its
	// first paint. What is worked out here is never written back into the document (m_quietSizing): the row keeps no
	// height of its own and stays automatic, however far its worked-out height is from the default.
	virtual void FitAutoRowHeights(int fromRow, int toRow) override;

	void GetCellDetailsParameter(int row, int col, wxString& s) const;
	void SetCellDetailsParameter(int row, int col, const wxString& s);

	wxString GetCellDetailsParameter(int row, int col) const {
		wxString s;
		GetCellDetailsParameter(row, col, s);
		return s;
	}

	void SetCellDetailsParameter(const ibGridBlockCoords& coords, const wxString& s, bool sendUndoCommand = true);

	bool SaveSpreadsheet(ibSpreadsheetDescription& spreadsheetDesc) const;

	//events:
	void OnMouseLeftDown(ibGridEvent& event);
	void OnMouseRightDown(ibGridEvent& event);

	// This seems to be required for wxMotif/wxGTK otherwise the mouse
	// cursor must be in the cell edit control to get key events
	//
	void OnKeyDown(wxKeyEvent& event);

	void OnGridRowSize(ibGridSizeEvent& event);
	void OnGridColSize(ibGridSizeEvent& event);
	void OnGridRowBrake(ibGridSizeEvent& event);
	void OnGridColBrake(ibGridSizeEvent& event);
	void OnGridRowArea(ibGridAreaEvent& event);
	void OnGridColArea(ibGridAreaEvent& event);
	void OnGridRowFreeze(ibGridSizeEvent& event);
	void OnGridColFreeze(ibGridSizeEvent& event);

	void OnGridTableModified(ibGridEvent& event);
	void OnGridTableAttrModified(ibGridEvent& event);

	void OnCopy(wxCommandEvent& event);
	void OnPaste(wxCommandEvent& event);
	void OnDelete(wxCommandEvent& event);
	// Find handed to the editor itself — a form's sheet is asked through its window (ibVisualHostView).
	void OnFind(wxFindDialogEvent& event);

	void OnRowHeight(wxCommandEvent& event);
	void OnColWidth(wxCommandEvent& event);
	void OnFitColWidth(wxCommandEvent& event);
	void OnHideCell(wxCommandEvent& event);
	void OnShowCell(wxCommandEvent& event);

	void OnProperties(wxCommandEvent& event);

public:
	// Public entry points used by ibSpreadsheetEditView menu items.
	void GroupSelectedRows();
	void UngroupSelectedRows();
	void GroupSelectedCols();
	void UngroupSelectedCols();

	void OnScroll(wxScrollWinEvent& event);

	void OnSize(wxSizeEvent& event);

	// ⭐ THE SHEET SAYS WHAT IT IS TO ITS REPORT. A report composes on a background run, so the window stays
	// alive and usable while it does — and a window that looks finished while it is still filling is worse
	// than one that waits: the person reads a half-built report as the answer. A small spinner with
	// a word beside it, centred over the sheet, is the whole story (Max, 2026-08-19: "while the
	// report is being built the little circle turns, like in the list, and meanwhile we can work").
	// …and the same plate, without the spinner, says the sheet is OUTDATED: the setting changed since it
	// was built, and a report is re-formed when the person says Compose (Max, 2026-10-12).
	//
	// Idempotent: showing it twice is one plate, hiding a hidden one does nothing.
	void ShowComposeState(ibProtocol::ibForm::ibSpreadsheet::ibComposeState state);

	// ⭐ THE SHEET FILLS THE WINDOW. A document holds as many columns and rows as somebody put into
	// it — a composed report may hold two — and the space to the right of them is not "outside the
	// sheet", it is empty sheet. Growing the table to cover the visible area is what makes it look
	// like one; without it a report ends in a blank void with no grid lines, which reads as a
	// rendering failure rather than as an empty page (Max, 2026-08-19).
	//
	// Called on resize AND whenever the document is rebuilt: a report replaces the whole sheet
	// without the window ever changing size, so a resize-only trigger never fires for it.
	void FillVisibleArea(int width = -1, int height = -1);

	void OnGridZoom(ibGridEvent& event);

private:

	const wxString m_GRID_VALUE_STRING = wxGRID_VALUE_STRING;

	//document 
	ibMetaDocument* m_document;

	// grid property
	ibPropertyGridEditorSpreadsheet* m_propertySpreadsheet;

	//grid doc
	std::shared_ptr<ibSpreadsheetObject> m_spreadsheetObject;
	std::shared_ptr<ibSpreadsheetNotifier> m_notifier;

	// The plate over the sheet — see ShowComposeState. Created on first use, kept hidden after: its spinner turns
	// while composing, its text says which.
	class wxWindow*             m_composeState = nullptr;
	class ibActivityIndicator*  m_composeSpinner = nullptr;
	class ibStaticText*         m_composeText = nullptr;

	// The rows fitted to their text already (FitAutoRowHeights) — set when a row is fitted, cleared by
	// RequestAutoRowHeights and by a document loaded; a row past its end has not been fitted.
	std::vector<bool> m_rowsFitted;

	// 🛑 WHILE THIS IS ON, A SIZE IS NOT ONE SOMEBODY CHOSE. `ibGrid::DoSetRowSize` / `DoSetColSize` send
	// ibEVT_GRID_ROW_MODIFIED / _COL_MODIFIED themselves whenever a size changes outside a drag, and the
	// handlers write that into the document — so the height FitAutoRowHeights works out came straight back
	// as a height of the row's own, and the row stopped being automatic on its first showing (Max,
	// 2026-09-22: *"a row that became automatic is still automatic — its height is a WORKED-OUT one"*).
	// The same door serves a band PUT BACK to the default, which must leave no size behind it either.
	bool m_quietSizing = false;

	//grid enabled property? 
	bool m_enableProperty;

	wxDECLARE_EVENT_TABLE();
};


#endif 