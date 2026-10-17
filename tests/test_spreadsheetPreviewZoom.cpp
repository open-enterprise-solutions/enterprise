// =============================================================================
// Spreadsheet preview zoom (#484).
//
// A SpreadsheetDocument's own scale is set for the page it prints on, not for a screen - at the
// grid's 1.0 default a printed form read as too small to use, and there was no toolbar control to
// change it from the viewer itself. The fix has two parts:
//   - ShowSpreadsheetDocument's own preview (mainFrameParts.cpp) starts the grid's existing zoom
//     (ibGrid::SetGridZoom, the same door Ctrl+wheel already used - gridext.cpp) at double scale
//     instead of the 1.0 every other use of the grid keeps; a template opened for editing in the
//     Designer is untouched, because that call sits only in the preview's own document class.
//   - docViewSpreadsheet.cpp's toolbar gained a zoom percentage choice reaching every other step
//     from there.
//
// No existing test drives ShowSpreadsheetDocument end-to-end (it wants a full doc-template +
// AUI-toolbar host the harnesses here do not stand up), so this pins the one thing the fix's
// production line actually depends on: that SetGridZoom(4) from a freshly created grid - the exact
// call ShowSpreadsheetDocument makes - lands on 2.0, through the real widget, not a stand-in.
// =============================================================================

#include "frontendFormFix.h"

#include "frontend/win/editor/gridEditor/gridEditor.h"

#include <wx/frame.h>

namespace {

// A real grid window (ibGrid::Init, where m_zoomScale starts at 1.0, runs only once the window is
// actually created) with no document - SetGridZoom/GetGridZoom touch neither.
struct GridZoomFix : FrontendFormFix {
	wxFrame* parent = nullptr;
	ibGridEditor* grid = nullptr;

	void SetUp() override {
		FrontendFormFix::SetUp();
		if (!frameReady) return;
		parent = new wxFrame(nullptr, wxID_ANY, wxT("grid-zoom-parent"));
		grid = new ibGridEditor(nullptr, parent, wxID_ANY);   // leaked with parent, as VisualHostFix's hosts are
	}
	void TearDown() override {
		if (parent != nullptr) { parent->Destroy(); parent = nullptr; }
		FrontendFormFix::TearDown();
	}
};

} // namespace

TEST_F(GridZoomFix, FreshGrid_StartsAtOneToOne)
{
	if (!frameReady) GTEST_SKIP();
	EXPECT_FLOAT_EQ(grid->GetGridZoom(), 1.0f);
}

TEST_F(GridZoomFix, SetGridZoomOfFour_FromFresh_ReachesDoubleScale)
{
	if (!frameReady) GTEST_SKIP();

	// The exact call ShowSpreadsheetDocument's preview makes (mainFrameParts.cpp): four of
	// SetGridZoom's own 0.25 steps, from the 1.0 a freshly created grid starts at.
	grid->SetGridZoom(4);

	EXPECT_FLOAT_EQ(grid->GetGridZoom(), 2.0f)
		<< "a Show() preview reads at double the grid's 1.0 default so a form set for paper is not tiny on screen";
}

TEST_F(GridZoomFix, ZoomStaysWithinTheGridsOwnClamp)
{
	if (!frameReady) GTEST_SKIP();

	// gridext.cpp's SetGridZoom refuses past [0.5, 2.25] - confirms the toolbar's "225%" top choice
	// (docViewSpreadsheet.cpp) is reachable and nothing past it silently clamps to a different value.
	for (int i = 0; i < 20; ++i)
		grid->SetGridZoom(1);
	EXPECT_FLOAT_EQ(grid->GetGridZoom(), 2.25f);

	for (int i = 0; i < 20; ++i)
		grid->SetGridZoom(-1);
	EXPECT_FLOAT_EQ(grid->GetGridZoom(), 0.5f);
}
