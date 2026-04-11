#include "backend_spreadsheet.h"
#include "backend/fileSystem/fs.h"

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

		spreadsheetDesc.SetRowBrake(rowLeft - rowRight);
		spreadsheetDesc.SetColBrake(colTop - colBottom);
	}
	else if (rowLeft >= 0 && colTop < 0 && rowRight > 0 && colBottom < 0)
	{
		for (int row = rowLeft; row < rowRight; row++) {
			for (int col = 0; col < GetMaxColBrake(); col++) {
				ibSpreadsheetCellDescription* cell =
					spreadsheetDesc.GetOrCreateCell(row - rowLeft, col - colTop);
				cell->SetCell(m_spreadsheetDesc.GetCell(row, col));
			}
		}

		for (int row = rowLeft; row < rowRight; row++)
			spreadsheetDesc.SetRowSize(row - rowLeft, m_spreadsheetDesc.GetRowSize(row));

		for (int col = 0; col < GetMaxColBrake(); col++)
			spreadsheetDesc.SetColSize(col, m_spreadsheetDesc.GetColSize(col));

		spreadsheetDesc.SetRowBrake(rowLeft - rowRight);
		spreadsheetDesc.SetColBrake(GetMaxColBrake());
	}
	else if (rowLeft < 0 && colTop >= 0 && rowRight < 0 && colBottom > 0) {
		for (int row = 0; row < GetMaxRowBrake(); row++) {
			for (int col = colTop; col < colBottom; col++) {
				ibSpreadsheetCellDescription* cell =
					spreadsheetDesc.GetOrCreateCell(row - rowLeft, col - colTop);
				cell->SetCell(m_spreadsheetDesc.GetCell(row, col));
			}
		}

		for (int row = 0; row < GetMaxRowBrake(); row++)
			spreadsheetDesc.SetRowSize(row, m_spreadsheetDesc.GetRowSize(row));

		for (int col = colTop; col < colBottom; col++)
			spreadsheetDesc.SetColSize(col - colTop, m_spreadsheetDesc.GetColSize(col));

		spreadsheetDesc.SetRowBrake(GetMaxRowBrake());
		spreadsheetDesc.SetColBrake(colTop - colBottom);
	}

	return spreadsheetDesc;
}

ibSpreadsheetDescription ibBackendSpreadsheetObject::GetAreaByName(const wxString& strAreaLeftName, const wxString& strAreaTopName)
{
	const ibSpreadsheetAreaDescription* r = m_spreadsheetDesc.GetRowAreaByName(strAreaLeftName);
	const ibSpreadsheetAreaDescription* c = m_spreadsheetDesc.GetColAreaByName(strAreaTopName);

	ibSpreadsheetDescription spreadsheetDesc;

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
		for (int row = r->m_start; row <= (int)r->m_end; row++) {
			for (int col = 0; col <= GetMaxColBrake(); col++) {
				ibSpreadsheetCellDescription* cell =
					spreadsheetDesc.GetOrCreateCell(row - r->m_start, col);
				cell->SetCell(m_spreadsheetDesc.GetCell(row, col));
			}
		}

		for (int row = r->m_start; row <= (int)r->m_end; row++)
			spreadsheetDesc.SetRowSize(row - r->m_start, m_spreadsheetDesc.GetRowSize(row));

		for (int col = 0; col <= GetMaxColBrake(); col++)
			spreadsheetDesc.SetColSize(col, m_spreadsheetDesc.GetColSize(col));

		spreadsheetDesc.SetRowBrake(r->m_end - r->m_start);
		spreadsheetDesc.SetColBrake(GetMaxColBrake());
	}
	else if (c != nullptr) {
		for (int row = 0; row <= GetMaxRowBrake(); row++) {
			for (int col = c->m_start; col <= (int)c->m_end; col++) {
				ibSpreadsheetCellDescription* cell =
					spreadsheetDesc.GetOrCreateCell(row, col - c->m_start);
				cell->SetCell(m_spreadsheetDesc.GetCell(row, col));
			}
		}

		for (int row = 0; row <= GetMaxRowBrake(); row++)
			spreadsheetDesc.SetRowSize(row, m_spreadsheetDesc.GetRowSize(row));

		for (int col = c->m_start; col <= (int)c->m_end; col++)
			spreadsheetDesc.SetColSize(col - c->m_start, m_spreadsheetDesc.GetColSize(col));

		spreadsheetDesc.SetRowBrake(GetMaxRowBrake());
		spreadsheetDesc.SetColBrake(c->m_end - c->m_start);
	}

	return spreadsheetDesc;
}

void ibBackendSpreadsheetObject::PutArea(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc)
{
	const int maxRowBrake = GetNumberRows();
	const int maxColBrake = GetNumberCols();

	spreadsheetNotify->PutArea(doc);

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
}

void ibBackendSpreadsheetObject::JoinArea(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc)
{
	const int maxRowBrake = GetNumberRows();
	const int maxColBrake = GetNumberCols();

	spreadsheetNotify->JoinArea(doc);

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

						static ibValue cVal;
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

		static ibValue cVal;
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

bool ibBackendSpreadsheetObject::OpenCellDetailsParameter(int row, int col) const
{
	const ibSpreadsheetCellDescription* cellDesc = m_spreadsheetDesc.GetCell(row, col);
	if (cellDesc == nullptr)
		return false;

	const wxString& detailsParameter = cellDesc->m_detailsParameter;

	ibValue valueParam;
	if (!detailsParameter.IsEmpty() && GetParameter(detailsParameter, valueParam)) {
		valueParam.ShowValue();
		return true;
	}

	return false;
}

#pragma region __fs_h__

#include <fstream>

bool ibBackendSpreadsheetObject::LoadFromFile(const wxString& strFileName)
{
	std::ifstream in(strFileName.ToStdString(), std::ios::in | std::ios::binary);

	if (!in.is_open())
		return false;

	//go to end
	in.seekg(0, in.end);
	//get size of file
	std::streamsize fsize = in.tellg();
	//go to beginning
	in.seekg(0, in.beg);

	wxMemoryBuffer tempBuffer(fsize);
	in.read((char*)tempBuffer.GetWriteBuf(fsize), fsize);

	ibReaderMemory readerData(tempBuffer.GetData(), tempBuffer.GetBufSize());

	if (readerData.eof())
		return false;

	in.close();

	return ibSpreadsheetDescriptionMemory::LoadData(readerData, m_spreadsheetDesc);
}

bool ibBackendSpreadsheetObject::SaveToFile(const wxString& strFileName)
{
	//common data
	ibWriterMemory writerData;

	if (!ibSpreadsheetDescriptionMemory::SaveData(writerData, m_spreadsheetDesc))
		return false;

	std::ofstream datafile;
	datafile.open(strFileName.ToStdWstring(), std::ios::binary);
	datafile.write(reinterpret_cast <char*> (writerData.pointer()), writerData.size());
	datafile.close();

	return true;
}

#pragma endregion 