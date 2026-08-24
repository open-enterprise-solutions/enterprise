// L5 — WHAT A COMPOSER COMPOSES ON, and the description it is made of.
//
// PURE: no database, no appData, no GUI. Everything here is decided by ibDataComposer and
// ibCompositionDescription alone, which is what makes it worth asserting — the arc of 2026-08-24
// cost a day precisely because these rules lived only in people's heads and in a UI that was hard
// to drive.
//
// ⭐⭐ THE RULE, in one sentence: **the reader's saved setting composes when there is one, and
// `m_variants[0]`'s does when there is not.** No cursor, no flag, no "author's section" — the
// composer holds the variants (const, only ever copied out of) and one setting a reader saved, and
// `IsOk()` on that setting is the whole of "is there one".
//
// What is proven:
//   1. A SAVED SETTING COMPOSES WHOLE and the zeroth is dropped entire — and the imperative doors
//      (`Filter`, `Sort`) therefore start from a COPY of what composes, so stating one thing does
//      not silently drop the rest.
//   2. CLEARING THE READER'S SETTING IS THE RESET. No restore step, nothing remembered.
//   3. LOADING THE VARIANTS DOES NOT TOUCH THE READER'S SETTING. A source rebuild restates the
//      array every time — the defect that made a person's settings come back to the old ones a
//      second after they pressed OK.
//   4. SELECTED FIELDS PILE UP — composition + output + node, deduped. Replacing was the old
//      behaviour and it silently dropped what a storey above had asked for.
//   5. A FIELD NAMED TWICE IS NAMED ONCE — the guard behind Firebird's -104, "column … was
//      specified multiple times".
//   6. A DESCRIPTION SURVIVES A ROUND TRIP with its variant and that variant's structure.
//   7. EQUALITY NOTICES A CHANGED STRUCTURE — equality is how "modified" is decided, and a member
//      left out of it is a form that never saves.
//   8. VARIANT ZERO IS WHAT COMPOSES while nobody has saved a setting, whatever else is authored
//      beside it. There is no stored "active" index: at runtime there is no chosen variant at all,
//      and picking one will simply BE saving a setting.
//   9. THERE IS ALWAYS AT LEAST ONE VARIANT — by construction, and again after reading a record
//      that has none. `[0]` is handed out as the front element.
//  10. STATING AN ORDER SETS THE READER'S SORT — one store, so a heading click, a script and the
//      keyset anchor cannot disagree with the ORDER BY.
//  11. STATING A FILTER SETS THE READER'S FILTER — a declared line and a typed one are one fact.
//  12. THE SCOPE OF ONE FETCH IS NOT A SETTING — pushed, ANDed, popped, and the reader's own
//      setting is the same on both sides of it.

#include <gtest/gtest.h>

#include "backend/composition/dataComposer.h"     // ibDataDBComposer — the two sections live on the base
#include "backend/compositionDescription.h"       // the description + its Memory (read/write) pair
#include "backend/serialize/dataBuilder.h"        // ibDataNode — what a description is written into

namespace {

ibSettingsDescription MakeFilterOn(const wxString& path)
{
	ibSettingsDescription settings;
	ibFilterNodeDescription node;
	node.m_kind = ibFilterNodeKind_Condition;
	node.m_left.m_path = path;
	settings.m_filter.m_nodes.push_back(node);
	return settings;
}

// ⭐ WHAT THE AUTHOR DECLARED — variant ZERO, driven in as the whole array. There is no "author's
// section" to set: the composer holds the variants and composes on `[0]` until a reader saves a
// setting of their own (Max, 2026-08-24).
void DeclareZeroth(ibDataDBComposer& composer, const ibSettingsDescription& settings)
{
	ibVariantDescription zeroth;
	zeroth.m_name = wxT("Main");
	zeroth.m_settings = settings;
	composer.LoadVariants({ zeroth });
}

ibSettingsDescription MakeSortOn(const wxString& path, bool ascending = true)
{
	ibSettingsDescription settings;
	settings.m_sort.Append(path, ascending);
	return settings;
}

}   // namespace

// ===========================================================================
//  1. What composes — the reader's setting, or variant ZERO
// ===========================================================================

// ⭐⭐ THE READER'S SETTING IS A PLACE WHERE VALUES ARE PUT, AND EACH PART ANSWERS FOR ITSELF: what
// they put there composes, and while a part is empty the zeroth variant's composes (Max, 2026-08-25).
//
// 🛑 It read "a saved setting is the WHOLE setting, variant zero is dropped entire" (2026-08-24) —
// one part of the reader's setting deciding for the other three. Under it, saving a filter alone
// silently threw away the order the author set up.
TEST(ComposerSettings, InForce_ReadersPartComposesOverTheZeroth)
{
	ibDataDBComposer composer;
	DeclareZeroth(composer, MakeSortOn(wxT("Date")));
	composer.SetUserSettingsDesc(MakeFilterOn(wxT("Partner")));

	// Their filter composes…
	ASSERT_TRUE(composer.GetCurrentFilterDesc().IsOk());
	ASSERT_EQ(1u, composer.GetCurrentFilterDesc().m_nodes.size());
	EXPECT_EQ(wxT("Partner"), composer.GetCurrentFilterDesc().m_nodes[0].m_left.m_path);

	// …and so does the ZEROTH'S ORDER, because the reader put no order anywhere. Saying something
	// about the filter is not saying anything about the sort.
	ASSERT_EQ(1u, composer.GetCurrentSortDesc().m_lines.size());
	EXPECT_EQ(wxT("Date"), composer.GetCurrentSortDesc().m_lines[0].m_path);
}

// ⭐ …WHICH IS WHY THE IMPERATIVE DOORS START FROM A COPY. A column heading clicked or `Filter()`
// from a script states ONE thing, and it must not silently drop everything else the report was
// composing on.
TEST(ComposerSettings, InForce_StatingOneThingKeepsTheRest)
{
	ibDataDBComposer composer;
	DeclareZeroth(composer, MakeSortOn(wxT("Date")));

	composer.Filter(wxT("Partner"), wxT("="), ibValue(true));

	ASSERT_EQ(1u, composer.GetCurrentFilterDesc().m_nodes.size());
	ASSERT_EQ(1u, composer.GetCurrentSortDesc().m_lines.size());
	EXPECT_EQ(wxT("Date"), composer.GetCurrentSortDesc().m_lines[0].m_path)
		<< "stating a filter must not throw away the order the report was composing on";
}

TEST(ComposerSettings, InForce_UserPartReplacesAuthorPartWhole)
{
	ibDataDBComposer composer;
	DeclareZeroth(composer, MakeSortOn(wxT("Date")));

	// The reader sorts by something else — theirs REPLACES, it is not merged. Half of one setting
	// beside half of another is a setting nobody wrote.
	composer.SetUserSettingsDesc(MakeSortOn(wxT("Number"), false));

	ASSERT_EQ(1u, composer.GetCurrentSortDesc().m_lines.size());
	EXPECT_EQ(wxT("Number"), composer.GetCurrentSortDesc().m_lines[0].m_path);
	EXPECT_FALSE(composer.GetCurrentSortDesc().m_lines[0].m_ascending);
}

TEST(ComposerSettings, InForce_NeitherSaidAnything)
{
	ibDataDBComposer composer;
	EXPECT_FALSE(composer.GetCurrentFilterDesc().IsOk());
	EXPECT_FALSE(composer.GetCurrentSortDesc().IsOk());
	EXPECT_FALSE(composer.GetCurrentGroupDesc().IsOk());
}

// ===========================================================================
//  2. Reset — an empty user section, and nothing else
// ===========================================================================

TEST(ComposerSettings, Reset_EmptyUserSectionFallsBackToAuthor)
{
	ibDataDBComposer composer;
	DeclareZeroth(composer, MakeSortOn(wxT("Date")));
	composer.SetUserSettingsDesc(MakeSortOn(wxT("Number")));
	ASSERT_EQ(wxT("Number"), composer.GetCurrentSortDesc().m_lines[0].m_path);

	// "Back to the defaults" IS this line. No restore step, nothing remembered, no second mechanism.
	composer.ClearUserSettings();

	ASSERT_EQ(1u, composer.GetCurrentSortDesc().m_lines.size());
	EXPECT_EQ(wxT("Date"), composer.GetCurrentSortDesc().m_lines[0].m_path);
}

// ===========================================================================
//  3. The author's section is not the reader's
// ===========================================================================

TEST(ComposerSettings, RestatingTheAuthorDoesNotTouchTheReader)
{
	ibDataDBComposer composer;
	composer.SetUserSettingsDesc(MakeFilterOn(wxT("Partner")));

	// Rebuilding a source restates what the AUTHOR declared — every time, because a rebuild builds
	// the composer from scratch. It must not reach the reader's section.
	//
	// 🛑 THIS IS THE REGRESSION. The declared settings were driven into the USER slot for a few
	// hours on 2026-08-24: a person accepted the settings window, the next rebuild ran, and their
	// filter was replaced by the author's — "I press OK, reopen, and see the old settings".
	DeclareZeroth(composer, MakeSortOn(wxT("Date")));

	ASSERT_TRUE(composer.GetUserSettingsDesc().m_filter.IsOk());
	EXPECT_EQ(wxT("Partner"), composer.GetUserSettingsDesc().m_filter.m_nodes[0].m_left.m_path);
	EXPECT_EQ(wxT("Partner"), composer.GetCurrentFilterDesc().m_nodes[0].m_left.m_path);
}

// ===========================================================================
//  4-5. Selected fields — the pile, and the dedupe behind -104
// ===========================================================================

TEST(ComposerSettings, Selected_PilesUpCompositionThenOutput)
{
	ibDataDBComposer composer;
	composer.CommonSelected() = { wxT("Code"), wxT("Description") };

	ibDataComposer::Output& output = composer.Outputs().front();
	output.m_selected = { wxT("Date") };

	const std::vector<wxString> selected = composer.SelectedFor(output);

	// ADDED, not replaced. Before 2026-08-24 an output naming one field made everything the report
	// was told to show disappear under it.
	ASSERT_EQ(3u, selected.size());
	EXPECT_EQ(wxT("Code"), selected[0]);
	EXPECT_EQ(wxT("Description"), selected[1]);
	EXPECT_EQ(wxT("Date"), selected[2]);
}

TEST(ComposerSettings, Selected_FieldNamedTwiceIsNamedOnce)
{
	ibDataDBComposer composer;

	// The same field on both storeys — and once in the base list itself, which is the case that
	// reached the server: "column FLD1022_TYPE was specified multiple times for derived table
	// Q_SUB0" (Firebird -104, measured 2026-08-24).
	composer.CommonSelected() = { wxT("Code"), wxT("Code") };
	composer.Outputs().front().m_selected = { wxT("Code") };

	const std::vector<wxString> selected = composer.SelectedFor(composer.Outputs().front());
	ASSERT_EQ(1u, selected.size());
	EXPECT_EQ(wxT("Code"), selected[0]);
}

// ===========================================================================
//  6-7. The description — round trip and equality
// ===========================================================================

TEST(CompositionDescription, RoundTrip_KeepsVariantStructure)
{
	ibCompositionDescription written;
	written.m_query = wxT("SELECT Code FROM Catalog.Products");
	written.m_selected = { wxT("Code") };

	// ⭐ THE AUTHOR'S SETTINGS ARE VARIANT ZERO, and a composition is born with it — so this fills
	// the one that is there rather than adding a second (Max, 2026-08-24). Pushing made the report
	// two variants deep, with the structure in the one a run never looks at.
	ASSERT_EQ(1u, written.m_variants.size());
	written.m_variants[0].m_name = wxT("Main");

	ibOutputDescription output;
	ibLevelDescription level;
	level.m_kind = ibCompositionLevelKind::Grouping;
	level.m_settings.m_group.Append(wxT("Partner"));
	output.m_rowGroups.push_back(level);
	written.m_variants[0].m_settings.m_structure.push_back(output);

	ibDataNode node;
	ASSERT_TRUE(ibCompositionDescriptionMemory::WriteNode(node, written));

	ibCompositionDescription read;
	ASSERT_TRUE(ibCompositionDescriptionMemory::ReadNode(node, read));

	// A LEVEL WENT MISSING HERE on 2026-08-24 and was hunted through three layers before the
	// journal said the structure had never left the window. This is the assertion that would have
	// answered it in a second.
	ASSERT_EQ(1u, read.m_variants.size());
	ASSERT_EQ(1u, read.m_variants[0].m_settings.m_structure.size());
	ASSERT_EQ(1u, read.m_variants[0].m_settings.m_structure[0].m_rowGroups.size());
	ASSERT_EQ(1u, read.m_variants[0].m_settings.m_structure[0].m_rowGroups[0].m_settings.m_group.m_lines.size());
	EXPECT_EQ(wxT("Partner"),
		read.m_variants[0].m_settings.m_structure[0].m_rowGroups[0].m_settings.m_group.m_lines[0].m_path);
	EXPECT_EQ(written.m_query, read.m_query);
	EXPECT_EQ(written.m_selected, read.m_selected);
}

TEST(CompositionDescription, Equality_NoticesAChangedStructure)
{
	ibCompositionDescription before;
	before.m_variants[0].m_name = wxT("Main");

	ibCompositionDescription after = before;
	EXPECT_TRUE(before == after);

	// ⭐ EQUALITY IS HOW "MODIFIED" IS DECIDED — a property asks it to tell a change from a
	// re-open, and a member left out of it is an edit that leaves the configuration looking
	// untouched, with Save having nothing to do.
	ibOutputDescription output;
	after.m_variants[0].m_settings.m_structure.push_back(output);
	EXPECT_TRUE(before != after);

	// …and the composition-wide selected set is in it too, for the same reason.
	ibCompositionDescription narrowed = before;
	narrowed.m_selected = { wxT("Code") };
	EXPECT_TRUE(before != narrowed);
}

// ===========================================================================
//  8-9. Variants — the author's settings, and the invariant under them
// ===========================================================================

// ⭐⭐ THE AUTHOR'S SETTING IS VARIANT ZERO (Max, 2026-08-24). Not "the active one" — there is no
// active one to store: which variant a reader chose is a frontend setting, and choosing one puts it
// in the composer's USER section rather than moving anything here.
TEST(CompositionDescription, AuthorSettingsAreVariantZero)
{
	ibCompositionDescription desc;
	desc.m_variants.emplace_back();          // a second one the designer authored
	desc.m_variants[0].m_name = wxT("Main");
	desc.m_variants[1].m_name = wxT("With profitability");

	desc.m_variants[0].m_settings.m_sort.Append(wxT("Code"), /*ascending*/true);
	desc.m_variants[1].m_settings.m_sort.Append(wxT("Profit"), /*ascending*/true);

	// The description hands out the FIRST one, whatever else is authored beside it…
	ASSERT_EQ(1u, desc.GetCompositionSettingsDesc().m_sort.m_lines.size());
	EXPECT_EQ(wxT("Code"), desc.GetCompositionSettingsDesc().m_sort.m_lines[0].m_path);

	// …and it survives a round trip through the file, second variant and all.
	ibDataNode node;
	ASSERT_TRUE(ibCompositionDescriptionMemory::WriteNode(node, desc));
	ibCompositionDescription read;
	ASSERT_TRUE(ibCompositionDescriptionMemory::ReadNode(node, read));

	ASSERT_EQ(2u, read.m_variants.size());
	ASSERT_EQ(1u, read.GetCompositionSettingsDesc().m_sort.m_lines.size());
	EXPECT_EQ(wxT("Code"), read.GetCompositionSettingsDesc().m_sort.m_lines[0].m_path);
	EXPECT_EQ(wxT("With profitability"), read.m_variants[1].m_name);
}

// 🛑 THERE IS ALWAYS AT LEAST ONE. `GetCompositionSettingsDesc` hands out the front element, so an
// empty vector would be a read past the end — and a report with no variant has nowhere to keep its
// settings at all. A record can say anything; the invariant is ours.
TEST(CompositionDescription, ReadAlwaysLeavesOneVariant)
{
	ibCompositionDescription born;
	EXPECT_EQ(1u, born.m_variants.size());   // …by construction, before anything is read

	// A NODE WITH NO VARIANT CHILDREN AT ALL — what a record written before variants existed looks
	// like, and what an empty vector would write.
	ibDataNode empty;
	ibCompositionDescription read;
	read.m_variants.clear();
	ASSERT_TRUE(ibCompositionDescriptionMemory::ReadNode(empty, read));
	ASSERT_EQ(1u, read.m_variants.size());
	EXPECT_TRUE(read.GetCompositionSettingsDesc().m_sort.m_lines.empty());
}

// ===========================================================================
//  10-12. One store per question — the order, the filter, and the fetch's scope
// ===========================================================================

// ⭐⭐ STATING AN ORDER IS SETTING THE READER'S SORT. The imperative door and the settings section
// used to be two stores, and the render preferred the section — so on a list that had a sort setting
// a column-heading click moved the arrow and nothing else, `AddSort` from a script did nothing, and
// `ValueTable.Sort` did nothing. The keyset anchor is the fourth: it was built from the flat store
// while the SQL ordered by the setting. All four read through here now.
TEST(ComposerSettings, Sort_StatesTheReadersOrder)
{
	ibDataDBComposer composer;
	DeclareZeroth(composer, MakeSortOn(wxT("Date")));

	composer.ClearSorts();
	composer.Sort(wxT("Code"), /*ascending*/false);

	// What is IN FORCE is what was just stated — not the author's, which the reader has now replaced.
	ASSERT_EQ(1u, composer.SortCount());
	wxString path; bool ascending = true;
	ASSERT_TRUE(composer.GetSortAt(0, path, ascending));
	EXPECT_EQ(wxT("Code"), path);
	EXPECT_FALSE(ascending);
	ASSERT_EQ(1u, composer.GetUserSettingsDesc().m_sort.m_lines.size());
	EXPECT_EQ(wxT("Code"), composer.GetUserSettingsDesc().m_sort.m_lines[0].m_path);

	// …and taking the reader's order back out leaves the author's standing: with nothing in that part
	// of the reader's setting, the zeroth's is what composes.
	composer.ClearSorts();
	ASSERT_EQ(1u, composer.SortCount());
	ASSERT_TRUE(composer.GetSortAt(0, path, ascending));
	EXPECT_EQ(wxT("Date"), path);
}

// ⭐ AND SO IS STATING A FILTER. A filter a script declares and a filter a person types are the same
// fact; the first used to live in a store the settings window could not see.
TEST(ComposerSettings, Filter_StatesTheReadersFilter)
{
	ibDataDBComposer composer;
	composer.Filter(wxT("IsFolder"), wxT("="), ibValue(true));

	ASSERT_TRUE(composer.GetCurrentFilterDesc().IsOk());
	ASSERT_EQ(1u, composer.GetUserSettingsDesc().m_filter.m_nodes.size());
	const ibFilterNodeDescription& node = composer.GetUserSettingsDesc().m_filter.m_nodes[0];
	EXPECT_EQ(wxT("IsFolder"), node.m_left.m_path);
	EXPECT_EQ(ibComparisonKind_Equal, node.m_comparison);

	// The operator arrives as TEXT and is stored as a KIND — one mapping, so a spelling cannot reach
	// the renderer as a word it has no meaning for.
	composer.Filter(wxT("Date"), wxT(">="), ibValue(true));
	ASSERT_EQ(2u, composer.GetUserSettingsDesc().m_filter.m_nodes.size());
	EXPECT_EQ(ibComparisonKind_GreaterEqual,
		composer.GetUserSettingsDesc().m_filter.m_nodes[1].m_comparison);
}

// 🛑 THE SCOPE OF ONE FETCH IS NOT A SETTING. The drilled parent and the primary key of a point
// query are pushed before a read and popped after it — putting them in the reader's section would
// mean drilling into a folder cost the reader their filter, and popping put back a filter they
// never wrote.
TEST(ComposerSettings, Scope_IsNotASettingAndPopsBack)
{
	ibDataDBComposer composer;
	composer.Filter(wxT("Partner"), wxT("="), ibValue(true));
	ASSERT_EQ(1u, composer.GetUserSettingsDesc().m_filter.m_nodes.size());

	const ibDataComposer::SettingsScope scope = composer.MarkScope();
	composer.ScopeTo(wxT("Parent"), wxT("="), ibValue(true));
	EXPECT_EQ(1u, composer.ScopeCount());
	// …and the reader's setting is untouched by it.
	EXPECT_EQ(1u, composer.GetUserSettingsDesc().m_filter.m_nodes.size());

	composer.RestoreScope(scope);
	EXPECT_EQ(0u, composer.ScopeCount());
	EXPECT_EQ(1u, composer.GetUserSettingsDesc().m_filter.m_nodes.size());
}
