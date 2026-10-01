#include "gridEditor.h"

class ibGridCommandClipboard : public ibGridCommandComposite
{
public:

	ibGridCommandClipboard(ibGrid* view,
		const wxVector<ibSpreadsheetCellDescription>& cells) : ibGridCommandComposite()
	{
		for (const auto& cell : cells) AppendCellCommand(view, cell.m_row, cell.m_col, cell);
	}

private:

	ibGridCellBorder GetCellGridBorder(const ibSpreadsheetBorderDescription& rhs) const {
		ibGridCellBorder border;
		border.m_colour = rhs.m_colour;
		border.m_style = rhs.m_style;
		border.m_width = rhs.m_width;
		return border;
	}

	void AppendCellCommand(ibGrid* view, int row, int col, const ibSpreadsheetCellDescription& cell)
	{
		AppendCommand<ibGridCommandAttrBackgroundColour>(row, col, cell.m_backgroundColour,
			ibGridCommandAttrBackgroundColour::GetAttrValue(view->GetOrCreateCellAttrPtr(row, col)));

		AppendCommand<ibGridCommandAttrTextColour>(row, col, cell.m_textColour,
			ibGridCommandAttrTextColour::GetAttrValue(view->GetOrCreateCellAttrPtr(row, col)));

		AppendCommand<ibGridCommandAttrTextOrient>(row, col, cell.m_textOrient,
			ibGridCommandAttrTextOrient::GetAttrValue(view->GetOrCreateCellAttrPtr(row, col)));

		AppendCommand<ibGridCommandAttrFont>(row, col, cell.m_font,
			ibGridCommandAttrFont::GetAttrValue(view->GetOrCreateCellAttrPtr(row, col)));

		AppendCommand<ibGridCommandAttrAlignment>(row, col, wxSize(cell.m_alignHorz, cell.m_alignVert),
			ibGridCommandAttrAlignment::GetAttrValue(view->GetOrCreateCellAttrPtr(row, col)));

		AppendCommand<ibGridCommandAttrBorderLeft>(row, col, GetCellGridBorder(cell.m_borderAt[0]),
			ibGridCommandAttrBorderLeft::GetAttrValue(view->GetOrCreateCellAttrPtr(row, col)));

		AppendCommand<ibGridCommandAttrBorderRight>(row, col, GetCellGridBorder(cell.m_borderAt[1]),
			ibGridCommandAttrBorderRight::GetAttrValue(view->GetOrCreateCellAttrPtr(row, col)));

		AppendCommand<ibGridCommandAttrBorderTop>(row, col, GetCellGridBorder(cell.m_borderAt[2]),
			ibGridCommandAttrBorderTop::GetAttrValue(view->GetOrCreateCellAttrPtr(row, col)));

		AppendCommand<ibGridCommandAttrBorderBottom>(row, col, GetCellGridBorder(cell.m_borderAt[3]),
			ibGridCommandAttrBorderBottom::GetAttrValue(view->GetOrCreateCellAttrPtr(row, col)));

		AppendCommand<ibGridCommandAttrFitMode>(row, col, ibToGridFitMode(cell.m_fitMode),
			ibGridCommandAttrFitMode::GetAttrValue(view->GetOrCreateCellAttrPtr(row, col)));

		AppendCommand<ibGridCommandAttrSize>(row, col, wxSize(cell.m_row_size, cell.m_col_size),
			ibGridCommandAttrSize::GetAttrValue(view->GetOrCreateCellAttrPtr(row, col)));

		AppendCommand<ibGridCommandAttrReadOnly>(row, col, cell.m_isReadOnly,
			ibGridCommandAttrReadOnly::GetAttrValue(view->GetOrCreateCellAttrPtr(row, col)));

		AppendCommand<ibGridCommandCellValue>(row, col, cell.m_value,
			view->GetCellValue(row, col));
	}

	wxVector<ibSpreadsheetCellDescription> m_cells;
};

#include <wx/clipbrd.h>

void ibGridEditor::Copy()
{
	if (wxTheClipboard->Open()) {

		bool hasBlocks = false;

		ibWriterMemory dataWritter;
		ibWriterMemory cellWriter;

		for (const auto& coords : ibGrid::GetSelectedBlocks())
		{
			for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
			{
				for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
				{
					ibWriterMemory bodyWriter;

					const ibSpreadsheetCellDescription* cell =
						m_spreadsheetObject->GetSpreadsheetDesc().GetCell(row, col);

					bodyWriter.w_s32(row - coords.GetTopRow());
					bodyWriter.w_s32(col - coords.GetLeftCol());

					bodyWriter.w_u8(cell != nullptr);

					if (cell != nullptr && !ibSpreadsheetCellDescriptionMemory::SaveData(bodyWriter, *cell))
						continue;

					wxString s;

					s << row - coords.GetTopRow()
						<< col - coords.GetLeftCol();

					cellWriter.w_chunk(stringUtils::StrToUInt(s), bodyWriter.buffer());
				}
			}

			hasBlocks = true;
		}

		if (!hasBlocks)
		{
			int start_row = m_numRows,
				start_col = m_numCols;

			if (start_row > ibGrid::GetGridCursorRow() && ibGrid::GetGridCursorRow() >= 0)
				start_row = ibGrid::GetGridCursorRow(); else start_row = 0;

			if (start_col > ibGrid::GetGridCursorCol() && ibGrid::GetGridCursorCol() >= 0)
				start_col = ibGrid::GetGridCursorCol(); else start_col = 0;

			ibWriterMemory bodyWriter;

			const ibSpreadsheetCellDescription* cell =
				m_spreadsheetObject->GetSpreadsheetDesc().GetCell(start_row, start_col);

			bodyWriter.w_s32(0);
			bodyWriter.w_s32(0);

			bodyWriter.w_u8(cell != nullptr);

			wxString s;
			s << start_row << start_col;

			if (cell != nullptr) ibSpreadsheetCellDescriptionMemory::SaveData(bodyWriter, *cell);

			cellWriter.w_chunk(stringUtils::StrToUInt(s), bodyWriter.buffer());
		}

		dataWritter.w_chunk(1, cellWriter.buffer());

		wxDataObjectComposite* composite = new wxDataObjectComposite();
		wxCustomDataObject* custom_object = new wxCustomDataObject(oes_clipboard_template);

		custom_object->SetData(dataWritter.size(), dataWritter.pointer()); // the +1 is used to force copy of the \0 character		

		wxString copy_data;
		bool something_in_this_line;

		for (int i = 0; i < m_numRows; i++) {
			something_in_this_line = false;
			for (int j = 0; j < m_numCols; j++) {
				if (IsInSelection(i, j)) {
					if (something_in_this_line == false) {
						if (copy_data.IsEmpty() == false) {
							copy_data.Append(wxT("\n"));
						}
						something_in_this_line = true;
					}
					else {
						copy_data.Append(wxT("\t"));
					}
					copy_data = copy_data + GetCellValue(i, j);
				}
			}
		}

		composite->Add(custom_object);
		composite->Add(new wxTextDataObject(copy_data), true);

		wxTheClipboard->SetData(composite);
		wxTheClipboard->Close();
	}
}

void ibGridEditor::Paste()
{
	if (wxTheClipboard->Open() &&
		wxTheClipboard->IsSupported(oes_clipboard_template))
	{
		wxCustomDataObject data(oes_clipboard_template);

		if (wxTheClipboard->GetData(data)) {

			int start_row = m_numRows,
				start_col = m_numCols;

			bool hasBlocks = false;

			for (const auto& coords : ibGrid::GetSelectedBlocks()) {

				if (start_row > coords.GetTopRow())
					start_row = coords.GetTopRow();

				if (start_col > coords.GetLeftCol())
					start_col = coords.GetLeftCol();

				hasBlocks = true;
			}

			if (!hasBlocks) {

				if (start_row > ibGrid::GetGridCursorRow() && ibGrid::GetGridCursorRow() >= 0)
					start_row = ibGrid::GetGridCursorRow(); else start_row = 0;

				if (start_col > ibGrid::GetGridCursorCol() && ibGrid::GetGridCursorCol() >= 0)
					start_col = ibGrid::GetGridCursorCol(); else start_col = 0;
			}

			ibReaderMemory* prevReaderCellMemory = nullptr;
			ibReaderMemory reader(data.GetData(), data.GetDataSize());

			wxVector<ibSpreadsheetCellDescription> cells;

			wxMemoryBuffer buf;
			if (reader.r_chunk(1, buf)) {

				int i = GetGridCursorRow();

				ibReaderMemory readerData(buf);

				while (!readerData.eof()) {

					u64 id = 0;

					ibReaderMemory* readerCellMemory = readerData.open_chunk_iterator(id, prevReaderCellMemory);
					if (!readerCellMemory)
						break;

					const int row = start_row + readerCellMemory->r_s32();
					const int col = start_col + readerCellMemory->r_s32();

					if (readerCellMemory->r_u8()) {
						ibSpreadsheetCellDescription cell(row, col);
						if (ibSpreadsheetCellDescriptionMemory::LoadData(*readerCellMemory, cell))
							cells.push_back(cell);
					}

					if (m_numRows <= row) ibGrid::AppendRows(row - m_numRows + 1);
					if (m_numCols <= col) ibGrid::AppendCols(col - m_numCols + 1);

					prevReaderCellMemory = readerCellMemory;
				}

				PushCommand<ibGridCommandClipboard>(this, cells);

				for (const auto& cell : cells) {

					SetCellAlignment(cell.m_row, cell.m_col, cell.m_alignHorz, cell.m_alignVert, false);

					if (cell.m_row_size >= 0 && cell.m_col_size >= 0)
						SetCellSize(cell.m_row, cell.m_col, cell.m_row_size, cell.m_col_size, false);

					SetCellTextOrient(cell.m_row, cell.m_col, cell.m_textOrient, false);
					SetCellFont(cell.m_row, cell.m_col, cell.m_font, false);
					SetCellBackgroundColour(cell.m_row, cell.m_col, cell.m_backgroundColour, false);
					SetCellTextColour(cell.m_row, cell.m_col, cell.m_textColour, false);

					SetCellBorderLeft(cell.m_row, cell.m_col, cell.m_borderAt[0].m_style, cell.m_borderAt[0].m_colour, cell.m_borderAt[0].m_width, false);
					SetCellBorderRight(cell.m_row, cell.m_col, cell.m_borderAt[1].m_style, cell.m_borderAt[1].m_colour, cell.m_borderAt[1].m_width, false);
					SetCellBorderTop(cell.m_row, cell.m_col, cell.m_borderAt[2].m_style, cell.m_borderAt[2].m_colour, cell.m_borderAt[2].m_width, false);
					SetCellBorderBottom(cell.m_row, cell.m_col, cell.m_borderAt[3].m_style, cell.m_borderAt[3].m_colour, cell.m_borderAt[3].m_width, false);

					SetCellFitMode(cell.m_row, cell.m_col, ibToGridFitMode(cell.m_fitMode), false);
					SetCellReadOnly(cell.m_row, cell.m_col, cell.m_isReadOnly, false);

					if (cell.m_fillSetType == ibSpreadsheetFillType::ibSpreadsheetFillType_StrText) {
						wxSharedPtr<wxString> ptr = wxSharedPtr<wxString>(new wxString(cell.m_value));
						m_table->SetValueAsCustom(cell.m_row, cell.m_col, s_strTypeTextOrString, ptr.get());
					}
					else if (cell.m_fillSetType == ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate) {
						wxSharedPtr<wxString> ptr = wxSharedPtr<wxString>(new wxString(cell.m_value));
						m_table->SetValueAsCustom(cell.m_row, cell.m_col, s_strTypeTemplate, ptr.get());
					}
					else if (cell.m_fillSetType == ibSpreadsheetFillType::ibSpreadsheetFillType_StrParameter) {
						wxSharedPtr<wxString> ptr = wxSharedPtr<wxString>(new wxString(cell.m_value));
						m_table->SetValueAsCustom(cell.m_row, cell.m_col, s_strTypeParameter, ptr.get());
					}

					SetCellValue(cell.m_row, cell.m_col, cell.m_value, false);
				}
			}
		}

		wxTheClipboard->Close();

	}
	else if (wxTheClipboard->Open()
		&& wxTheClipboard->IsSupported(wxDF_TEXT)) {

		wxString copy_data, cur_field, cur_line;

		wxTextDataObject textObj;
		if (wxTheClipboard->GetData(textObj)) {
			copy_data = textObj.GetText();
		}
		wxTheClipboard->Close();

		int i = GetGridCursorRow(), j = GetGridCursorCol();
		int k = j;

		while (!copy_data.IsEmpty()) {
			cur_line = copy_data.BeforeFirst('\n');
			while (!cur_line.IsEmpty()) {
				cur_field = cur_line.BeforeFirst('\t');
				SetCellValue(i, j, cur_field);
				j++;
				cur_line = cur_line.AfterFirst('\t');
			}
			i++;
			j = k;
			copy_data = copy_data.AfterFirst('\n');
		}
	}

	ibGrid::ForceRefresh();
}
