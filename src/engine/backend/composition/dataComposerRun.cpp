////////////////////////////////////////////////////////////////////////////
//	Description : the composer READING — execute, and the walk that hands a
//	              driver what it read (dataComposer.h)
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ THE ASPECT SPLIT (2026-08-28). The composer was two files of four thousand lines holding four
// different subjects: the settings vocabulary, what is SHOWN, the TEXT it writes, and the READ. This
// file is the last of the four — one query executed, and one walk over what came back, handing each
// node to the driver that output was given.
//
// It is also the biggest, and that is the point of cutting here: a walk is a walk, and nothing in it
// has to know how a `TOTALS` clause is spelled.

#include "dataComposerInternal.h"

#include "backend/query/queryParser.h"        // ibQueryParser — text -> AST
#include "backend/query/queryable.h"          // ibBackendQueryable / ibBackendQueryColumn
#include "backend/query/queryableFactory.h"   // the source factory — the column dictionary
#include "backend/query/dataQueryBuilder.h"   // ibDataQueryResult / ibSelectKind
#include "backend/query/querySelector.h"      // ibSelector — the TOTALS pre-order walk
#include "backend/appData.h"                  // ibApplicationData::GetQueryableFactory
#include "backend/metaData.h"                 // ibMetaData::GetSourceFactory
#include "backend/backend_exception.h"        // ibBackendCoreException
#include "backend/query/queryReadState.h"     // ibQueryReadState — one build, one state of the data
#include "backend/query/queryRender.h"        // ibQueryColumnFromPath
#include "backend/query/queryKeywords.h"      // ibQueryKeywordText
#include "backend/query/queryLexer.h"         // ibQueryLexer::IsIdentifier

ibDataQueryResult ibDataDBComposer::Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals)
{
	return Execute(schema, hasTotals, ibReadPageRequest{});
}

// THE TWO SHORT FORMS forward with a LOCAL: a caller that does not ask whether the server grouped is
// a caller that does not care, and the answer dies with the call rather than waiting on the object
// for somebody it was not about.
ibDataQueryResult ibDataDBComposer::Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals,
                                          const ibReadPageRequest& page)
{
	bool serverGrouped = false;
	return Execute(schema, hasTotals, page, serverGrouped);
}

ibDataQueryResult ibDataDBComposer::Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals,
                                          const ibReadPageRequest& page, bool& serverGrouped)
{
	EnsureAst();

	// The auxiliary registry of transient (RAM / temp) sources is live for THIS execution: the
	// lowering resolves the rendered "FROM Temp.t0" directly to the registered queryable. Source
	// resolution happens entirely inside the lowering call below, so this scope covers it; the
	// returned result holds the queryable already bound (no re-resolution during the row walk).
	// ⭐ THE PREPARED TABLES ARE PART OF THE REGISTRY, and they are made before the first read of
	// this text — a named selection is declared to the server as `WITH`, and what stands inside it
	// resolves at that moment (Max: INTO is for the ONTO selections to use).
	EnsureTempTables();
	ibTempSourceScope tempScope(m_prepared.m_sources);
	// Thread THIS query's config into the lowering (parallel to the temp-source scope) — ResolveSource resolves a
	// by-name metaobject source against it, not the global factory.
	ibSourceMetaDataScope mdScope(m_metaData);
	// ⭐ …AND THE SELECTIONS THIS COMPOSER'S QUERY NAMED, for the same reason and by the same means: a
	// `FROM Sales` in the statement below is a NAMED RESULT, and the lowering decides per source
	// whether to declare it to the server (`WITH Sales AS (…)`) or take its rows. Empty for a
	// composition over one query, which is every composition that names nothing.
	ibNamedResultScope namedScope(m_rendered.m_named);

	hasTotals     = m_rendered.m_ast->m_hasTotals;
	serverGrouped = false;
	if (hasTotals) {
		// Pass THIS fetch's page so a single-scalar-dim TOTALS drill can page its groups server-side
		// (`serverGrouped` then tells the walk to emit the flat groups at level 1, skipping the fold).
		//
		// ⭐⭐ A PAGE MEANS A LEVEL, AND A LEVEL HAS NO ROWS IN IT. This is the line where a RESULT and
		// a DRILL stop being the same question. A report asks for the whole thing and its rows are the
		// bottom of it — always, unconditionally, that is what a total is made of. A browsed list asks
		// for ONE FLOOR at a time: RunComposerPage renders just the browsed level's TotalBy, and the
		// rows under the deepest heading arrive as their own flat fetch with a parent filter, not as
		// detail nodes hanging off this one ("a list drills through headings and its rows ARE its
		// detail" — the note above TakeGroups, which is where this became visible).
		//
		// Asking for details here cost everything and bought nothing: details and the DBMS's own fold
		// are exclusive, so every page of a grouped list fell back to reading the WHOLE table and
		// folding it in memory, to show twenty headings. The page was never applied to the detail read
		// either, so it was the whole table per page. On a register of a million rows that is not a
		// slow list, it is an unusable one.
		//
		// ⚠ THE TEST IS THE PAGE, not the caller. A door that asked "am I a report or a list?" would
		// be answering by who is calling instead of by what was asked for, and the next caller would
		// have to be added to it by hand.
		const bool wholeResult = (page.m_count == 0);
		return ibQueryLowering::ExecuteTotals(*m_rendered.m_ast, m_params, schema, page, &serverGrouped,
			wholeResult && WantsDetails(Root()), LayoutFor(Root()));
	}

	wxString signature;
	if (BuildPageSignature(page, signature)) {
		if (!m_pageCache)
			m_pageCache = ibDataQueryBuilder::NewPageCache();
		return ibQueryLowering::Execute(*m_rendered.m_ast, m_params, schema, page, *m_pageCache, signature);
	}
	return ibQueryLowering::Execute(*m_rendered.m_ast, m_params, schema, page);
}

// ⭐ WHICH NODE KINDS AN OUTPUT WRITES. Asked BY KIND rather than by a yes/no flag, so the walk and
// the ladder speak one vocabulary: a node knows what it is (ibSelectorNodeKind), a level knows what
// it declares (ibCompositionLevelKind), and this is the one place the two are matched up.
//
// Headings are what a fold is FOR, so an output always writes them. Rows are written by an output
// whose ladder names them — the level a person adds when the report should print what it counted.
// It lives here, beside the walk, because it is a question about traversal and not a property of
// the output; `WantsDetails` in the header answers the other question, whether they are READ, and
// that one is now always yes.
static bool OutputWrites(const ibDataComposer::Output& output, ibSelectorNodeKind kind)
{
	if (kind != ibSelectorNodeKind::Detail)
		return true;
	// EITHER AXIS MAY NAME THEM (Max, 2026-08-25: "detail records can be on the rows as well as on
	// the groupings"). A table whose columns end in detail records is asking for its resources laid
	// out across the page, which is the plainest cross-table there is.
	//
	// 🛑 AND IT ASKED THE OLD QUESTION — `m_kind == Details` — while the read had already learnt the
	// new one. So the fold produced 126 nodes, the rows were there, and this said "this output does
	// not write records": the sheet printed one total line over 125 invisible rows (Max, 2026-08-28,
	// live, third screenshot). THREE places asked "is this the records" and I had fixed two.
	for (const std::vector<ibDataComposer::GroupNode>* axis : { &output.m_rowGroups, &output.m_columnGroups })
		for (const ibDataComposer::GroupNode& level : *axis)
			if (level.IsDetailRecords())
				return true;
	return false;
}

// ⭐ A LEVEL'S FILTER HIDES, IT DOES NOT DROP (Max, the outputs arc: "a filter on the output THROWS
// AWAY, one on a level HIDES"). So it is answered here, on the walk, against the row already read —
// and it is answered from the stored DESCRIPTION, which is a tree of conditions and groups.
//
// 🛑 IT USED TO READ A FLAT LIST NOBODY FILLED. The level carried a `std::vector<FilterItem>` beside
// its filter description, and the composer's Filter() writes the COMPOSITION-wide one — so no line
// ever landed there and this function could only ever answer yes. A level's filter was editable,
// saved, and did nothing.
static bool ibLevelNodeShows(const ibFilterNodeDescription& node,
	const std::vector<ibQueryLowering::OutputColumn>& schema, const std::vector<ibValue>& row)
{
	if (!node.m_use)
		return true;   // switched off reads as if it were not written

	if (node.m_kind == ibFilterNodeKind_Group) {
		// AND is "every child agrees", OR is "some child does" — and an empty group narrows nothing,
		// which is why the OR case starts from `false` only when it has something to ask.
		if (node.m_children.empty())
			return true;
		const bool isOr = (node.m_groupKind == ibFilterGroupKind_Or);
		for (const ibFilterNodeDescription& child : node.m_children) {
			const bool shows = ibLevelNodeShows(child, schema, row);
			if (isOr && shows)   return true;
			if (!isOr && !shows) return false;
		}
		return !isOr;
	}

	// A CONDITION NAMES AN OUTPUT COLUMN — the same names a person picked from. A name this result
	// does not carry cannot hide anything: it says nothing about the rows in hand, and hiding on it
	// would be hiding for a reason nobody can see.
	if (!node.m_left.IsField())
		return true;
	size_t at = schema.size();
	for (size_t i = 0; i < schema.size(); ++i) {
		if (schema[i].m_name.IsSameAs(node.m_left.m_path, false)
		    || schema[i].m_alias.IsSameAs(node.m_left.m_path, false)) {
			at = i;
			break;
		}
	}
	if (at >= schema.size() || at >= row.size())
		return true;

	// The right-hand side is a VALUE here. A field-to-field comparison is the query's business —
	// both sides are columns and the server already answered it.
	if (node.m_right.IsField())
		return true;
	return ibCompositionCompare(row[at], node.m_comparison, node.m_right.m_value);
}

bool ibDataDBComposer::LevelShows(const Output& output, int depth, ibSelectorNodeKind kind,
	const std::vector<ibQueryLowering::OutputColumn>& schema, const std::vector<ibValue>& row) const
{
	// Depth 0 is the grand total and belongs to no level; past the last level of either axis there
	// is nothing left to hide by.
	const GroupNode* found = LevelAt(output, depth, kind);
	if (found == nullptr)
		return true;

	const GroupNode& level = *found;
	for (const ibFilterNodeDescription& node : level.m_settings.m_filter.m_nodes)
		if (!ibLevelNodeShows(node, schema, row))
			return false;
	return true;
}

// ⭐⭐ THE ORDER ONE LEVEL'S HEADINGS COME IN — and it is read the same way its filter is, off the
// level a person set it on.
//
// 🛑 IT WAS PROMISED AND NEVER WRITTEN. The settings window points its sort editor at the SELECTED
// node (composerSettings.cpp), so a sort set on a grouping was stored, serialised and carried
// through variants — and the walk read only the filter, so nothing about it ever showed. The
// comment beside the query's ORDER BY said this was "applied on the walk"; this is that.
//
// A key names an output the same way a filter's left side does: by the name a person picked from.
// One that this result does not carry orders nothing — the same answer the filter gives, and for
// the same reason: a report with a stale line still prints.
std::vector<ibSelectorSort> ibDataDBComposer::LevelOrder(const Output& output, int depth,
	const std::vector<ibQueryLowering::OutputColumn>& schema) const
{
	std::vector<ibSelectorSort> keys;
	// Depth 0 is the grand total — one node, and one node has no order. Past the last level of
	// either axis there is no level to have stated one.
	const GroupNode* found = LevelAt(output, depth);
	if (found == nullptr)
		return keys;

	const GroupNode& level = *found;
	for (const ibSortLineDescription& line : level.m_settings.m_sort.m_lines) {
		if (line.m_path.IsEmpty())
			continue;   // a line with no field is the absence of one — same rule as the output's sort
		for (const ibQueryLowering::OutputColumn& oc : schema) {
			if (!oc.m_name.IsSameAs(line.m_path, false) && !oc.m_alias.IsSameAs(line.m_path, false))
				continue;
			// ⭐ READ THE NODE THE WAY THE WALK READS IT. An aggregate is reached by its alias and a
			// field by its column — the very choice the row-filling loop makes — so "sort the groups
			// by their total" needs no separate road: it is the same key, spelled the other way.
			ibSelectorSort key;
			if (oc.m_byAlias) key.m_alias = oc.m_alias;
			else              key.m_col   = oc.m_col;
			key.m_ascending = line.m_ascending;
			keys.push_back(key);
			break;
		}
	}
	return keys;
}

bool ibDataDBComposer::Run(ibCompositionDriver& driver)
{
	// ⭐⭐ ONE BUILD, ONE STATE OF THE DATA. A composition is not one query — it is a dozen: the source
	// itself, a join the server would not take stitched from two reads, a subquery promoted to a temp
	// table, a page at a time, and a fetch per reference whose presentation is printed. Under
	// read-committed each of those reads whatever has committed by the moment it starts, so a batch of
	// documents posted mid-build lands in some parts of the answer and not others — the total in the
	// header stops matching the rows beneath it, with nothing to say which half is which. A snapshot
	// makes the whole build read one state; see ibDbTxOptions::snapshot.
	//
	// ⚠ HERE AND NOT IN L3, and the reason is worth keeping: L3's ExecuteRead does not finish the read
	// it starts. It hands back a live cursor and the caller draws the rows afterwards, so a
	// transaction ending when that function returns kills the cursor its own result depends on —
	// measured on 2026-08-22 as "-504, cursor is not open" on the first row of the first query. The
	// transaction has to outlive the RESULT, and this is the nearest place that does.
	//
	// ⚠ AROUND THE BUILD, NOT AROUND THE WINDOW. Holding one state costs the server the record
	// versions that state needs. A build ends; a list left open and scrolled for minutes does not, and
	// must not hold one — between its pages the data legitimately moves.
	//
	// A build reads its rows before it returns, so holding the snapshot in a local is enough here —
	// unlike the script's query door, where the answer outlives the call and the snapshot travels
	// with it. Same object either way; only who holds it differs. A transaction already open makes
	// this null, and a null holder holds nothing.
	const std::shared_ptr<ibQueryReadState> readsOneState;   // ⛔ NOT OPENED — see queryReadState.h

	// THE FIRST OUTPUT — what a list has, and what "run the composer" has always meant.
	return RunOutput(Root(), driver);
}

// ⭐⭐ ONE OUTPUT, ONE FOLD — including a cross-table's.
//
// 🛑 IT USED TO BE TWO. A table was read a second time, folded by its column keys alone, to get the
// figures the bottom line needs; the reason written here was that the two sets of subtotals "cannot
// come out of one tree — rows-then-columns gives the grand total and every row's, and the columns
// alone is not a prefix of that order". That was true of the tree as it was BUILT, not of the
// question: a column total is the cells of the heading over EVERYTHING, and once every heading
// carries its own column branch (ibStreamingFold::FeedAcross) the root carries one too.
//
// What the second pass cost was not only a read: the two folds had to agree about which column a
// figure belonged to, and "a key the second pass found and the first did not" was a case that had
// to be written down and silently dropped. One tree, one answer, no such case.
bool ibDataDBComposer::RunOutput(const Output& output, ibCompositionDriver& driver)
{
	bool hasTotals = false;
	const bool read = RunOutputPass(output, driver, hasTotals);
	driver.OnOutputEnd(hasTotals);
	// …and if this was the last branch of a shared read, the rows are done with — see
	// ReleaseSharedRead. Here rather than deeper in, because the walk is finished only once the
	// output has ENDED, and that is the event this function owns.
	ReleaseSharedRead(output);
	return read;
}

bool ibDataDBComposer::RunOutputPass(const Output& output, ibCompositionDriver& driver, bool& hasTotalsOut)
{
	// ONE READ PER REFERENCE — and nothing declared here to arrange it. The reference knows whether it
	// has read (its own initialised flag), and there is one of it per identity per session, so forty
	// printed lines naming the same object hold one object and cost one query. This function briefly
	// opened a scope to bound that; the scope was the wrong shape, because knowing every place a
	// reference gets reused is knowing nearly every place there is.

	// The driver IS the envelope: a paged driver (the list fetch) vends the page
	// request; a plain driver reads everything.
	ibReadPageRequest page;
	const bool paged = driver.GetPageRequest(page);

	std::vector<ibQueryLowering::OutputColumn> schema;
	bool hasTotals = false;
	// ⭐ AND WHETHER THE DBMS GROUPED — this read's own answer, held for as long as this walk needs
	// it and no longer. A SHARED read is never server-grouped (that road is a paged single-level
	// drill, and a shared read serves a report), so `false` is the right answer when it is taken.
	bool serverGrouped = false;
	// ⭐⭐ ONE READ, OR MY OWN. Where the outputs fold the same rows they were rendered as BRANCHES of
	// a single query and executed once — this output then walks its branch of what is already in
	// hand. Where they could not (an output with a filter of its own, a sort of its own, no name to
	// be addressed by), it reads for itself exactly as it always did.
	std::unique_ptr<ibDataQueryResult> own;
	ibDataQueryResult* shared = SharedReadFor(output, schema, hasTotals);
	if (shared == nullptr) {
		own    = std::make_unique<ibDataQueryResult>(ExecuteFor(output, schema, hasTotals, page, serverGrouped));
		shared = own.get();
	}
	ibDataQueryResult& result = *shared;

	// WHICH SHAPE THIS OUTPUT IS ABOUT TO BE READ IN. Three facts decide everything below — whether a
	// page was asked for, whether the query folds, and whether the DBMS already did the folding — and
	// a report that comes out wrong is almost always wrong about one of them.
	ibJournalInfo(wxT("composer"), wxT("output '%s': %s, totals %s, server-grouped %s, %u columns"),
		output.m_name,
		paged ? wxT("paged") : wxT("whole"),
		hasTotals ? wxT("yes") : wxT("no"),
		serverGrouped ? wxT("yes") : wxT("no"),
		static_cast<unsigned>(schema.size()));

	// WHAT IS COMING, said before the first row: the output's kind, its own schema and its name.
	// Two outputs of one composition show different fields, so the schema belongs to the output and
	// not to the composition.
	ibCompositionOutputInfo info;
	info.m_kind   = output.Kind();   // read off its fields — a column grouping is what makes it a cross-table
	info.m_schema = schema;
	info.m_name   = output.m_name;
	// WHERE THE ROWS' DIMENSIONS END — the same count the clause writer wrote them by, asked the
	// same way, so the two can never disagree about which heading belongs where.
	//
	// ⚠ A USER'S GROUPING FLATTENS THE TABLE, and honestly so: it REPLACES the ladder whole (the
	// rule every setting follows), and a flat list of lines cannot say "these read across the page".
	// So everything it names is the rows', and the report a person re-grouped by hand comes back a
	// plain grouping — which is what they asked for by stating one list.
	// ⭐⭐ WHAT EACH COLUMN IS CALLED — worked out HERE, where the fields are known, and read by every
	// driver off one answer (ibCompositionOutputInfo::TitleOf).
	//
	// A column of the result carries a NAME the query gave it; what stands over it on the page comes
	// from the FIELD it is a reading of, and which field that is depends on the column's ROLE:
	//
	//   * a MEASURE is a reading of the field it aggregates — the resources are declared in order,
	//     so the k-th measure column is the k-th resource, and its title is its ARGUMENT's. Which is
	//     why `COUNT(Number)` is headed "Number" and never "CountNumber": the qualification the NAME
	//     needed to stay unique is not something a reader should ever meet (Max, 2026-08-26).
	//   * a DIMENSION is a reading of the field its level groups by — several fields to a level, in
	//     the order the level states them, which is the order their columns arrive in.
	//   * anything else is a projected field, named after itself.
	info.m_titles.assign(schema.size(), wxString());
	{
		size_t measure = 0;
		std::map<int, size_t> filledInLevel;   // level -> how many of its fields are already titled
		for (size_t i = 0; i < schema.size(); ++i) {
			switch (schema[i].m_role) {
			case ibQueryLowering::ibColumnRole::Measure:
				if (measure < m_resources.size())
					info.m_titles[i] = TitleForPath(m_resources[measure].m_path);
				++measure;
				break;
			case ibQueryLowering::ibColumnRole::Dimension: {
				const GroupNode* level = LevelAt(output, schema[i].m_level + 1);
				const size_t at = filledInLevel[schema[i].m_level]++;
				if (level != nullptr && at < level->m_settings.m_group.m_lines.size())
					info.m_titles[i] = TitleForPath(level->m_settings.m_group.m_lines[at].m_path);
				break;
			}
			default:
				break;
			}
			// WHATEVER IS LEFT IS TITLED BY ITS OWN NAME — a projected field, or a column whose field
			// could not be found (a user's own grouping replaces the ladder, and then there is no
			// level to ask). Said here rather than left empty, so TitleOf never has to guess.
			if (info.m_titles[i].IsEmpty())
				info.m_titles[i] = ibTitleFromName(schema[i].m_name);
		}
	}
	info.m_detailsAxis = DetailAxisOf(output);
	info.m_rowLevels = GetCurrentGroupDesc().IsOk()
		? [&] {
			size_t named = 0;
			for (const ibGroupLineDescription& line : GetCurrentGroupDesc().m_lines)
				if (!line.m_path.IsEmpty())
					++named;
			return named;
		}()
		: DimensionCount(output.m_rowGroups);
	driver.OnOutputBegin(info);

	// ⭐ WHERE THE ROWS END AND THE PAGE-WIDTH BEGINS — the same number the clause writer wrote the
	// keys by, and the one the walk below classifies each heading with. Asked once, here, so no
	// consumer has to re-derive an axis from a depth.
	const int rowLevels = static_cast<int>(info.m_rowLevels);

	std::vector<ibValue> row(schema.size());
	if (serverGrouped) {
		// Server-paged GROUPS (one grouping level, keyset-paged by the DB) — already grouped, so emit each as a
		// level-1 DRILLABLE group node WITHOUT the ByGroups fold (which folds a flat detail snapshot). The row
		// reads exactly like the flat cursor. (⚠ a reference-spread group value needs m_objectPrefix in the
		// schema — a follow-up; a scalar dim reads straight. docs: group-level paging)
		while (result.Next()) {
			for (size_t i = 0; i < schema.size(); ++i) {
				const ibQueryLowering::OutputColumn& oc = schema[i];
				if (!oc.m_objectPrefix.empty() && oc.m_col != nullptr)
					row[i] = result.GetColumnObject(oc.m_objectPrefix, oc.m_col);
				else
					row[i] = oc.m_byAlias ? result.GetColumn(oc.m_alias) : result.GetValue(oc.m_col);
			}
			// GROUPS the server already folded — a group, said as one. It stands over nothing HERE
			// (the server returned the folded rows, not what went into them), so it is a heading with
			// nothing to open: a list must not offer an expander, a printed report must still style it
			// as the heading it is. Which is exactly why the two answers travel separately.
			driver.OnGroupBegin(1, ibSelectorNodeKind::Group, /*hasChildren*/true, /*showsWhatIsUnder*/false, row);
		}
	}
	else if (!hasTotals) {
		// Flat result — the forward cursor; a dot-walk object leaf reassembles from
		// its prefixed field spread (mirrors the runtime selection's ReadColumn).
		//
		// hasChildren = KNOWN TO HAVE CHILDREN, and a flat cursor never knows: finding out costs an
		// EXISTS per row. So it answers `false` and does not guess.
		//
		// It must not be pressed into answering "may this row be entered" either. That was the shape
		// of the first fix here — a level read reported every row as having children, which is true
		// of an ITEM hierarchy (a chart of accounts: an account is subordinate to an account) and
		// false of a folders+items one, where only a folder may be entered. One flag, two meanings,
		// so every item in a catalog grew an expander.
		//
		// Being ENTERABLE is decided where the source is known — the model reads the hierarchy KIND
		// off the queryable (IsItemHierarchy) and the folder flag off the row, and ORs this in.
		while (result.Next()) {
			for (size_t i = 0; i < schema.size(); ++i) {
				const ibQueryLowering::OutputColumn& oc = schema[i];
				if (!oc.m_objectPrefix.empty() && oc.m_col != nullptr)
					row[i] = result.GetColumnObject(oc.m_objectPrefix, oc.m_col);
				else
					row[i] = oc.m_byAlias ? result.GetColumn(oc.m_alias) : result.GetValue(oc.m_col);
			}
			// NOTHING WAS GROUPED, so every row is a DETAIL row — which is exactly what an output
			// with no grouping fields is for. Said as a detail rather than as a level-0 group,
			// because a printer lays the two out differently and should not have to infer which
			// it got from the depth.
			driver.OnRow(0, row);
		}
	}
	else {
		// TOTALS — the folded tree; the selector's Next() is a pre-order walk over
		// EVERY node, so one loop covers groups and details, Level() = depth.
		//
		// ⭐ A LEVEL'S FILTER IS APPLIED HERE, and hiding is all it does: the fold has already run,
		// so a heading that fails its level's filter simply is not written, and every total above it
		// keeps the rows it was computed from. `hiddenAbove` carries that down — what hangs under a
		// hidden heading is hidden with it, since printing a child of an unprinted parent would put
		// it under the wrong heading.
		// ⭐ AND IT WALKS ITS OWN BRANCH of the shared fold, asked for by the SAME name the render
		// wrote (`SPLIT <name> BY …`) — both go through BranchNameFor, so an output cannot end up
		// asking for a branch nobody wrote. Reading alone, there are no branches to choose between
		// and the walk is the plain one.
		ibSelector sel = ReadsAsBranch(output)
			? result.Select(ibSelectKind::ibSelectKind_ByGroups, BranchNameFor(output, BranchIndexOf(output)))
			: result.Select(ibSelectKind::ibSelectKind_ByGroups);
		// ⭐ THE GRAND TOTAL IS PART OF THE WALK WHEN THE READER WANTS ONE. It is the tree's root and
		// the fold already rolled every row into it; asking for it here is what puts it in front of
		// the driver, which prints it at the BOTTOM of the section (a pre-order walk hands it over
		// first — see ibSpreadsheetComposeDriver).
		if (driver.WantsGrandTotal())
			sel.WalkOverall();
		else
			// ⭐ AND THE FIRST LEVEL'S OWN ORDER, stated before the first Next() so the fold is walked
			// in it rather than re-sorted after. Asked for the grand total, this selection holds ONE
			// node — the root — and its children get their order on the descent below, like every
			// other level's.
			sel.OrderBy(LevelOrder(output, 1, schema));

		// ⭐⭐ ONE LOOP PER LEVEL, NESTED — the shape the language itself reads: walk the groups, and
		// for each of them walk what is under it. A selection now visits its OWN level only, so the
		// walk descends instead of relying on a single pre-order cursor and a depth counter.
		//
		// The hidden-heading rule falls out of the shape rather than being carried in a variable: a
		// heading its level's filter rejects is simply not descended into, so nothing under it is
		// written. `hiddenAbove` existed to say that in a flat walk, and there is nothing left for it
		// to say here.
		// The set the walk starts from: the composition resolved, then this output's table over it.
		const std::vector<wxString> shownAtOutput = SelectedFor(output);
		const bool everyField = ReadsEveryField();

		// ⭐⭐ THE SET IN FORCE TRAVELS WITH THE WALK, exactly as the branch does. A node's selected
		// fields are inherited DOWNWARD and only downward: a field named on a child is shown by that
		// child and everything under it, and the nodes ABOVE never see it (Max, 2026-08-28). That is
		// a fact about the node, so the walk carries it and nobody climbs back up to work it out.
		//
		// ⚠ THE READ IS THE UNION, THE PRINT IS PER NODE — two questions over one tree. The query
		// fetches every field any node asked for (ProjectionFor), because a field nobody fetched
		// cannot be printed anywhere; what each node SHOWS out of that is decided here.
		std::function<void(ibSelector&, const std::vector<wxString>&)> walk =
			[&](ibSelector& level, const std::vector<wxString>& shownAbove) {
			while (level.Next()) {
				for (size_t i = 0; i < schema.size(); ++i) {
					const ibQueryLowering::OutputColumn& oc = schema[i];
					row[i] = oc.m_byAlias ? level.GetColumn(oc.m_alias) : level.GetValue(oc.m_col);
				}

				if (!LevelShows(output, level.Level(), level.Kind(), schema, row))
					continue;                   // hidden heading — and with it everything beneath

				// WHAT THIS NODE SHOWS — its own table resolved against what stood above it.
				const GroupNode* here = LevelAt(output, level.Level(), level.Kind());
				const std::vector<wxString> shownHere = here != nullptr
					? ibComposerSelectedUnder(shownAbove, *here) : shownAbove;

				// …AND EVERYTHING IT DOES NOT SHOW IS BLANKED, not removed. The COLUMNS belong to the
				// output — a table has the columns it has — so a node fills the cells that are its
				// own and leaves the rest empty. That is what "the nodes above do not see a child's
				// field" looks like on paper.
				//
				// ⭐⭐ ONE RULE FOR THE WHOLE TABLE — a RESOURCE is chosen exactly as a field is, on a
				// node, and inherited downward (Max, 2026-08-28: "a resource is not only a property
				// of the output but of the node as well… you can declare it on the root element and
				// it will reach every output").
				//
				// So "a total is shown against every heading" is not a rule in the code — it is what
				// declaring the resource AT THE ROOT does: every node inherits it. And activating one
				// in the detail section alone shows it there and nowhere above, which is the same
				// sentence read the other way. A special case for measures would have been a second
				// answer to a question the table already answers.
				//
				// A node's OWN KEY is the one thing printed without being chosen — it is the
				// grouping, and a heading that does not print its own key is not a heading.
				// 🛑 AND IT BLANKS ONLY WHERE SOMEBODY ACTUALLY CHOSE. With nothing selected anywhere,
				// `shownHere` is empty and this loop blanked EVERY cell — the grouping's own key
				// vanished from its heading, and a detail row came out empty in every column, which
				// the printer draws as no row at all (Max, 2026-08-28, live: two screenshots, one
				// with a blank `Ref` column and one with no records).
				//
				// The narrowing that matters already happened in the QUERY: with nothing selected the
				// read fetches only what it folds by, so there is nothing extra in the row to hide.
				// Blanking exists for the other question — a field chosen BELOW must not appear
				// above — and that question only arises once something was chosen.
				//
				// ⚠ A DIMENSION IS NEVER BLANKED. It is the grouping's key, and a heading that does
				// not print its own key is not a heading. Asked by ROLE, because a level NUMBER is
				// the fold's own counting and does not have to agree with the walk's depth — which
				// is exactly how the key came to be blanked in the first place.
				for (size_t i = 0; !everyField && !shownHere.empty() && i < schema.size(); ++i) {
					const ibQueryLowering::OutputColumn& oc = schema[i];
					if (oc.m_role == ibQueryLowering::ibColumnRole::Dimension)
						continue;               // a heading's key — printed because it IS the grouping
					// ⚠ ASKED, NOT COMPARED — a path and an output name are spelled differently for
					// every dot-walk. See ibComposerColumnAnswersTo.
					bool shows = false;
					for (const wxString& name : shownHere)
						if (ibComposerColumnAnswersTo(oc, name)) { shows = true; break; }
					if (!shows)
						row[i] = ibValue();
				}
			// ⭐ A HEADING OR A ROW — the NODE says which, and the driver is told in its own words.
			// The fold produces headings for every level of the BY list and one node per source row
			// under the deepest one. A printer lays the two out differently, so it must not have to
			// infer the difference from the depth — a depth cannot answer it once the tree holds both.
			//
			// ⭐⭐ AND WHAT IS WRITTEN IS CHOSEN BY THE NODE'S KIND, not by whether the rows were read.
			// The rows are ALWAYS there now — they are what the totals were made of — so an output
			// that never declared a Details level meets them here and simply steps over them. Reading
			// and printing are two questions: the tree holds everything, the ladder says which kinds
			// this output writes.
				if (level.Kind() == ibSelectorNodeKind::Detail) {
					if (!OutputWrites(output, ibSelectorNodeKind::Detail))
						continue;
					// A RECORD, and which way IT reads is ASKED — not worked out from its depth.
					//
					// 🛑 IT WAS `level.Level() > rowLevels`, which is the right test for a HEADING
					// (past the last row level, a heading stands across the page) and the wrong one
					// for a record: a record is numbered PAST the last dimension by construction, so
					// that comparison is true for every record there has ever been. Every one of them
					// went to OnColumn — the cross-table road, which draws nothing in an ordinary
					// report — and the records were simply absent, with the fold holding all 125 of
					// them (Max, 2026-08-28: *"it ignores detail records entirely"*).
					//
					// The axis of a record is its own fact and is already answered: DetailAxisOf, on
					// the output, carried here in the info the driver was handed.
					if (info.m_detailsAxis == ibTotalsAxis::Columns)
						driver.OnColumn(level.Level(), ibSelectorNodeKind::Detail, row);
					else
						driver.OnRow(level.Level(), row);
					// ⭐ A ROW HAS NOTHING UNDER IT — DOWN THE PAGE. In a TABLE the column keys stand
					// across it, and those cells are its children: the fold hangs them there so a
					// detail record is a line of the table with figures beside it rather than
					// something inside one cell (Max, 2026-08-26). An ordinary report's rows have no
					// children at all, so this walk simply finds none.
					if (level.HasChildren()) {
						ibSelector cells = level.Select(ibSelectKind::ibSelectKind_ByGroups);
						walk(cells, shownHere);
					}
					continue;
				}

				// A heading, and then whatever it stands over — including the GRAND TOTAL, which is
				// a level like any other: the one that groups by nothing. Descending into it is how
				// the first dimension level is reached when a report asked for it.
				// ⚠ EXPANDABLE MEANS "THERE IS SOMETHING TO SHOW", not "there is something there".
				// The rows are always folded in now, so the deepest heading always HAS children —
				// but an output whose ladder never declared a Details level does not write them, and
				// a triangle that opens onto nothing is worse than no triangle. So the flag asks the
				// same question the writing does.
				ibSelector under = level.Select(ibSelectKind::ibSelectKind_ByGroups);
				// ⭐ EACH LEVEL IN ITS OWN ORDER, and the depth is the CHILDREN's, not this heading's.
				// Stated on the descent rather than inherited: a sort belongs to the level whose
				// headings it arranges, and carrying this one down would arrange the next level by a
				// key its author never wrote there (ibSelector::OrderBy).
				under.OrderBy(LevelOrder(output, level.Level() + 1, schema));

				// Asked by LOOKING: step onto the first child, read what kind it is, and rewind. The
				// selection is already folded, so this costs a pointer move — and it is the only way
				// to know, because a heading two levels up stands over headings while the deepest one
				// stands over rows, and nothing on the node itself says which.
				bool showsWhatIsUnder = false;
				if (under.Next()) {
					showsWhatIsUnder = under.Kind() != ibSelectorNodeKind::Detail
						|| OutputWrites(output, ibSelectorNodeKind::Detail);
					under.Reset();
				}

				// BOTH answers travel — see ibCompositionDriver::OnGroupBegin. HasChildren() is the
				// fold's own fact and is what makes a heading a heading; showsWhatIsUnder is this
				// output's promise and is what an expander may offer.
				//
				// ⭐ AND WHICH WAY IT READS is settled HERE, where the axis is known: a heading past
				// the row levels stands ACROSS the page. The driver used to work that out from a
				// depth and a count handed to it separately — two facts to keep in step for one
				// answer this walk already has.
				if (level.Level() > rowLevels)
					driver.OnColumn(level.Level(), level.Kind(), row);
				else
					driver.OnGroupBegin(level.Level(), level.Kind(), level.HasChildren(), showsWhatIsUnder, row);

				// ⚠ THIS HEADING'S OWN VALUES, TAKEN BEFORE THE DESCENT. `row` is ONE buffer for the
				// whole walk — filled at each visit and reused by the nested walk below — so by the
				// time the descent returns it holds the LAST row that was read, not this heading.
				// Read straight from it, the closing event printed the last detail record's values
				// into the grand-total line (seen live, 2026-08-27: the document number and its date
				// standing where the totals belong).
				//
				// Copied only for the headings that will be closed, so an ordinary report pays one
				// vector per heading and a detail-heavy one pays nothing extra.
				const std::vector<ibValue> mine = level.Level() <= rowLevels ? row : std::vector<ibValue>();
				walk(under, shownHere);
				// ⭐⭐ …AND THE HEADING CLOSES, with the figures it ended up with. Everything under it
				// has been written by now, which is the whole difference between this and the event
				// that opened it — and it is where a total belongs on the page.
				if (level.Level() <= rowLevels)
					driver.OnGroupEnd(level.Level(), mine);
			}
		};
		// THE WALK STARTS WITH THE OUTPUT'S OWN SET — the composition resolved, then the output's
		// table over it. Everything below inherits from here.
		walk(sel, shownAtOutput);
	}

	hasTotalsOut = hasTotals;
	return true;
}

// EXECUTE FOR ONE OUTPUT. The first output rides the CACHED parse — a list re-reads it on every
// page, and re-parsing per page is what that cache exists to avoid. Any other output renders and
// parses on the spot: keying one cache by which output asked would be a second question for it to
// answer, and outputs past the first are read once per composition, not once per scroll.
// ⭐⭐ WHICH OUTPUTS CAN BE READ TOGETHER. They share a read when they differ ONLY in how they fold
// it — same source, same WHERE, same ORDER BY — because one read has one of each of those.
//
// So an output with a filter of its own stays alone (its filter is ANDed into the text and would
// narrow everybody else's rows), and so does one that sorts by a field of its own. What is left —
// outputs that state only their groupings — is the ordinary report with several tables in it, which
// is exactly the case that used to cost one query apiece.
std::vector<const ibDataComposer::Output*> ibDataDBComposer::BranchableOutputs() const
{
	std::vector<const Output*> together;
	for (const Output& output : Outputs()) {
		if (output.m_driver == nullptr || !Declares(output))
			continue;
		if (output.m_settings.m_filter.IsOk() || output.m_settings.m_sort.IsOk())
			return {};   // one output reads on its own terms — then nobody shares, and no read is built
		if (!HasGroupingFields(output))
			return {};   // a branch with no levels would render `SPLIT` with nothing after it
		// ⚠ A BRANCH IS ADDRESSED BY NAME, so the name has to be one the query can spell. An output
		// called "Sales for the year" is not renamed behind a person's back — it simply reads alone.
		// Asked of the LEXER, which owns the definition of a name — a second set of rules written
		// here would disagree with it the day either changed.
		// ⚠ AN OUTPUT THAT WAS NEVER NAMED STILL FOLDS. Its branch is addressed by a name derived
		// from its POSITION (BranchNameFor) — nothing stored, nothing migrated. Refused is only a
		// name that exists and cannot be spelled as an identifier.
		if (!output.m_name.IsEmpty() && !ibQueryLexer::IsIdentifier(output.m_name))
			return {};
		// ⭐⭐ …AND THEY MUST BE ASKING THE READ FOR THE SAME THING. A branch chooses how the rows are
		// FOLDED; everything the READ itself is asked — whether the detail records travel with the
		// totals, and how the levels lay out down and across the page — has ONE answer for all of
		// them. So outputs that answer differently are not branches of one read: they read apart,
		// exactly as an output with a filter of its own does.
		//
		// 🛑 Written because the first output's answer silently became everyone's: `withDetails` was
		// a constant `true` at the read below, and both tables came out with a line per document
		// under them — 125 blank rows nobody had asked for (Max, live, 2026-08-27).
		if (!together.empty()) {
			const Output& first = *together.front();
			const ibTotalsLayout a = LayoutFor(output), b = LayoutFor(first);
			if (WantsDetails(output) != WantsDetails(first) ||
			    a.m_rowLevels != b.m_rowLevels || a.m_hasColumns != b.m_hasColumns ||
			    a.m_detailsAxis != b.m_detailsAxis)
				return {};
		}
		together.push_back(&output);
	}
	// ONE OUTPUT IS NOT A SHARED READ. It is the ordinary road, and taking the branch road for it
	// would spend a `SPLIT` to say what a plain ladder already says.
	return together.size() >= 2 ? together : std::vector<const Output*>{};
}

// ⭐⭐ THE SHARED READ, BUILT BY WHOEVER ASKS FIRST. Returns the read this output should walk, or
// null when it reads for itself.
//
// There is no "the run is starting" hook and there does not need to be one: the outputs are visited
// in order, so the first branch to arrive builds the read and every later one finds it. The last of
// them lets it go (ReleaseSharedRead, called where the output ends) — a composition left on screen
// must not sit on an open cursor.
ibDataQueryResult* ibDataDBComposer::SharedReadFor(const Output& output,
	std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals)
{
	if (m_sharedRead == nullptr && m_sharedBranches.empty()) {
		const std::vector<const Output*> together = BranchableOutputs();
		if (together.empty())
			return nullptr;   // nobody shares — and nothing is built, so nothing has to be released

		const wxString text = RenderTextFor(together);
		// ⭐ AND IT IS PRINTED, like every other query this composer writes. The shared read is the
		// ONE text in the house that carries `SPLIT`, and it was the one text nobody could see: the
		// journal's `composer.text` is written in EnsureAst, which is the single-output road (Max,
		// 2026-08-27, reading the log: not one occurrence of the word). Everything below it is
		// already lowered — `query.sql` shows SQL, where a fold in memory cannot appear by
		// construction — so without this line the branch road had no readable form anywhere.
		ibJournalInfo(wxT("composer.text"), wxT("shared read rendered:\n%s"), text);
		ibQueryPackage package;
		std::map<wxString, const ibQuerySelect*> named;
		ibQuerySelectPtr ast = ParseComposed(text, package, named);
		if (ast == nullptr)
			ibBackendCoreException::Error(_("Composer: the rendered query of the shared read failed to parse"));

		// ⭐⭐ …AND THE FILTER IN FORCE, which the text does not carry (see AndWhere). It is the
		// REPORT'S own — "applies to every output" — and a read shared by every output is exactly
		// where it belongs: one condition, decided at the top, and nothing below can see more than
		// it admits. The branches have no filters of their own to add — an output that states one
		// reads alone (BranchableOutputs), which is why there is a single call here and not one
		// per branch.
		AndWhere(*ast, BuildFilterAst(GetCurrentFilterDesc()));

		EnsureTempTables();
		ibTempSourceScope     tempScope(m_prepared.m_sources);
		ibSourceMetaDataScope mdScope(m_metaData);
		ibNamedResultScope    namedScope(named);   // the package's own selections, as in Execute

		std::vector<ibQueryLowering::OutputColumn> shared;
		// ⚠ NO PAGE HERE. A shared read serves a REPORT — several tables of one sheet — and a page is
		// a question a list asks about ONE ladder. A paged output never reaches this road at all
		// (a list has one output and no branches to share with).
		// ⭐ THE DETAILS AND THE LAYOUT ARE ASKED, NOT DECIDED — of the outputs themselves, exactly as
		// the single-output road asks (`WantsDetails(output)` / `LayoutFor(output)` at ExecuteFor).
		// Any of them will do: BranchableOutputs let them share only because they agree on both.
		ibDataQueryResult read = ibQueryLowering::ExecuteTotals(*ast, m_params, shared, ibReadPageRequest{},
			nullptr, WantsDetails(*together.front()), LayoutFor(*together.front()));

		m_sharedRead     = std::make_shared<ibDataQueryResult>(std::move(read));
		m_sharedSchema   = std::move(shared);
		m_sharedBranches = together;
		m_branchesServed = 0;
		// THE PAIR OF NUMBERS THIS IS JUDGED BY: one read, this many outputs out of it.
		ibJournalInfo(wxT("composer"), wxT("shared read: %u outputs read as branches of one query, %u columns"),
			static_cast<unsigned>(m_sharedBranches.size()), static_cast<unsigned>(m_sharedSchema.size()));
	}

	if (!ReadsAsBranch(output))
		return nullptr;

	schema    = SchemaFor(output, m_sharedSchema);
	hasTotals = true;   // a shared read is a TOTALS read by construction — every branch is a ladder
	return m_sharedRead.get();
}

// …AND LET IT GO once every branch has had it. Counted rather than guessed, because "the last
// output" is not the same as "the last branch": an output that reads alone sits between them.
void ibDataDBComposer::ReleaseSharedRead(const Output& output)
{
	if (!ReadsAsBranch(output))
		return;
	if (++m_branchesServed < m_sharedBranches.size())
		return;
	m_sharedRead.reset();
	m_sharedSchema.clear();
	m_sharedBranches.clear();
	m_branchesServed = 0;
}

// THE NAME A BRANCH ANSWERS TO. Given, or made from where the output stands — see the declaration.
// Both sides of the run derive it here: the render writes it into `SPLIT <name> BY …`, the walk asks
// for it. One function, so they cannot spell it differently.
wxString ibDataDBComposer::BranchNameFor(const Output& output, size_t at) const
{
	return output.m_name.IsEmpty() ? wxString::Format(wxT("Output%u"), static_cast<unsigned>(at) + 1)
	                               : output.m_name;
}

// WHERE THIS OUTPUT STANDS AMONG THE BRANCHES OF THE SHARED READ — the position its derived name is
// made from. Asked of the SAME list the render walked, so the two agree by construction.
size_t ibDataDBComposer::BranchIndexOf(const Output& output) const
{
	for (size_t at = 0; at < m_sharedBranches.size(); ++at)
		if (m_sharedBranches[at] == &output)
			return at;
	return 0;
}

bool ibDataDBComposer::ReadsAsBranch(const Output& output) const
{
	for (const Output* branch : m_sharedBranches)
		if (branch == &output)
			return true;
	return false;
}

// ⭐ THE COLUMNS THIS OUTPUT SHOWS, out of what the shared read publishes. One read publishes every
// branch's keys; a branch prints its OWN — otherwise every table on the sheet would carry an empty
// column for each of its neighbours' headings.
//
// Matched by NAME, which is how the composer already reads a schema everywhere else (a level's sort,
// a level's filter): the settings speak paths, and a path is what the query named the column.
std::vector<ibQueryLowering::OutputColumn> ibDataDBComposer::SchemaFor(const Output& output,
	const std::vector<ibQueryLowering::OutputColumn>& shared) const
{
	// What this output names: its levels' fields, the resources (which every output rolls), and
	// whatever it selected outright.
	std::vector<wxString> mine;
	const auto take = [&mine](const wxString& path) {
		if (path.IsEmpty())
			return;
		for (const wxString& have : mine)
			if (have.IsSameAs(path, false))
				return;
		mine.push_back(path);
	};
	for (const std::vector<GroupNode>* axis : { &output.m_rowGroups, &output.m_columnGroups })
		for (const GroupNode& level : *axis)
			for (const TotalByItem& field : level.m_settings.m_group.m_lines)
				take(field.m_path);
	for (const ibResourceDescription& resource : m_resources) {
		take(resource.m_path);
		take(resource.m_alias);
	}
	for (const wxString& name : ProjectionFor(output))
		take(name);

	std::vector<ibQueryLowering::OutputColumn> schema;
	for (const ibQueryLowering::OutputColumn& column : shared) {
		bool wanted = false;
		for (const wxString& path : mine)
			if (column.m_name.IsSameAs(path, false) || column.m_alias.IsSameAs(path, false)) { wanted = true; break; }
		if (wanted)
			schema.push_back(column);
	}
	// A BRANCH THAT MATCHED NOTHING KEEPS THE WHOLE SCHEMA rather than printing an empty table: the
	// names come from settings a person edits, and a report that silently loses every column is a
	// worse answer than one that shows a column too many.
	return schema.empty() ? shared : schema;
}

ibDataQueryResult ibDataDBComposer::ExecuteFor(const Output& output,
	std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals, const ibReadPageRequest& page,
	bool& serverGrouped)
{
	if (&output == &Root())
		return Execute(schema, hasTotals, page, serverGrouped);

	const wxString text = RenderTextFor(output);
	ibQueryPackage package;
	std::map<wxString, const ibQuerySelect*> named;
	ibQuerySelectPtr ast = ParseComposed(text, package, named);
	if (ast == nullptr)
		ibBackendCoreException::Error(_("Composer: the rendered query of an output failed to parse"));

	// The output's own tree condition, ANDed into what the text already asks — the same rule the
	// first output follows in EnsureAst, and for the same reason: a condition built as an expression
	// is never rendered and re-parsed.
	//
	// ⭐ BUILT HERE, FROM THE DESCRIPTION. It used to be a cached expression carried on the output
	// with a version counter beside it, filled in by whoever happened to edit the filter — so the
	// condition ran or did not depending on which door the edit came through. An expression derived
	// from stored data is made where it is used; there is nothing to keep in step.
	AndWhere(*ast, BuildFilterAst(output.m_settings.m_filter));

	EnsureTempTables();
	ibTempSourceScope     tempScope(m_prepared.m_sources);
	ibSourceMetaDataScope mdScope(m_metaData);
	ibNamedResultScope    namedScope(named);   // the package's own selections, as in Execute

	hasTotals = ast->m_hasTotals;
	serverGrouped = false;   // group-level paging belongs to the paged list, not to a report's output
	// Same rule as the root output above: a fetch that carries a page is asking for ONE LEVEL, and a
	// level has no rows in it. Written the same way in both places on purpose — one question, one
	// answer, wherever it is asked from.
	return hasTotals
		? ibQueryLowering::ExecuteTotals(*ast, m_params, schema, page, nullptr,
			page.m_count == 0 && WantsDetails(output), LayoutFor(output))
		: ibQueryLowering::Execute(*ast, m_params, schema, page);
}

//////////////////////////////////////////////////////////////////////
// PruneUnresolvedSettings — the settings, re-asked rather than chased
//////////////////////////////////////////////////////////////////////

// ⭐ WHAT "GONE" MEANS FOR A SETTING — one walk, used for both sections.
//
// A SORT or a GROUPING line names one field: it survives or it does not. A FILTER is a TREE, so the
// same question is asked of each side of a condition — either side may be a field (`Price > Cost`) —
// and a GROUP is kept for as long as it still holds something. A group emptied by the pruning goes
// with its last condition; a group the author wrote empty is not this function's business, because
// it did not stop resolving.
static int ibPruneFilterNodes(std::vector<ibFilterNodeDescription>& nodes,
	const std::function<bool(const wxString&)>& resolves)
{
	int dropped = 0;
	std::vector<ibFilterNodeDescription> kept;
	for (ibFilterNodeDescription& node : nodes) {
		if (node.m_kind == ibFilterNodeKind_Group) {
			const size_t before = node.m_children.size();
			dropped += ibPruneFilterNodes(node.m_children, resolves);
			if (before > 0 && node.m_children.empty())
				continue;   // it held conditions and holds none now — it went with them
			kept.push_back(std::move(node));
			continue;
		}
		const bool leftGone  = node.m_left.IsField()  && !resolves(node.m_left.m_path);
		const bool rightGone = node.m_right.IsField() && !resolves(node.m_right.m_path);
		if (leftGone || rightGone) { ++dropped; continue; }
		kept.push_back(std::move(node));
	}
	// 🛑 UNCONDITIONALLY, AND THAT IS THE WHOLE BUG THIS ONCE HAD. The loop MOVES every node it
	// keeps into `kept`, so by the time this line is reached `nodes` holds moved-from elements —
	// their wxString and their ibValue have already gone. Assigning back only when something was
	// dropped therefore GUTTED every condition on the ordinary path where nothing is dropped: the
	// node count stayed right, `m_use` and the comparison survived (an int and a bool do not care
	// about a move), and the field and the value came back EMPTY.
	//
	// ⭐ AND THAT IS WHY IT LOOKED INTERMITTENT: a run that actually pruned something took the
	// assignment and left the survivors whole, so the same click worked or did not depending on
	// whether some OTHER setting had stopped resolving (Max, 2026-08-28: "out of four tries it
	// worked once"). A conditional write over moved-from state is not an optimisation — the
	// condition it was guarding is precisely when the write is needed.
	nodes = std::move(kept);
	return dropped;
}

int ibDataComposer::PruneSettingsDesc(ibSettingsDescription& settings,
	const std::function<bool(const wxString&)>& resolves)
{
	int dropped = ibPruneFilterNodes(settings.m_filter.m_nodes, resolves);

	// ⚠ ASSIGNED UNCONDITIONALLY, like the filter above and for the same reason. These two build
	// `kept` by COPY, so skipping the write would merely be pointless here rather than wrong — but
	// "safe as long as nobody moves" is a mine with no compiler behind it: the day somebody swaps
	// the copy for a std::move to save an allocation, the guard turns into the defect the filter
	// walk had (moved-from lines kept in place). The write costs a pointer swap; the guard was
	// saving nothing worth the shape.
	{
		std::vector<ibSortLineDescription> kept;
		for (const ibSortLineDescription& line : settings.m_sort.m_lines) {
			if (!resolves(line.m_path)) { ++dropped; continue; }
			kept.push_back(line);
		}
		settings.m_sort.m_lines = std::move(kept);
	}
	{
		std::vector<ibGroupLineDescription> kept;
		for (const ibGroupLineDescription& line : settings.m_group.m_lines) {
			if (!resolves(line.m_path)) { ++dropped; continue; }
			kept.push_back(line);
		}
		settings.m_group.m_lines = std::move(kept);
	}
	return dropped;
}

int ibDataComposer::PruneUnresolvedSettings(const std::function<bool(const wxString&)>& resolves)
{
	if (!resolves)
		return 0;   // no host answer = no verdict, and no verdict means nothing is dropped

	int dropped = 0;

	// Read the survivors out, then put them back. There is no remove-one on this store by design —
	// the lines are a LIST the fetch reads in order, and a rebuild keeps that order exact.
	{
		std::vector<FilterItem> kept;
		std::map<wxString, ibValue> keptParams;
		for (const FilterItem& item : m_scopeConditions) {
			if (!resolves(item.m_path)) { ++dropped; continue; }
			const auto param = m_params.find(item.m_param);
			if (param != m_params.end())
				keptParams.emplace(param->first, param->second);
			kept.push_back(item);
		}
		// THE LIST IS WRITTEN BACK EITHER WAY; only the parameter sweep below is worth guarding, and
		// it is guarded by the ANSWER rather than by a comparison against a list that has just moved.
		const bool anyDropped = kept.size() != m_scopeConditions.size();
		m_scopeConditions = std::move(kept);
		if (anyDropped) {
			// A parameter belongs to the line that bound it; the ones whose line went are gone with
			// it. Left behind they would be bound into a query that never mentions them.
			for (auto it = m_params.begin(); it != m_params.end(); ) {
				// Only the AUTO-named ones (AddParam: `__f<n>`) belong to a filter line. A parameter the
				// caller named itself is theirs, and dropping it here would be this pass reaching outside
				// what it was asked about.
				it = (it->first.StartsWith(wxT("__f")) && keptParams.find(it->first) == keptParams.end())
					? m_params.erase(it) : std::next(it);
			}
		}
	}

	// (No flat sort list to walk any more — the order lives in the two sections, pruned below with
	//  the rest of what they hold.)

	// EVERY LEVEL OF THE LADDER, and every field inside it. A level that loses ALL its fields loses
	// itself and the levels below move up — the author's deeper grouping is not what stopped
	// resolving, so it is not what should disappear.
	// BOTH AXES: a field that stopped existing stopped existing whichever way its heading reads.
	for (std::vector<GroupNode>* axis : { &Root().m_rowGroups, &Root().m_columnGroups }) {
		for (GroupNode& level : *axis) {
			std::vector<TotalByItem>& lines = level.m_settings.m_group.m_lines;
			std::vector<TotalByItem> kept;
			for (const TotalByItem& item : lines) {
				if (!resolves(item.m_path)) { ++dropped; continue; }
				kept.push_back(item);
			}
			lines = std::move(kept);
		}
	}
	CollapseEmptyLevels();

	// ⭐⭐ …AND THE TWO SETTINGS SECTIONS, which is where everything the settings window writes lives.
	// This walked the flat store only, so the promise above — "drop every setting whose field the
	// source no longer has" — was kept for the declared lines and broken for the reader's and the
	// author's alike: nothing in the tree pruned an ibSettingsDescription (audit, 2026-08-24).
	//
	// A FILTER LINE IS NOT DROPPED BY PATH ALONE: its tree carries groups, and a group that loses its
	// last condition is not a condition that stopped resolving. Handled by the description's own
	// walk, so the shape stays the description's business and this only says what "gone" means.
	// …THE READER'S SETTING AND EVERY VARIANT. A field that stopped existing stopped existing for
	// whoever named it, and a variant nobody is composing on today is one a picker may reach
	// tomorrow.
	dropped += PruneSettingsDesc(m_userSettings, resolves);
	for (ibVariantDescription& variant : m_variants)
		dropped += PruneSettingsDesc(variant.m_settings, resolves);

	// (NOTHING TO INVALIDATE BY HAND. The render's cache key is the rendered TEXT and the filter that
	//  never becomes text, and a dropped line changes one of them by definition. The version counter
	//  that stood here was the key for a condition handed in from outside, and nothing ever handed
	//  one in.)
	return dropped;
}
