#ifndef __GRID_EXT_COMMON_H__
#define __GRID_EXT_COMMON_H__

#include "frontend/frontend.h"

#include "backend/backend_spreadsheet.h"
#include "backend/propertyManager/propertyManager.h"
#include "backend/system/value/valueSpreadsheet.h"

static const wxArrayString wxEmptyArrayString;

static const wxString s_strTypeTextOrString = wxT("stringText");
static const wxString s_strTypeTemplate = wxT("stringTemplate");
static const wxString s_strTypeParameter = wxT("stringParameter");

#include "frontend/win/ctrls/grid/gridextctrl.h"
#include "frontend/win/ctrls/grid/gridexteditors.h"

class FRONTEND_API ibGridEditor : public ibGrid {

	class ibGenericSpreadsheetNotifier : public ibBackendSpreadsheetNotifier {
	public:

		ibGenericSpreadsheetNotifier(ibGridEditor* view) : m_view(view) {}
		virtual void ClearSpreadsheet() { m_view->ClearGrid(); }
		virtual void EnableEditing(bool edit) { m_view->EnableEditing(edit); }

		//size 
		virtual void SetRowSize(int row, int height = 0) { m_view->SetRowSize(row, height); }
		virtual void SetColSize(int col, int width = 0) { m_view->SetColSize(col, width); }

		//freeze 
		virtual void SetRowFreeze(int row) { m_view->FreezeTo(row, 0); }
		virtual void SetColFreeze(int col) { m_view->FreezeTo(0, col); }

		// ------ row and col formatting
		//

		virtual void SetCellBackgroundColour(int row, int col, const wxColour& colour) { GetOrCreateCell(row, col)->SetCellBackgroundColour(row, col, colour, false); }
		virtual void SetCellTextColour(int row, int col, const wxColour& colour) { GetOrCreateCell(row, col)->SetCellTextColour(row, col, colour, false); }
		virtual void SetCellTextOrient(int row, int col, const int orient) { GetOrCreateCell(row, col)->SetCellTextOrient(row, col, orient, false); }
		virtual void SetCellFont(int row, int col, const wxFont& font) { GetOrCreateCell(row, col)->SetCellFont(row, col, font, false); }
		virtual void SetCellAlignment(int row, int col, const int horiz, const int vert) { GetOrCreateCell(row, col)->SetCellAlignment(row, col, horiz, vert, false); }
		virtual void SetCellBorderLeft(int row, int col, const ibSpreadsheetBorderDescription& desc) {}
		virtual void SetCellBorderRight(int row, int col, const ibSpreadsheetBorderDescription& desc) {}
		virtual void SetCellBorderTop(int row, int col, const ibSpreadsheetBorderDescription& desc) {}
		virtual void SetCellBorderBottom(int row, int col, const ibSpreadsheetBorderDescription& desc) {}
		virtual void SetCellSize(int row, int col, int num_rows, int num_cols) { GetOrCreateCell(row, col)->SetCellSize(row, col, num_rows, num_cols, false); }
		virtual void SetCellFitMode(int row, int col, ibSpreadsheetCellDescription::ibFitMode fitMode) { GetOrCreateCell(row, col)->SetCellFitMode(row, col, fitMode == ibSpreadsheetCellDescription::ibFitMode::Mode_Overflow ? ibGridFitMode::Overflow() : ibGridFitMode::Clip(), false); }
		virtual void SetCellReadOnly(int row, int col, bool isReadOnly = true) { GetOrCreateCell(row, col)->SetCellReadOnly(row, col, isReadOnly, false); }

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
		virtual void SetCellValue(int row, int col, const wxString& s) { GetOrCreateCell(row, col)->SetCellValue(row, col, s, false); }

		// ------ area value accessors
		//
		virtual void PutArea(
			const wxObjectDataPtr<class ibBackendSpreadsheetObject>& doc,
			unsigned int groupLevel = 0) override {
			m_view->PutDocument(doc, groupLevel);
		}

		virtual void JoinArea(
			const wxObjectDataPtr<class ibBackendSpreadsheetObject>& doc,
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
		wxTextCtrl* Text() const { return (wxTextCtrl*)m_control; }

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

	class ibGridEditorStringTable : public ibGridStringTable {

		struct ibGridEditorStringTableFillType {

			ibGridEditorStringTableFillType(int row, int col, ibSpreadsheetFillType type) :
				m_row(row), m_col(col), m_fillType(type) {
			}

			int m_row, m_col;
			ibSpreadsheetFillType m_fillType;
		};

	public:

		ibGridEditorStringTable() : ibGridStringTable() {}
		ibGridEditorStringTable(int numRows, int numCols) : ibGridStringTable(numRows, numCols) {}

		virtual bool IsEmptyCell(int row, int col) {
			wxCHECK_MSG((row >= 0 && row < GetNumberRows()) &&
				(col >= 0 && col < GetNumberCols()),
				true,
				wxT("invalid row or column index in ibGridStringTable"));

			const ibSpreadsheetFillType type = GetTypeString(row, col);
			return type == ibSpreadsheetFillType_StrText || type == ibSpreadsheetFillType_StrTemplate ?
				ibBackendLocalization::IsEmptyLocalizationString(m_data[row][col]) : m_data[row][col].IsEmpty();
		}

		virtual bool CanGetValueAs(int row, int col, const wxString& typeName)
		{
			const ibSpreadsheetFillType type = GetTypeString(row, col);

			if (type == ibSpreadsheetFillType_StrText)
				return typeName == s_strTypeTextOrString;
			else if (type == ibSpreadsheetFillType_StrTemplate)
				return typeName == s_strTypeTemplate;
			else if (type == ibSpreadsheetFillType_StrParameter)
				return typeName == s_strTypeParameter;

			return typeName == wxT("string");
		}

		virtual bool CanSetValueAs(int row, int col, const wxString& typeName)
		{
			const ibSpreadsheetFillType type = GetTypeString(row, col);

			if (type == ibSpreadsheetFillType_StrText)
				return typeName == s_strTypeTextOrString;
			else if (type == ibSpreadsheetFillType_StrTemplate)
				return typeName == s_strTypeTemplate;
			else if (type == ibSpreadsheetFillType_StrParameter)
				return typeName == s_strTypeParameter;

			return typeName == wxT("string");
		}

		virtual void GetValue(int row, int col, wxString& s) override {
			// Paint passes on column scroll can request (row, col) pairs for
			// cells that don't physically exist yet — the grid's visible-range
			// cache is ahead of the table's actual storage when rows/cols are
			// inserted. Base ibGridStringTable::GetValue already guards this
			// via wxCHECK2_MSG; the override used to skip the check entirely
			// and hit m_data[row][col] on a non-existing row, producing an
			// assertion failure mid-scroll.
			wxCHECK_RET((row >= 0 && row < GetNumberRows()) &&
				(col >= 0 && col < GetNumberCols()),
				wxT("invalid row or column index in ibGridEditorStringTable::GetValue"));
			if (col >= static_cast<int>(m_data[row].GetCount()))
				return; // logical column exists but this row isn't filled yet

			const ibSpreadsheetFillType type = GetTypeString(row, col);
			if (type == ibSpreadsheetFillType_StrText || type == ibSpreadsheetFillType_StrTemplate)
				ibBackendLocalization::GetTranslateGetRawLocText(m_data[row][col], s);
			else if (type == ibSpreadsheetFillType_StrParameter)
				s = m_data[row][col];
		}

		virtual void SetValue(int row, int col, const wxString& s) override {
			wxCHECK_RET((row >= 0 && row < GetNumberRows()) &&
				(col >= 0 && col < GetNumberCols()),
				wxT("invalid row or column index in ibGridEditorStringTable::SetValue"));

			// m_data[row] may not yet have this column entry (row appended
			// but column fill deferred). Treat the current value as empty
			// in that case — the subsequent ibGridStringTable::SetValue path
			// will expand the row via the base class's storage logic.
			const wxString emptyValue;
			const wxString& value = (col < static_cast<int>(m_data[row].GetCount()))
				? m_data[row][col]
				: emptyValue;

			if (s != value) {
				const ibSpreadsheetFillType type = GetTypeString(row, col);
				if (type == ibSpreadsheetFillType_StrText || type == ibSpreadsheetFillType_StrTemplate) {
					if (ibBackendLocalization::IsLocalizationString(s)) {
						ibGridStringTable::SetValue(row, col, s);
					}
					else if (ibBackendLocalization::IsLocalizationString(value)) {
						static ibBackendLocalizationEntryArray array;
						if (ibBackendLocalization::CreateLocalizationArray(value, array)) {
							ibBackendLocalization::SetArrayTranslate(array, s);
							ibGridStringTable::SetValue(row, col,
								ibBackendLocalization::GetRawLocText(array));
						}
					}
					else {
						ibGridStringTable::SetValue(row, col, ibBackendLocalization::CreateLocalizationRawLocText(s));
					}
				}
				else if (type == ibSpreadsheetFillType_StrParameter) {
					ibGridStringTable::SetValue(row, col, s);
				}
			}
		}

		// For user defined types
		virtual void SetValueAsCustom(int row, int col, const wxString& typeName, void* value) {

			if (value && stringUtils::CompareString(typeName, s_strTypeTextOrString)) {
				const wxString* s = static_cast<wxString*>(value);
				auto iterator = std::find_if(m_setColRowType.begin(), m_setColRowType.end(),
					[row, col](const auto& value) { return value.m_row == row && value.m_col == col; });
				if (iterator != m_setColRowType.end())
					m_setColRowType.erase(iterator);
				ibGridEditorStringTable::SetValue(row, col, *s);
			}
			else if (value && stringUtils::CompareString(typeName, s_strTypeTemplate)) {
				const wxString* s = static_cast<wxString*>(value);
				auto iterator = std::find_if(m_setColRowType.begin(), m_setColRowType.end(),
					[row, col](const auto& value) { return value.m_row == row && value.m_col == col; });
				if (iterator == m_setColRowType.end())
					m_setColRowType.emplace_back(row, col, ibSpreadsheetFillType_StrTemplate);
				else
					iterator->m_fillType = ibSpreadsheetFillType_StrTemplate;
				ibGridEditorStringTable::SetValue(row, col, *s);
			}
			else if (value && stringUtils::CompareString(typeName, s_strTypeParameter)) {
				const wxString* s = static_cast<wxString*>(value);
				auto iterator = std::find_if(m_setColRowType.begin(), m_setColRowType.end(),
					[row, col](const auto& value) { return value.m_row == row && value.m_col == col; });
				if (iterator == m_setColRowType.end())
					m_setColRowType.emplace_back(row, col, ibSpreadsheetFillType_StrParameter);
				else
					iterator->m_fillType = ibSpreadsheetFillType_StrParameter;
				ibGridEditorStringTable::SetValue(row, col, *s);
			}
		}

		virtual void* GetValueAsCustom(int row, int col, const wxString& typeName) {
			// Same reasoning as GetValue/SetValue — renderer can hit this
			// for cells that don't physically exist during an insert/scroll
			// race. Return nullptr (empty string) instead of dereferencing
			// out-of-bounds m_data[row][col].
			wxCHECK_MSG((row >= 0 && row < GetNumberRows()) &&
				(col >= 0 && col < GetNumberCols()),
				nullptr,
				wxT("invalid row or column index in ibGridEditorStringTable::GetValueAsCustom"));
			if (col >= static_cast<int>(m_data[row].GetCount()))
				return nullptr;

			const ibSpreadsheetFillType typeFill = GetTypeString(row, col);
			if (stringUtils::CompareString(typeName, s_strTypeTextOrString)) {
				if (typeFill == ibSpreadsheetFillType::ibSpreadsheetFillType_StrText || typeFill == ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate) {
					ibBackendLocalizationEntryArray array;
					ibBackendLocalization::CreateLocalizationArray(m_data[row][col], array);
					wxString* s = new wxString;
					ibBackendLocalization::GetRawLocText(array, *s);
					return s;
				}
				return new wxString(ibBackendLocalization::CreateLocalizationRawLocText(m_data[row][col]));
			}
			else if (stringUtils::CompareString(typeName, s_strTypeTemplate)) {
				if (typeFill == ibSpreadsheetFillType::ibSpreadsheetFillType_StrText || typeFill == ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate) {
					ibBackendLocalizationEntryArray array;
					ibBackendLocalization::CreateLocalizationArray(m_data[row][col], array);
					wxString* s = new wxString;
					ibBackendLocalization::GetRawLocText(array, *s);
					return s;
				}
				return new wxString(ibBackendLocalization::CreateLocalizationRawLocText(m_data[row][col]));
			}
			else if (stringUtils::CompareString(typeName, s_strTypeParameter)) {
				if (typeFill == ibSpreadsheetFillType::ibSpreadsheetFillType_StrText || typeFill == ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate) {
					wxString* s = new wxString;
					ibBackendLocalization::GetTranslateGetRawLocText(m_data[row][col], *s);
					return s;
				}
				return new wxString(m_data[row][col]);
			}

			return nullptr;
		}

		// overridden functions from ibGridTableBase
		//
		wxString GetRowLabelValue(int row) {
			// RD: Starting the rows at zero confuses users,
			// no matter how much it makes sense to us geeks.
			return stringUtils::IntToStr(row + 1);
		}

		virtual wxString GetColLabelValue(int col) override {
			return stringUtils::IntToStr(col + 1);
		}

	private:

		ibSpreadsheetFillType GetTypeString(int row, int col) const {

			for (const auto& value : m_setColRowType) {
				if (value.m_row == row && value.m_col == col)
					return value.m_fillType;
			}

			return ibSpreadsheetFillType_StrText;
		}

		std::vector<ibGridEditorStringTableFillType> m_setColRowType;
	};

	// the property of grid 
	class ibPropertyGridEditorSpreadsheet :
		public ibPropertyObject {
	public:

		ibPropertyGridEditorSpreadsheet(ibGridEditor* view) : m_view(view) {
			if (m_view != nullptr) {
				m_view->Bind(wxEVT_GRID_SELECT_CELL, &ibPropertyGridEditorSpreadsheet::OnSelectCell, this);
				m_view->Bind(wxEVT_GRID_RANGE_SELECTED, &ibPropertyGridEditorSpreadsheet::OnSelectCells, this);
			}
		}

		virtual ~ibPropertyGridEditorSpreadsheet() {
			if (m_view != nullptr) m_view->DeletePendingEvents();
		}

		virtual bool IsEditable() const { return m_view->IsEditable(); }

		//system override 
		virtual wxString GetObjectTypeName() const override { return s_strPropertyClass; }
		virtual wxString GetClassName() const { return s_strPropertyClass; }

		/// Gets the metadata object
		virtual ibMetaData* GetMetaData() const;

		/**
		* Property events
		*/
		virtual void OnPropertyCreated(ibProperty* property);
		virtual void OnPropertyRefresh() override;
		virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

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

		ibPropertyCategory* m_categoryGeneral = ibPropertyObject::CreatePropertyCategory(wxT("General"), _("General"));
		ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_categoryGeneral, wxT("Name"), _("Name"), wxEmptyString);
		ibPropertyTString* m_propertyText = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryGeneral, wxT("Text"), _("Text"), wxEmptyString);
		ibPropertyBoolean* m_propertyReadOnly = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryGeneral, wxT("ReadOnly"), _("Read only"), false);

		ibPropertyCategory* m_categoryTemplate = ibPropertyObject::CreatePropertyCategory(wxT("Template"), _("Template"));
		ibPropertyEnum<ibValueEnumSpreadsheetFillType>* m_propertyFillType = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetFillType>>(m_categoryTemplate, wxT("FillType"), _("Fill type"), ibSpreadsheetFillType::ibSpreadsheetFillType_StrText);
		ibPropertyUEString* m_propertyParameter = ibPropertyObject::CreateProperty<ibPropertyUEString>(m_categoryTemplate, wxT("Parameter"), _("Parameter"), wxEmptyString);
		ibPropertyUEString* m_propertyDetailsParameter = ibPropertyObject::CreateProperty<ibPropertyUEString>(m_categoryTemplate, wxT("DetailsParameter"), _("Details parameter"), wxEmptyString);

		ibPropertyCategory* m_categoryAlignment = ibPropertyObject::CreatePropertyCategory(wxT("Alignment"), _("Alignment"));
		ibPropertyEnum<ibValueEnumSpreadsheetFitMode>* m_propertyFitMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetFitMode>>(m_categoryAlignment, wxT("Git_mode"), _("Fit mode"), ibSpreadsheetFitMode::ibFitMode_Overflow);
		ibPropertyEnum<ibValueEnumSpreadsheetHorizontalAlignment>* m_propertyAlignHorz = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetHorizontalAlignment>>(m_categoryAlignment, wxT("Align_horz"), _("Horizontal"), ibSpreadsheetAlignmentHorz::ibAlignmentHorz_Left);
		ibPropertyEnum<ibValueEnumSpreadsheetVerticalAlignment>* m_propertyAlignVert = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetVerticalAlignment>>(m_categoryAlignment, wxT("Align_vert"), _("Vertical"), ibSpreadsheetAlignmentVert::ibAlignmentVert_Center);
		ibPropertyEnum<ibValueEnumSpreadsheetOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetOrient>>(m_categoryAlignment, wxT("Orient_text"), _("Orientation text"), ibSpreadsheetOrientation::ibOrient_Vertical);

		ibPropertyCategory* m_categoryAppearance = ibPropertyObject::CreatePropertyCategory(wxT("Appearance"), _("Appearance"));
		ibPropertyFont* m_propertyFont = ibPropertyObject::CreateProperty<ibPropertyFont>(m_categoryAppearance, wxT("Font"), _("Font"));
		ibPropertyColour* m_propertyBackgroundColour = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryAppearance, wxT("Background_colour"), _("Background colour"), wxNullColour);
		ibPropertyColour* m_propertyTextColour = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryAppearance, wxT("Text_colour"), _("Text colour"), wxNullColour);

		ibPropertyCategory* m_categoryBorder = ibPropertyObject::CreatePropertyCategory(wxT("Border"), _("Border"));
		ibPropertyEnum<ibValueEnumSpreadsheetBorder>* m_propertyLeftBorder = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetBorder>>(m_categoryBorder, wxT("Left_border"), _("Left"), ibSpreadsheetPenStyle::ibPenStyle_Transparent);
		ibPropertyEnum<ibValueEnumSpreadsheetBorder>* m_propertyRightBorder = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetBorder>>(m_categoryBorder, wxT("Right_border"), _("Right"), ibSpreadsheetPenStyle::ibPenStyle_Transparent);
		ibPropertyEnum<ibValueEnumSpreadsheetBorder>* m_propertyTopBorder = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetBorder>>(m_categoryBorder, wxT("Top_border"), _("Top"), ibSpreadsheetPenStyle::ibPenStyle_Transparent);
		ibPropertyEnum<ibValueEnumSpreadsheetBorder>* m_propertyBottomBorder = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSpreadsheetBorder>>(m_categoryBorder, wxT("Bottom_border"), _("Bottom"), ibSpreadsheetPenStyle::ibPenStyle_Transparent);
		ibPropertyColour* m_propertyColourBorder = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryBorder, wxT("Border_colour"), _("Colour"), wxNullColour);
	};

public:

	void ActivateEditor();

	void SendPropertyModify() {
		ibGrid::SendEvent(wxEVT_GRID_EDITOR_HIDDEN, m_currentCellCoords);
	}

	void SendPropertyModify(const ibGridCellCoords& coords) {
		ibGrid::SendEvent(wxEVT_GRID_EDITOR_HIDDEN, coords);
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

	bool AssociatibDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc);
	bool GetActivibDocument(wxObjectDataPtr<ibBackendSpreadsheetObject>& doc) const;

#pragma region file

	bool LoadDocument(const ibSpreadsheetDescription& spreadsheetDesc);
	bool LoadDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc);
	bool SaveDocument(ibSpreadsheetDescription& spreadsheetDesc) const;
	bool SaveDocument(wxObjectDataPtr<ibBackendSpreadsheetObject>& doc) const;

#pragma endregion 

	void PutDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, unsigned int groupLevel = 0);
	void JoinDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, unsigned int groupLevel = 0);

	// Bridge called by the spreadsheet notifier when BeginGroup/EndGroup closes
	// a block — mirrors the new area into m_rowAreaAt / m_colAreaAt so the
	// outline pane repaints immediately.
	void AppendRowOutlineGroup(unsigned int start, unsigned int end, unsigned int level);
	void AppendColOutlineGroup(unsigned int start, unsigned int end, unsigned int level);

	class ibGridEditorPrintout* CreatePrintout() const;

protected:

	void GetCellDetailsParameter(int row, int col, wxString& s) const;
	void SetCellDetailsParameter(int row, int col, const wxString& s);

	wxString GetCellDetailsParameter(int row, int col) const {
		wxString s;
		GetCellDetailsParameter(row, col, s);
		return s;
	}

	void SetCellDetailsParameter(const ibGridBlockCoords& coords, const wxString& s, bool sendUndoCommand = true);

	bool LoadSpreadsheet(const ibSpreadsheetDescription& spreadsheetDesc);
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

	void OnRowHeight(wxCommandEvent& event);
	void OnColWidth(wxCommandEvent& event);
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

	void OnIdle(wxIdleEvent& event);
	void OnSize(wxSizeEvent& event);

	void OnGridZoom(ibGridEvent& event);

private:

	const wxString m_GRID_VALUE_STRING = wxGRID_VALUE_STRING;

	//document 
	ibMetaDocument* m_document;

	// grid property
	ibPropertyGridEditorSpreadsheet* m_propertySpreadsheet;

	//grid doc
	wxObjectDataPtr<ibBackendSpreadsheetObject> m_spreadsheetObject;
	wxSharedPtr<ibBackendSpreadsheetNotifier> m_notifier;

	//grid enabled property? 
	bool m_enableProperty;

	wxDECLARE_EVENT_TABLE();
};


#endif 