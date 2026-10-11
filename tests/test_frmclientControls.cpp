#include <gtest/gtest.h>

#include "frmclient/visualView/controlEnum.h"

TEST(FormClientControls, CellAlignMatchesTheDesktop) {
	ibValueEnumCellAlign align;
	ASSERT_TRUE(align.Init());
	ASSERT_EQ(align.GetEnumCount(), 4u);
	EXPECT_EQ(align.GetEnumName(0), wxT("Auto"));
	EXPECT_EQ(align.GetEnumName(1), wxT("Left"));
	EXPECT_EQ(align.GetEnumName(2), wxT("Center"));
	EXPECT_EQ(align.GetEnumName(3), wxT("Right"));
}

TEST(FormClientControls, WindowOpeningModeMatchesTheDesktop) {
	ibValueEnumWindowOpeningMode mode;
	ASSERT_TRUE(mode.Init());
	ASSERT_EQ(mode.GetEnumCount(), 3u);
	EXPECT_EQ(mode.GetEnumName(0), wxT("Independent"));
	EXPECT_EQ(mode.GetEnumName(1), wxT("LockOwnerWindow"));
	EXPECT_EQ(mode.GetEnumName(2), wxT("LockWholeInterface"));
}
