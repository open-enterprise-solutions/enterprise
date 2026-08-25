#ifndef __CELL_DESCRIPTION_H__
#define __CELL_DESCRIPTION_H__

#include "backend/compiler/value.h"

#include <unordered_map>   // the cell index — MSVC drags it in transitively, libstdc++ does not
#include <cstdint>
#include <algorithm>

////////////////////////////////////////
// spreadsheet defines 

enum ibSpreadsheetOrientation {
	ibOrient_Horizontal = wxHORIZONTAL,
	ibOrient_Vertical = wxVERTICAL
};

enum ibSpreadsheetAlignmentHorz {
	ibAlignmentHorz_Left = wxAlignment::wxALIGN_LEFT,
	ibAlignmentHorz_Center = wxAlignment::wxALIGN_CENTER,
	ibAlignmentHorz_Right = wxAlignment::wxALIGN_RIGHT
};

enum ibSpreadsheetAlignmentVert {
	ibAlignmentVert_Top = wxAlignment::wxALIGN_TOP,
	ibAlignmentVert_Center = wxAlignment::wxALIGN_CENTER,
	ibAlignmentVert_Bottom = wxAlignment::wxALIGN_BOTTOM
};

enum ibSpreadsheetFitMode {
	ibFitMode_Overflow = 4,
	ibFitMode_Clip = 5
};

enum ibSpreadsheetPenStyle {
	ibPenStyle_Transparent = wxPenStyle::wxPENSTYLE_TRANSPARENT,
	ibPenStyle_Solid = wxPenStyle::wxPENSTYLE_SOLID,
	ibPenStyle_Dot = wxPenStyle::wxPENSTYLE_DOT,
	ibPenStyle_ShortDash = wxPenStyle::wxPENSTYLE_SHORT_DASH,
	ibPenStyle_DotDash = wxPenStyle::wxPENSTYLE_DOT_DASH,
	ibPenStyle_LongDash = wxPenStyle::wxPENSTYLE_LONG_DASH,
};

enum ibSpreadsheetFillType {
	ibSpreadsheetFillType_StrText = 1, //default
	ibSpreadsheetFillType_StrParameter,
	ibSpreadsheetFillType_StrTemplate
};

///////////////////////////////////////

const static int s_defaultRowHeight = 15;
const static int s_rowLabelWidth = 40;

const static int s_defaultColWidth = 70;
const static int s_colLabelHeight = 15;

static const wxFont s_defaultSpreadsheetFont = wxFont(8, wxFontFamily::wxFONTFAMILY_DEFAULT, wxFontStyle::wxFONTSTYLE_NORMAL, wxFontWeight::wxFONTWEIGHT_NORMAL);

///////////////////////////////////////

struct ibSpreadsheetBorderDescription {

	bool operator == (const ibSpreadsheetBorderDescription& rhs) const {
		return m_style == rhs.m_style &&
			m_width == rhs.m_width && m_colour == rhs.m_colour;
	}

	wxPenStyle m_style = wxPENSTYLE_TRANSPARENT;
	wxColour m_colour = *wxBLACK;
	int m_width = 1;
};

struct ibSpreadsheetCellDescription {

	enum ibFitMode
	{
		// This is a hack to save space: the first 4 elements of this enum are
		// the same as those of wxEllipsizeMode.
		Mode_Unset = wxELLIPSIZE_NONE,
		Mode_EllipsizeStart = wxELLIPSIZE_START,
		Mode_EllipsizeMiddle = wxELLIPSIZE_MIDDLE,
		Mode_EllipsizeEnd = wxELLIPSIZE_END,
		Mode_Overflow,
		Mode_Clip
	};

	ibSpreadsheetCellDescription(int row, int col) : m_row(row), m_col(col) {}

	bool IsEmptyValue() const { return m_value.IsEmpty(); }

	void GetValue(wxString& s) const { s = m_value; }
	void SetValue(const wxString& s) { m_value = s; }

	//special set
	wxString GetValue() const { return m_value; }

	bool IsEmptyParameter() const { return m_detailsParameter.IsEmpty(); }

	void GetParameter(wxString& s) const { s = m_detailsParameter; }
	void SetParameter(const wxString& s) { m_detailsParameter = s; }

	//special set
	wxString GetParameter() const { return m_detailsParameter; }

	int GetSize(int* num_rows, int* num_cols) const {

		if (num_rows != nullptr)
			*num_rows = m_row_size;
		if (num_cols != nullptr)
			*num_cols = m_col_size;

		if (m_row_size == 1 && m_col_size == 1)
			return 0; // just a normal cell

		if (m_row_size < 0 || m_col_size < 0)
			return -1; // covered by a multi-span cell

		// this cell spans multiple cells to its right/bottom
		return 1;
	}

	void SetSize(int num_rows, int num_cols) {
		m_row_size = num_rows;
		m_col_size = num_cols;
	}

	void SetCell(const ibSpreadsheetCellDescription* rhs) {
		if (rhs == nullptr)
			return;
		//m_row = rhs->m_row;
		//m_col = rhs->m_col;
		m_alignHorz = rhs->m_alignHorz;
		m_alignVert = rhs->m_alignVert;
		m_textOrient = rhs->m_textOrient;
		m_font = rhs->m_font;
		m_backgroundColour = rhs->m_backgroundColour;
		m_textColour = rhs->m_textColour;
		m_borderAt[0] = rhs->m_borderAt[0];
		m_borderAt[1] = rhs->m_borderAt[1];
		m_borderAt[2] = rhs->m_borderAt[2];
		m_borderAt[3] = rhs->m_borderAt[3];
		m_row_size = rhs->m_row_size;
		m_col_size = rhs->m_col_size;
		m_fitMode = rhs->m_fitMode;
		m_isReadOnly = rhs->m_isReadOnly;
		m_fillSetType = rhs->m_fillSetType;
		m_value = rhs->m_value;
		m_detailsParameter = rhs->m_detailsParameter;
	}

	ibSpreadsheetCellDescription& operator =(const ibSpreadsheetCellDescription& rhs) {
		SetCell(&rhs);
		return *this;
	}

	bool operator == (const ibSpreadsheetCellDescription& rhs) const {
		return m_row == rhs.m_row && m_col == rhs.m_col
			&& m_alignHorz == rhs.m_alignHorz
			&& m_alignVert == rhs.m_alignVert
			&& m_textOrient == rhs.m_textOrient
			&& m_font == rhs.m_font
			&& m_backgroundColour == rhs.m_backgroundColour
			&& m_textColour == rhs.m_textColour
			&& m_borderAt[0] == rhs.m_borderAt[0] && m_borderAt[1] == rhs.m_borderAt[1] && m_borderAt[2] == rhs.m_borderAt[2] && m_borderAt[3] == rhs.m_borderAt[3]
			&& m_row_size == rhs.m_row_size && m_col_size == rhs.m_col_size
			&& m_fillSetType == rhs.m_fillSetType
			&& m_detailsParameter == rhs.m_detailsParameter;
	}

	unsigned int m_row, m_col;
	wxString m_value;
	int m_alignHorz = wxALIGN_LEFT;
	int m_alignVert = wxALIGN_TOP;
	int m_textOrient = wxHORIZONTAL;
	wxFont m_font = s_defaultSpreadsheetFont;
	// 🛑 NOT SEEDED FROM THE SYSTEM. An unset colour is UNSET — it is not the window colour written
	// down early. Asking wxSystemSettings here made every cell CONSTRUCTION a question to the
	// desktop, and on GTK that question builds a GtkStyleContext, which needs a display connection:
	// on a headless runner it is a fatal Gtk-ERROR, so every test that created a single cell died
	// with SIGTRAP while the one that created none passed (CI, 2026-08-20).
	//
	// ⭐ AND IT WAS A LAYER VIOLATION BEFORE IT WAS A CRASH: wxSystemSettings is a GUI class, and the
	// backend is GUI-free by rule. What the desktop's window colour is happens to be a question only
	// something with a screen can answer — so it is asked where there IS one: the getters below
	// resolve an unset colour, exactly as they already resolved a missing cell.
	wxColour m_backgroundColour;
	wxColour m_textColour;
	ibSpreadsheetBorderDescription m_borderAt[4]; //left, right, top, bottom
	int m_row_size = 1, m_col_size = 1;
	ibFitMode m_fitMode = ibFitMode::Mode_Overflow;
	bool m_isReadOnly = false;
	ibSpreadsheetFillType m_fillSetType = ibSpreadsheetFillType::ibSpreadsheetFillType_StrText;	
	wxString m_detailsParameter;
};

class BACKEND_API ibSpreadsheetCellDescriptionMemory {
public:
	//load & save object in control 
	static bool LoadData(class ibReaderMemory& reader, ibSpreadsheetCellDescription& spreadsheetDesc);
	static bool SaveData(class ibWriterMemory& writer, const ibSpreadsheetCellDescription& spreadsheetDesc);
};

struct ibSpreadsheetAreaDescription {

	ibSpreadsheetAreaDescription(const wxString& label, unsigned int start, unsigned int end)
		: m_label(label), m_start(start), m_end(end) {}

	bool operator == (const ibSpreadsheetAreaDescription& rhs) const {
		return m_label == rhs.m_label
			&& m_start == rhs.m_start && m_end == rhs.m_end;
	}

	wxString m_label;
	unsigned int m_start, m_end;
};

// Outline grouping description — orthogonal to labels.
struct ibSpreadsheetGroupDescription {
	ibSpreadsheetGroupDescription(unsigned int start, unsigned int end,
		unsigned int level = 1, bool collapsed = false, int head = -1)
		: m_start(start), m_end(end), m_level(level), m_collapsed(collapsed), m_head(head) {}

	unsigned int m_start, m_end;
	unsigned int m_level;
	bool m_collapsed;
	// ⭐⭐ WHERE THE MARKER SITS, when the producer already knows. Left at -1 the grid works it out
	// (the line before the range — see ibGrid::NormalizeRowGroups), which is right for a document
	// whose groups arrive as HEADINGS with a depth each.
	//
	// A cross-table's columns do not arrive that way: a heading's TOTAL closes its group rather than
	// opening it (Max, 2026-08-26), so there is no line before the range to hang the button on — the
	// marker belongs on the group's own FIRST column. Stated here, the range is taken as final and
	// the normaliser leaves it alone.
	int m_head = -1;
};

struct ibSpreadsheetRowSizeDescription
{
	ibSpreadsheetRowSizeDescription(unsigned int row, unsigned int height = 0) : m_row(row), m_height(height) {}

	unsigned int m_row;
	unsigned int m_height = 0;
};

struct ibSpreadsheetColSizeDescription
{
	ibSpreadsheetColSizeDescription(unsigned int col, unsigned int width = 0) : m_col(col), m_width(width) {}

	unsigned int m_col;
	unsigned int m_width = 0;
};

struct ibSpreadsheetDescription {

	void ClearSpreadsheet(int count = 0) {

		m_cellAt.reserve(count);
		m_cellAt.clear();
		m_cellIndex.clear();
		m_cellIndex.reserve(count);
		m_maxRow = 0, m_maxCol = 0;

		m_rowBrakeAt.clear();
		m_colBrakeAt.clear();

		m_freezeRow = 0, m_freezeCol = 0;

		m_rowAreaAt.clear();
		m_colAreaAt.clear();
	}

	bool IsEmptySpreadsheet() const {
		return GetCellCount() == 0 &&
			GetBrakeNumberRows() == 0 && GetBrakeNumberCols() == 0 &&
			GetSizeNumberRows() == 0 && GetSizeNumberCols() == 0 &&
			GetAreaNumberRows() == 0 && GetAreaNumberCols() == 0;
	}

	// ⚠ FOUND BY ITS ADDRESS, NOT BY SCANNING. Both lookups used to be a linear
	// std::find_if over every cell, which made FILLING a sheet quadratic — and
	// filling is what a report does. Measured before the index (Debug): 1k cells
	// 5 ms, 2k 12 ms, 4k 33 ms, 8k 88 ms — the curve, not the constant. A
	// 10 000 x 10 report is 100 000 cells, i.e. tens of seconds of bookkeeping
	// before anything is drawn.
	//
	// The vector still OWNS the cells (order is insertion order, which the
	// serializer and the comparison rely on); the map only says where each one is.
	const ibSpreadsheetCellDescription* GetCell(int row, int col) const {

		if (row < 0 || col < 0)
			return nullptr;

		const auto iterator = m_cellIndex.find(CellKey(row, col));
		if (iterator != m_cellIndex.end())
			return &m_cellAt[iterator->second];

		return nullptr;
	}

	ibSpreadsheetCellDescription* GetOrCreateCell(int row, int col) {

		if (row < 0 || col < 0)
			return nullptr;

		const uint64_t key = CellKey(row, col);
		const auto iterator = m_cellIndex.find(key);
		if (iterator != m_cellIndex.end())
			return &m_cellAt[iterator->second];

		m_cellIndex.emplace(key, m_cellAt.size());

		ibSpreadsheetCellDescription& entry =
			m_cellAt.emplace_back(row, col);

		// The extent is maintained here rather than re-derived by walking every cell
		// on each GetNumberRows() — the same scan, in the other direction.
		m_maxRow = std::max(m_maxRow, entry.m_row + 1);
		m_maxCol = std::max(m_maxCol, entry.m_col + 1);

		return &entry;
	}

	const ibSpreadsheetCellDescription* GetCellByIdx(size_t idx) const {
		if (idx >= m_cellAt.size())
			return nullptr;
		return &m_cellAt[idx];
	}

	int GetCellCount() const { return m_cellAt.size(); }

	int GetRowBrakeByIdx(size_t idx) const {
		if (idx >= m_rowBrakeAt.size())
			return 0;
		return m_rowBrakeAt[idx];
	}

	int GetColBrakeByIdx(size_t idx) const {
		if (idx >= m_colBrakeAt.size())
			return 0;
		return m_colBrakeAt[idx];
	}

	int GetBrakeNumberRows() const { return m_rowBrakeAt.size(); }
	int GetBrakeNumberCols() const { return m_colBrakeAt.size(); }

	//area
	void AddRowArea(const wxString& strAreaName,
		unsigned int start, unsigned int end) {
		m_rowAreaAt.emplace_back(strAreaName, start, end);
	}

	void DeleteRowArea(const wxString& strAreaName) {
		auto iterator = std::find_if(m_rowAreaAt.begin(), m_rowAreaAt.end(),
			[strAreaName](const auto& v) { return stringUtils::CompareString(v.m_label, strAreaName); });
		if (iterator != m_rowAreaAt.end())
			m_rowAreaAt.erase(iterator);
	}

	void AddColArea(const wxString& strAreaName,
		unsigned int start, unsigned int end) {
		m_colAreaAt.emplace_back(strAreaName, start, end);
	}

	void DeleteColArea(const wxString& strAreaName) {
		auto iterator = std::find_if(m_colAreaAt.begin(), m_colAreaAt.end(),
			[strAreaName](const auto& v) { return stringUtils::CompareString(v.m_label, strAreaName); });
		if (iterator != m_colAreaAt.end())
			m_colAreaAt.erase(iterator);
	}

	const ibSpreadsheetAreaDescription* GetRowAreaByIdx(size_t idx) const {
		if (idx >= m_rowAreaAt.size())
			return nullptr;
		return &m_rowAreaAt[idx];
	}

	const ibSpreadsheetAreaDescription* GetRowAreaByName(const wxString& strAreaName) const {
		auto iterator = std::find_if(m_rowAreaAt.begin(), m_rowAreaAt.end(),
			[strAreaName](const auto& v) { return stringUtils::CompareString(v.m_label, strAreaName); });
		if (iterator != m_rowAreaAt.end())
			return &*iterator;
		return nullptr;
	}

	void SetRowSizeArea(const wxString& strAreaName, int start, int end) {
		auto iterator = std::find_if(m_rowAreaAt.begin(), m_rowAreaAt.end(),
			[strAreaName](const auto& v) { return stringUtils::CompareString(v.m_label, strAreaName); });
		if (iterator != m_rowAreaAt.end()) {
			iterator->m_start = start;
			iterator->m_end = end;
		}
	}

	void SetRowNameArea(size_t idx, const wxString& strAreaName) {
		if (idx >= m_rowAreaAt.size())
			return;
		m_rowAreaAt[idx].m_label = strAreaName;
	}

	const ibSpreadsheetAreaDescription* GetColAreaByIdx(size_t idx) const {
		if (idx >= m_colAreaAt.size())
			return nullptr;
		return &m_colAreaAt[idx];
	}

	const ibSpreadsheetAreaDescription* GetColAreaByName(const wxString& strAreaName) const {
		auto iterator = std::find_if(m_colAreaAt.begin(), m_colAreaAt.end(),
			[strAreaName](const auto& v) { return stringUtils::CompareString(v.m_label, strAreaName); });
		if (iterator != m_colAreaAt.end())
			return &*iterator;
		return nullptr;
	}

	void SetColSizeArea(const wxString& strAreaName, int start, int end) {
		auto iterator = std::find_if(m_colAreaAt.begin(), m_colAreaAt.end(),
			[strAreaName](const auto& v) { return stringUtils::CompareString(v.m_label, strAreaName); });
		if (iterator != m_colAreaAt.end()) {
			iterator->m_start = start;
			iterator->m_end = end;
		}
	}

	void SetColNameArea(size_t idx, const wxString& strAreaName) {
		if (idx >= m_colAreaAt.size())
			return;
		m_colAreaAt[idx].m_label = strAreaName;
	}

	int GetAreaNumberRows() const { return m_rowAreaAt.size(); }
	int GetAreaNumberCols() const { return m_colAreaAt.size(); }

	// ------ outline groups (independent of label areas) ------
	void AddRowGroup(unsigned int start, unsigned int end, unsigned int level = 1, bool collapsed = false) {
		m_rowGroupAt.emplace_back(start, end, level, collapsed);
	}
	void AddColGroup(unsigned int start, unsigned int end, unsigned int level = 1, bool collapsed = false,
		int head = -1) {
		m_colGroupAt.emplace_back(start, end, level, collapsed, head);
	}
	int GetGroupNumberRows() const { return (int)m_rowGroupAt.size(); }
	int GetGroupNumberCols() const { return (int)m_colGroupAt.size(); }
	const ibSpreadsheetGroupDescription* GetRowGroupByIdx(size_t idx) const {
		return idx < m_rowGroupAt.size() ? &m_rowGroupAt[idx] : nullptr;
	}
	const ibSpreadsheetGroupDescription* GetColGroupByIdx(size_t idx) const {
		return idx < m_colGroupAt.size() ? &m_colGroupAt[idx] : nullptr;
	}
	void ClearRowGroups() { m_rowGroupAt.clear(); }
	void ClearColGroups() { m_colGroupAt.clear(); }

	// ------ grid dimensions
	//

	// MAINTAINED, not re-derived. These are asked constantly — every PutArea, every
	// paint, every append decides where it lands by them — and each answer used to
	// walk the whole cell vector.
	int GetNumberRows() const { return (int)m_maxRow; }
	int GetNumberCols() const { return (int)m_maxCol; }

	// ------ row and col formatting
	//

	void SetRowSize(int row, int height = 0) {

		auto iterator = std::find_if(m_rowHeightAt.begin(),
			m_rowHeightAt.end(), [row](const auto& value) { return row == value.m_row; });

		if (iterator != m_rowHeightAt.end()) {
			iterator->m_height = height;
			return;
		}

		m_rowHeightAt.emplace_back(row, height);
	}

	const ibSpreadsheetRowSizeDescription* GetRowSizeByIdx(size_t idx) const {
		if (idx >= m_rowHeightAt.size())
			return nullptr;
		return &m_rowHeightAt[idx];
	}

	void SetColSize(int col, int width = 0) {

		auto iterator = std::find_if(m_colWidthAt.begin(),
			m_colWidthAt.end(), [col](const auto& value) { return col == value.m_col; });

		if (iterator != m_colWidthAt.end()) {
			iterator->m_width = width;
			return;
		}

		m_colWidthAt.emplace_back(col, width);
	}

	const ibSpreadsheetColSizeDescription* GetColSizeByIdx(size_t idx) const {
		if (idx >= m_colWidthAt.size())
			return nullptr;
		return &m_colWidthAt[idx];
	}

	int GetRowSize(int row) const {
		auto iterator = std::find_if(m_rowHeightAt.begin(),
			m_rowHeightAt.end(), [row](const auto& value) { return row == value.m_row; });

		if (iterator != m_rowHeightAt.end())
			return iterator->m_height;

		return s_defaultRowHeight;
	}

	bool IsRowShown(int row) const { return GetRowSize(row) != 0; }

	int GetColSize(int col) const {
		auto iterator = std::find_if(m_colWidthAt.begin(),
			m_colWidthAt.end(), [col](const auto& value) { return col == value.m_col; });

		if (iterator != m_colWidthAt.end())
			return iterator->m_width;

		return s_defaultColWidth;
	}

	bool IsColShown(int col) const { return GetColSize(col) != 0; }

	int GetSizeNumberRows() const { return m_rowHeightAt.size(); }
	int GetSizeNumberCols() const { return m_colWidthAt.size(); }

	//cell
	// ⭐ THE ONE PLACE THE DESKTOP IS ASKED — and only when somebody actually wants a colour. A cell
	// that was never given one and a cell that does not exist are the same answer: whatever the
	// window colour is. Storing that answer at construction is what took the question to a machine
	// with no screen; asking it here takes it to a caller that is about to paint.
	wxColour GetCellBackgroundColour(int row, int col) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr && cell->m_backgroundColour.IsOk())
			return cell->m_backgroundColour;
		return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	}

	void SetCellBackgroundColour(int row, int col, const wxColour& colour) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr)
			cell->m_backgroundColour = colour;
	}

	wxColour GetCellTextColour(int row, int col) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr && cell->m_textColour.IsOk())
			return cell->m_textColour;
		return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	}

	void SetCellTextColour(int row, int col, const wxColour& colour) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr)
			cell->m_textColour = colour;
	}

	int GetCellTextOrient(int row, int col) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr)
			return cell->m_textOrient;
		return wxHORIZONTAL;
	}

	void SetCellTextOrient(int row, int col, const int orient) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr)
			cell->m_textOrient = orient;
	}

	wxFont GetCellFont(int row, int col) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr)
			return cell->m_font;
		return s_defaultSpreadsheetFont;
	}

	void SetCellFont(int row, int col, const wxFont& font) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr)
			cell->m_font = font;
	}

	// 🛑 IT ALWAYS ANSWERS. It used to leave the outputs UNTOUCHED when the cell did not exist, and
	// every caller declares a bare `int` for them — so reading the alignment of a cell nobody has
	// written handed back whatever was on the stack, and WRITING one alignment carried the other's
	// garbage into the cell it created (`SetCellAlignment(m_row, m_col, horz, vertical)`). GCC says
	// so plainly ("may be used uninitialized") and MSVC never has (CI, 2026-08-20).
	//
	// A cell that does not exist has the alignment a new cell would have — which is what the caller
	// means by asking. Answered HERE rather than at the four call sites, so a fifth caller cannot
	// get it wrong: the same shape the colour getters already have.
	void GetCellAlignment(int row, int col, int* horiz, int* vert) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (horiz) *horiz = cell != nullptr ? cell->m_alignHorz : wxALIGN_LEFT;
		if (vert)  *vert  = cell != nullptr ? cell->m_alignVert : wxALIGN_TOP;
	}

	void SetCellAlignment(int row, int col, const int horiz, const int vert) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr) {
			cell->m_alignHorz = horiz;
			cell->m_alignVert = vert;
		}
	}

	ibSpreadsheetBorderDescription GetCellBorderLeft(int row, int col) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr) return cell->m_borderAt[0];
		return ibSpreadsheetBorderDescription();
	}

	void SetCellBorderLeft(int row, int col, const ibSpreadsheetBorderDescription& desc) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr)
			cell->m_borderAt[0] = desc;
	}

	ibSpreadsheetBorderDescription GetCellBorderRight(int row, int col) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr) return cell->m_borderAt[1];
		return ibSpreadsheetBorderDescription();
	}

	void SetCellBorderRight(int row, int col, const ibSpreadsheetBorderDescription& desc) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr)
			cell->m_borderAt[1] = desc;
	}

	ibSpreadsheetBorderDescription GetCellBorderTop(int row, int col) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr) return cell->m_borderAt[2];
		return ibSpreadsheetBorderDescription();
	}

	void SetCellBorderTop(int row, int col, const ibSpreadsheetBorderDescription& desc) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr)
			cell->m_borderAt[2] = desc;
	}

	ibSpreadsheetBorderDescription GetCellBorderBottom(int row, int col) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr) return cell->m_borderAt[3];
		return ibSpreadsheetBorderDescription();
	}

	void SetCellBorderBottom(int row, int col, const ibSpreadsheetBorderDescription& desc) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr)
			cell->m_borderAt[3] = desc;
	}

	int GetCellSize(int row, int col, int* num_rows, int* num_cols) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr) return cell->GetSize(num_rows, num_cols);
		if (num_rows != nullptr) *num_rows = 1;
		if (num_cols != nullptr) *num_cols = 1;
		return 0;
	}

	void SetCellSize(int row, int col, int num_rows, int num_cols) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr) cell->SetSize(num_rows, num_cols);
	}

	ibSpreadsheetCellDescription::ibFitMode GetCellFitMode(int row, int col) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr) return cell->m_fitMode;
		return ibSpreadsheetCellDescription::ibFitMode::Mode_Overflow;
	}

	void SetCellFitMode(int row, int col, ibSpreadsheetCellDescription::ibFitMode fitMode) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr) cell->m_fitMode = fitMode;
	}

	// ⭐ A READ THAT DOES NOT WRITE. This asked GetOrCreateCell, so ASKING whether a cell was
	// read-only MATERIALISED it — a walk that probes a sheet grew the description by every cell it
	// merely looked at. GetCell answers the same question without creating: an absent cell is not
	// read-only, which is exactly what the created-and-empty one used to answer.
	//
	// It also carried an `isReadOnly` argument it never read — a getter wearing its setter's
	// signature, one slip away from `IsCellReadOnly(r, c, false)` reading as a question that was
	// never asked.
	bool IsCellReadOnly(int row, int col) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		return cell != nullptr && cell->m_isReadOnly;
	}

	void SetCellReadOnly(int row, int col, bool isReadOnly = true) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr) cell->m_isReadOnly = isReadOnly;
	}

	// ------ cell brake accessors
	//
	//support printing 
	void AddRowBrake(int row) { m_rowBrakeAt.emplace_back(row); }
	void AddColBrake(int col) { m_colBrakeAt.emplace_back(col); }

	void DeleteRowBrake(int row) { m_rowBrakeAt.erase(std::remove(m_rowBrakeAt.begin(), m_rowBrakeAt.end(), row), m_rowBrakeAt.end()); }
	void DeleteColBrake(int col) { m_colBrakeAt.erase(std::remove(m_colBrakeAt.begin(), m_colBrakeAt.end(), col), m_colBrakeAt.end()); }

	void SetRowBrake(int row) {
		if (m_rowBrakeAt.size() != 0)
			m_rowBrakeAt[std::distance(m_rowBrakeAt.begin(),
				std::max_element(m_rowBrakeAt.begin(), m_rowBrakeAt.end()))] = wxMax(wxMin(m_rowBrakeAt[m_rowBrakeAt.size() - 1], GetNumberRows() - 1), row);
		else
			m_rowBrakeAt.emplace_back(row);
	}

	void SetColBrake(int col) {
		if (m_colBrakeAt.size() != 0)
			m_colBrakeAt[std::distance(m_colBrakeAt.begin(),
				std::max_element(m_colBrakeAt.begin(), m_colBrakeAt.end()))] = wxMax(wxMin(m_colBrakeAt[m_colBrakeAt.size() - 1], GetNumberCols() - 1), col);
		else
			m_colBrakeAt.emplace_back(col);
	}

	bool IsRowBrake(int row) const {
		auto iterator =
			std::find(m_rowBrakeAt.begin(), m_rowBrakeAt.end(), row);
		return iterator != m_rowBrakeAt.end();
	}

	bool IsColBrake(int col) const {
		auto iterator =
			std::find(m_colBrakeAt.begin(), m_colBrakeAt.end(), col);
		return iterator != m_colBrakeAt.end();
	}

	int GetMaxRowBrake() const {
		if (m_rowBrakeAt.size() != 0)
			return m_rowBrakeAt[std::distance(m_rowBrakeAt.begin(),
				std::max_element(m_rowBrakeAt.begin(), m_rowBrakeAt.end()))];
		return 0;
	}

	int GetMaxColBrake() const {
		if (m_colBrakeAt.size() != 0)
			return m_colBrakeAt[std::distance(m_colBrakeAt.begin(),
				std::max_element(m_colBrakeAt.begin(), m_colBrakeAt.end()))];
		return 0;
	}

	// ------ freeze
	//
	void SetRowFreeze(int row) { m_freezeRow = row; }
	void SetColFreeze(int col) { m_freezeCol = col; }

	int GetRowFreeze() const { return m_freezeRow; }
	int GetColFreeze() const { return m_freezeCol; }

	// ------ label and gridline formatting
	//
	int GetRowLabelSize() const { return s_rowLabelWidth; }
	int GetColLabelSize() const { return s_colLabelHeight; }

	wxFont GetLabelFont() const { return m_labelFont; }

	wxString GetRowLabelValue(int row) const { return stringUtils::IntToStr(row + 1); }
	wxString GetColLabelValue(int col) const { return stringUtils::IntToStr(col + 1); }

	// ------ cell value accessors
	//
	bool IsEmptyCell(int row, int col) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr)
			return cell->IsEmptyValue();
		return true;
	}

	ibSpreadsheetFillType GetFillType(int row, int col) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr)
			return cell->m_fillSetType;
		return ibSpreadsheetFillType::ibSpreadsheetFillType_StrText;
	}

	void SetCellFillType(int row, int col, ibSpreadsheetFillType type) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr)
			cell->m_fillSetType = type;
	}

	void GetCellValue(int row, int col, wxString& s) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr)
			s = cell->m_value;
	}

	void SetCellValue(int row, int col, const wxString& s) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr)
			cell->m_value = s;
	}

	//special string return 
	wxString GetCellValue(int row, int col) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr)
			return cell->m_value;
		return wxT("");
	}

	void GetCellDetailsParameter(int row, int col, wxString& s) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr)
			s = cell->m_detailsParameter;
	}

	void SetCellDetailsParameter(int row, int col, const wxString& s) {
		ibSpreadsheetCellDescription* cell = GetOrCreateCell(row, col);
		if (cell != nullptr)
			cell->m_detailsParameter = s;
	}

	//special string return 
	wxString GetCellDetailsParameter(int row, int col) const {
		const ibSpreadsheetCellDescription* cell = GetCell(row, col);
		if (cell != nullptr)
			return cell->m_detailsParameter;
		return wxT("");
	}

	bool operator == (const ibSpreadsheetDescription& rhs) const {

		if (m_cellAt != rhs.m_cellAt)
			return false;

		if (m_rowAreaAt != rhs.m_rowAreaAt || m_colAreaAt != rhs.m_colAreaAt)
			return false;

		if (m_rowBrakeAt != rhs.m_rowBrakeAt || m_colBrakeAt != rhs.m_colBrakeAt)
			return false;

		return m_freezeRow == rhs.m_freezeRow &&
			m_freezeCol == rhs.m_freezeCol;
	}

private:

	// default font
	wxFont m_labelFont;

	//size row 
	std::vector <ibSpreadsheetRowSizeDescription> m_rowHeightAt;

	//size col 
	std::vector <ibSpreadsheetColSizeDescription> m_colWidthAt;

	//cell
	std::vector<ibSpreadsheetCellDescription> m_cellAt;

	// WHERE each cell is, keyed by its address. Kept in step with m_cellAt by the two
	// places that touch it — GetOrCreateCell (insert) and ClearSpreadsheet (drop);
	// nothing else adds or removes a cell, which is what makes one index enough.
	// Copies with the description, so a copied sheet is consistent by construction.
	std::unordered_map<uint64_t, size_t> m_cellIndex;

	// The extent, maintained on insert instead of re-derived by a full scan.
	unsigned int m_maxRow = 0;
	unsigned int m_maxCol = 0;

	// One cell address in one integer. Row and column are unsigned in the cell itself,
	// and negatives are refused before this is ever called.
	static uint64_t CellKey(int row, int col) {
		return (static_cast<uint64_t>(static_cast<unsigned int>(row)) << 32)
			| static_cast<unsigned int>(col);
	}

	//print brake 
	std::vector <unsigned int> m_rowBrakeAt;
	std::vector <unsigned int> m_colBrakeAt;

	//freeze 
	int m_freezeRow = 0, m_freezeCol = 0;

	//area 
	std::vector<ibSpreadsheetAreaDescription> m_rowAreaAt;
	std::vector<ibSpreadsheetAreaDescription> m_colAreaAt;

	std::vector<ibSpreadsheetGroupDescription> m_rowGroupAt;
	std::vector<ibSpreadsheetGroupDescription> m_colGroupAt;
};

class BACKEND_API ibSpreadsheetDescriptionMemory {
public:
	//load & save object in control
	static bool LoadData(class ibReaderMemory& reader, ibSpreadsheetDescription& spreadsheetDesc);
	static bool SaveData(class ibWriterMemory& writer, const ibSpreadsheetDescription& spreadsheetDesc);

	// node form — a genuine blob (whole sheet): a Binary value, the byte writer is
	// contained here, not in the property.
	static bool ReadNode(const class ibDataValue& value, ibSpreadsheetDescription& spreadsheetDesc);
	static bool WriteNode(class ibDataValue& value, const ibSpreadsheetDescription& spreadsheetDesc);
};

#endif  