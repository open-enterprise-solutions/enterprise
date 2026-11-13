#include "backend/sheetFormat/sheetFormatNative.h"

#include "core/fileKind.h"           // the one table that names our files
#include "core/memory/memoryStream.h"      // ibReaderMemory / ibWriterMemory

#include <istream>
#include <ostream>

ibString ibSheetFormatNative::GetName() const
{
	return _("Spreadsheet document");
}

ibString ibSheetFormatNative::GetExtension() const
{
	// NOT A LITERAL. What our files are called is fileKind.h's answer, and it stays
	// the only one — that table exists because ".oxl" used to be typed in eight
	// places and a rename had to find all of them.
	return ibFileExtension(ibFileKind::Table);
}

bool ibSheetFormatNative::Read(std::istream& input, ibSpreadsheetDescription& sheet) const
{
	// Whole, to its end — a stream from the temporary storage has no length to ask for up front.
	constexpr std::size_t kPiece = 64 * 1024;
	ibMemoryBuffer buffer;
	for (std::size_t got = kPiece; got == kPiece; ) {
		input.read(static_cast<char*>(buffer.GetAppendBuf(kPiece)), static_cast<std::streamsize>(kPiece));
		got = static_cast<std::size_t>(input.gcount());
		buffer.UngetAppendBuf(got);
	}

	if (buffer.GetDataLen() == 0)
		return false;

	// ⚠ THE BUFFER IS A NAMED VARIABLE — ibReaderMemory borrows its bytes and would
	// otherwise read freed memory the moment the expression ended.
	ibReaderMemory reader(buffer);
	if (reader.eof())
		return false;

	return ibSpreadsheetDescriptionMemory::LoadData(reader, sheet);
}

bool ibSheetFormatNative::Write(std::ostream& output, const ibSpreadsheetDescription& sheet) const
{
	ibWriterMemory writer;
	if (!ibSpreadsheetDescriptionMemory::SaveData(writer, sheet))
		return false;

	output.write(reinterpret_cast<const char*>(writer.pointer()), static_cast<std::streamsize>(writer.size()));
	return output.good();
}

///////////////////////////////////////////////////////////////////////////////
//							Runtime register
///////////////////////////////////////////////////////////////////////////////

SHEET_FORMAT_REGISTER(ibSheetFormatNative);
