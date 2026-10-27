#include "core/description/spreadsheetDescription.h"
#include "core/memory/memoryStream.h"
#include "core/serialize/dataBuilder.h"   // ibDataValue — node form (Binary blob)

#define cell_sign		0x0243565431

#define grid_block		0x10000

#define main_block		0x12000
#define cell_block		0x14000
#define area_block		0x16000
#define data_block		0x18000

////////////////////////////////////////////////////////////////////////////////
// A FONT AND A COLOUR are written in their own text (ibFont / ibColour ToString), the form the sheets were
// always written in: a sheet written before the move reads the same, and one written now reads in an older build.
//
// ⚠ AN UNSET COLOUR IS WRITTEN AS NOTHING. The byte form wrote an unset wxColour as "0,0,0", which read back as
// BLACK — a cell copied to the clipboard and pasted came back with a black fill it never had. The node form
// already wrote nothing; the byte form says the same now.
////////////////////////////////////////////////////////////////////////////////

bool ibSpreadsheetCellDescriptionMemory::LoadData(ibReaderMemory& reader, ibSpreadsheetCellDescription& spreadsheetCellDesc)
{
	spreadsheetCellDesc.m_value = reader.r_stringZ();

	spreadsheetCellDesc.m_alignHorz = reader.r_s32();
	spreadsheetCellDesc.m_alignVert = reader.r_s32();
	spreadsheetCellDesc.m_textOrient = reader.r_s32();

	// The field is always in the stream, the FONT is not: an empty string is a cell that carries
	// no font of its own, and the font refuses it.
	spreadsheetCellDesc.m_font.FromString(reader.r_stringZ());

	spreadsheetCellDesc.m_backgroundColour.FromString(reader.r_stringZ());
	spreadsheetCellDesc.m_textColour.FromString(reader.r_stringZ());

	spreadsheetCellDesc.m_borderAt[0].m_style = static_cast<ibSpreadsheetPenStyle>(reader.r_s32());
	spreadsheetCellDesc.m_borderAt[0].m_width = reader.r_s32();
	spreadsheetCellDesc.m_borderAt[0].m_colour.FromString(reader.r_stringZ());

	spreadsheetCellDesc.m_borderAt[1].m_style = static_cast<ibSpreadsheetPenStyle>(reader.r_s32());
	spreadsheetCellDesc.m_borderAt[1].m_width = reader.r_s32();
	spreadsheetCellDesc.m_borderAt[1].m_colour.FromString(reader.r_stringZ());

	spreadsheetCellDesc.m_borderAt[2].m_style = static_cast<ibSpreadsheetPenStyle>(reader.r_s32());
	spreadsheetCellDesc.m_borderAt[2].m_width = reader.r_s32();
	spreadsheetCellDesc.m_borderAt[2].m_colour.FromString(reader.r_stringZ());

	spreadsheetCellDesc.m_borderAt[3].m_style = static_cast<ibSpreadsheetPenStyle>(reader.r_s32());
	spreadsheetCellDesc.m_borderAt[3].m_width = reader.r_s32();
	spreadsheetCellDesc.m_borderAt[3].m_colour.FromString(reader.r_stringZ());

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

	writer.w_stringZ(spreadsheetCellDesc.m_font.ToString());
	writer.w_stringZ(spreadsheetCellDesc.m_backgroundColour.ToString());
	writer.w_stringZ(spreadsheetCellDesc.m_textColour.ToString());

	writer.w_s32(spreadsheetCellDesc.m_borderAt[0].m_style);
	writer.w_s32(spreadsheetCellDesc.m_borderAt[0].m_width);
	writer.w_stringZ(spreadsheetCellDesc.m_borderAt[0].m_colour.ToString());

	writer.w_s32(spreadsheetCellDesc.m_borderAt[1].m_style);
	writer.w_s32(spreadsheetCellDesc.m_borderAt[1].m_width);
	writer.w_stringZ(spreadsheetCellDesc.m_borderAt[1].m_colour.ToString());

	writer.w_s32(spreadsheetCellDesc.m_borderAt[2].m_style);
	writer.w_s32(spreadsheetCellDesc.m_borderAt[2].m_width);
	writer.w_stringZ(spreadsheetCellDesc.m_borderAt[2].m_colour.ToString());

	writer.w_s32(spreadsheetCellDesc.m_borderAt[3].m_style);
	writer.w_s32(spreadsheetCellDesc.m_borderAt[3].m_width);
	writer.w_stringZ(spreadsheetCellDesc.m_borderAt[3].m_colour.ToString());

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
	ibMemoryBuffer mainBuffer;
	if (!reader.r_chunk(grid_block, mainBuffer))
		return false;

	ibReaderMemory mainReader(mainBuffer);

	ibMemoryBuffer headerBuffer;
	if (!mainReader.r_chunk(main_block, headerBuffer))
		return false;

	ibReaderMemory headerReader(headerBuffer);
	if (headerReader.r_u64() != cell_sign)
		return false;

	ibMemoryBuffer cellBuffer;
	if (!mainReader.r_chunk(cell_block, cellBuffer))
		return false;

	ibReaderMemory cellReader(cellBuffer);

	{
		const size_t capacity = cellReader.r_u64();
		spreadsheetDesc.ClearSpreadsheet(capacity);

		for (std::uint64_t c = 0; c < capacity; c++)
		{
			int row = cellReader.r_s32();
			int col = cellReader.r_s32();

			ibSpreadsheetCellDescriptionMemory::LoadData(cellReader,
				*spreadsheetDesc.GetOrCreateCell(row, col));
		}
	}

	ibMemoryBuffer areaBuffer;
	if (!mainReader.r_chunk(area_block, areaBuffer))
		return false;

	ibReaderMemory areaReader(areaBuffer);

	{
		const size_t capacity = areaReader.r_u64();
		for (std::uint64_t c = 0; c < capacity; c++) {

			ibString areaLabel = areaReader.r_stringZ();

			int start = areaReader.r_s32(),
				end = areaReader.r_s32();

			spreadsheetDesc.AddRowArea(areaLabel, start, end);
		}
	}
	{
		const size_t capacity = areaReader.r_u64();
		for (std::uint64_t c = 0; c < capacity; c++) {

			ibString areaLabel = areaReader.r_stringZ();

			int start = areaReader.r_s32(),
				end = areaReader.r_s32();

			spreadsheetDesc.AddColArea(areaLabel, start, end);
		}
	}

	ibMemoryBuffer dataBuffer;
	if (!mainReader.r_chunk(data_block, dataBuffer))
		return false;

	ibReaderMemory dataReader(dataBuffer);

	{
		const size_t capacity = dataReader.r_u64();
		for (std::uint64_t c = 0; c < capacity; c++) spreadsheetDesc.AddRowBrake(dataReader.r_s32());
	}
	{
		const size_t capacity = dataReader.r_u64();
		for (std::uint64_t c = 0; c < capacity; c++) spreadsheetDesc.AddColBrake(dataReader.r_s32());
	}
	{
		const size_t capacity = dataReader.r_u64();
		for (std::uint64_t c = 0; c < capacity; c++) {
			int row = dataReader.r_s32();
			spreadsheetDesc.SetRowSize(row, dataReader.r_s32());
		}
	}
	{
		const size_t capacity = dataReader.r_u64();
		for (std::uint64_t c = 0; c < capacity; c++) {
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
// node form — A STRUCTURE, cell by cell.
//
// ⭐ THIS IS THE LAST OF THE SEVEN. Type, composition, home page, picture,
// source and command all describe themselves into the node; the sheet was the
// one still travelling as an opaque Binary block — the tail of the migration,
// not a decision.
//
// AND IT IS THE ONE THAT MATTERS MOST FOR WHAT COMES NEXT. A print layout IS a
// sheet: the areas a report fills, the parameters it substitutes, the fonts and
// borders a person will look at. While it was a blob, a report template could be
// stored and shown and nothing else — not read, not written, not generated. As a
// structure it is all three.
//
// ONLY WHAT DIFFERS FROM THE DEFAULT IS WRITTEN. A sheet is mostly empty and a
// cell is mostly ordinary; writing every field of every cell would bury the two
// that were set. Absent means default, which is also why an older sheet with
// fewer fields reads correctly.
//
// ⚠ NO FALLBACK TO THE BLOB (Max, 2026-08-30: the binary form is a remnant and is
// not supported). LoadData / SaveData stay — they are the CLIPBOARD's form, and
// the file format for foreign sheets is a different mechanism again.

namespace {

const ibChar* const kSheetCells   = ibT("cells");
const ibChar* const kSheetRows    = ibT("rows");
const ibChar* const kSheetCols    = ibT("cols");
const ibChar* const kSheetFreeze  = ibT("freeze");

// One border, and only when it is drawn at all. A transparent border is the
// absence of a border, so it says nothing rather than saying "transparent".
void WriteBorder(ibDataNode& into, const ibChar* name,
	const ibSpreadsheetBorderDescription& border)
{
	if (border.m_style == ibPenStyle_Transparent)
		return;

	ibDataNode& node = into.Child(name);
	node.AddField(ibT("style"), ibDataValue::Int((std::int64_t)border.m_style));
	node.AddField(ibT("width"), ibDataValue::Int((std::int64_t)border.m_width));
	node.SetValue(ibT("colour"), border.m_colour.ToString());
}

void ReadBorder(const ibDataNode& from, const ibChar* name,
	ibSpreadsheetBorderDescription& border)
{
	auto node = from.FindChild(name);
	if (!node)
		return;

	border.m_style = (ibSpreadsheetPenStyle)node->GetValue<std::int32_t>(ibT("style"));
	border.m_width = node->GetValue<std::int32_t>(ibT("width"));
	border.m_colour.FromString(node->GetValue<ibString>(ibT("colour")));
}

} // namespace

//----------------------------------------------------------------------
// the CELL's own node form
//----------------------------------------------------------------------

bool ibSpreadsheetCellDescriptionMemory::ReadNode(const ibDataValue& value, ibSpreadsheetCellDescription& cell)
{
	const auto node = value.AsChild();
	if (!node)
		return false;

	cell.m_value = node->GetValue<ibString>(ibT("value"));
	cell.m_detailsParameter = node->GetValue<ibString>(ibT("parameter"));

	if (node->Has(ibT("alignHorz")))
		cell.m_alignHorz = node->GetValue<std::int32_t>(ibT("alignHorz"));
	if (node->Has(ibT("alignVert")))
		cell.m_alignVert = node->GetValue<std::int32_t>(ibT("alignVert"));
	if (node->Has(ibT("orient")))
		cell.m_textOrient = node->GetValue<std::int32_t>(ibT("orient"));

	cell.m_font.FromString(node->GetValue<ibString>(ibT("font")));
	cell.m_backgroundColour.FromString(node->GetValue<ibString>(ibT("background")));
	cell.m_textColour.FromString(node->GetValue<ibString>(ibT("textColour")));

	ReadBorder(*node, ibT("left"),   cell.m_borderAt[0]);
	ReadBorder(*node, ibT("right"),  cell.m_borderAt[1]);
	ReadBorder(*node, ibT("top"),    cell.m_borderAt[2]);
	ReadBorder(*node, ibT("bottom"), cell.m_borderAt[3]);

	if (node->Has(ibT("rowSpan")))
		cell.m_row_size = node->GetValue<std::int32_t>(ibT("rowSpan"));
	if (node->Has(ibT("colSpan")))
		cell.m_col_size = node->GetValue<std::int32_t>(ibT("colSpan"));

	if (node->Has(ibT("fit")))
		cell.m_fitMode =
			(ibSpreadsheetCellDescription::ibFitMode)node->GetValue<std::int32_t>(ibT("fit"));
	if (node->Has(ibT("readOnly")))
		cell.m_isReadOnly = node->GetValue<bool>(ibT("readOnly"));
	if (node->Has(ibT("fill")))
		cell.m_fillSetType = (ibSpreadsheetFillType)node->GetValue<std::int32_t>(ibT("fill"));

	return true;
}

bool ibSpreadsheetCellDescriptionMemory::WriteNode(ibDataValue& value, const ibSpreadsheetCellDescription& cell)
{
	std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();

	// ONLY WHAT DIFFERS FROM THE DEFAULT. A cell is mostly ordinary, and writing
	// every field of every one would bury the two that were actually set.
	//
	// WHAT IS IN IT, first: the text a person sees and the parameter a report
	// substitutes. Everything below is appearance.
	if (!cell.m_value.IsEmpty())
		node->SetValue(ibT("value"), cell.m_value);
	if (!cell.m_detailsParameter.IsEmpty())
		node->SetValue(ibT("parameter"), cell.m_detailsParameter);

	if (cell.m_alignHorz != ibAlignmentHorz_Left)
		node->AddField(ibT("alignHorz"), ibDataValue::Int((std::int64_t)cell.m_alignHorz));
	if (cell.m_alignVert != ibAlignmentVert_Top)
		node->AddField(ibT("alignVert"), ibDataValue::Int((std::int64_t)cell.m_alignVert));
	if (cell.m_textOrient != ibOrient_Horizontal)
		node->AddField(ibT("orient"), ibDataValue::Int((std::int64_t)cell.m_textOrient));

	// An UNSET font is unset — the same rule as the colours below, and it replaced a comparison
	// with the default that MEASURED both fonts through a wxScreenDC (see the member's note).
	if (cell.m_font.IsOk())
		node->SetValue(ibT("font"), cell.m_font.ToString());

	// An UNSET colour is unset — see the note on the members. Writing the resolved
	// one would freeze today's desktop theme into the file.
	if (cell.m_backgroundColour.IsOk())
		node->SetValue(ibT("background"), cell.m_backgroundColour.ToString());
	if (cell.m_textColour.IsOk())
		node->SetValue(ibT("textColour"), cell.m_textColour.ToString());

	WriteBorder(*node, ibT("left"),   cell.m_borderAt[0]);
	WriteBorder(*node, ibT("right"),  cell.m_borderAt[1]);
	WriteBorder(*node, ibT("top"),    cell.m_borderAt[2]);
	WriteBorder(*node, ibT("bottom"), cell.m_borderAt[3]);

	if (cell.m_row_size != 1)
		node->AddField(ibT("rowSpan"), ibDataValue::Int((std::int64_t)cell.m_row_size));
	if (cell.m_col_size != 1)
		node->AddField(ibT("colSpan"), ibDataValue::Int((std::int64_t)cell.m_col_size));

	if (cell.m_fitMode != ibSpreadsheetCellDescription::Mode_Overflow)
		node->AddField(ibT("fit"), ibDataValue::Int((std::int64_t)cell.m_fitMode));
	if (cell.m_isReadOnly)
		node->AddField(ibT("readOnly"), ibDataValue::Bool(true));
	if (cell.m_fillSetType != ibSpreadsheetFillType::ibSpreadsheetFillType_StrText)
		node->AddField(ibT("fill"), ibDataValue::Int((std::int64_t)cell.m_fillSetType));

	value = ibDataValue::Child(node);
	return true;
}

//----------------------------------------------------------------------
// the SHEET's node form — positions, bands, freeze; the cells answer for
// themselves.
//----------------------------------------------------------------------

bool ibSpreadsheetDescriptionMemory::ReadNode(const ibDataValue& value, ibSpreadsheetDescription& spreadsheetDesc)
{
	const auto sheet = value.AsChild();
	if (!sheet)
		return true;   // an empty sheet — an ordinary state

	if (auto cells = sheet->FindField(kSheetCells)) {
		for (const ibDataValue& entry : cells->AsArray()) {

			const auto node = entry.AsChild();
			if (!node)
				continue;

			const int row = node->GetValue<std::int32_t>(ibT("row"));
			const int col = node->GetValue<std::int32_t>(ibT("col"));

			// The cell fills itself in; the sheet places it.
			ibSpreadsheetCellDescription read(row, col);
			ibSpreadsheetCellDescriptionMemory::ReadNode(entry, read);
			spreadsheetDesc.GetOrCreateCell(row, col, &read);
		}
	}

	// Rows and columns carry the same three things — an area is named, a break is
	// a position, a size is a measurement — so they are read by one helper each
	// side rather than two copies that could disagree.
	auto readBand = [&](const ibChar* which, bool rows) {

		auto band = sheet->FindChild(which);
		if (!band)
			return;

		if (auto areas = band->FindField(ibT("areas"))) {
			for (const ibDataValue& entry : areas->AsArray()) {
				const auto node = entry.AsChild();
				if (!node)
					continue;

				const ibString label = node->GetValue<ibString>(ibT("name"));
				const int start = node->GetValue<std::int32_t>(ibT("start"));
				const int end   = node->GetValue<std::int32_t>(ibT("end"));

				if (rows) spreadsheetDesc.AddRowArea(label, start, end);
				else      spreadsheetDesc.AddColArea(label, start, end);
			}
		}

		if (auto breaks = band->FindField(ibT("breaks"))) {
			for (const ibDataValue& entry : breaks->AsArray()) {
				if (rows) spreadsheetDesc.AddRowBrake((int)entry.AsInt());
				else      spreadsheetDesc.AddColBrake((int)entry.AsInt());
			}
		}

		if (auto sizes = band->FindField(ibT("sizes"))) {
			for (const ibDataValue& entry : sizes->AsArray()) {
				const auto node = entry.AsChild();
				if (!node)
					continue;

				const int at = node->GetValue<std::int32_t>(ibT("at"));
				const int size = node->GetValue<std::int32_t>(ibT("size"));

				if (rows) spreadsheetDesc.SetRowSize(at, size);
				else      spreadsheetDesc.SetColSize(at, size);
			}
		}

		// ⭐ GROUPS WERE NEVER STORED AT ALL — not here and not in the byte form
		// that came before it (the word does not occur in this file's history).
		// An outline a person folded lived in memory and was gone on the next
		// open, which reads as the platform forgetting rather than as a gap: the
		// bands were still there, only their nesting was not.
		//
		// Written now because the node form is where a description says what it
		// is, and a fold is part of what a sheet is.
		if (auto groups = band->FindField(ibT("groups"))) {
			for (const ibDataValue& entry : groups->AsArray()) {
				const auto node = entry.AsChild();
				if (!node)
					continue;

				const unsigned int start = (unsigned int)node->GetValue<std::int32_t>(ibT("start"));
				const unsigned int end = (unsigned int)node->GetValue<std::int32_t>(ibT("end"));
				const unsigned int level = (unsigned int)node->GetValue<std::int32_t>(ibT("level"));
				const bool collapsed = node->GetValue<bool>(ibT("collapsed"));

				if (rows) spreadsheetDesc.AddRowGroup(start, end, level, collapsed);
				else      spreadsheetDesc.AddColGroup(start, end, level, collapsed);
			}
		}
	};

	readBand(kSheetRows, true);
	readBand(kSheetCols, false);

	if (auto freeze = sheet->FindChild(kSheetFreeze)) {
		spreadsheetDesc.SetRowFreeze(freeze->GetValue<std::int32_t>(ibT("row")));
		spreadsheetDesc.SetColFreeze(freeze->GetValue<std::int32_t>(ibT("col")));
	}

	return true;
}

bool ibSpreadsheetDescriptionMemory::WriteNode(ibDataValue& value, const ibSpreadsheetDescription& spreadsheetDesc)
{
	std::shared_ptr<ibDataNode> sheet = std::make_shared<ibDataNode>();

	std::vector<ibDataValue> cells;
	for (int idx = 0; idx < spreadsheetDesc.GetCellCount(); ++idx) {

		const ibSpreadsheetCellDescription* cell = spreadsheetDesc.GetCellByIdx(idx);
		if (cell == nullptr)
			continue;

		// The cell says what it is; the sheet only says WHERE.
		ibDataValue entry;
		if (!ibSpreadsheetCellDescriptionMemory::WriteNode(entry, *cell))
			continue;

		// A value is its bytes: the cell's node with its place added is the entry written again.
		if (auto node = entry.AsChild()) {
			node->AddField(ibT("row"), ibDataValue::Int((std::int64_t)cell->m_row));
			node->AddField(ibT("col"), ibDataValue::Int((std::int64_t)cell->m_col));
			entry = ibDataValue::Child(std::make_shared<ibDataNode>(std::move(*node)));
		}

		cells.push_back(entry);
	}

	sheet->AddField(kSheetCells, ibDataValue::Array(cells));

	auto writeBand = [&](const ibChar* which, bool rows) {

		ibDataNode& band = sheet->Child(which);

		std::vector<ibDataValue> areas;
		const int areaCount = rows
			? spreadsheetDesc.GetAreaNumberRows() : spreadsheetDesc.GetAreaNumberCols();

		for (int idx = 0; idx < areaCount; ++idx) {

			const ibSpreadsheetAreaDescription* area = rows
				? spreadsheetDesc.GetRowAreaByIdx(idx) : spreadsheetDesc.GetColAreaByIdx(idx);
			if (area == nullptr)
				continue;

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->SetValue(ibT("name"), area->m_label);
			node->AddField(ibT("start"), ibDataValue::Int((std::int64_t)area->m_start));
			node->AddField(ibT("end"), ibDataValue::Int((std::int64_t)area->m_end));

			areas.push_back(ibDataValue::Child(node));
		}
		band.AddField(ibT("areas"), ibDataValue::Array(areas));

		std::vector<ibDataValue> breaks;
		const int breakCount = rows
			? spreadsheetDesc.GetBrakeNumberRows() : spreadsheetDesc.GetBrakeNumberCols();

		for (int idx = 0; idx < breakCount; ++idx) {
			breaks.push_back(ibDataValue::Int((std::int64_t)(rows
				? spreadsheetDesc.GetRowBrakeByIdx(idx)
				: spreadsheetDesc.GetColBrakeByIdx(idx))));
		}
		band.AddField(ibT("breaks"), ibDataValue::Array(breaks));

		std::vector<ibDataValue> sizes;
		const int sizeCount = rows
			? spreadsheetDesc.GetSizeNumberRows() : spreadsheetDesc.GetSizeNumberCols();

		for (int idx = 0; idx < sizeCount; ++idx) {

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();

			if (rows) {
				const ibSpreadsheetRowSizeDescription* size = spreadsheetDesc.GetRowSizeByIdx(idx);
				if (size == nullptr)
					continue;
				node->AddField(ibT("at"), ibDataValue::Int((std::int64_t)size->m_row));
				node->AddField(ibT("size"), ibDataValue::Int((std::int64_t)size->m_height));
			}
			else {
				const ibSpreadsheetColSizeDescription* size = spreadsheetDesc.GetColSizeByIdx(idx);
				if (size == nullptr)
					continue;
				node->AddField(ibT("at"), ibDataValue::Int((std::int64_t)size->m_col));
				node->AddField(ibT("size"), ibDataValue::Int((std::int64_t)size->m_width));
			}

			sizes.push_back(ibDataValue::Child(node));
		}
		band.AddField(ibT("sizes"), ibDataValue::Array(sizes));

		// …and the OUTLINE, which nothing has ever stored — see the note on the
		// reading side.
		std::vector<ibDataValue> groups;
		const int groupCount = rows
			? spreadsheetDesc.GetGroupNumberRows() : spreadsheetDesc.GetGroupNumberCols();

		for (int idx = 0; idx < groupCount; ++idx) {

			const ibSpreadsheetGroupDescription* group = rows
				? spreadsheetDesc.GetRowGroupByIdx(idx) : spreadsheetDesc.GetColGroupByIdx(idx);
			if (group == nullptr)
				continue;

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->AddField(ibT("start"), ibDataValue::Int((std::int64_t)group->m_start));
			node->AddField(ibT("end"), ibDataValue::Int((std::int64_t)group->m_end));
			node->AddField(ibT("level"), ibDataValue::Int((std::int64_t)group->m_level));

			// FOLDED IS A STATE OF THE DOCUMENT, not of the person looking at it —
			// a template that opens folded is how a long form stays readable.
			if (group->m_collapsed)
				node->AddField(ibT("collapsed"), ibDataValue::Bool(true));

			groups.push_back(ibDataValue::Child(node));
		}

		band.AddField(ibT("groups"), ibDataValue::Array(groups));
	};

	writeBand(kSheetRows, true);
	writeBand(kSheetCols, false);

	ibDataNode& freeze = sheet->Child(kSheetFreeze);
	freeze.AddField(ibT("row"), ibDataValue::Int((std::int64_t)spreadsheetDesc.GetRowFreeze()));
	freeze.AddField(ibT("col"), ibDataValue::Int((std::int64_t)spreadsheetDesc.GetColFreeze()));

	value = ibDataValue::Child(sheet);
	return true;
}
