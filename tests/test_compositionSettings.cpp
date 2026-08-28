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

// ⭐ A SELECTED FIELD IS A ROW, NOT A STRING (2026-08-27). The table has a second kind of row —
// `Auto`, which is what "take the storey above" IS and has a POSITION among the fields — so a plain
// list of paths no longer says what a table holds. These tests state ordinary fields, so they say
// so once, here, rather than nine times.
std::vector<ibSelectedFieldDescription> Fields(std::initializer_list<wxString> paths)
{
	std::vector<ibSelectedFieldDescription> rows;
	for (const wxString& path : paths)
		rows.push_back(ibSelectedFieldDescription::Field(path));
	return rows;
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

// ⭐⭐ THE PILE IS WHAT `Auto` MEANS, AND IT HAS A POSITION (2026-08-27). It used to be unconditional
// — every storey added to the one above it, and there was no way to say otherwise. Now the table
// says it: an `Auto` ROW stands where the inherited fields come in, so a node can put what it
// inherits before, after or between its own.
TEST(ComposerSettings, Selected_AutoTakesTheStoreyAboveAtItsOwnPosition)
{
	ibDataDBComposer composer;
	composer.CommonSelected() = Fields({ wxT("Code"), wxT("Description") });

	ibDataComposer::Output& output = composer.Outputs().front();
	output.m_selected = { ibSelectedFieldDescription::Auto(),
	                      ibSelectedFieldDescription::Field(wxT("Date")) };

	const std::vector<wxString> selected = composer.SelectedFor(output);

	// ADDED, not replaced. Before 2026-08-24 an output naming one field made everything the report
	// was told to show disappear under it.
	ASSERT_EQ(3u, selected.size());
	EXPECT_EQ(wxT("Code"), selected[0]);
	EXPECT_EQ(wxT("Description"), selected[1]);
	EXPECT_EQ(wxT("Date"), selected[2]);
}

// ⭐⭐ …AND TAKING THE ROW OUT STATES A COMPOSITION OF ONE'S OWN. That is the whole of "hide what is
// above me": the node shows its own fields and nothing else, and its children then inherit from IT
// (Max, 2026-08-27). It is also where the saving is — the fields nobody shows do not reach the
// SELECT: *"the main thing we must end up with is that we shrink the SELECT itself"*.
TEST(ComposerSettings, Selected_WithoutAutoANodeStatesItsOwnComposition)
{
	ibDataDBComposer composer;
	composer.CommonSelected() = Fields({ wxT("Code"), wxT("Description") });

	ibDataComposer::Output& output = composer.Outputs().front();
	output.m_selected = Fields({ wxT("Date") });

	const std::vector<wxString> selected = composer.SelectedFor(output);
	ASSERT_EQ(1u, selected.size());
	EXPECT_EQ(wxT("Date"), selected[0]);
}

// ⚠ AND AN EMPTY TABLE INHERITS. Saying nothing is not the same as saying "nothing" — a node nobody
// has touched shows what the storey above shows, which is the state every node starts in.
TEST(ComposerSettings, Selected_AnUntouchedTableInherits)
{
	ibDataDBComposer composer;
	composer.CommonSelected() = Fields({ wxT("Code"), wxT("Description") });

	const std::vector<wxString> selected = composer.SelectedFor(composer.Outputs().front());
	ASSERT_EQ(2u, selected.size());
	EXPECT_EQ(wxT("Code"), selected[0]);
	EXPECT_EQ(wxT("Description"), selected[1]);
}

TEST(ComposerSettings, Selected_FieldNamedTwiceIsNamedOnce)
{
	ibDataDBComposer composer;

	// The same field on both storeys — and once in the base list itself, which is the case that
	// reached the server: "column FLD1022_TYPE was specified multiple times for derived table
	// Q_SUB0" (Firebird -104, measured 2026-08-24).
	composer.CommonSelected() = Fields({ wxT("Code"), wxT("Code") });
	composer.Outputs().front().m_selected = Fields({ wxT("Code") });

	const std::vector<wxString> selected = composer.SelectedFor(composer.Outputs().front());
	ASSERT_EQ(1u, selected.size());
	EXPECT_EQ(wxT("Code"), selected[0]);
}

// ⭐⭐ WHAT THE READ OWES IS NOT WHAT THE REPORT SHOWS. A level hides on a field, orders on a field
// and selects fields of its own — all three answered off the row already read — so all three are
// owed by the projection even though only the third is ever printed.
//
// 🛑 THE MISS WAS SILENT AND THAT IS THE POINT: a filter whose column is not in the schema hides
// nothing, a sort key not in the schema orders nothing. The setting stayed on screen and stopped
// meaning anything, which is why this is asserted rather than watched for.
TEST(ComposerSettings, Projection_OwesWhatEveryLevelNamesByName)
{
	ibDataDBComposer composer;
	composer.CommonSelected() = Fields({ wxT("Code") });

	ibDataComposer::Output& output = composer.Outputs().front();
	output.m_selected = { ibSelectedFieldDescription::Auto(),           // …and the report's own before it
	                      ibSelectedFieldDescription::Field(wxT("Date")) };

	ibDataComposer::GroupNode level;
	level.m_kind = ibCompositionLevelKind::Grouping;
	level.m_settings.m_group.Append(wxT("Partner"));
	level.m_selected = Fields({ wxT("Partner.Region") });               // shown by the level
	level.m_settings.m_filter.Append(wxT("Partner.IsActive"),
		ibComparisonKind_Equal, ibValue(true));                         // hidden on by the level
	level.m_settings.m_sort.Append(wxT("Partner.Rating"), /*ascending*/false);   // ordered on
	output.m_rowGroups.push_back(level);

	// What the report shows down to the output is unchanged — the level's fields are the LEVEL's.
	const std::vector<wxString> shown = composer.SelectedFor(output);
	ASSERT_EQ(2u, shown.size());
	EXPECT_EQ(wxT("Code"), shown[0]);
	EXPECT_EQ(wxT("Date"), shown[1]);

	// What the read owes carries all three, in the order they were said.
	const std::vector<wxString> owed = composer.ProjectionFor(output);
	ASSERT_EQ(5u, owed.size());
	EXPECT_EQ(wxT("Code"), owed[0]);
	EXPECT_EQ(wxT("Date"), owed[1]);
	EXPECT_EQ(wxT("Partner.Region"), owed[2]);
	EXPECT_EQ(wxT("Partner.IsActive"), owed[3]);
	EXPECT_EQ(wxT("Partner.Rating"), owed[4]);
}

// A SWITCHED-OFF LINE STILL OWES ITS COLUMN — `m_use` is a checkbox on a line already written, and
// turning it back on must not need a re-read to start meaning something.
TEST(ComposerSettings, Projection_OwesTheColumnOfASwitchedOffLine)
{
	ibDataDBComposer composer;
	ibDataComposer::Output& output = composer.Outputs().front();

	ibDataComposer::GroupNode level;
	level.m_settings.m_filter.Append(wxT("Partner.IsActive"),
		ibComparisonKind_Equal, ibValue(true), /*use*/false);
	output.m_rowGroups.push_back(level);

	const std::vector<wxString> owed = composer.ProjectionFor(output);
	ASSERT_EQ(1u, owed.size());
	EXPECT_EQ(wxT("Partner.IsActive"), owed[0]);
}

// ⭐ A LEVEL IS THE SAME THING ON EITHER AXIS. The columns of a cross-table hide, order and select
// exactly as its rows do, so the projection asks both — and asks them before there is anything to
// print across, because a read that owes only what it can already draw is a second thing to
// remember later.
TEST(ComposerSettings, Projection_AsksTheColumnAxisToo)
{
	ibDataDBComposer composer;
	ibDataComposer::Output& output = composer.Outputs().front();

	ibDataComposer::GroupNode rows;
	rows.m_settings.m_group.Append(wxT("Partner"));
	rows.m_selected = Fields({ wxT("Partner.Region") });
	output.m_rowGroups.push_back(rows);

	ibDataComposer::GroupNode columns;
	columns.m_settings.m_group.Append(wxT("Warehouse"));
	columns.m_selected = Fields({ wxT("Warehouse.Kind") });
	output.m_columnGroups.push_back(columns);

	const std::vector<wxString> owed = composer.ProjectionFor(output);
	ASSERT_EQ(2u, owed.size());
	EXPECT_EQ(wxT("Partner.Region"), owed[0]);
	EXPECT_EQ(wxT("Warehouse.Kind"), owed[1]);
}

// ===========================================================================
//  6-7. The description — round trip and equality
// ===========================================================================

TEST(CompositionDescription, RoundTrip_KeepsVariantStructure)
{
	ibCompositionDescription written;
	written.m_query = wxT("SELECT Code FROM Catalog.Products");
	written.m_selected = Fields({ wxT("Code") });

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
	narrowed.m_selected = Fields({ wxT("Code") });
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

// ⭐⭐ WHAT AN OUTPUT IS, IS A DECISION AND IS STORED. It used to be read off the content —
// `m_columnGroups.empty() ? Grouping : Table` — which answered "has a column axis been filled in",
// a different question. A table is ADDED empty and its two axes are undeletable, so it has to be a
// table before anything is in it.
TEST(CompositionDescription, AnEmptyTableIsStillATable)
{
	ibCompositionDescription written;
	written.m_variants[0].m_name = wxT("Main");

	ibOutputDescription table;
	table.m_kind = ibCompositionOutputKind::Table;   // added, not yet filled in
	written.m_variants[0].m_settings.m_structure.push_back(table);

	ibDataNode node;
	ASSERT_TRUE(ibCompositionDescriptionMemory::WriteNode(node, written));

	ibCompositionDescription read;
	ASSERT_TRUE(ibCompositionDescriptionMemory::ReadNode(node, read));

	ASSERT_EQ(1u, read.m_variants[0].m_settings.m_structure.size());
	const ibOutputDescription& back = read.m_variants[0].m_settings.m_structure[0];
	EXPECT_EQ(ibCompositionOutputKind::Table, back.m_kind);
	EXPECT_TRUE(back.m_rowGroups.empty());
	EXPECT_TRUE(back.m_columnGroups.empty());
}

// ⚠ A RECORD WRITTEN BEFORE THE KIND EXISTED reads back 0 — Grouping — and that is right for every
// output that could be authored then, except one: a table could not be SAID, but its column axis
// could be stored. So the content answers where the record is silent.
TEST(CompositionDescription, AnOlderRecordWithColumnsReadsBackAsATable)
{
	ibCompositionDescription written;
	written.m_variants[0].m_name = wxT("Main");

	ibOutputDescription output;                       // kind left at Grouping, as an old file has it
	ibLevelDescription columns;
	columns.m_settings.m_group.Append(wxT("Warehouse"));
	output.m_columnGroups.push_back(columns);
	written.m_variants[0].m_settings.m_structure.push_back(output);

	ibDataNode node;
	ASSERT_TRUE(ibCompositionDescriptionMemory::WriteNode(node, written));
	// …and the stored kind is scrubbed, standing in for a file that never had the property.
	ibCompositionDescription read;
	ASSERT_TRUE(ibCompositionDescriptionMemory::ReadNode(node, read));

	EXPECT_EQ(ibCompositionOutputKind::Table,
		read.m_variants[0].m_settings.m_structure[0].m_kind);
}

// A GROUPING STAYS A GROUPING. The rescue above must not fire on the ordinary output — it keys on a
// stored COLUMN axis, which a grouping never has.
TEST(CompositionDescription, AnOutputWithNoColumnAxisStaysAGrouping)
{
	ibCompositionDescription written;
	written.m_variants[0].m_name = wxT("Main");

	ibOutputDescription output;
	ibLevelDescription rows;
	rows.m_settings.m_group.Append(wxT("Partner"));
	output.m_rowGroups.push_back(rows);
	written.m_variants[0].m_settings.m_structure.push_back(output);

	ibDataNode node;
	ASSERT_TRUE(ibCompositionDescriptionMemory::WriteNode(node, written));
	ibCompositionDescription read;
	ASSERT_TRUE(ibCompositionDescriptionMemory::ReadNode(node, read));

	EXPECT_EQ(ibCompositionOutputKind::Grouping,
		read.m_variants[0].m_settings.m_structure[0].m_kind);
}

// ⭐⭐ A LEVEL CAN BE GROUPED BY PERIODS FROM THE SETTINGS WINDOW, and it is the same three parts the
// query text says: `BY <field> PERIODS(unit, from, to)`. Stored as TEXT, because a description goes
// to a file and an expression tree does not — what a person types is `&From`, a parameter.
TEST(ComposerSettings, Periods_AreStoredOnTheGroupingLine)
{
	ibCompositionDescription written;
	written.m_variants[0].m_name = wxT("Main");

	ibOutputDescription output;
	ibLevelDescription level;
	level.m_settings.m_group.Append(wxT("Date"));
	level.m_settings.m_group.m_lines[0].m_periods.m_unit = wxT("Month");
	level.m_settings.m_group.m_lines[0].m_periods.m_to   = wxT("&To");   // no lower bound stated
	output.m_rowGroups.push_back(level);
	written.m_variants[0].m_settings.m_structure.push_back(output);

	ibDataNode node;
	ASSERT_TRUE(ibCompositionDescriptionMemory::WriteNode(node, written));
	ibCompositionDescription read;
	ASSERT_TRUE(ibCompositionDescriptionMemory::ReadNode(node, read));

	const ibGroupPeriodsDescription& back =
		read.m_variants[0].m_settings.m_structure[0].m_rowGroups[0].m_settings.m_group.m_lines[0].m_periods;
	EXPECT_TRUE(back.IsOk());
	EXPECT_EQ(wxT("Month"), back.m_unit);
	EXPECT_TRUE(back.m_from.IsEmpty()) << "a bound nobody stated stays unstated";
	EXPECT_EQ(wxT("&To"), back.m_to);
}

// AN ORDINARY GROUPING LINE HAS NO PERIODICITY, and "is there any" is asked of the unit — a unit
// with no periodicity is nothing, and periodicity with no unit is impossible.
TEST(ComposerSettings, Periods_AnOrdinaryLineHasNone)
{
	ibGroupDescription group;
	group.Append(wxT("Partner"));
	EXPECT_FALSE(group.m_lines[0].m_periods.IsOk());
}

// ⭐⭐ AN OUTPUT NOBODY DECLARED ANYTHING ABOUT IS NOT PRINTED — because the settings tree does not
// show it either, and the two must agree. A composition is born with one output and keeps it, so a
// structure built beside it leaves that first one empty; printing it put a stray block of grand
// totals above the report, with nothing in the structure a person could click to remove it.
TEST(ComposerSettings, AnUndeclaredOutputIsNotRead)
{
	ibDataDBComposer composer;
	composer.Outputs().resize(2);

	ibDataComposer::GroupNode level;
	level.m_settings.m_group.Append(wxT("Partner"));
	composer.Outputs()[1].m_rowGroups.push_back(level);

	EXPECT_FALSE(composer.Declares(composer.Outputs()[0])) << "empty, and something else was declared";
	EXPECT_TRUE(composer.Declares(composer.Outputs()[1]));
}

// …AND THE LONE OUTPUT IS NOT THAT CASE. A composition nobody structured IS one empty output, and it
// means "the rows as they are" — every list and every plain report.
TEST(ComposerSettings, TheLoneEmptyOutputStillReads)
{
	ibDataDBComposer composer;
	ASSERT_EQ(1u, composer.Outputs().size());
	EXPECT_TRUE(composer.Declares(composer.Outputs().front()));
}

// A TABLE JUST ADDED IS EMPTY ON BOTH AXES and is still an output somebody declared — the kind is
// what says so, which is why it is stored (see AnEmptyTableIsStillATable).
TEST(ComposerSettings, AnEmptyTableCountsAsDeclared)
{
	ibDataDBComposer composer;
	composer.Outputs().resize(2);
	composer.Outputs()[1].m_kind = ibCompositionOutputKind::Table;

	EXPECT_TRUE(composer.Declares(composer.Outputs()[1]));
}

// ===========================================================================
//  The selects, and what a field is CALLED
// ===========================================================================

// ⭐⭐ A NAME IS FOR THE LANGUAGE, A TITLE IS FOR A READER. Nobody should have to type the second one
// out to get a report that can be read, so it is generated from the first: the capitals are where
// the words are (Max, 2026-08-26).
TEST(CompositionFields, ATitleIsGeneratedFromTheNameUntilSomebodySaysOtherwise)
{
	EXPECT_EQ(wxT("Data Version"), ibTitleFromName(wxT("DataVersion")));
	EXPECT_EQ(wxT("Number"),       ibTitleFromName(wxT("Number")));
	// A RUN OF CAPITALS IS ONE WORD — `IDNumber` is "ID Number", not "I D Number".
	EXPECT_EQ(wxT("ID Number"),    ibTitleFromName(wxT("IDNumber")));
	// …and something already written for a reader is left exactly as it is.
	EXPECT_EQ(wxT("Already Said"), ibTitleFromName(wxT("Already Said")));
	EXPECT_TRUE(ibTitleFromName(wxEmptyString).IsEmpty());
}

// ⭐ WHAT IS STORED IS THE DELTA. A query and its selects say everything by themselves; the table
// holds only what somebody added to that (Max: "so as not to clog the table"). So a description
// nobody touched writes nothing, and an untouched field is still titled.
TEST(CompositionFields, AnUntouchedCompositionStoresNothingAndStillTitlesItsFields)
{
	ibCompositionDescription composition;
	EXPECT_TRUE(composition.m_selects.empty());
	EXPECT_EQ(wxT("Data Version"), composition.TitleForPath(wxT("DataVersion")));
	// A PATH IS READ BY ITS LEAF — the walk to a field is not part of what the field is called.
	EXPECT_EQ(wxT("Contract"), composition.TitleForPath(wxT("Partner.Contract")));

	ibDataNode node;
	ibCompositionDescriptionMemory::WriteNode(node, composition);
	ibCompositionDescription read;
	ibCompositionDescriptionMemory::ReadNode(node, read);
	EXPECT_TRUE(read.m_selects.empty());   // nothing was said, so nothing was written
}

// …AND WHAT WAS SAID SURVIVES, under the select that said it.
TEST(CompositionFields, ATitleSomebodySetIsStoredAndReadBack)
{
	ibCompositionDescription composition;
	ibSelectDescription select;
	select.m_id = ibSelectDescription::NewId();
	ibFieldDescription field;
	field.m_path     = wxT("DataVersion");
	field.m_useTitle = true;
	field.m_title    = wxT("Version of the record");
	select.m_fields.push_back(field);
	composition.m_selects.push_back(select);

	EXPECT_EQ(wxT("Version of the record"), composition.TitleForPath(wxT("DataVersion")));

	ibDataNode node;
	ibCompositionDescriptionMemory::WriteNode(node, composition);
	ibCompositionDescription read;
	ibCompositionDescriptionMemory::ReadNode(node, read);

	ASSERT_EQ(1u, read.m_selects.size());
	EXPECT_EQ(select.m_id, read.m_selects.front().m_id);          // the identity is what paths refer to
	EXPECT_EQ(wxT("Version of the record"), read.TitleForPath(wxT("DataVersion")));
}

// ⭐⭐ TWO SELECTS MAKE THE NAME COMPULSORY — that is the whole job of `ONTO`. With one select an
// unqualified path can only mean it; with two it names nothing in particular, and the field falls
// back to being titled by its own name rather than picking up a stranger's caption.
TEST(CompositionFields, AnUnqualifiedPathNamesNothingOnceThereAreTwoSelects)
{
	ibCompositionDescription composition;

	ibSelectDescription sales;
	sales.m_id   = ibSelectDescription::NewId();
	sales.m_name = wxT("Sales");
	ibFieldDescription qty;
	qty.m_path = wxT("Qty"); qty.m_useTitle = true; qty.m_title = wxT("Sold");
	sales.m_fields.push_back(qty);

	ibSelectDescription stock;
	stock.m_id   = ibSelectDescription::NewId();
	stock.m_name = wxT("Stock");
	ibFieldDescription onHand;
	onHand.m_path = wxT("Qty"); onHand.m_useTitle = true; onHand.m_title = wxT("On hand");
	stock.m_fields.push_back(onHand);

	composition.m_selects.push_back(sales);
	composition.m_selects.push_back(stock);

	// The SAME word in two selects is two fields, and each keeps its own caption.
	EXPECT_EQ(wxT("Sold"),    composition.TitleForPath(wxT("Sales.Qty")));
	EXPECT_EQ(wxT("On hand"), composition.TitleForPath(wxT("Stock.Qty")));
	// …and unqualified, it is neither: the name read out loud, not a guess between them.
	EXPECT_EQ(wxT("Qty"),     composition.TitleForPath(wxT("Qty")));

	// ⭐ A QUALIFIER IS MATCHED BY IDENTITY FIRST — so a path that carries the id finds its select
	// whatever the select is currently called. That is the property a rename has to preserve.
	EXPECT_EQ(wxT("Sold"), composition.TitleForPath(sales.m_id + wxT(".Qty")));
}

// ⭐⭐ A RENAME IS ONE WRITE, and everything that referred to the select BY ID still does. The name
// is what the select is rendered with; the id is what it IS.
//
// ⚠ WHAT THIS DOES NOT YET COVER: a path stored as TEXT still says the old name, because that text
// is what goes into the query. Closing that is its own arc — see RenameSelect.
TEST(CompositionFields, RenamingASelectKeepsEveryReferenceThatHoldsItsId)
{
	ibCompositionDescription composition;
	ibSelectDescription sales;
	sales.m_id   = ibSelectDescription::NewId();
	sales.m_name = wxT("Sales");
	ibFieldDescription qty;
	qty.m_path = wxT("Qty"); qty.m_useTitle = true; qty.m_title = wxT("Sold");
	sales.m_fields.push_back(qty);
	composition.m_selects.push_back(sales);

	ASSERT_TRUE(composition.RenameSelect(sales.m_id, wxT("Turnover")));

	EXPECT_EQ(wxT("Sold"), composition.TitleForPath(sales.m_id + wxT(".Qty")));   // by identity — unchanged
	EXPECT_EQ(wxT("Sold"), composition.TitleForPath(wxT("Turnover.Qty")));        // …and by the new word
	EXPECT_FALSE(composition.RenameSelect(ibSelectDescription::NewId(), wxT("Nobody")));
}
