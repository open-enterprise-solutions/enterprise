#include <gtest/gtest.h>
#include <chrono>
#include "backend/backend_spreadsheet.h"

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
