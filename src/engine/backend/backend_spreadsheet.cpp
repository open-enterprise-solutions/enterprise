#include "backend_spreadsheet.h"
#include "backend/fileSystem/fs.h"
#include "backend/sheetFormat/sheetFormat.h"   // a table that came from somewhere else (Excel today)

#define spreadsheetNotify \
	for (auto notify : m_spreadsheetNotifiers) notify

#pragma region __notifier_h__

void ibBackendSpreadsheetObject::ClearSpreadsheet(int count)
{
	spreadsheetNotify->ClearSpreadsheet();
	m_spreadsheetDesc.ClearSpreadsheet(count);
}

void ibBackendSpreadsheetObject::EnableEditing(bool edit)
{
	spreadsheetNotify->EnableEditing(edit);
	m_editable = edit;
}

//area 
ibSpreadsheetDescription ibBackendSpreadsheetObject::GetArea(int rowLeft, int rowRight, int colTop, int colBottom)
{
	ibSpreadsheetDescription spreadsheetDesc;

	// ⭐ THE SAME RULE AS GetAreaByName BELOW, and for the same reason: the free side of a
	// half-specified range is bounded by the sheet's CONTENT, never by its page breaks. See the
	// long note there — this is the coordinate-taking twin of that function and carried the same
	// defect, so both are fixed together rather than one being left to be found again.
	const int lastRow = wxMax(0, m_spreadsheetDesc.GetNumberRows() - 1);
	const int lastCol = wxMax(0, m_spreadsheetDesc.GetNumberCols() - 1);

	if (rowLeft >= 0 && colTop >= 0 && rowRight > 0 && colBottom > 0) {
		for (int row = rowLeft; row < rowRight; row++) {
			for (int col = colTop; col < colBottom; col++) {
				ibSpreadsheetCellDescription* cell =
					spreadsheetDesc.GetOrCreateCell(row - rowLeft, col - colTop);
				cell->SetCell(m_spreadsheetDesc.GetCell(row, col));
			}
		}

		for (int row = rowLeft; row < rowRight; row++)
			spreadsheetDesc.SetRowSize(row - rowLeft, m_spreadsheetDesc.GetRowSize(row));

		for (int col = colTop; col < colBottom; col++)
			spreadsheetDesc.SetColSize(col - colTop, m_spreadsheetDesc.GetColSize(col));

		// ⚠ THE FRAGMENT'S OWN LAST INDEX — subtracted the right way round, and −1 because this
		// function's range is EXCLUSIVE (`row < rowRight`) while the marker is an index.
		// It used to read `rowLeft - rowRight`, which is NEGATIVE whenever the range is non-empty:
		// the fragment shipped with a stored break at, say, −3. Inert for pagination, since no loop
		// counter matches a negative, but it made GetMaxRowBrake() answer a negative number, kept
		// IsEmptySpreadsheet() from ever being true for such a fragment, and was serialised.
		// GetAreaByName states the same fact correctly (`m_end - m_start`), so the two halves of
		// one file disagreed.
		spreadsheetDesc.SetRowBrake(wxMax(0, rowRight - rowLeft - 1));
		spreadsheetDesc.SetColBrake(wxMax(0, colBottom - colTop - 1));
	}
	else if (rowLeft >= 0 && colTop < 0 && rowRight > 0 && colBottom < 0)
	{
		// Rows given, columns not: the whole width of what is written.
		for (int row = rowLeft; row < rowRight; row++) {
			for (int col = 0; col <= lastCol; col++) {
				// ⚠ NOT `col - colTop`. colTop is the ABSENCE marker (-1) on this branch, so
				// subtracting it shifted every cell one column to the right — a sentinel used as
				// an origin. The origin is 0 here, because the columns were not narrowed.
				ibSpreadsheetCellDescription* cell =
					spreadsheetDesc.GetOrCreateCell(row - rowLeft, col);
				cell->SetCell(m_spreadsheetDesc.GetCell(row, col));
			}
		}

		for (int row = rowLeft; row < rowRight; row++)
			spreadsheetDesc.SetRowSize(row - rowLeft, m_spreadsheetDesc.GetRowSize(row));

		for (int col = 0; col <= lastCol; col++)
			spreadsheetDesc.SetColSize(col, m_spreadsheetDesc.GetColSize(col));

		// Its own last row (exclusive range → −1); the column side is the sheet's, unnarrowed.
		spreadsheetDesc.SetRowBrake(wxMax(0, rowRight - rowLeft - 1));
		spreadsheetDesc.SetColBrake(lastCol);
	}
	else if (rowLeft < 0 && colTop >= 0 && rowRight < 0 && colBottom > 0) {
		// Columns given, rows not: the whole height of what is written.
		for (int row = 0; row <= lastRow; row++) {
			for (int col = colTop; col < colBottom; col++) {
				// ⚠ NOT `row - rowLeft` — rowLeft is the absence marker here, same trap, other axis.
				ibSpreadsheetCellDescription* cell =
					spreadsheetDesc.GetOrCreateCell(row, col - colTop);
				cell->SetCell(m_spreadsheetDesc.GetCell(row, col));
			}
		}

		for (int row = 0; row <= lastRow; row++)
			spreadsheetDesc.SetRowSize(row, m_spreadsheetDesc.GetRowSize(row));

		for (int col = colTop; col < colBottom; col++)
			spreadsheetDesc.SetColSize(col - colTop, m_spreadsheetDesc.GetColSize(col));

		spreadsheetDesc.SetRowBrake(lastRow);
		spreadsheetDesc.SetColBrake(wxMax(0, colBottom - colTop - 1));
	}

	return spreadsheetDesc;
}

ibSpreadsheetDescription ibBackendSpreadsheetObject::GetAreaByName(const wxString& strAreaLeftName, const wxString& strAreaTopName)
{
	const ibSpreadsheetAreaDescription* r = m_spreadsheetDesc.GetRowAreaByName(strAreaLeftName);
	const ibSpreadsheetAreaDescription* c = m_spreadsheetDesc.GetColAreaByName(strAreaTopName);

	ibSpreadsheetDescription spreadsheetDesc;

	// ⭐ HOW WIDE (AND HOW TALL) THE SHEET IS — asked of its CONTENT, not of its page breaks.
	//
	// The two asymmetric branches below used to bound the free side by GetMaxColBrake() /
	// GetMaxRowBrake(), which is the position of the last PAGE BREAK and returns 0 when the sheet
	// declares none. A break says where the PAPER ends; it knows nothing about how many columns
	// were written. So a template built without one yielded areas exactly ONE column wide, and
	// since every cell of a printed form lives to the right of column 0, every area came out
	// structurally right and completely empty — the correct number of rows, no content.
	//
	// The giveaway was that the receiving side already measures the other way: PutArea walks
	// doc->GetNumberCols(), the real extent. One width, two roads, disagreeing at the ends of a
	// single operation. (2026-08-31, an empty print form.)
	const int lastRow = wxMax(0, m_spreadsheetDesc.GetNumberRows() - 1);
	const int lastCol = wxMax(0, m_spreadsheetDesc.GetNumberCols() - 1);

	if (r != nullptr && c != nullptr) {
		for (int row = r->m_start; row <= (int)r->m_end; row++) {
			for (int col = c->m_start; col <= (int)c->m_end; col++) {
				ibSpreadsheetCellDescription* cell =
					spreadsheetDesc.GetOrCreateCell(row - r->m_start, col - c->m_start);
				cell->SetCell(m_spreadsheetDesc.GetCell(row, col));
			}
		}

		for (int row = r->m_start; row <= (int)r->m_end; row++)
			spreadsheetDesc.SetRowSize(row - r->m_start, m_spreadsheetDesc.GetRowSize(row));

		for (int col = c->m_start; col <= (int)c->m_end; col++)
			spreadsheetDesc.SetColSize(col - c->m_start, m_spreadsheetDesc.GetColSize(col));

		spreadsheetDesc.SetRowBrake(r->m_end - r->m_start);
		spreadsheetDesc.SetColBrake(c->m_end - c->m_start);
	}
	else if (r != nullptr) {
		// A ROW AREA IS AS WIDE AS THE SHEET. Nothing bounds it on the free side but the content.
		for (int row = r->m_start; row <= (int)r->m_end; row++) {
			for (int col = 0; col <= lastCol; col++) {
				ibSpreadsheetCellDescription* cell =
					spreadsheetDesc.GetOrCreateCell(row - r->m_start, col);
				cell->SetCell(m_spreadsheetDesc.GetCell(row, col));
			}
		}

		for (int row = r->m_start; row <= (int)r->m_end; row++)
			spreadsheetDesc.SetRowSize(row - r->m_start, m_spreadsheetDesc.GetRowSize(row));

		for (int col = 0; col <= lastCol; col++)
			spreadsheetDesc.SetColSize(col, m_spreadsheetDesc.GetColSize(col));

		// The fragment's own edges, the way the symmetric branch above states them — its last row
		// and its last column. Passing the SOURCE's break through put the mark at column 0 on a
		// sheet that declared none, which is a page break drawn before the first column.
		spreadsheetDesc.SetRowBrake(r->m_end - r->m_start);
		spreadsheetDesc.SetColBrake(lastCol);
	}
	else if (c != nullptr) {
		// …and a COLUMN area is as tall as the sheet. Same rule, other axis.
		for (int row = 0; row <= lastRow; row++) {
			for (int col = c->m_start; col <= (int)c->m_end; col++) {
				ibSpreadsheetCellDescription* cell =
					spreadsheetDesc.GetOrCreateCell(row, col - c->m_start);
				cell->SetCell(m_spreadsheetDesc.GetCell(row, col));
			}
		}

		for (int row = 0; row <= lastRow; row++)
			spreadsheetDesc.SetRowSize(row, m_spreadsheetDesc.GetRowSize(row));

		for (int col = c->m_start; col <= (int)c->m_end; col++)
			spreadsheetDesc.SetColSize(col - c->m_start, m_spreadsheetDesc.GetColSize(col));

		spreadsheetDesc.SetRowBrake(lastRow);
		spreadsheetDesc.SetColBrake(c->m_end - c->m_start);
	}

	return spreadsheetDesc;
}

void ibBackendSpreadsheetObject::PutArea(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, unsigned int groupLevel)
{
	const int maxRowBrake = GetNumberRows();
	const int maxColBrake = GetNumberCols();

	spreadsheetNotify->PutArea(doc, groupLevel);

	for (int row = 0; row < doc->GetNumberRows(); row++) {
		for (int col = 0; col < doc->GetNumberCols(); col++) {

			ibSpreadsheetCellDescription* cell =
				m_spreadsheetDesc.GetOrCreateCell(maxRowBrake + row, col);

			cell->SetCell(doc->GetSpreadsheetDesc().GetCell(row, col));

			if (cell->m_fillSetType == ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate || cell->m_fillSetType == ibSpreadsheetFillType::ibSpreadsheetFillType_StrParameter) {
				cell->m_value = doc->ComputeStringValueFromParameters(cell->m_value, cell->m_fillSetType);
				cell->m_fillSetType = ibSpreadsheetFillType::ibSpreadsheetFillType_StrText;
			}

			const wxString& detailsParameter =
				cell->m_detailsParameter;

			if (!detailsParameter.IsEmpty()) {
	
				wxString detailsComputeParameter;	
				detailsComputeParameter << detailsParameter << maxRowBrake + row << col;
				
				SetParameter(detailsComputeParameter, doc->GetParameter(detailsParameter));	
				cell->m_detailsParameter = detailsComputeParameter;
			}
		}
	}

	for (int row = 0; row < doc->GetNumberRows(); row++)
		SetRowSize(maxRowBrake + row, doc->GetRowSize(row));

	for (int col = 0; col < doc->GetNumberCols(); col++)
		SetColSize(col, doc->GetColSize(col));

	SetRowBrake(maxRowBrake + doc->GetNumberRows() - 1);

	if (maxColBrake < doc->GetNumberCols())
		SetColBrake(maxColBrake + doc->GetNumberCols() - 1);

	if (groupLevel > 0 && doc->GetNumberRows() > 0) {
		m_spreadsheetDesc.AddRowGroup(
			maxRowBrake, maxRowBrake + doc->GetNumberRows() - 1, groupLevel);
	}
}

void ibBackendSpreadsheetObject::JoinArea(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, unsigned int groupLevel)
{
	const int maxRowBrake = GetNumberRows();
	const int maxColBrake = GetNumberCols();

	spreadsheetNotify->JoinArea(doc, groupLevel);

	for (int col = 0; col < doc->GetNumberCols(); col++) {
		for (int row = 0; row < doc->GetNumberRows(); row++) {

			ibSpreadsheetCellDescription* cell =
				m_spreadsheetDesc.GetOrCreateCell(row, maxColBrake + col);

			cell->SetCell(doc->GetSpreadsheetDesc().GetCell(row, col));

			if (cell->m_fillSetType == ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate || cell->m_fillSetType == ibSpreadsheetFillType::ibSpreadsheetFillType_StrParameter) {
				cell->m_value = doc->ComputeStringValueFromParameters(cell->m_value, cell->m_fillSetType);
				cell->m_fillSetType = ibSpreadsheetFillType::ibSpreadsheetFillType_StrText;
			}

			const wxString& detailsParameter =
				cell->m_detailsParameter;

			if (!detailsParameter.IsEmpty()) {

				wxString detailsComputeParameter;
				detailsComputeParameter << detailsParameter << row << maxColBrake + col;

				SetParameter(detailsComputeParameter, doc->GetParameter(detailsParameter));
				cell->m_detailsParameter = detailsComputeParameter;
			}
		}
	}

	for (int row = 0; row < doc->GetNumberRows(); row++)
		SetRowSize(row, doc->GetRowSize(row));

	for (int col = 0; col < doc->GetNumberCols(); col++)
		SetColSize(maxColBrake + col, doc->GetColSize(col));

	if (maxRowBrake < doc->GetNumberRows())
		SetRowBrake(maxRowBrake + doc->GetNumberRows() - 1);

	SetColBrake(maxColBrake + doc->GetNumberCols() - 1);

	if (groupLevel > 0 && doc->GetNumberCols() > 0) {
		m_spreadsheetDesc.AddColGroup(
			maxColBrake, maxColBrake + doc->GetNumberCols() - 1, groupLevel);
	}
}

// ----- block-style grouping ---------------------------------------------

void ibBackendSpreadsheetObject::BeginRowGroup()
{
	m_rowGroupStack.push_back(GetNumberRows());
}

void ibBackendSpreadsheetObject::EndRowGroup()
{
	if (m_rowGroupStack.empty()) return;
	const int start = m_rowGroupStack.back();
	m_rowGroupStack.pop_back();
	const int end = GetNumberRows() - 1;
	if (end < start) return;
	// Depth after the pop gives the outer nesting count, so the group we're
	// closing is one level deeper — matches the "outer level 1, inner level 2"
	// convention the user expects.
	const unsigned int level = (unsigned int)m_rowGroupStack.size() + 1;
	m_spreadsheetDesc.AddRowGroup((unsigned)start, (unsigned)end, level);
	spreadsheetNotify->RowAreaAdded((unsigned)start, (unsigned)end, level);
}

void ibBackendSpreadsheetObject::BeginColGroup()
{
	m_colGroupStack.push_back(GetNumberCols());
}

void ibBackendSpreadsheetObject::EndColGroup()
{
	if (m_colGroupStack.empty()) return;
	const int start = m_colGroupStack.back();
	m_colGroupStack.pop_back();
	const int end = GetNumberCols() - 1;
	if (end < start) return;
	const unsigned int level = (unsigned int)m_colGroupStack.size() + 1;
	m_spreadsheetDesc.AddColGroup((unsigned)start, (unsigned)end, level);
	spreadsheetNotify->ColAreaAdded((unsigned)start, (unsigned)end, level);
}

//size 
void ibBackendSpreadsheetObject::SetRowSize(int row, int height)
{
	spreadsheetNotify->SetRowSize(row, height);
	m_spreadsheetDesc.SetRowSize(row, height);
}

void ibBackendSpreadsheetObject::SetColSize(int col, int width)
{
	spreadsheetNotify->SetColSize(col, width);
	m_spreadsheetDesc.SetColSize(col, width);
}

//freeze 
void ibBackendSpreadsheetObject::SetRowFreeze(int row)
{
	spreadsheetNotify->SetRowFreeze(row);
	m_spreadsheetDesc.SetRowFreeze(row);
}

void ibBackendSpreadsheetObject::SetColFreeze(int col)
{
	spreadsheetNotify->SetColFreeze(col);
	m_spreadsheetDesc.SetColFreeze(col);
}

// ------ row and col formatting
//

void ibBackendSpreadsheetObject::SetCellBackgroundColour(int row, int col, const wxColour& colour)
{
	spreadsheetNotify->SetCellBackgroundColour(row, col, colour);
	m_spreadsheetDesc.SetCellBackgroundColour(row, col, colour);
}

void ibBackendSpreadsheetObject::SetCellTextColour(int row, int col, const wxColour& colour)
{
	spreadsheetNotify->SetCellTextColour(row, col, colour);
	m_spreadsheetDesc.SetCellTextColour(row, col, colour);
}

void ibBackendSpreadsheetObject::SetCellTextOrient(int row, int col, const int orient)
{
	spreadsheetNotify->SetCellTextOrient(row, col, orient);
	m_spreadsheetDesc.SetCellTextOrient(row, col, orient);
}

void ibBackendSpreadsheetObject::SetCellFont(int row, int col, const wxFont& font)
{
	spreadsheetNotify->SetCellFont(row, col, font);
	m_spreadsheetDesc.SetCellFont(row, col, font);
}

void ibBackendSpreadsheetObject::SetCellAlignment(int row, int col, const int horiz, const int vert)
{
	spreadsheetNotify->SetCellAlignment(row, col, horiz, vert);
	m_spreadsheetDesc.SetCellAlignment(row, col, horiz, vert);
}

void ibBackendSpreadsheetObject::SetCellBorderLeft(int row, int col, const ibSpreadsheetBorderDescription& desc)
{
	spreadsheetNotify->SetCellBorderLeft(row, col, desc);
	m_spreadsheetDesc.SetCellBorderLeft(row, col, desc);
}

void ibBackendSpreadsheetObject::SetCellBorderRight(int row, int col, const ibSpreadsheetBorderDescription& desc)
{
	spreadsheetNotify->SetCellBorderRight(row, col, desc);
	m_spreadsheetDesc.SetCellBorderRight(row, col, desc);
}

void ibBackendSpreadsheetObject::SetCellBorderTop(int row, int col, const ibSpreadsheetBorderDescription& desc)
{
	spreadsheetNotify->SetCellBorderTop(row, col, desc);
	m_spreadsheetDesc.SetCellBorderTop(row, col, desc);
}

void ibBackendSpreadsheetObject::SetCellBorderBottom(int row, int col, const ibSpreadsheetBorderDescription& desc)
{
	spreadsheetNotify->SetCellBorderBottom(row, col, desc);
	m_spreadsheetDesc.SetCellBorderBottom(row, col, desc);
}

void ibBackendSpreadsheetObject::SetCellSize(int row, int col, int num_rows, int num_cols)
{
	spreadsheetNotify->SetCellSize(row, col, num_rows, num_cols);
	m_spreadsheetDesc.SetCellSize(row, col, num_rows, num_cols);
}

void ibBackendSpreadsheetObject::SetCellFitMode(int row, int col, ibSpreadsheetCellDescription::ibFitMode fitMode)
{
	spreadsheetNotify->SetCellFitMode(row, col, fitMode);
	m_spreadsheetDesc.SetCellFitMode(row, col, fitMode);
}

void ibBackendSpreadsheetObject::SetCellReadOnly(int row, int col, bool isReadOnly)
{
	spreadsheetNotify->SetCellReadOnly(row, col, isReadOnly);
	m_spreadsheetDesc.SetCellReadOnly(row, col, isReadOnly);
}

// ------ cell brake accessors
//
//support printing 
void ibBackendSpreadsheetObject::AddRowBrake(int row)
{
	spreadsheetNotify->AddRowBrake(row);
	m_spreadsheetDesc.AddRowBrake(row);
}

void ibBackendSpreadsheetObject::AddColBrake(int col)
{
	spreadsheetNotify->AddColBrake(col);
	m_spreadsheetDesc.AddColBrake(col);
}

void ibBackendSpreadsheetObject::DeleteRowBrake(int row)
{
	spreadsheetNotify->DeleteRowBrake(row);
	m_spreadsheetDesc.DeleteRowBrake(row);
}

void ibBackendSpreadsheetObject::DeleteColBrake(int col)
{
	spreadsheetNotify->DeleteColBrake(col);
	m_spreadsheetDesc.DeleteColBrake(col);
}

void ibBackendSpreadsheetObject::SetRowBrake(int row)
{
	spreadsheetNotify->SetRowBrake(row);
	m_spreadsheetDesc.SetRowBrake(row);
}

void ibBackendSpreadsheetObject::SetColBrake(int col)
{
	spreadsheetNotify->SetColBrake(col);
	m_spreadsheetDesc.SetColBrake(col);
}

// ------ cell value accessors
//

void ibBackendSpreadsheetObject::SetCellFillType(int row, int col, ibSpreadsheetFillType type)
{
	m_spreadsheetDesc.SetCellFillType(row, col, type);
}

void ibBackendSpreadsheetObject::SetCellValue(int row, int col, const wxString& s)
{
	spreadsheetNotify->SetCellValue(row, col, s);
	m_spreadsheetDesc.SetCellValue(row, col, s);
}

bool ibBackendSpreadsheetObject::GetParameter(const wxString& strParameter, ibValue& valueParam) const
{
	auto iterator = std::find_if(m_paramVector.begin(), m_paramVector.end(),
		[strParameter](const auto& pair) { return stringUtils::CompareString(strParameter, pair.first); });

	if (iterator == m_paramVector.end())
		return false;

	valueParam = iterator->second;
	return true;
}

void ibBackendSpreadsheetObject::SetParameter(const wxString& strParameter, const ibValue& valueParam)
{
	m_paramVector.insert_or_assign(strParameter, valueParam);
}

#include "backend_localization.h"

wxString ibBackendSpreadsheetObject::ComputeStringValueFromParameters(const wxString& strValue, ibSpreadsheetFillType type) const
{
	if (type == ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate) {

		if (!strValue.IsEmpty()) {

			wxString strTemplateValue;
			ibBackendLocalization::GetTranslateGetRawLocText(m_docLangCode, strValue, strTemplateValue);

			size_t start_pos = 0, end_pos = 0;

			// Find the first opening or closing bracket
			start_pos = strTemplateValue.find_first_of(wxT("[]"), start_pos);

			while (start_pos != wxString::npos) {

				// Find the next bracket of any type
				end_pos = strTemplateValue.find_first_of(wxT("[]"), start_pos + 1);

				if (end_pos != wxString::npos) {
					// Extract the substring between the brackets
					// +1 to start after the opening bracket
					const wxString& token =
						strTemplateValue.substr(start_pos + 1, end_pos - start_pos - 1);
					if (!token.empty()) {

						// NOT static — this is scratch for one call. Shared, an ibValue
						// holding a TYPE_REFFER makes concurrent renders race on its
						// refcount, and even one thread clobbers it if GetParameter
						// re-enters. Constructing one is cheap; the static was not a win.
						ibValue cVal;
						if (GetParameter(token, cVal))
							strTemplateValue.replace(start_pos, end_pos - start_pos + 1, cVal.GetString());
						else
							strTemplateValue.replace(start_pos, end_pos - start_pos + 1, wxT(""));
					}
					else {
						strTemplateValue.replace(start_pos, end_pos - start_pos, wxT(""));
					}

					// Move start_pos to the character after the closing bracket for the next iteration
					start_pos = end_pos + 1;
				}
				else {
					// No matching end bracket found, stop
					break;
				}

				// Find the next opening bracket for the next iteration
				start_pos = strTemplateValue.find_first_of(wxT("[]"), start_pos);
			}

			return ibBackendLocalization::CreateLocalizationRawLocText(strTemplateValue);
		}
	}
	else if (type == ibSpreadsheetFillType::ibSpreadsheetFillType_StrParameter) {

		ibValue cVal;//scratch for one call — see the sibling above
		if (!strValue.IsEmpty() && GetParameter(strValue, cVal))
			return ibBackendLocalization::CreateLocalizationRawLocText(cVal.GetString());

		return wxT("");
	}

	return strValue;
}

#pragma endregion 

void ibBackendSpreadsheetObject::SetCellDetailsParameter(int row, int col, const wxString& s)
{
	//spreadsheetNotify->SetCellValue(row, col, s);
	m_spreadsheetDesc.SetCellDetailsParameter(row, col, s);
}

// (⚠ NO `OpenCellDetailsParameter` HERE ANY MORE. Opening a value is the RUNTIME's — a caller asks
//  the cell what it is bound to and shows that value, which is the two lines this verb wrapped. A
//  sheet that also knew how to open things was a door in front of a door, and the door belonged to
//  the value: `ibValue::ShowValue` resolves through references and wrappers on its own.)

#pragma region __fs_h__

#include <fstream>

// ⭐⭐ WHICH FORMAT READS THIS NAME — one question, and OUR OWN LAYOUT IS ONE OF THE
// ANSWERS (backend/sheetFormat/). There is no "ours or theirs" branch here, and that
// is the point: `.oxl` and an Excel workbook are two entries in one registry, so a
// third format changes neither this function nor the file dialog that offers them.
//
// ⚠ THE READER FILLS A COPY and this document is replaced only once it succeeded: a
// caller told `false` must be free to keep the document it had.
bool ibBackendSpreadsheetObject::LoadFromFile(const wxString& strFileName)
{
	const ibSheetFormat* format = ibSheetFormatFor(strFileName);
	if (format == nullptr)
		return false;   // a name nothing here reads — said plainly, not guessed at

	ibSpreadsheetDescription read;
	if (!format->Read(strFileName, read))
		return false;

	m_spreadsheetDesc = read;
	return true;
}

bool ibBackendSpreadsheetObject::SaveToFile(const wxString& strFileName)
{
	// …and the same question on the way out: the name a person chose in the Save
	// dialog is what says which format they meant.
	const ibSheetFormat* format = ibSheetFormatFor(strFileName);
	if (format == nullptr)
		return false;

	return format->Write(strFileName, m_spreadsheetDesc);
}

#pragma endregion 