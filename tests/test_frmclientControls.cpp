#include <gtest/gtest.h>

#include <wx/init.h>

#include "frmclient/visualView/controlEnumNames.h"

// The names are read inside libfrmclient.so. Constructing the enumeration
// classes in this file would emit an ibValue vtable the shared library does
// not export.

TEST(FormClientControls, CellAlignMatchesTheDesktop) {
	wxInitializer wxInit;
	ASSERT_TRUE(wxInit.IsOk());
	ASSERT_EQ(ibFormClientCellAlignCount(), 4u);
	EXPECT_EQ(ibFormClientCellAlignName(0), wxT("Auto"));
	EXPECT_EQ(ibFormClientCellAlignName(1), wxT("Left"));
	EXPECT_EQ(ibFormClientCellAlignName(2), wxT("Center"));
	EXPECT_EQ(ibFormClientCellAlignName(3), wxT("Right"));
}

TEST(FormClientControls, WindowOpeningModeMatchesTheDesktop) {
	wxInitializer wxInit;
	ASSERT_TRUE(wxInit.IsOk());
	ASSERT_EQ(ibFormClientWindowOpeningModeCount(), 3u);
	EXPECT_EQ(ibFormClientWindowOpeningModeName(0), wxT("Independent"));
	EXPECT_EQ(ibFormClientWindowOpeningModeName(1), wxT("LockOwnerWindow"));
	EXPECT_EQ(ibFormClientWindowOpeningModeName(2), wxT("LockWholeInterface"));
}

TEST(FormClientControls, QuickChoiceKeepsTheScriptNames) {
	wxInitializer wxInit;
	ASSERT_TRUE(wxInit.IsOk());
	ASSERT_EQ(ibFormClientQuickChoiceCount(), 3u);
	EXPECT_EQ(ibFormClientQuickChoiceName(0), wxT("Auto"));
	EXPECT_EQ(ibFormClientQuickChoiceName(1), wxT("Use"));
	EXPECT_EQ(ibFormClientQuickChoiceName(2), wxT("DontUse"));
}
