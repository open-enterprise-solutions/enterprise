#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <string>
#include "core/spreadsheet/spreadsheetObject.h"
#include "core/xml/xmlDocument.h"
#include "backend/sheetFormat/sheetFormat.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/log.h>
#include "core/fileSystem.h"   // ibFILE_SEP_PATH

// ⚠ MEASUREMENT, not a check — DISABLED by default. Run with:
//   oes_tests --gtest_also_run_disabled_tests --gtest_filter=*Spreadsheet*Bench*
//
// What it shows: the cost of filling a block of cells is QUADRATIC, because
// ibSpreadsheetDescription::GetOrCreateCell does a linear std::find_if over the
// whole cell vector for every single write. A report is exactly the workload that
// hits it — see docs/private/spreadsheet-document.md § 2.
TEST(SpreadsheetDocumentBench, DISABLED_FillCost)
{
	printf("sizeof(ibSpreadsheetCellDescription) = %d bytes\n",
		(int)sizeof(ibSpreadsheetCellDescription));

	for (int rows : { 100, 200, 400, 800, 1600, 3200 }) {
		std::shared_ptr<ibSpreadsheetObject> doc = std::make_shared<ibSpreadsheetObject>();
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
// it read slower than it should; the profiler pointed at wxXmlDocument::Load (the
// reader then), i.e. at building a DOM for the whole sheet. This bench is the number behind that
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
	// measured. The refusal is real and belongs in docs/private/spreadsheet-document.md § 8a (a cell
	// costs a font), not in the middle of a timing run.
	wxLogNull noLog;

	const ibString dir = wxFileName::GetTempDir();

	printf("%-22s %8s %10s %10s %12s\n", "format", "rows", "write ms", "read ms", "bytes");

	for (int rows : { 1000, 5000, 20000, 50000 }) {

		// The sheet a report produces: a heading row and ten columns of values.
		ibSpreadsheetDescription sheet;
		for (int col = 0; col < 10; col++)
			sheet.SetCellValue(0, col, ibString::Format(wxT("Column %d"), col + 1));
		for (int row = 1; row < rows; row++) {
			for (int col = 0; col < 10; col++) {
				if (col == 0)
					sheet.SetCellValue(row, col, ibString::Format(wxT("Row %d"), row));
				else
					sheet.SetCellValue(row, col, ibString::Format(wxT("%d"), row * col));
			}
		}

		for (const ibSheetFormat* format : ibSheetFormats()) {

			const ibString file = dir + ibFILE_SEP_PATH
				+ ibString::Format(wxT("oes_bench_%d.%s"), rows, format->GetExtension());

			const auto t0 = std::chrono::steady_clock::now();
			const bool written = format->WriteFile(file, sheet);
			const auto t1 = std::chrono::steady_clock::now();

			long long readMs = -1;
			if (written && format->CanRead()) {
				ibSpreadsheetDescription back;
				const auto t2 = std::chrono::steady_clock::now();
				format->ReadFile(file, back);
				readMs = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - t2).count();
			}

			const wxULongLong size = written ? wxFileName::GetSize(file) : wxULongLong(0);

			printf("%-22s %8d %10lld %10lld %12s\n",
				format->GetName().ToUTF8().c_str(), rows,
				(long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
				readMs,
				(const char*)size.ToString().ToUTF8());

			wxRemoveFile(file);
		}
	}
}

// =============================================================================
// ⚠ MEASUREMENT of the XML PARSER on a sheet's part, not a check — DISABLED by default. Run with:
//   oes_tests --gtest_also_run_disabled_tests --gtest_filter=*SheetFormatBench*
//
// WHAT IT SPLITS. The read above is the zip, the parser and the cells together; this is the parser alone, on the
// part that holds the cells, two ways at a time: names resolved (kNamespaces — a URI made for every element and
// attribute) or as written (kAsWritten — the tree's), and a part handed over as its bytes or made a string first.
// =============================================================================
TEST(SheetFormatBench, DISABLED_ParseSheetPart)
{
	printf("%-30s %8s %10s %12s\n", "parse", "rows", "ms", "bytes");

	for (int rows : { 5000, 50000 }) {
		// A sheet's part as a workbook writer makes it: the namespaces on the root, rows of ten cells.
		std::string part = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
			"<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\""
			" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheetData>";
		for (int row = 1; row <= rows; row++) {
			const std::string r = std::to_string(row);
			part += "<row r=\"" + r + "\">";
			for (int col = 0; col < 10; col++) {
				const std::string ref = std::string(1, char('A' + col)) + r;
				if (col == 0)
					part += "<c r=\"" + ref + "\" t=\"s\"><v>" + r + "</v></c>";
				else
					part += "<c r=\"" + ref + "\"><v>" + std::to_string(row * col) + "</v></c>";
			}
			part += "</row>";
		}
		part += "</sheetData></worksheet>";

		// The best of three: what the machine let through least disturbed.
		const auto measure = [&](const char* what, const std::function<void()>& parse) {
			long long best = -1;
			for (int run = 0; run < 3; run++) {
				const auto t0 = std::chrono::steady_clock::now();
				parse();
				const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - t0).count();
				if (best < 0 || ms < best)
					best = ms;
			}
			printf("%-30s %8d %10lld %12zu\n", what, rows, best, part.size());
		};

		// The parser alone, its queue let go of as it fills.
		const auto drain = [&](ibXmlParser::ibNames names) {
			ibXmlParser parser(ibT("XML"), ibXmlParser::kBytes, names);
			size_t at = 0;
			do {
				const size_t chunk = std::min(ibXmlParser::kChunk, part.size() - at);
				parser.Feed(part.data() + at, chunk, at + chunk == part.size(), ibString());
				at += chunk;
				parser.Queue().clear();
				while (parser.Paused()) {
					parser.Resume(ibString());
					parser.Queue().clear();
				}
			} while (at < part.size());
		};
		measure("parser, names resolved", [&] { drain(ibXmlParser::kNamespaces); });
		measure("parser, names as written", [&] { drain(ibXmlParser::kAsWritten); });

		// The tree: from the part's bytes, and from a string made of them first (decoded whole, encoded back).
		measure("tree, from the bytes", [&] {
			ibXmlDocument xml;
			xml.Load(part.data(), part.size());
		});
		measure("tree, from a string of them", [&] {
			ibXmlDocument xml;
			xml.Load(ibString::FromUTF8(part.data(), part.size()));
		});
	}
}
