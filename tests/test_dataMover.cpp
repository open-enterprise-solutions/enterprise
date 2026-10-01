////////////////////////////////////////////////////////////////////////////
//	Description : the data mover's wire - a date off a dump, by the form the dump declares
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/backend_core.h"           // ibDateTime
#include "backend/query/dataMover.h"       // ibDataMover::DateOfWire, DateForm

// A dump says which form its dates are in (chunk 3); one made before 2026-09 says nothing and carried
// instants of the dumping machine's clock. An ibDateTime's count passes through; the first wall form
// (counted from 1970) moves to the count's own start; an instant crosses the bridge to the parts this
// machine's clock shows for it - the parts the dumper saw, on a machine in the dumper's zone - and the
// old empty literal is the empty date.
TEST(DataMover, ADateOffTheWireIsReadByTheFormTheDumpDeclares)
{
	wxInitializer wx;
	using ibDataMover::DateForm;
	const ibDateTime reading(2026, 3, 29, 2, 30);
	EXPECT_EQ(reading, ibDataMover::DateOfWire(reading.GetValue(), DateForm::DateTime));
	EXPECT_TRUE(ibDataMover::DateOfWire(0, DateForm::DateTime).IsEmpty());

	EXPECT_EQ(reading, ibDataMover::DateOfWire(reading - ibDateTime(1970, 1, 1), DateForm::WallFrom1970));
	EXPECT_TRUE(ibDataMover::DateOfWire(-62135596800000ll, DateForm::WallFrom1970).IsEmpty()) << "that form's empty date";

	const wxDateTime instant(15, wxDateTime::Jan, 2025, 10, 30, 0);
	const long long ms = static_cast<long long>(instant.GetValue().GetValue());
	EXPECT_EQ(ibDateTime(2025, 1, 15, 10, 30), ibDataMover::DateOfWire(ms, DateForm::Instant));
	EXPECT_TRUE(ibDataMover::DateOfWire(-62135604000000ll, DateForm::Instant).IsEmpty()) << "the old literal";
	EXPECT_EQ(ibDateTime(1970, 1, 1), ibDataMover::DateOfWire(0, DateForm::Instant));
}
