// =============================================================================
// FUNCTIONAL OPTIONS — the rule that decides whether a member is available, and the set a member keeps.
//
// The rule is pure (functionalOptionGate.h): a member of no option is available; a member of options is
// available while ANY of them is on; an option the configuration no longer has counts for nothing. The set lives on
// the member, like its sections (functionalOptionHelper.h), in a chunk of its own.
//
// No database and no configuration: the rule takes plain sets and maps, and the set round-trips through
// the chunk writer and reader the metaobject header uses.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/functionalOptionHelper.h"
#include "backend/functionalOption/functionalOptionGate.h"

namespace {

// Opens the protected load / save pair, which the metaobject header calls.
class ibFunctionalOptionProbe : public ibFunctionalOptionObject {
public:
	bool Save(ibWriterMemory& writer) const { return SaveFunctionalOptions(writer); }
	bool Load(ibReaderMemory& reader) { return LoadFunctionalOptions(reader); }
};

} // namespace

TEST(FunctionalOption, AMemberOfNoOptionIsAvailable)
{
	EXPECT_TRUE(ibFunctionalOptionGate::IsMemberAvailable({}, { { 10, false } }));
}

TEST(FunctionalOption, AMemberOfAnOptionThatIsOffIsUnavailable)
{
	EXPECT_FALSE(ibFunctionalOptionGate::IsMemberAvailable({ 10 }, { { 10, false } }));
}

TEST(FunctionalOption, AMemberOfAnOptionThatIsOnIsAvailable)
{
	EXPECT_TRUE(ibFunctionalOptionGate::IsMemberAvailable({ 10 }, { { 10, true } }));
}

// A field two parts of the system use is needed while either of them is used.
TEST(FunctionalOption, OneOptionOnIsEnoughForAMemberOfTwo)
{
	EXPECT_TRUE(ibFunctionalOptionGate::IsMemberAvailable({ 10, 20 }, { { 10, false }, { 20, true } }));
	EXPECT_FALSE(ibFunctionalOptionGate::IsMemberAvailable({ 10, 20 }, { { 10, false }, { 20, false } }));
}

// An option deleted since the member was saved takes nothing away — and does not stand in for the others.
TEST(FunctionalOption, AnOptionTheConfigurationNoLongerHasCountsForNothing)
{
	EXPECT_TRUE(ibFunctionalOptionGate::IsMemberAvailable({ 99 }, { { 10, false } }));
	EXPECT_FALSE(ibFunctionalOptionGate::IsMemberAvailable({ 99, 10 }, { { 10, false } }));
}

// An option works only under the one it requires: off above means off here, whatever its own value.
TEST(FunctionalOption, AnOptionUnderOneThatIsOffIsOff)
{
	const auto effective = ibFunctionalOptionGate::EffectiveValues(
		{ { 1, false }, { 2, true }, { 3, true } },   // 1 warehouses, 2 bins, 3 serials
		{ { 2, 1 }, { 3, 2 } });                       // bins require warehouses, serials require bins
	EXPECT_FALSE(effective.at(1));
	EXPECT_FALSE(effective.at(2));
	EXPECT_FALSE(effective.at(3));   // two steps up the chain
}

TEST(FunctionalOption, AnOptionUnderOneThatIsOnKeepsItsOwnValue)
{
	const auto effective = ibFunctionalOptionGate::EffectiveValues(
		{ { 1, true }, { 2, true }, { 3, false } }, { { 2, 1 }, { 3, 1 } });
	EXPECT_TRUE(effective.at(2));
	EXPECT_FALSE(effective.at(3));
}

// A chain that comes back to itself stops there instead of walking forever; a requirement the
// configuration no longer has counts for nothing.
TEST(FunctionalOption, ALoopOrAMissingRequirementEndsTheChain)
{
	const auto looped = ibFunctionalOptionGate::EffectiveValues({ { 1, true }, { 2, true } }, { { 1, 2 }, { 2, 1 } });
	EXPECT_TRUE(looped.at(1));
	EXPECT_TRUE(looped.at(2));

	const auto missing = ibFunctionalOptionGate::EffectiveValues({ { 1, true } }, { { 1, 99 } });
	EXPECT_TRUE(missing.at(1));
}

// With no application running (this test), everything is available: the gate belongs to the enterprise run.
TEST(FunctionalOption, OutsideTheRunningApplicationEverythingIsAvailable)
{
	EXPECT_TRUE(ibFunctionalOptionGate::IsAvailable(nullptr));
	EXPECT_FALSE(ibFunctionalOptionGate::AnyUnavailable(nullptr));
}

TEST(FunctionalOption, TheSetAMemberKeepsSurvivesWriteAndRead)
{
	ibFunctionalOptionProbe written;
	written.SetFunctionalOption(10);
	written.SetFunctionalOption(20);
	written.SetFunctionalOption(20, false);
	written.SetFunctionalOption(30);

	ibWriterMemory writer;
	ASSERT_TRUE(written.Save(writer));

	const wxMemoryBuffer blob = writer.buffer();   // held: a reader never reads a buffer that is already gone
	ibReaderMemory reader(blob);
	ibFunctionalOptionProbe read;
	ASSERT_TRUE(read.Load(reader));

	EXPECT_EQ(read.GetFunctionalOptions(), (std::set<ibMetaID>{ 10, 30 }));
	EXPECT_TRUE(read.IsInFunctionalOption(10));
	EXPECT_FALSE(read.IsInFunctionalOption(20));
}

// A configuration saved before the mechanism existed has no such chunk: reading it is not an error, and a
// member made fresh belongs to nothing.
TEST(FunctionalOption, AnAbsentChunkIsNotAnError)
{
	wxMemoryBuffer none;
	ibReaderMemory reader(none);
	ibFunctionalOptionProbe read;
	ASSERT_TRUE(read.Load(reader));
	EXPECT_TRUE(read.GetFunctionalOptions().empty());
}
