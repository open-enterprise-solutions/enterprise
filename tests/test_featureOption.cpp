// =============================================================================
// FEATURE OPTIONS — the rule that decides whether a member is available, and the set a member keeps.
//
// The rule is pure (featureOptionGate.h): a member of no option is available; a member of options is
// available while ANY of them is on; an option the application no longer has counts for nothing. The set lives on
// the member, like its sections (featureOptionHelper.h), in a chunk of its own.
//
// No database and no application: the rule takes plain sets and maps, and the set round-trips through
// the chunk writer and reader the metaobject header uses.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/featureOptionHelper.h"
#include "backend/featureOption/featureOptionGate.h"

namespace {

// Opens the protected load / save pair, which the metaobject header calls.
class ibFeatureOptionProbe : public ibFeatureOptionObject {
public:
	bool Save(ibWriterMemory& writer) const { return SaveFeatureOptions(writer); }
	bool Load(ibReaderMemory& reader) { return LoadFeatureOptions(reader); }
};

} // namespace

TEST(FeatureOption, AMemberOfNoOptionIsAvailable)
{
	EXPECT_TRUE(ibFeatureOptionGate::IsMemberAvailable({}, { { 10, false } }));
}

TEST(FeatureOption, AMemberOfAnOptionThatIsOffIsUnavailable)
{
	EXPECT_FALSE(ibFeatureOptionGate::IsMemberAvailable({ 10 }, { { 10, false } }));
}

TEST(FeatureOption, AMemberOfAnOptionThatIsOnIsAvailable)
{
	EXPECT_TRUE(ibFeatureOptionGate::IsMemberAvailable({ 10 }, { { 10, true } }));
}

// A field two parts of the system use is needed while either of them is used.
TEST(FeatureOption, OneOptionOnIsEnoughForAMemberOfTwo)
{
	EXPECT_TRUE(ibFeatureOptionGate::IsMemberAvailable({ 10, 20 }, { { 10, false }, { 20, true } }));
	EXPECT_FALSE(ibFeatureOptionGate::IsMemberAvailable({ 10, 20 }, { { 10, false }, { 20, false } }));
}

// An option deleted since the member was saved takes nothing away — and does not stand in for the others.
TEST(FeatureOption, AnOptionTheApplicationNoLongerHasCountsForNothing)
{
	EXPECT_TRUE(ibFeatureOptionGate::IsMemberAvailable({ 99 }, { { 10, false } }));
	EXPECT_FALSE(ibFeatureOptionGate::IsMemberAvailable({ 99, 10 }, { { 10, false } }));
}

// An option works only under the one it requires: off above means off here, whatever its own value.
TEST(FeatureOption, AnOptionUnderOneThatIsOffIsOff)
{
	const auto effective = ibFeatureOptionGate::EffectiveValues(
		{ { 1, false }, { 2, true }, { 3, true } },   // 1 warehouses, 2 bins, 3 serials
		{ { 2, 1 }, { 3, 2 } });                       // bins require warehouses, serials require bins
	EXPECT_FALSE(effective.at(1));
	EXPECT_FALSE(effective.at(2));
	EXPECT_FALSE(effective.at(3));   // two steps up the chain
}

TEST(FeatureOption, AnOptionUnderOneThatIsOnKeepsItsOwnValue)
{
	const auto effective = ibFeatureOptionGate::EffectiveValues(
		{ { 1, true }, { 2, true }, { 3, false } }, { { 2, 1 }, { 3, 1 } });
	EXPECT_TRUE(effective.at(2));
	EXPECT_FALSE(effective.at(3));
}

// A chain that comes back to itself stops there instead of walking forever; a requirement the
// application no longer has counts for nothing.
TEST(FeatureOption, ALoopOrAMissingRequirementEndsTheChain)
{
	const auto looped = ibFeatureOptionGate::EffectiveValues({ { 1, true }, { 2, true } }, { { 1, 2 }, { 2, 1 } });
	EXPECT_TRUE(looped.at(1));
	EXPECT_TRUE(looped.at(2));

	const auto missing = ibFeatureOptionGate::EffectiveValues({ { 1, true } }, { { 1, 99 } });
	EXPECT_TRUE(missing.at(1));
}

// With no application running (this test), everything is available: the gate belongs to the enterprise run.
TEST(FeatureOption, OutsideTheRunningApplicationEverythingIsAvailable)
{
	EXPECT_TRUE(ibFeatureOptionGate::IsAvailable(nullptr));
	EXPECT_FALSE(ibFeatureOptionGate::AnyUnavailable(nullptr));
}

TEST(FeatureOption, TheSetAMemberKeepsSurvivesWriteAndRead)
{
	ibFeatureOptionProbe written;
	written.SetFeatureOption(10);
	written.SetFeatureOption(20);
	written.SetFeatureOption(20, false);
	written.SetFeatureOption(30);

	ibWriterMemory writer;
	ASSERT_TRUE(written.Save(writer));

	const ibMemoryBuffer blob = writer.buffer();   // held: a reader never reads a buffer that is already gone
	ibReaderMemory reader(blob);
	ibFeatureOptionProbe read;
	ASSERT_TRUE(read.Load(reader));

	EXPECT_EQ(read.GetFeatureOptions(), (std::set<ibMetaID>{ 10, 30 }));
	EXPECT_TRUE(read.IsInFeatureOption(10));
	EXPECT_FALSE(read.IsInFeatureOption(20));
}

// An application saved before the mechanism existed has no such chunk: reading it is not an error, and a
// member made fresh belongs to nothing.
TEST(FeatureOption, AnAbsentChunkIsNotAnError)
{
	ibMemoryBuffer none;
	ibReaderMemory reader(none);
	ibFeatureOptionProbe read;
	ASSERT_TRUE(read.Load(reader));
	EXPECT_TRUE(read.GetFeatureOptions().empty());
}
