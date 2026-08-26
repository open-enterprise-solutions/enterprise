#include <gtest/gtest.h>
#include <chrono>
#include "backend/backend_spreadsheet.h"
#include "backend/sheetFormat/sheetFormat.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/log.h>

// ⚠ MEASUREMENT, not a check — DISABLED by default. Run with:
//   oes_tests --gtest_also_run_disabled_tests --gtest_filter=*Spreadsheet*Bench*
//
// What it shows: the cost of filling a block of cells is QUADRATIC, because
// ibSpreadsheetDescription::GetOrCreateCell does a linear std::find_if over the
// whole cell vector for every single write. A report is exactly the workload that
// hits it — see docs/spreadsheet-document.md § 2.
TEST(SpreadsheetDocumentBench, DISABLED_FillCost)
{
	printf("sizeof(ibSpreadsheetCellDescription) = %d bytes\n",
		(int)sizeof(ibSpreadsheetCellDescription));

	for (int rows : { 100, 200, 400, 800, 1600, 3200 }) {
		wxObjectDataPtr<ibBackendSpreadsheetObject> doc(new ibBackendSpreadsheetObject());
		const auto t0 = std::chrono::steady_clock::now();
		for (int r = 0; r < rows; ++r)
			for (int c = 0; c < 10; ++c)
				doc->SetCellValue(r, c, wxT("x"));
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - t0).count();
		printf("%5d cells : %4lld ms\n", rows * 10, (long long)ms);
	}
}

// =============================================================================
// ⚠ MEASUREMENT of the FILE FORMATS, not a check — DISABLED by default. Run with:
//   oes_tests --gtest_also_run_disabled_tests --gtest_filter=*SheetFormatBench*
//
// WHY IT EXISTS. A person opened a real workbook of tens of thousands of rows and
// it read slower than it should; the profiler pointed at wxXmlDocument::Load, i.e.
// at building a DOM for the whole sheet. This bench is the number behind that
// sentence: how long each format takes to write and to read the same sheet, at
// sizes that grow, so the SHAPE of the curve is visible and not just one figure.
//
// ⚠ FIGURES ARE FOR THIS MACHINE AND THIS BUILD. What travels is the RATIO
// between sizes (does ten times the rows cost ten times the seconds, or a hundred)
// and the ratio between formats.
// =============================================================================
TEST(SheetFormatBench, DISABLED_ReadWrite)
{
	// ⚠ SILENCE THE LOG, OR THE MEASUREMENT DROWNS IN IT. Every cell description carries a
	// wxFont, and in a console process without a GUI toolkit CreateFont refuses — one line per
	// cell, which at half a million cells is ~100 MB of identical text and dwarfs what is being
	// measured. The refusal is real and belongs in docs/spreadsheet-document.md § 8a (a cell
	// costs a font), not in the middle of a timing run.
	wxLogNull noLog;

	const wxString dir = wxFileName::GetTempDir();

	printf("%-22s %8s %10s %10s %12s\n", "format", "rows", "write ms", "read ms", "bytes");

	for (int rows : { 1000, 5000, 20000, 50000 }) {

		// The sheet a report produces: a heading row and ten columns of values.
		ibSpreadsheetDescription sheet;
		for (int col = 0; col < 10; col++)
			sheet.SetCellValue(0, col, wxString::Format(wxT("Column %d"), col + 1));
		for (int row = 1; row < rows; row++) {
			for (int col = 0; col < 10; col++) {
				if (col == 0)
					sheet.SetCellValue(row, col, wxString::Format(wxT("Row %d"), row));
				else
					sheet.SetCellValue(row, col, wxString::Format(wxT("%d"), row * col));
			}
		}

		for (const ibSheetFormat* format : ibSheetFormats()) {

			const wxString file = dir + wxFileName::GetPathSeparator()
				+ wxString::Format(wxT("oes_bench_%d.%s"), rows, format->GetExtension());

			const auto t0 = std::chrono::steady_clock::now();
			const bool written = format->Write(file, sheet);
			const auto t1 = std::chrono::steady_clock::now();

			long long readMs = -1;
			if (written && format->CanRead()) {
				ibSpreadsheetDescription back;
				const auto t2 = std::chrono::steady_clock::now();
				format->Read(file, back);
				readMs = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - t2).count();
			}

			const wxULongLong size = written ? wxFileName::GetSize(file) : wxULongLong(0);

			printf("%-22s %8d %10lld %10lld %12s\n",
				(const char*)format->GetName().ToUTF8(), rows,
				(long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
				readMs,
				(const char*)size.ToString().ToUTF8());

			wxRemoveFile(file);
		}
	}
}
