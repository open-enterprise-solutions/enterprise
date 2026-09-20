// =============================================================================
// ibSelectionIsLive — when the designer canvas may follow the control it remembers.
//
// The canvas keeps raw pointers to the selected control and paints its highlight from them. A control can
// be gone by the next paint; following the pointer then read a destroyed object (EXC_BAD_ACCESS at 0x48
// in ibDesignerWindow::HighlightSelection). The pointer is followed only when the host still knows it.
// The tests use pointers that are NEVER dereferenced — which is the whole point of the rule.
// =============================================================================

#include <gtest/gtest.h>
#include "designer/win/editor/visualEditor/designerSelection.h"

#include <set>

namespace {

// A control address that stands for "an object that may or may not still exist"; never dereferenced.
const ibValueFrame* Fake(std::uintptr_t n)
{
	return reinterpret_cast<const ibValueFrame*>(n * 16);
}

} // namespace

TEST(DesignerSelection, NothingSelected_IsNotLive)
{
	EXPECT_FALSE(ibSelectionIsLive(nullptr, nullptr));
	EXPECT_FALSE(ibSelectionIsLive(nullptr, [](const ibValueFrame*) { return true; }));
}

TEST(DesignerSelection, NoCheckInstalled_TheSelectionIsTrusted)
{
	EXPECT_TRUE(ibSelectionIsLive(Fake(1), nullptr));
}

TEST(DesignerSelection, ControlTheHostKnows_IsLive)
{
	const std::set<const ibValueFrame*> known{ Fake(1), Fake(2) };
	const ibSelectionLiveCheck host = [&](const ibValueFrame* c) { return known.count(c) != 0; };

	EXPECT_TRUE(ibSelectionIsLive(Fake(1), host));
	EXPECT_TRUE(ibSelectionIsLive(Fake(2), host));
}

TEST(DesignerSelection, ControlTheHostNoLongerKnows_IsNotLive)
{
	std::set<const ibValueFrame*> known{ Fake(1) };
	const ibSelectionLiveCheck host = [&](const ibValueFrame* c) { return known.count(c) != 0; };

	ASSERT_TRUE(ibSelectionIsLive(Fake(1), host));
	known.erase(Fake(1));                                // the form was rebuilt / the control removed
	EXPECT_FALSE(ibSelectionIsLive(Fake(1), host));
}

TEST(DesignerSelection, Check_IsConsultedEveryTime_NotCached)
{
	int asked = 0;
	const ibSelectionLiveCheck host = [&](const ibValueFrame*) { ++asked; return true; };

	ibSelectionIsLive(Fake(3), host);
	ibSelectionIsLive(Fake(3), host);
	EXPECT_EQ(asked, 2);
}

TEST(DesignerSelection, Nothing_IsAskedAboutNoSelection)
{
	int asked = 0;
	const ibSelectionLiveCheck host = [&](const ibValueFrame*) { ++asked; return true; };

	EXPECT_FALSE(ibSelectionIsLive(nullptr, host));
	EXPECT_EQ(asked, 0);
}
