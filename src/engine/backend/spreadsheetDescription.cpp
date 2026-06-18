#include "spreadsheetDescription.h"
#include "backend/fileSystem/fs.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node form (Binary blob)

#define cell_sign		0x0243565431

#define grid_block		0x10000

#define main_block		0x12000
#define cell_block		0x14000
#define area_block		0x16000
#define data_block		0x18000

#include "backend/typeconv.h"

////////////////////////////////////////////////////////////////////////////////

bool ibSpreadsheetCellDescriptionMemory::LoadData(ibReaderMemory& reader, ibSpreadsheetCellDescription& spreadsheetCellDesc)
{
	spreadsheetCellDesc.m_value = reader.r_stringZ();

	spreadsheetCellDesc.m_alignHorz = reader.r_s32();
	spreadsheetCellDesc.m_alignVert = reader.r_s32();
	spreadsheetCellDesc.m_textOrient = reader.r_s32();

	spreadsheetCellDesc.m_font = typeConv::StringToFont(reader.r_stringZ());
	spreadsheetCellDesc.m_backgroundColour = typeConv::StringToColour(reader.r_stringZ());
	spreadsheetCellDesc.m_textColour = typeConv::StringToColour(reader.r_stringZ());

	spreadsheetCellDesc.m_borderAt[0].m_style = static_cast<wxPenStyle>(reader.r_s32());
	spreadsheetCellDesc.m_borderAt[0].m_width = reader.r_s32();
	spreadsheetCellDesc.m_borderAt[0].m_colour = typeConv::StringToColour(reader.r_stringZ());

	spreadsheetCellDesc.m_borderAt[1].m_style = static_cast<wxPenStyle>(reader.r_s32());
	spreadsheetCellDesc.m_borderAt[1].m_width = reader.r_s32();
	spreadsheetCellDesc.m_borderAt[1].m_colour = typeConv::StringToColour(reader.r_stringZ());

	spreadsheetCellDesc.m_borderAt[2].m_style = static_cast<wxPenStyle>(reader.r_s32());
	spreadsheetCellDesc.m_borderAt[2].m_width = reader.r_s32();
	spreadsheetCellDesc.m_borderAt[2].m_colour = typeConv::StringToColour(reader.r_stringZ());

	spreadsheetCellDesc.m_borderAt[3].m_style = static_cast<wxPenStyle>(reader.r_s32());
	spreadsheetCellDesc.m_borderAt[3].m_width = reader.r_s32();
	spreadsheetCellDesc.m_borderAt[3].m_colour = typeConv::StringToColour(reader.r_stringZ());

	spreadsheetCellDesc.m_row_size = reader.r_s32();
	spreadsheetCellDesc.m_col_size = reader.r_s32();

	spreadsheetCellDesc.m_fitMode = static_cast<ibSpreadsheetCellDescription::ibFitMode>(reader.r_s32());
	spreadsheetCellDesc.m_isReadOnly = reader.r_u8();

	spreadsheetCellDesc.m_fillSetType = static_cast<ibSpreadsheetFillType>(reader.r_s32());

	spreadsheetCellDesc.m_detailsParameter = reader.r_stringZ();
	return true;
}

bool ibSpreadsheetCellDescriptionMemory::SaveData(ibWriterMemory& writer, const ibSpreadsheetCellDescription& spreadsheetCellDesc)
{
	writer.w_stringZ(spreadsheetCellDesc.m_value);

	writer.w_s32(spreadsheetCellDesc.m_alignHorz);
	writer.w_s32(spreadsheetCellDesc.m_alignVert);
	writer.w_s32(spreadsheetCellDesc.m_textOrient);

	writer.w_stringZ(typeConv::FontToString(spreadsheetCellDesc.m_font));
	writer.w_stringZ(typeConv::ColourToString(spreadsheetCellDesc.m_backgroundColour));
	writer.w_stringZ(typeConv::ColourToString(spreadsheetCellDesc.m_textColour));

	writer.w_s32(spreadsheetCellDesc.m_borderAt[0].m_style);
	writer.w_s32(spreadsheetCellDesc.m_borderAt[0].m_width);
	writer.w_stringZ(typeConv::ColourToString(spreadsheetCellDesc.m_borderAt[0].m_colour));

	writer.w_s32(spreadsheetCellDesc.m_borderAt[1].m_style);
	writer.w_s32(spreadsheetCellDesc.m_borderAt[1].m_width);
	writer.w_stringZ(typeConv::ColourToString(spreadsheetCellDesc.m_borderAt[1].m_colour));

	writer.w_s32(spreadsheetCellDesc.m_borderAt[2].m_style);
	writer.w_s32(spreadsheetCellDesc.m_borderAt[2].m_width);
	writer.w_stringZ(typeConv::ColourToString(spreadsheetCellDesc.m_borderAt[2].m_colour));

	writer.w_s32(spreadsheetCellDesc.m_borderAt[3].m_style);
	writer.w_s32(spreadsheetCellDesc.m_borderAt[3].m_width);
	writer.w_stringZ(typeConv::ColourToString(spreadsheetCellDesc.m_borderAt[3].m_colour));

	writer.w_s32(spreadsheetCellDesc.m_row_size);
	writer.w_s32(spreadsheetCellDesc.m_col_size);

	writer.w_s32(spreadsheetCellDesc.m_fitMode);
	writer.w_s8(spreadsheetCellDesc.m_isReadOnly);

	writer.w_s32(spreadsheetCellDesc.m_fillSetType);

	writer.w_stringZ(spreadsheetCellDesc.m_detailsParameter);
	return true;
}

////////////////////////////////////////////////////////////////////////////////

bool ibSpreadsheetDescriptionMemory::LoadData(ibReaderMemory& reader, ibSpreadsheetDescription& spreadsheetDesc)
{
	wxMemoryBuffer mainBuffer;
	if (!reader.r_chunk(grid_block, mainBuffer))
		return false;

	ibReaderMemory mainReader(mainBuffer);

	wxMemoryBuffer headerBuffer;
	if (!mainReader.r_chunk(main_block, headerBuffer))
		return false;

	ibReaderMemory headerReader(headerBuffer);
	if (headerReader.r_u64() != cell_sign)
		return false;

	wxMemoryBuffer cellBuffer;
	if (!mainReader.r_chunk(cell_block, cellBuffer))
		return false;

	ibReaderMemory cellReader(cellBuffer);

	{
		const size_t capacity = cellReader.r_u64();
		spreadsheetDesc.ClearSpreadsheet(capacity);

		for (u64 c = 0; c < capacity; c++)
		{
			int row = cellReader.r_s32();
			int col = cellReader.r_s32();

			ibSpreadsheetCellDescriptionMemory::LoadData(cellReader,
				*spreadsheetDesc.GetOrCreateCell(row, col));
		}
	}

	wxMemoryBuffer areaBuffer;
	if (!mainReader.r_chunk(area_block, areaBuffer))
		return false;

	ibReaderMemory areaReader(areaBuffer);

	{
		const size_t capacity = areaReader.r_u64();
		for (u64 c = 0; c < capacity; c++) {

			wxString areaLabel = areaReader.r_stringZ();

			int start = areaReader.r_s32(),
				end = areaReader.r_s32();

			spreadsheetDesc.AddRowArea(areaLabel, start, end);
		}
	}
	{
		const size_t capacity = areaReader.r_u64();
		for (u64 c = 0; c < capacity; c++) {

			wxString areaLabel = areaReader.r_stringZ();

			int start = areaReader.r_s32(),
				end = areaReader.r_s32();

			spreadsheetDesc.AddColArea(areaLabel, start, end);
		}
	}

	wxMemoryBuffer dataBuffer;
	if (!mainReader.r_chunk(data_block, dataBuffer))
		return false;

	ibReaderMemory dataReader(dataBuffer);

	{
		const size_t capacity = dataReader.r_u64();
		for (u64 c = 0; c < capacity; c++) spreadsheetDesc.AddRowBrake(dataReader.r_s32());
	}
	{
		const size_t capacity = dataReader.r_u64();
		for (u64 c = 0; c < capacity; c++) spreadsheetDesc.AddColBrake(dataReader.r_s32());
	}
	{
		const size_t capacity = dataReader.r_u64();
		for (u64 c = 0; c < capacity; c++) {
			int row = dataReader.r_s32();
			spreadsheetDesc.SetRowSize(row, dataReader.r_s32());
		}
	}
	{
		const size_t capacity = dataReader.r_u64();
		for (u64 c = 0; c < capacity; c++) {
			int col = dataReader.r_s32();
			spreadsheetDesc.SetColSize(col, dataReader.r_s32());
		}
	}

	spreadsheetDesc.SetRowFreeze(dataReader.r_s32());
	spreadsheetDesc.SetColFreeze(dataReader.r_s32());
	return true;
}

bool ibSpreadsheetDescriptionMemory::SaveData(ibWriterMemory& writer, const ibSpreadsheetDescription& spreadsheetDesc)
{
	ibWriterMemory mainWriter;

	ibWriterMemory headerWriter;
	headerWriter.w_u64(cell_sign); //sign
	headerWriter.w_u64(0); //reserved
	mainWriter.w_chunk(main_block, headerWriter.buffer());

	ibWriterMemory cellWriter;
	cellWriter.w_u64(spreadsheetDesc.GetCellCount());

	for (int idx = 0; idx < spreadsheetDesc.GetCellCount(); idx++) {

		const ibSpreadsheetCellDescription* cell = spreadsheetDesc.GetCellByIdx(idx);

		cellWriter.w_s32(cell->m_row);
		cellWriter.w_s32(cell->m_col);

		ibSpreadsheetCellDescriptionMemory::SaveData(cellWriter,
			*spreadsheetDesc.GetCellByIdx(idx));
	}

	mainWriter.w_chunk(cell_block, cellWriter.buffer());

	ibWriterMemory areaWriter;

	areaWriter.w_u64(spreadsheetDesc.GetAreaNumberRows());

	for (int idx = 0; idx < spreadsheetDesc.GetAreaNumberRows(); idx++)
	{
		const ibSpreadsheetAreaDescription* area = spreadsheetDesc.GetRowAreaByIdx(idx);

		areaWriter.w_stringZ(area->m_label);
		areaWriter.w_s32(area->m_start);
		areaWriter.w_s32(area->m_end);
	}

	areaWriter.w_u64(spreadsheetDesc.GetAreaNumberCols());

	for (int idx = 0; idx < spreadsheetDesc.GetAreaNumberCols(); idx++)
	{
		const ibSpreadsheetAreaDescription* area = spreadsheetDesc.GetColAreaByIdx(idx);

		areaWriter.w_stringZ(area->m_label);
		areaWriter.w_s32(area->m_start);
		areaWriter.w_s32(area->m_end);
	}

	mainWriter.w_chunk(area_block, areaWriter.buffer());

	ibWriterMemory dataWriter;

	dataWriter.w_u64(spreadsheetDesc.GetBrakeNumberRows());
	for (int idx = 0; idx < spreadsheetDesc.GetBrakeNumberRows(); idx++) dataWriter.w_s32(spreadsheetDesc.GetRowBrakeByIdx(idx));

	dataWriter.w_u64(spreadsheetDesc.GetBrakeNumberCols());
	for (int idx = 0; idx < spreadsheetDesc.GetBrakeNumberCols(); idx++) dataWriter.w_s32(spreadsheetDesc.GetColBrakeByIdx(idx));

	dataWriter.w_u64(spreadsheetDesc.GetSizeNumberRows());
	for (int idx = 0; idx < spreadsheetDesc.GetSizeNumberRows(); idx++) {
		const ibSpreadsheetRowSizeDescription* desc = spreadsheetDesc.GetRowSizeByIdx(idx);
		dataWriter.w_s32(desc->m_row);
		dataWriter.w_s32(desc->m_height);
	}

	dataWriter.w_u64(spreadsheetDesc.GetSizeNumberCols());
	for (int idx = 0; idx < spreadsheetDesc.GetSizeNumberCols(); idx++) {
		const ibSpreadsheetColSizeDescription* desc = spreadsheetDesc.GetColSizeByIdx(idx);
		dataWriter.w_s32(desc->m_col);
		dataWriter.w_s32(desc->m_width);
	}

	dataWriter.w_s32(spreadsheetDesc.GetRowFreeze());
	dataWriter.w_s32(spreadsheetDesc.GetColFreeze());

	mainWriter.w_chunk(data_block, dataWriter.buffer());

	writer.w_chunk(grid_block, mainWriter.buffer());
	return true;
}

////////////////////////////////////////////////////////////////////////
// node form — the whole sheet is a genuine blob: a Binary value. The byte reader /
// writer is contained here, not in the property.

bool ibSpreadsheetDescriptionMemory::ReadNode(const ibDataValue& value, ibSpreadsheetDescription& spreadsheetDesc)
{
	const wxMemoryBuffer& data = value.AsBinary();
	if (data.GetDataLen()) {
		ibReaderMemory reader(data);
		return LoadData(reader, spreadsheetDesc);
	}
	return true;
}

bool ibSpreadsheetDescriptionMemory::WriteNode(ibDataValue& value, const ibSpreadsheetDescription& spreadsheetDesc)
{
	ibWriterMemory writer;
	if (!SaveData(writer, spreadsheetDesc))
		return false;
	value = ibDataValue::Binary(writer.buffer());
	return true;
}
