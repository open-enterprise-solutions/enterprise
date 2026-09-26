////////////////////////////////////////////////////////////////////////////
//	Description : the data mover's wire - a date off a dump, by the form the dump declares
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/backend_core.h"           // emptyDate, ibWallFromParts, the bridge
#include "backend/query/dataMover.h"       // ibDataMover::DateOfWire

// A dump made since 2026-09 carries wall-clock readings and says so (chunk 3); one made before
// carried instants of the dumping machine's clock and says nothing. A reading passes through; an
// instant crosses the bridge to the parts this machine's clock shows for it - the parts the dumper
// saw, on a machine in the dumper's zone; the old empty literal is the empty date; 0 stays 0.
TEST(DataMover, ADateOffTheWireIsReadByTheFormTheDumpDeclares)
{
	wxInitializer wx;
	const wxLongLong_t reading = ibWallFromParts(2026, 3, 29, 2, 30);
	EXPECT_EQ(reading, ibDataMover::DateOfWire(reading, false));
	EXPECT_EQ(emptyDate, ibDataMover::DateOfWire(emptyDate, false));
	EXPECT_EQ(emptyDate, ibDataMover::DateOfWire(-62135604000000ll, false)) << "the old literal, whatever the dump says";

	const wxDateTime instant(15, wxDateTime::Jan, 2025, 10, 30, 0);
	const wxLongLong_t ms = instant.GetValue().GetValue();
	EXPECT_EQ(ibWallFromParts(2025, 1, 15, 10, 30), ibDataMover::DateOfWire(ms, true));
	EXPECT_EQ(emptyDate, ibDataMover::DateOfWire(-62135604000000ll, true));
	EXPECT_EQ(0, ibDataMover::DateOfWire(0, true));
}
