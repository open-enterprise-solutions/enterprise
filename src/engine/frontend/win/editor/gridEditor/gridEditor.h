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
				wxT("invalid row or column index in ibGridEditorStringTable::IsEmptyCell"));

			// 🛑 A ROW THE TABLE HAS IS NOT A ROW THAT HAS ITS COLUMNS. AppendRows leaves the new
			// row an empty wxArrayString when m_numCols is 0, and the fill is deferred — so
			// m_data[row] can hold fewer entries than the table reports columns. GetValue and
			// SetValue both guard exactly this (see their bodies); IsEmptyCell did not, and it is
			// the one the content scan walks over EVERY cell of the sheet with. A logical column
			// with no entry behind it holds nothing, which is what "empty" means.
			if (col >= static_cast<int>(m_data[row].GetCount()))
				return true;

			const ibSpreadsheetFillType type = GetTypeString(row, col);
			return type == ibSpreadsheetFillType_StrText || type == ibSpreadsheetFillType_StrTemplate ?
				ibBackendLocalization::IsEmptyLocalizationString(m_data[row][col]) : m_data[row][col].IsEmpty();
		}

		// ⭐ WHERE THE CONTENT ENDS — a question for the TABLE, because the table is where content
		// lives. The endless sheet asks it before trimming itself back, i.e. on every scroll EVENT
		// (three per wheel notch), so it cannot be a walk over the sheet: at twenty thousand rows
		// the walk is a visible stall.
		//
		// 🛑 AND IT IS NOT INVALIDATED BY A LIST OF CALLERS. Everything that can move the edge —
		// a value written, rows or columns inserted or deleted, the table cleared — passes through
		// THIS class, so the answer is kept where the change happens and a door added later cannot
		// forget to say so. Same arrangement as ibGridLineSizes in gridext.h.
		//
		// ⭐ MAINTAINED where the new answer is knowable, dropped only where it is not: writing
		// into a row past the edge MOVES the edge (no walk), and growing the sheet does not move it
		// at all — which matters, because the endless sheet grows on the very event that asks.
		// Clearing the cell that IS the edge is the one case nothing short of a walk can answer.
		int GetLastContentRow() {
			if (m_lastContentRow == kExtentUnknown)
				RebuildContentExtent();
			return m_lastContentRow;
		}

		int GetLastContentCol() {
			if (m_lastContentCol == kExtentUnknown)
				RebuildContentExtent();
			return m_lastContentCol;
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

				// The cell now holds something else than it did — which is the only way a WRITE can
				// move where the content ends. Asked of the cell rather than of `s`, because empty
				// is not the same question for a localisation string as for a parameter.
				NoteContentAt(row, col);
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

		// ------ the structural doors: everything below keeps the content edge honest ------
		//
		// Appending is NOT here on purpose. Rows and columns grown at the far end are empty, so
		// the edge does not move — and the endless sheet grows exactly while it is asking, so
		// dropping the answer there would put the walk back on every scroll.

		void Clear() override {
			ibGridStringTable::Clear();
			m_lastContentRow = m_lastContentCol = -1;   // known, not unknown: there is nothing anywhere
		}

		bool InsertRows(size_t pos = 0, size_t numRows = 1) override {
			if (!ibGridStringTable::InsertRows(pos, numRows))
				return false;
			// Everything at or below the insert point moved down by that much, the edge with it.
			if (m_lastContentRow >= 0 && static_cast<int>(pos) <= m_lastContentRow)
				m_lastContentRow += static_cast<int>(numRows);
			return true;
		}

		bool InsertCols(size_t pos = 0, size_t numCols = 1) override {
			if (!ibGridStringTable::InsertCols(pos, numCols))
				return false;
			if (m_lastContentCol >= 0 && static_cast<int>(pos) <= m_lastContentCol)
				m_lastContentCol += static_cast<int>(numCols);
			return true;
		}

		bool DeleteRows(size_t pos = 0, size_t numRows = 1) override {
			// Reaching into content is the case nothing short of a walk can answer — what was cut
			// may have BEEN the edge. Cutting the empty tail past it changes nothing.
			const bool touchesContent = m_lastContentRow < 0 || static_cast<int>(pos) <= m_lastContentRow;
			if (!ibGridStringTable::DeleteRows(pos, numRows))
				return false;
			if (touchesContent)
				DropContentExtent();
			return true;
		}

		bool DeleteCols(size_t pos = 0, size_t numCols = 1) override {
			const bool touchesContent = m_lastContentCol < 0 || static_cast<int>(pos) <= m_lastContentCol;
			if (!ibGridStringTable::DeleteCols(pos, numCols))
				return false;
			if (touchesContent)
				DropContentExtent();
			return true;
		}

	private:

		// -2, because -1 is a real answer: "nothing anywhere, the sheet may shrink freely".
		static const int kExtentUnknown = -2;

		void DropContentExtent() { m_lastContentRow = m_lastContentCol = kExtentUnknown; }

		// A cell was written. Something put where nothing was moves the edge out; nothing put
		// where the edge WAS is the one case that needs the walk back.
		void NoteContentAt(int row, int col) {
			if (!IsEmptyCell(row, col)) {
				if (m_lastContentRow != kExtentUnknown && row > m_lastContentRow) m_lastContentRow = row;
				if (m_lastContentCol != kExtentUnknown && col > m_lastContentCol) m_lastContentCol = col;
			}
			else if (row == m_lastContentRow || col == m_lastContentCol) {
				DropContentExtent();
			}
		}

		// One pass answers BOTH edges — the caller asks for the row and the column together
		// (trimming does), and a second walk for the second answer is the same walk twice.
		void RebuildContentExtent() {
			int lastRow = -1, lastCol = -1;
			const int rows = GetNumberRows(), cols = GetNumberCols();
			for (int row = 0; row < rows; row++) {
				for (int col = 0; col < cols; col++) {
					if (IsEmptyCell(row, col))
						continue;
					lastRow = row;
					if (col > lastCol) lastCol = col;
				}
			}
			m_lastContentRow = lastRow;
			m_lastContentCol = lastCol;
		}

		int m_lastContentRow = kExtentUnknown;
		int m_lastContentCol = kExtentUnknown;

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

	// ⭐⭐ WHERE THE SHEET'S CONTENT ENDS — the last row (column) that holds anything.
	//
	// 🛑 THE ENDLESS SHEET MUST NOT EAT DATA. Scrolling down grows the sheet and
	// scrolling back up trims what it grew; the trim used to stop at the last PAGE
	// BREAK, which is zero in a document that has none — so a file opened from disk
	// (an Excel workbook with a single sheet, say) had its rows deleted the moment
	// somebody scrolled back up. What was added for the view may be taken away;
	// what a person's file brought may not.
	//
	// The TABLE keeps this answer — it is asked three times per wheel notch, and it is the
	// table that every write and every insert goes through. Not const, because the first
	// question after a change is what re-derives it.
	int GetLastContentRow();
	int GetLastContentCol();

protected:

	// The table is an ibGridEditorStringTable by construction: this editor sets its own in the
	// constructor and at every load, and nothing else ever calls SetTable on it. One cast, in
	// one place, rather than one at each question.
	ibGridEditorStringTable* GetEditorTable() const {
		return static_cast<ibGridEditorStringTable*>(ibGrid::GetTable());
	}

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

	// ⭐ THE SHEET SAYS IT IS BUSY. A report composes on a background run, so the window stays alive
	// and usable while it does — and a window that looks finished while it is still filling is worse
	// than one that waits: the person reads a half-built report as the answer. A small spinner with
	// a word beside it, centred over the sheet, is the whole story (Max, 2026-08-19: "while the
	// report is being built the little circle turns, like in the list, and meanwhile we can work").
	//
	// Idempotent: showing it twice is one spinner, hiding a hidden one does nothing.
	void ShowComposeProgress(bool busy);

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
	wxObjectDataPtr<ibBackendSpreadsheetObject> m_spreadsheetObject;
	wxSharedPtr<ibBackendSpreadsheetNotifier> m_notifier;

	// The "composing…" overlay — see ShowComposeProgress. Created on first use, kept hidden after.
	class wxWindow* m_composeProgress = nullptr;

	//grid enabled property? 
	bool m_enableProperty;

	wxDECLARE_EVENT_TABLE();
};


#endif 