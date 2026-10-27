////////////////////////////////////////////////////////////////////////////
//	Description : THE ONE PLACE A COMPOSITION IS READ AND WRITTEN
////////////////////////////////////////////////////////////////////////////
//
// 🛑⭐ WHY EVERYTHING IS HERE (Max, 2026-08-23: "take all the serialisation you have spread around
// the settings and put it in one file"). What a composition consists of used to be written in three
// places at once — the composition itself (source, query, parameters, resources, variants), the list
// settings (filter, order, grouping) and the dynamic list — so "what is in a saved composition" had
// no single answer, and one format change had to be made three times to stay one format.
//
// A LIST and a REPORT stand on the SAME description: main table (a number, the list's own) plus the
// composer part (both). A list is not a different breed — it uses half.
//
// ⚠ NOTHING HERE RESOLVES ANYTHING. No name is looked up, no source is opened, no query described.
// Reading happens while the configuration is still loading, and whatever needs a finished world
// belongs to the asking instead. That is the rule ibSourceDescription and ibTypeDescription follow,
// and this is their third.

#include "compositionDescription.h"

#include "core/serialize/dataBuilder.h"         // ibDataNode / ibDataValue

#include "core/localization/dictionary.h"                        // _() — the platform's names for its own parameters

// ===========================================================================

// ===========================================================================
//  Structure — the outputs of a variant, their levels, and the fields inside
// ===========================================================================
namespace {
// The node a variant is written into. Not a registered value type — a variant is a record inside
// the composition, not something a script hands around.
constexpr ibClassID g_variantNodeClsid = make_clsid("CompositionVariant", ibClassKind_None);
const ibString  kVariantName       = ibT("Name");
// …AND WHAT A PICKER SHOWS. A variant is a name, a SYNONYM and a setting; the synonym is what a
// person reads in the list, and empty means "derive it from the name" (the rule every metaobject's
// synonym follows).
const ibString  kVariantSynonym    = ibT("Synonym");

// ---------------------------------------------------------------------------
// THE STRUCTURE OF A VARIANT — its outputs, their levels, and the fields inside them.
//
// The flat filter / sort / grouping a variant has always written cannot say any of this: a level
// made of several fields is indistinguishable there from several levels, and an output beside the
// first — or an axis of columns — has nowhere to go at all. So the structure is written as itself,
// beside the settings rather than instead of them.
//
// Without it everything the settings window builds lives until the file is closed and no further,
// which is exactly what "external reports do not serialise their groupings" was.
// ---------------------------------------------------------------------------
constexpr ibClassID g_outputNodeClsid = make_clsid("CompositionOutput", ibClassKind_None);
constexpr ibClassID g_levelNodeClsid = make_clsid("CompositionLevel", ibClassKind_None);
constexpr ibClassID g_fieldNodeClsid = make_clsid("CompositionField", ibClassKind_None);
const ibString  kStructureNode  = ibT("Structure");
const ibString  kRowsNode       = ibT("Rows");
const ibString  kColumnsNode    = ibT("Columns");
const ibString  kSelectedNode   = ibT("Selected");
// ⚠ THE READER'S OWN TABLE NEEDS A NAME OF ITS OWN. A LEVEL writes two things into one node — its
// own selected fields (`kSelectedNode`) and its settings, through the settings pair — so if the
// settings wrote their table under the same name, the two would land on top of each other. Today a
// level's settings carry no fields and nothing collides; a name apart is what keeps that true when
// somebody fills them.
const ibString  kSelectedFieldsNode = ibT("SelectedFields");
// …and the other settings of a setting — a node of their own for the same reason.
const ibString  kOutputParametersNode = ibT("OutputParameters");
// Read and written as every list of the platform's parameters is — the pair stands with them, further down.
void ReadOutputParameters(const ibDataNode& node, ibOutputParametersDescription& parameters);
void WriteOutputParameters(ibDataNode& node, const ibOutputParametersDescription& parameters);
// …and the conditional appearance, whose rules carry an appearance — beside the appearance's own pair.
const ibString  kConditionalAppearanceNode = ibT("ConditionalAppearance");
void ReadConditionalAppearance(const ibDataNode& node, ibConditionalAppearanceDescription& appearance);
void WriteConditionalAppearance(ibDataNode& node, const ibConditionalAppearanceDescription& appearance);
const ibString  kPathName       = ibT("Path");
const ibString  kKindName       = ibT("Kind");
// A LEVEL'S OWN SORT AND FILTER — written inside the level, because that is where they belong.
//
// (⛔ A SECOND SPELLING OF THE SORT SECTION STOOD HERE — `g_sortNodeClsid`, `kSortNode = "Sort"`,
//  `kAscendingName = "Ascending"` — declared and used ZERO times, eight lines from the live pair.
//  On disk the section is `kOrderNode = "Order"` with lines of class `CompositionOrderLine`, and
//  `kDirName` already spells "Ascending". A rename was started at the constants and never carried
//  through; what it left behind was a dead vocabulary that reads as if it were the format.)
const ibString  kFilterNode     = ibT("Filter");
// WHAT THE LEVEL IS — a grouping or the detail records. Absent in a file written before detail
// levels existed, and absence reads back as Grouping, which is what every level in such a file is.
const ibString  kLevelKindName  = ibT("LevelKind");
const ibString  kOutputKindName = ibT("OutputKind");
// WHAT UNFOLDS UNDER A NODE — written INSIDE it, so where a node is written is which node it belongs
// to. The axes on an output (Rows / Columns) are the same shape at the top; this is every storey below.
const ibString  kChildrenNode   = ibT("Children");

// The axis writer / reader, declared here because a LEVEL uses them for its own children — a node
// and the run of nodes under it are mutually recursive, which is what a tree is.
void WriteLevels(ibDataNode& parent, const ibString& name, const std::vector<ibLevelDescription>& levels);
void ReadLevels(const ibDataNode& parent, const ibString& name, std::vector<ibLevelDescription>& levels);

void WriteFieldList(ibDataNode& node, const ibString& name, const std::vector<ibString>& list)
{
	if (list.empty())
		return;   // an empty set writes nothing — absence reads back as absence
	ibDataNode& sub = node.Child(name);
	for (size_t i = 0; i < list.size(); ++i)
		sub.AddChild(g_fieldNodeClsid, static_cast<ibMetaID>(i)).SetValue<ibString>(kPathName, list[i]);
}

void ReadFieldList(const ibDataNode& node, const ibString& name, std::vector<ibString>& list)
{
	list.clear();
	auto sub = node.FindChild(name);
	if (!sub)
		return;
	for (const ibDataNode& child : sub->Children())
		if (child.GetClsid() == g_fieldNodeClsid)
			list.push_back(child.GetValue<ibString>(kPathName));
}

// ⭐⭐ THE SELECTED-FIELDS TABLE — the same node shape as a plain field list, plus the row's KIND.
//
// ⚠ AND THE KIND IS WRITTEN ONLY WHEN IT IS NOT A FIELD. Everything saved before this is a list of
// paths, and a path with no kind beside it IS a field — so old settings read back unchanged, and a
// table of ordinary fields keeps writing exactly what it wrote before. `Auto` carries no path: it
// names nothing, it says WHERE what the storey above chose lands.
void WriteSelectedList(ibDataNode& node, const ibString& name,
                       const std::vector<ibSelectedFieldDescription>& list)
{
	if (list.empty())
		return;   // an empty table writes nothing — absence reads back as absence, and absence inherits
	ibDataNode& sub = node.Child(name);
	for (size_t i = 0; i < list.size(); ++i) {
		ibDataNode& row = sub.AddChild(g_fieldNodeClsid, static_cast<ibMetaID>(i));
		row.SetValue<ibString>(kPathName, list[i].m_path);
		// ⚠ `kKindName` is spelled on a node of ANOTHER class above (a filter group's); a field node
		// has no kind of its own, so the word is free here and means what it says.
		if (list[i].m_kind != ibSelectedFieldKind_Field)
			row.SetValue<std::int32_t>(kKindName, static_cast<std::int32_t>(list[i].m_kind));
	}
}

void ReadSelectedList(const ibDataNode& node, const ibString& name,
                      std::vector<ibSelectedFieldDescription>& list)
{
	list.clear();
	auto sub = node.FindChild(name);
	if (!sub)
		return;
	for (const ibDataNode& child : sub->Children()) {
		if (child.GetClsid() != g_fieldNodeClsid)
			continue;
		ibSelectedFieldDescription row;
		// A record that predates the kind has none — and it is a FIELD, which is what every one of
		// them was. (ibDataNode answers a missing value with the type's default, and 0 IS Field.)
		row.m_kind = static_cast<ibSelectedFieldKind>(child.GetValue<std::int32_t>(kKindName));
		row.m_path = child.GetValue<ibString>(kPathName);
		list.push_back(std::move(row));
	}
}

} // namespace

// (A LEVEL'S FIELD had a pair of its own here, writing {path, unfold} into a node. What a level
//  groups by is its GROUPING — ibGroupDescriptionMemory already writes exactly that pair, so the
//  second spelling went with the second type.)

// ===========================================================================
//  A LEVEL — a node of an output: what it folds by, what it narrows and orders,
//  and what unfolds under it
// ===========================================================================
bool ibLevelDescriptionMemory::WriteNode(ibDataNode& node, const ibLevelDescription& level)
{
	node.SetValue<std::int32_t>(kLevelKindName, static_cast<std::int32_t>(level.m_kind));
	WriteSelectedList(node, kSelectedNode, level.m_selected);

	// ⭐ ITS SETTINGS LIVE WHERE THE LEVEL DOES (Max), so they are written INSIDE it — through the ONE
	// pair that knows the whole shape. All three parts are LISTS: a filter that is a tree of many
	// conditions and groups, a sort of many lines, a grouping of many. (What the engine runs is an
	// expression DERIVED from the filter tree and is not saved: it cannot be edited back into lines.)
	//
	// ⚠ THE SORT USED TO BE WRITTEN OUT BY HAND HERE, line by line, beside the pair that already knew
	// how — a second spelling of one format, free to drift from the first the day either changes.
	ibSettingsDescriptionMemory::WriteNode(node, level.m_settings);

	// ⭐ …AND WHAT IS UNDER IT, AS A TREE. The children go INSIDE their parent — nothing names a
	// parent, because where a node is written IS which node it belongs to. The same rule the filter
	// tree follows, and for the same reason: a flat list plus a pointer upwards can be inconsistent,
	// and nesting cannot.
	if (!level.m_children.empty())
		WriteLevels(node, kChildrenNode, level.m_children);
	return true;
}

bool ibLevelDescriptionMemory::ReadNode(const ibDataNode& node, ibLevelDescription& level)
{
	level.m_kind = static_cast<ibCompositionLevelKind>(node.GetValue<std::int32_t>(kLevelKindName));
	ReadSelectedList(node, kSelectedNode, level.m_selected);

	// ITS SETTINGS, back the way they were written — through the one pair. The filter comes back as
	// the LINES it is; the expression the engine runs is built from them once the composer is in
	// hand, because building it needs the parameters the composer names.
	ibSettingsDescriptionMemory::ReadNode(node, level.m_settings);

	// …AND WHAT UNFOLDS UNDER IT, read from where it was written — inside this node.
	ReadLevels(node, kChildrenNode, level.m_children);
	return true;
}

namespace {

// THE AXIS — a run of levels under one name (rows, columns). It combines levels and knows nothing
// of what a level is made of.
void WriteLevels(ibDataNode& parent, const ibString& name, const std::vector<ibLevelDescription>& levels)
{
	if (levels.empty())
		return;
	ibDataNode& axis = parent.Child(name);
	for (size_t i = 0; i < levels.size(); ++i)
		ibLevelDescriptionMemory::WriteNode(axis.AddChild(g_levelNodeClsid, static_cast<ibMetaID>(i)), levels[i]);
}

void ReadLevels(const ibDataNode& parent, const ibString& name, std::vector<ibLevelDescription>& levels)
{
	levels.clear();
	auto axis = parent.FindChild(name);
	if (!axis)
		return;
	for (const ibDataNode& node : axis->Children()) {
		if (node.GetClsid() != g_levelNodeClsid)
			continue;
		ibLevelDescription level;
		ibLevelDescriptionMemory::ReadNode(node, level);
		levels.push_back(std::move(level));
	}
}

} // namespace

// ===========================================================================
//  AN OUTPUT — what a report shows: its rows, its columns, and what it reads
// ===========================================================================
bool ibOutputDescriptionMemory::WriteNode(ibDataNode& node, const ibOutputDescription& output)
{
	node.SetValue<ibString>(kVariantName, output.m_name);
	// ⭐ AND WHAT IT IS. Stored because it is a DECISION — "add grouping" or "add table" — and a
	// table that has not been filled in yet is empty on both axes: read back off the content, it
	// would come up a grouping and the person's second axis would have nowhere to go.
	node.SetValue<std::int32_t>(kOutputKindName, static_cast<std::int32_t>(output.m_kind));
	WriteSelectedList(node, kSelectedNode, output.m_selected);
	// ITS SETTINGS, written the way a level writes them — one storey up, same shape, same pair.
	ibSettingsDescriptionMemory::WriteNode(node, output.m_settings);
	// AND ITS TWO AXES. A grouping output has rows; a TABLE has rows AND columns, and that is the
	// whole of the difference — each axis is a run of ROOT nodes, and what unfolds under one is on it.
	WriteLevels(node, kRowsNode, output.m_rowGroups);
	WriteLevels(node, kColumnsNode, output.m_columnGroups);
	return true;
}

bool ibOutputDescriptionMemory::ReadNode(const ibDataNode& node, ibOutputDescription& output)
{
	output.m_name          = node.GetValue<ibString>(kVariantName);
	// (The "Source" property an older record carries is READ BY NOBODY — see ibOutputDescription.)
	ReadSelectedList(node, kSelectedNode, output.m_selected);
	ibSettingsDescriptionMemory::ReadNode(node, output.m_settings);
	ReadLevels(node, kRowsNode, output.m_rowGroups);
	ReadLevels(node, kColumnsNode, output.m_columnGroups);
	// ⚠ A RECORD WRITTEN BEFORE THE KIND EXISTED reads back 0 — Grouping — and that is right for
	// every one of them except a table, which could not be authored then. So the content answers
	// where the record is silent: a stored column axis means a table, whatever the number says.
	output.m_kind = static_cast<ibCompositionOutputKind>(node.GetValue<std::int32_t>(kOutputKindName));
	if (output.m_kind == ibCompositionOutputKind::Grouping && !output.m_columnGroups.empty())
		output.m_kind = ibCompositionOutputKind::Table;
	return true;
}

// ===========================================================================
//  THE STRUCTURE — the outputs, one after another, and nothing about what one is
// ===========================================================================
bool ibStructureDescriptionMemory::WriteNode(ibDataNode& node, const std::vector<ibOutputDescription>& outputs)
{
	// ⚠ NOTHING FOR AN EMPTY ONE. Every SETTING writes itself through here now, and a level's own
	// settings have no structure — creating the node anyway would put an empty `Structure` child
	// inside every level of every report.
	if (outputs.empty())
		return true;
	ibDataNode& structure = node.Child(kStructureNode);
	for (size_t i = 0; i < outputs.size(); ++i)
		ibOutputDescriptionMemory::WriteNode(structure.AddChild(g_outputNodeClsid, static_cast<ibMetaID>(i)), outputs[i]);
	return true;
}

bool ibStructureDescriptionMemory::ReadNode(const ibDataNode& node, std::vector<ibOutputDescription>& outputs)
{
	auto structure = node.FindChild(kStructureNode);
	if (!structure)
		return false;   // written before there was a structure — the caller keeps what it had

	std::vector<ibOutputDescription> read;
	for (const ibDataNode& sub : structure->Children()) {
		if (sub.GetClsid() != g_outputNodeClsid)
			continue;
		ibOutputDescription output;
		ibOutputDescriptionMemory::ReadNode(sub, output);
		read.push_back(std::move(output));
	}
	if (read.empty())
		return false;
	outputs = std::move(read);
	return true;
}

// ===========================================================================
//  Settings — filter tree, order, grouping
// ===========================================================================

namespace {

// ⚠ AN OPAQUE KEY, NOT A NAME. The tier says **sort** everywhere (ibSortDescription, m_sort,
// SetSort); this string is what the bytes on disk already say, and renaming it would make every
// saved setting unreadable to buy a word. Same call the CLSID keys took when their concepts were
// renamed (`MD_SKTB` after subconto → account dimension).
const ibChar* const kOrderNode = ibT("Order");
const ibChar* const kGroupNode = ibT("Group");
const ibChar* const kFieldName = ibT("Field");
const ibChar* const kDirName   = ibT("Ascending");

// The nodes a settings record is written into. Named the way every other node here is — a stored
// line is a RECORD inside the composition, not a registered value type, so nothing spells a class
// id of the runtime kind. (The sort line carried `value_to_clsid("VL_SORTI")` from the days its
// storage was a script object; that id now belongs to nothing.)
constexpr ibClassID g_filterNodeClsid = make_clsid("CompositionFilterNode", ibClassKind_None);
constexpr ibClassID g_orderNodeClsid = make_clsid("CompositionOrderLine", ibClassKind_None);
constexpr ibClassID g_groupNodeClsid = make_clsid("CompositionGroupLine", ibClassKind_None);

// …and the filter's own names, which every node carries: what the node IS, whether it applies, how
// the user meets it, and the label they gave it. Nothing names a PARENT — a child is written under
// its group, so standing there is the statement. `Kind` does double duty on purpose — on a group it
// is the operator, on a condition the comparison — because a node is one or the other, never both.
const ibChar* const kNodeKindName = ibT("Node");
const ibChar* const kUseName      = ibT("Use");
const ibChar* const kDisplayName  = ibT("Display");
const ibChar* const kTextName     = ibT("Text");
// ⚠ NAMED APART FROM THE LEVEL'S `kKindName` ABOVE, which is a different field of a different node
// that happens to be spelled the same on disk. Two anonymous namespaces in one file are ONE
// namespace, so the collision is a compile error rather than a subtle mix-up — but the reason the
// two names exist is worth saying: a grouping LINE's unfold and a LEVEL's kind are not one thing.
const ibChar* const kGroupKindName = ibT("Kind");
// BY PERIODS — the three parts of one answer, written only when the line has one (see the writer).
const ibChar* const kPeriodUnitName = ibT("PeriodUnit");
const ibChar* const kPeriodFromName = ibT("PeriodFrom");
const ibChar* const kPeriodToName   = ibT("PeriodTo");

// THE LIST'S OWN HALF — the main table, written only when there is one.
const ibChar* const kMainTableName = ibT("MainTable");
// …AND THE QUERY, which is the composition's real source. Kept under the name the composition's own
// property already used, so a record written before this file existed reads back unchanged.
const ibChar* const kQueryName     = ibT("Query");

} // namespace


// ===========================================================================
//  Filter — the nodes, flat, in display order
// ===========================================================================
//
// ⭐ ONE NODE PER LINE, and the tree is the parent index. The filter used to be written as a live
// object packing itself (a root ibValueFilterGroup with children), which meant reading it back was
// creating objects — through the configuration's door, because a condition compares against
// references and enum members. As plain lines there is nothing to create: only the VALUE on the
// right-hand side is a value, and it is kept as the node it was packed into.
//
// ⭐⭐ THE VALUE IS TAKEN AS IT IS, like a parameter's: a filter compares against references and enum
// members, which only the configuration can build, so it is built by whoever runs the filter, against
// the configuration it runs in (ibStoredValue, the side's backend_composition.h) — never here, while it loads.
namespace {
void ibWriteOperand(ibDataNode& node, const ibString& prefix, const ibFilterOperandDescription& side)
{
	node.SetValue<ibString>(prefix + ibT("Path"), side.m_path);
	node.SetValue<ibString>(prefix + ibT("Text"), side.m_presentation);
	node.SetValue<std::int32_t>(prefix + ibT("Leaf"), static_cast<std::int32_t>(side.m_leafId));
	// The TYPE is deliberately NOT packed — it is derived from the source the field is bound to, and
	// a saved setting outlives the schema it was saved against. The value goes back as it was packed;
	// nothing stored (Undefined) writes nothing.
	if (!side.m_value.Fields().empty())
		node.Child(prefix + ibT("Value")) = side.m_value;
}

void ibReadOperand(const ibDataNode& node, const ibString& prefix, ibFilterOperandDescription& side)
{
	side.m_path         = node.GetValue<ibString>(prefix + ibT("Path"));
	side.m_presentation = node.GetValue<ibString>(prefix + ibT("Text"));
	side.m_leafId       = static_cast<ibMetaID>(node.GetValue<std::int32_t>(prefix + ibT("Leaf")));
	side.m_type         = ibTypeDescription();
	side.m_value        = ibDataNode();
	if (auto value = node.FindChild(prefix + ibT("Value")))
		side.m_value = *value;
}
} // namespace

namespace {
// ⭐ THE TREE IS WRITTEN AS A TREE. A group's children are children of its node — nothing states a
// parent, because standing under it IS the statement. That is the shape the node format already has,
// and the shape a person editing a filter sees.
void ibWriteFilterNodes(ibDataNode& into, const std::vector<ibFilterNodeDescription>& nodes)
{
	for (size_t i = 0; i < nodes.size(); ++i) {
		const ibFilterNodeDescription& item = nodes[i];
		ibDataNode& line = into.AddChild(g_filterNodeClsid, static_cast<ibMetaID>(i));
		line.SetValue<std::int32_t>(kNodeKindName, static_cast<std::int32_t>(item.m_kind));
		line.SetValue<std::int32_t>(kUseName, item.m_use ? 1 : 0);
		line.SetValue<std::int32_t>(kDisplayName, static_cast<std::int32_t>(item.m_display));
		line.SetValue<ibString>(kTextName, item.m_presentation);
		if (item.m_kind == ibFilterNodeKind_Group) {
			line.SetValue<std::int32_t>(kGroupKindName, static_cast<std::int32_t>(item.m_groupKind));
			ibWriteFilterNodes(line, item.m_children);
		}
		else {
			line.SetValue<std::int32_t>(kGroupKindName, static_cast<std::int32_t>(item.m_comparison));
			ibWriteOperand(line, ibT("L"), item.m_left);
			ibWriteOperand(line, ibT("R"), item.m_right);
		}
	}
}

void ibReadFilterNodes(const ibDataNode& from, std::vector<ibFilterNodeDescription>& nodes)
{
	for (const ibDataNode& line : from.Children()) {
		ibFilterNodeDescription item;
		item.m_kind    = static_cast<ibFilterNodeKind>(line.GetValue<std::int32_t>(kNodeKindName));
		item.m_use     = line.GetValue<std::int32_t>(kUseName) != 0;
		item.m_display = static_cast<ibFilterDisplayMode>(line.GetValue<std::int32_t>(kDisplayName));
		item.m_presentation = line.GetValue<ibString>(kTextName);
		if (item.m_kind == ibFilterNodeKind_Group) {
			item.m_groupKind = static_cast<ibFilterGroupKind>(line.GetValue<std::int32_t>(kGroupKindName));
			ibReadFilterNodes(line, item.m_children);
		}
		else {
			item.m_comparison = static_cast<ibComparisonKind>(line.GetValue<std::int32_t>(kGroupKindName));
			ibReadOperand(line, ibT("L"), item.m_left);
			ibReadOperand(line, ibT("R"), item.m_right);
		}
		nodes.push_back(item);
	}
}
} // namespace

bool ibFilterDescriptionMemory::ReadNode(const ibDataNode& node, ibFilterDescription& filter)
{
	filter.Clear();
	auto sub = node.FindChild(kFilterNode);
	if (!sub)
		return true;   // no filter written is not a failure — it is a composition without one

	filter.m_rootKind = static_cast<ibFilterGroupKind>(sub->GetValue<std::int32_t>(kKindName));
	ibReadFilterNodes(*sub, filter.m_nodes);
	return true;
}

bool ibFilterDescriptionMemory::WriteNode(ibDataNode& node, const ibFilterDescription& filter)
{
	if (!filter.IsOk())
		return true;   // nothing to write, and absence reads back as absence

	ibDataNode& sub = node.Child(kFilterNode);
	sub.SetValue<std::int32_t>(kKindName, static_cast<std::int32_t>(filter.m_rootKind));
	ibWriteFilterNodes(sub, filter.m_nodes);
	return true;
}

// ===========================================================================
//  Sort — a path and a direction, per line
// ===========================================================================
bool ibSortDescriptionMemory::ReadNode(const ibDataNode& node, ibSortDescription& sort)
{
	sort.Clear();
	auto order = node.FindChild(kOrderNode);
	if (!order)
		return true;
	for (const ibDataNode& line : order->Children()) {
		const ibString field = line.GetValue<ibString>(kFieldName);
		if (field.IsEmpty())
			continue;
		sort.Append(field, line.GetValue<std::int32_t>(kDirName) != 0);
	}
	return true;
}

bool ibSortDescriptionMemory::WriteNode(ibDataNode& node, const ibSortDescription& sort)
{
	if (!sort.IsOk())
		return true;
	ibDataNode& sub = node.Child(kOrderNode);
	for (size_t i = 0; i < sort.m_lines.size(); ++i) {
		if (sort.m_lines[i].m_path.IsEmpty())
			continue;
		ibDataNode& line = sub.AddChild(g_orderNodeClsid, static_cast<ibMetaID>(i));
		line.SetValue<ibString>(kFieldName, sort.m_lines[i].m_path);
		line.SetValue<std::int32_t>(kDirName, sort.m_lines[i].m_ascending ? 1 : 0);
	}
	return true;
}

// ===========================================================================
//  Grouping — a path and its UNFOLD kind, per line
// ===========================================================================
//
// The kind is load-bearing: a hierarchy grouping IS what makes a list a tree, so dropping it
// reloads every tree as a flat grouping and reads as data loss.
bool ibGroupDescriptionMemory::ReadNode(const ibDataNode& node, ibGroupDescription& group)
{
	group.Clear();
	auto sub = node.FindChild(kGroupNode);
	if (!sub)
		return true;
	for (const ibDataNode& line : sub->Children()) {
		const ibString field = line.GetValue<ibString>(kFieldName);
		if (field.IsEmpty())
			continue;
		group.Append(field, static_cast<ibQueryDimUnfold>(line.GetValue<std::int32_t>(kGroupKindName)));
		// AND ITS PERIODICITY, when it has one. A record written before this existed has no unit,
		// which reads back as "not by periods" — the same answer the member's own IsOk gives.
		ibGroupPeriodsDescription& periods = group.m_lines.back().m_periods;
		periods.m_unit = line.GetValue<ibString>(kPeriodUnitName);
		periods.m_from = line.GetValue<ibString>(kPeriodFromName);
		periods.m_to   = line.GetValue<ibString>(kPeriodToName);
	}
	return true;
}

bool ibGroupDescriptionMemory::WriteNode(ibDataNode& node, const ibGroupDescription& group)
{
	if (!group.IsOk())
		return true;
	ibDataNode& sub = node.Child(kGroupNode);
	for (size_t i = 0; i < group.m_lines.size(); ++i) {
		if (group.m_lines[i].m_path.IsEmpty())
			continue;
		ibDataNode& line = sub.AddChild(g_groupNodeClsid, static_cast<ibMetaID>(i));
		line.SetValue<ibString>(kFieldName, group.m_lines[i].m_path);
		line.SetValue<std::int32_t>(kGroupKindName, static_cast<std::int32_t>(group.m_lines[i].m_kind));
		// ⭐ THE PERIODICITY, WRITTEN ONLY WHEN THERE IS ONE — three empty properties on every
		// ordinary grouping line would be three ways for a file to look different without meaning
		// anything different, and equality is what "modified" is decided by.
		const ibGroupPeriodsDescription& periods = group.m_lines[i].m_periods;
		if (periods.IsOk()) {
			line.SetValue<ibString>(kPeriodUnitName, periods.m_unit);
			if (!periods.m_from.IsEmpty()) line.SetValue<ibString>(kPeriodFromName, periods.m_from);
			if (!periods.m_to.IsEmpty())   line.SetValue<ibString>(kPeriodToName, periods.m_to);
		}
	}
	return true;
}

// ===========================================================================
//  Settings — the three of them, combined
// ===========================================================================
//
// ⭐ THE COMBINATION KNOWS NO PART'S SHAPE. Each description reads and writes its own node; this
// only says which ones there are, and in what order. Adding a fourth part is adding a line here.
bool ibSettingsDescriptionMemory::ReadNode(const ibDataNode& node, ibSettingsDescription& settings)
{
	// ⭐ AND THE STRUCTURE, because it is PART of a setting — the outputs a setting defines, with
	// every level's own settings inside them. It used to be written beside a setting by whoever held
	// one, which is how a per-node edit could be handed back without it.
	//
	// A LEVEL's settings go through here too and simply have none: the tree hangs off the levels,
	// not off their settings, so nothing is written and nothing is found on the way back.
	// ⚠ AND THE STRUCTURE IS NOT CHAINED INTO THE VERDICT. Its reader answers FALSE for "this record
	// has none", which is the ordinary case for a level and for any setting written before outputs
	// existed — ANDing it in would have turned a legitimate absence into "the settings failed to
	// read".
	const bool ok = ibFilterDescriptionMemory::ReadNode(node, settings.m_filter)
		&& ibSortDescriptionMemory::ReadNode(node, settings.m_sort)
		&& ibGroupDescriptionMemory::ReadNode(node, settings.m_group);
	ibStructureDescriptionMemory::ReadNode(node, settings.m_structure);
	// ⭐ AND THE FIELDS THE READER CHOSE — a setting like the three above. Absent from every record
	// written before 2026-08-28, and absence is a legitimate answer: nothing was chosen.
	ReadSelectedList(node, kSelectedFieldsNode, settings.m_selected);
	// ⭐ …AND THE VALUES THE READER FILLED IN. Only the parameters the author offered them ever land
	// here, and only the name and the packed value travel: the declaration stays with the author.
	ibParameterDescriptionMemory::ReadNode(node, settings.m_parameters);
	// ⭐ …AND HOW THE REPORT BEHAVES — the other settings. Absent from every record written before 2026-09-29,
	// which reads as "nobody said anything": the platform's own answers.
	ReadOutputParameters(node, settings.m_outputParameters);
	// ⭐ …AND WHAT STANDS OUT, WHEN — its conditions are filters, their values kept packed like a filter's.
	ReadConditionalAppearance(node, settings.m_conditionalAppearance);
	return ok;
}

bool ibSettingsDescriptionMemory::WriteNode(ibDataNode& node, const ibSettingsDescription& settings)
{
	WriteSelectedList(node, kSelectedFieldsNode, settings.m_selected);   // see ReadNode
	// …and the reader's parameter values, written as a VARIANT's are: name and value, no declaration.
	ibParameterDescriptionMemory::WriteNode(node, settings.m_parameters, /*full*/false);
	WriteOutputParameters(node, settings.m_outputParameters);
	WriteConditionalAppearance(node, settings.m_conditionalAppearance);
	return ibFilterDescriptionMemory::WriteNode(node, settings.m_filter)
		&& ibSortDescriptionMemory::WriteNode(node, settings.m_sort)
		&& ibGroupDescriptionMemory::WriteNode(node, settings.m_group)
		&& ibStructureDescriptionMemory::WriteNode(node, settings.m_structure);   // see ReadNode
}

// ===========================================================================
//  The composition — the outermost doll
// ===========================================================================
//
// ⭐⭐ AND THIS IS THE DOOR A FILE GOES THROUGH. The node it fills is an ordinary ibDataNode, so
// SAVING A COMPOSER is handing this node to a provider: ibBinaryProvider writes it as bytes,
// ibJsonProvider writes it as JSON. Neither choice is baked into the format — it is the caller's.
bool ibCompositionDescriptionMemory::ReadNode(const ibDataNode& node, ibCompositionDescription& composition)
{
	// THE MAIN TABLE IS A NUMBER, and its absence is a number too: a record written by a report (or
	// by a list before one was picked) simply has no such property, and wxNOT_FOUND is what "nothing
	// picked" IS. Nothing is resolved here — that is the whole reason the id is stored at all.
	composition.m_mainTable = wxNOT_FOUND;
	node.GetValue<std::int32_t>(kMainTableName, composition.m_mainTable);   // …left as it is when the name is absent
	composition.m_query     = node.GetValue<ibString>(kQueryName);

	// ⭐ THE VARIANTS ARE THE SETTINGS. A record written before they existed carries ONE set and no
	// variant nodes at all — it is read into the FIRST variant, which is where the settings live now,
	// so nobody's saved work turns into a migration and there is no second member to land it in.
	if (!ibVariantDescriptionMemory::ReadNode(node, composition.m_variants)) {
		composition.m_variants.assign(1, ibVariantDescription());
		ibSettingsDescriptionMemory::ReadNode(node, composition.m_variants[0].m_settings);
	}
	if (composition.m_variants.empty())
		composition.m_variants.emplace_back();   // a record can say anything; the invariant is ours

	// A parameter holds references and enum members like a filter's right side does — packed, and built by
	// whoever runs the composition against its configuration (Max, 2026-08-28; 2026-10-12).
	ibParameterDescriptionMemory::ReadNode(node, composition.m_parameters);
	ibResourceDescriptionMemory::ReadNode(node, composition.m_resources);
	ibSelectDescriptionMemory::ReadNode(node, composition.m_selects);

	// WHAT THE COMPOSITION SHOWS — the bottom of the pile every output and level adds to. Absent from
	// a record written before 2026-08-24, and the helper clears before it reads, so such a record
	// comes back empty — which is exactly what "said nothing" IS.
	ReadSelectedList(node, kSelectedNode, composition.m_selected);
	return true;
}

bool ibCompositionDescriptionMemory::WriteNode(ibDataNode& node, const ibCompositionDescription& composition)
{
	// ⚠ THE QUERY IS THE SOURCE, and the main table is a convenience over it. A LIST stands on a main
	// table; a COMPOSITION is complete the moment its text exists (Max, 2026-08-18: "the composer's
	// source IS the query"). So the table is written only when there is one, and its absence is a
	// legitimate, complete record rather than a missing field.
	if (composition.HasMainTable())
		node.SetValue<std::int32_t>(kMainTableName, static_cast<std::int32_t>(composition.m_mainTable));
	node.SetValue<ibString>(kQueryName, composition.m_query);

	// …and there is always at least one variant to write, so there is no second road out of here.
	ibVariantDescriptionMemory::WriteNode(node, composition.m_variants);

	ibParameterDescriptionMemory::WriteNode(node, composition.m_parameters, /*full*/true);
	ibResourceDescriptionMemory::WriteNode(node, composition.m_resources);
	ibSelectDescriptionMemory::WriteNode(node, composition.m_selects);

	// …AND IT IS WRITTEN, which is the whole point of moving it here. It lived in the RUNNING composer
	// (`ibDataComposer::m_commonSelected`) with no road to the file at all, so a person who chose what
	// the report shows lost it the moment the report closed — the same defect the resources had, one
	// field over.
	WriteSelectedList(node, kSelectedNode, composition.m_selected);
	return true;
}

// ===========================================================================
//  Parameters — what the query asks for, and who fills it in
// ===========================================================================
//
// Written as a node per parameter, told apart by its own clsid: a reader walks the children and
// takes the ones it recognises, so parameters, variants and resources never disturb one another
// though they all hang off the same parent.
namespace {
constexpr ibClassID g_parameterNodeClsid = make_clsid("CompositionParameter", ibClassKind_None);
const ibString  kParamName       = ibT("Name");
const ibString  kParamExpression = ibT("Expression");
const ibString  kParamUser       = ibT("ForUser");
const ibString  kParamFromQuery  = ibT("FromQuery");
const ibString  kParamValue      = ibT("Value");
const ibString  kParamType       = ibT("Type");

constexpr ibClassID g_resourceNodeClsid = make_clsid("CompositionResource", ibClassKind_None);
const ibString  kResourceFunc = ibT("Func");
const ibString  kResourcePath = ibT("Path");
const ibString  kResourceAlias = ibT("Alias");
// The grouping this figure is computed over (`OVER Item`). Optional, and absence reads back as
// absence: every settings file written before areas existed holds no such key and means "the area
// comes from the ladder", which is exactly what an empty scope says.
const ibString  kResourceScope = ibT("Scope");

// THE PACKAGES — a node per select somebody has said something about, each holding its own field
// entries. The fields themselves are not stored: they are whatever the select projects, so a list
// of them here would be a copy that goes stale the moment the text is edited.
//
// ⚠ ITS OWN CLSID, not the one the plain field LISTS use (g_fieldNodeClsid, one file up): those
// nodes carry a path and nothing else, and a reader walking children by class must not meet two
// different records answering to one name.
constexpr ibClassID g_selectNodeClsid    = make_clsid("CompositionSelect", ibClassKind_None);
const ibString  kSelectId   = ibT("Id");
const ibString  kSelectName = ibT("Name");

constexpr ibClassID g_fieldInfoNodeClsid = make_clsid("CompositionFieldInfo", ibClassKind_None);
const ibString  kFieldInfoPath     = ibT("Path");
const ibString  kFieldInfoName     = ibT("Name");
const ibString  kFieldInfoTitle    = ibT("Title");
const ibString  kFieldInfoUseTitle = ibT("UseTitle");
const ibString  kFieldInfoRole     = ibT("Role");   // present = a person's word over the source's (m_useRole)
const ibString  kFieldInfoPeriodRank = ibT("PeriodRank");   // a period's number; absent = in order
const ibString  kFieldInfoAppearance = ibT("Appearance");   // the sub-node the field's appearance is written in

// A LIST OF THE PLATFORM'S PARAMETERS — one child per parameter a person touched, under whoever holds it. Each
// list is written under a class of its own, so a reader walking children by class meets only its own.
constexpr ibClassID g_appearanceNodeClsid      = make_clsid("CompositionAppearance", ibClassKind_None);
constexpr ibClassID g_outputParameterNodeClsid = make_clsid("CompositionOutputParameter", ibClassKind_None);
const ibString  kAppearanceParameter = ibT("Parameter");
const ibString  kAppearanceUse       = ibT("Use");
const ibString  kAppearanceValue     = ibT("Value");

// `known` — every parameter the platform defines on this list: a stored one it does not define shows nothing.
template <class Parameter>
void ReadParameterValues(const ibDataNode& node, ibClassID clsid, const std::vector<Parameter>& known,
	ibParameterValuesDescription<Parameter>& values)
{
	values.m_values.clear();
	for (const ibDataNode& child : node.Children()) {
		if (child.GetClsid() != clsid)
			continue;
		const Parameter parameter = static_cast<Parameter>(child.GetValue<std::int32_t>(kAppearanceParameter));
		if (std::find(known.begin(), known.end(), parameter) == known.end())
			continue;
		ibParameterValueDescription<Parameter> value;
		value.m_parameter = parameter;
		value.m_use       = child.GetValue<bool>(kAppearanceUse);
		if (auto stored = child.FindChild(kAppearanceValue))
			value.m_value = *stored;
		values.m_values.push_back(std::move(value));
	}
}

template <class Parameter>
void WriteParameterValues(ibDataNode& node, ibClassID clsid, const ibParameterValuesDescription<Parameter>& values)
{
	for (size_t i = 0; i < values.m_values.size(); ++i) {
		const ibParameterValueDescription<Parameter>& value = values.m_values[i];
		ibDataNode& sub = node.AddChild(clsid, static_cast<ibMetaID>(i));
		sub.SetValue<std::int32_t>(kAppearanceParameter, static_cast<std::int32_t>(value.m_parameter));
		sub.SetValue<bool>(kAppearanceUse, value.m_use);
		sub.Child(kAppearanceValue) = value.m_value;
	}
}

void ReadAppearance(const ibDataNode& node, ibAppearanceDescription& appearance)
{
	ReadParameterValues(node, g_appearanceNodeClsid, ibAppearanceParameters(), appearance);
}

void WriteAppearance(ibDataNode& node, const ibAppearanceDescription& appearance)
{
	WriteParameterValues(node, g_appearanceNodeClsid, appearance);
}

// THE OTHER SETTINGS OF A STOREY — every parameter either storey may hold, because a record is read before
// anybody says which storey it is.
const std::vector<ibOutputParameter>& EveryOutputParameter()
{
	static const std::vector<ibOutputParameter> s_every = [] {
		std::vector<ibOutputParameter> every = ibOutputParameters(ibOutputParameterScope::Report);
		for (const ibOutputParameter parameter : ibOutputParameters(ibOutputParameterScope::Node))
			if (std::find(every.begin(), every.end(), parameter) == every.end())
				every.push_back(parameter);
		return every;
	}();
	return s_every;
}

void ReadOutputParameters(const ibDataNode& node, ibOutputParametersDescription& parameters)
{
	parameters.Clear();
	if (auto sub = node.FindChild(kOutputParametersNode))
		ReadParameterValues(*sub, g_outputParameterNodeClsid, EveryOutputParameter(), parameters);
}

// Written only when somebody said anything — an empty node on every setting would be a way for two files to
// look different without meaning anything different.
void WriteOutputParameters(ibDataNode& node, const ibOutputParametersDescription& parameters)
{
	if (!parameters.IsEmpty())
		WriteParameterValues(node.Child(kOutputParametersNode), g_outputParameterNodeClsid, parameters);
}

// ONE RULE PER CHILD — its tick, its condition through the filter's own pair, its fields as a field list and its
// appearance through the appearance's. Nothing here knows how any of the three is written.
constexpr ibClassID g_appearanceRuleNodeClsid = make_clsid("CompositionAppearanceRule", ibClassKind_None);
const ibString  kRuleUse        = ibT("Use");
const ibString  kRuleFields     = ibT("Fields");
const ibString  kRuleAppearance = ibT("Appearance");

void ReadConditionalAppearance(const ibDataNode& node, ibConditionalAppearanceDescription& appearance)
{
	appearance.Clear();
	auto sub = node.FindChild(kConditionalAppearanceNode);
	if (!sub)
		return;   // a setting written before 2026-09-30, or one that highlights nothing
	for (const ibDataNode& child : sub->Children()) {
		if (child.GetClsid() != g_appearanceRuleNodeClsid)
			continue;
		ibConditionalAppearanceRuleDescription rule;
		rule.m_use = child.GetValue<bool>(kRuleUse);
		ibFilterDescriptionMemory::ReadNode(child, rule.m_condition);
		ReadFieldList(child, kRuleFields, rule.m_fields);
		if (auto ruleAppearance = child.FindChild(kRuleAppearance))
			ReadAppearance(*ruleAppearance, rule.m_appearance);
		appearance.m_rules.push_back(std::move(rule));
	}
}

void WriteConditionalAppearance(ibDataNode& node, const ibConditionalAppearanceDescription& appearance)
{
	if (appearance.IsEmpty())
		return;
	ibDataNode& sub = node.Child(kConditionalAppearanceNode);
	for (size_t i = 0; i < appearance.m_rules.size(); ++i) {
		const ibConditionalAppearanceRuleDescription& rule = appearance.m_rules[i];
		ibDataNode& child = sub.AddChild(g_appearanceRuleNodeClsid, static_cast<ibMetaID>(i));
		child.SetValue<bool>(kRuleUse, rule.m_use);
		ibFilterDescriptionMemory::WriteNode(child, rule.m_condition);
		WriteFieldList(child, kRuleFields, rule.m_fields);
		if (!rule.m_appearance.IsEmpty())
			WriteAppearance(child.Child(kRuleAppearance), rule.m_appearance);
	}
}
} // namespace

bool ibParameterDescriptionMemory::ReadNode(const ibDataNode& node,
	std::vector<ibParameterDescription>& parameters)
{
	std::vector<ibParameterDescription> read;
	for (const ibDataNode& child : node.Children()) {
		if (child.GetClsid() != g_parameterNodeClsid)
			continue;   // variants and resources are children here too
		ibParameterDescription parameter;
		parameter.m_name = child.GetValue<ibString>(kParamName);
		if (parameter.m_name.IsEmpty())
			continue;   // a nameless parameter answers nothing
		parameter.m_expression   = child.GetValue<ibString>(kParamExpression);
		parameter.m_userSettable = child.GetValue<std::int32_t>(kParamUser) != 0;
		parameter.m_fromQuery    = child.GetValue<std::int32_t>(kParamFromQuery) != 0;
		// ⭐⭐ THE NODE IS TAKEN AS IT IS. Nothing is built here: a parameter's value is a BLOB in this
		// tier, and it is read into a runtime value by whoever runs the composition, against the
		// configuration it runs in. Building one here made a reference — a runtime object — in the
		// middle of a configuration load, where the type it names may still be three branches away
		// (Max, 2026-08-29: "the schema keeps the serialised node, and the runtime reads it and
		// writes it back itself").
		if (auto value = child.FindChild(kParamValue))
			parameter.m_value = *value;
		ibTypeDescriptionMemory::ReadNode(child.GetProperty(kParamType), parameter.m_type);
		read.push_back(parameter);
	}
	if (!read.empty())
		parameters = read;
	return true;
}

bool ibParameterDescriptionMemory::WriteNode(ibDataNode& node,
	const std::vector<ibParameterDescription>& parameters, bool full)
{
	for (size_t i = 0; i < parameters.size(); ++i) {
		const ibParameterDescription& parameter = parameters[i];
		ibDataNode& sub = node.AddChild(g_parameterNodeClsid, static_cast<ibMetaID>(i));
		sub.SetValue<ibString>(kParamName, parameter.m_name);
		sub.SetValue<ibString>(kParamExpression, parameter.m_expression);
		// …AND GOES BACK AS IT IS. It was packed by whoever put it there — the designer choosing a
		// declared value, the runtime writing one back — so writing is copying, byte for byte.
		sub.Child(kParamValue) = parameter.m_value;
		if (!full)
			continue;   // a VARIANT keeps only what it owns — see the header
		sub.SetValue<std::int32_t>(kParamUser, parameter.m_userSettable ? 1 : 0);
		sub.SetValue<std::int32_t>(kParamFromQuery, parameter.m_fromQuery ? 1 : 0);
		ibDataValue declared;
		ibTypeDescriptionMemory::WriteNode(declared, parameter.m_type);
		sub.SetProperty(kParamType, declared);
	}
	return true;
}

// ⭐ A NAME READ OUT LOUD — see the header. The rule is the one a person applies without thinking:
// a capital starts a word, unless it stands INSIDE a run of capitals (`IDNumber` → "ID Number", not
// "I D Number" — the run ends where a lower-case letter follows).
//
// A name already written for a reader — one that holds a space — comes back untouched, which is
// what makes this safe to apply everywhere rather than only where it was noticed to help.
ibString ibTitleFromName(const ibString& name)
{
	if (name.IsEmpty() || name.Find(ibT(' ')) != wxNOT_FOUND)
		return name;

	ibString title;
	title.reserve(name.length() + 4);
	for (size_t i = 0; i < name.length(); ++i) {
		const ibChar ch   = name[i];
		const ibChar prev = i > 0 ? name[i - 1] : ibChar(0);
		const ibChar next = i + 1 < name.length() ? name[i + 1] : ibChar(0);
		const bool startsWord = i > 0 && ibIsUpper(ch)
			&& (!ibIsUpper(prev) || (next != 0 && ibIsLower(next)));
		if (startsWord)
			title += ibT(' ');
		title += ch;
	}
	return title;
}

ibString ibNameFromPath(const ibString& path)
{
	const int dot = path.Find(ibT('.'), /*fromEnd*/true);
	return dot == wxNOT_FOUND ? path : path.Mid(dot + 1);
}

const ibSelectDescription* ibSelectOfPath(const std::vector<ibSelectDescription>& selects, const ibString& path)
{
	const int dot = path.Find(ibT('.'), /*fromEnd*/true);
	if (dot != wxNOT_FOUND) {
		// THE QUALIFIER — matched by IDENTITY first and by name second (ibSelectDescription::Matches).
		// Today every stored path carries a name, because a path is the text that goes into the
		// query; asked this way, the day a path carries an id instead, nothing here changes.
		const ibString qualifier = path.Left(dot);
		for (const ibSelectDescription& select : selects)
			if (select.Matches(qualifier))
				return &select;
	}
	// UNQUALIFIED CAN ONLY MEAN THE ONE THERE IS — which is precisely why a second select makes the
	// name compulsory: with two of them an unqualified path names nothing in particular.
	return selects.size() == 1 ? &selects.front() : nullptr;
}

ibString ibTitleForPath(const std::vector<ibSelectDescription>& selects, const ibString& path, const ibString& caption)
{
	const ibString leaf = ibNameFromPath(path);
	if (const ibSelectDescription* select = ibSelectOfPath(selects, path))
		if (const ibFieldDescription* field = select->Find(leaf))
			return field->TitleInForce(caption);
	return !caption.IsEmpty() ? caption : ibTitleFromName(leaf);
}

ibBalanceRole ibRoleForPath(const std::vector<ibSelectDescription>& selects, const ibString& path, ibBalanceRole fromSource)
{
	if (const ibSelectDescription* select = ibSelectOfPath(selects, path))
		if (const ibFieldDescription* field = select->Find(ibNameFromPath(path)))
			return field->RoleInForce(fromSource);
	return fromSource;
}

ibAppearanceDescription ibAppearanceForPath(const std::vector<ibSelectDescription>& selects, const ibString& path)
{
	if (const ibSelectDescription* select = ibSelectOfPath(selects, path))
		if (const ibFieldDescription* field = select->Find(ibNameFromPath(path)))
			return field->m_appearance;
	return ibAppearanceDescription();
}

// --- APPEARANCE — see the header ---------------------------------------------

const std::vector<ibAppearanceParameter>& ibAppearanceParameters()
{
	static const std::vector<ibAppearanceParameter> s_parameters = {
		ibAppearanceParameter::BackgroundColour,
		ibAppearanceParameter::TextColour,
		ibAppearanceParameter::Font,
		ibAppearanceParameter::HorizontalAlignment,
		ibAppearanceParameter::Format,
		ibAppearanceParameter::Text,
	};
	return s_parameters;
}

ibString ibAppearanceParameterCaption(ibAppearanceParameter parameter)
{
	switch (parameter) {
		case ibAppearanceParameter::Format:              return _("Format");
		case ibAppearanceParameter::BackgroundColour:    return _("Background colour");
		case ibAppearanceParameter::TextColour:          return _("Text colour");
		case ibAppearanceParameter::Font:                return _("Font");
		case ibAppearanceParameter::HorizontalAlignment: return _("Horizontal alignment");
		case ibAppearanceParameter::Text:                return _("Text");
	}
	return ibEmptyString;
}

// --- OTHER SETTINGS — see the header -----------------------------------------

const std::vector<ibOutputParameter>& ibOutputParameters(ibOutputParameterScope scope)
{
	static const std::vector<ibOutputParameter> s_report = {
		ibOutputParameter::Theme,
		ibOutputParameter::Title,
		ibOutputParameter::ShowTitle,
		ibOutputParameter::ShowFilter,
		ibOutputParameter::ShowParameters,
	};
	static const std::vector<ibOutputParameter> s_node = {
		ibOutputParameter::Theme,
	};
	return scope == ibOutputParameterScope::Node ? s_node : s_report;
}

ibString ibOutputParameterCaption(ibOutputParameter parameter)
{
	switch (parameter) {
		case ibOutputParameter::Theme:          return _("Theme");
		case ibOutputParameter::Title:          return _("Title");
		case ibOutputParameter::ShowTitle:      return _("Show title");
		case ibOutputParameter::ShowFilter:     return _("Show filter");
		case ibOutputParameter::ShowParameters: return _("Show parameters");
	}
	return ibEmptyString;
}

ibString ibShowModeCaption(ibShowMode mode)
{
	switch (mode) {
		case ibShowMode::Auto: return _("Auto");
		case ibShowMode::Show: return _("Show");
		case ibShowMode::Hide: return _("Do not show");
	}
	return ibEmptyString;
}

ibFieldDescription* ibFieldEntryForPath(std::vector<ibSelectDescription>& selects, const ibString& path)
{
	// THE SELECT A COMPOSITION STARTS WITH — made the first time anything is said about one of its
	// fields, since a description that said nothing carries none.
	if (selects.empty())
		selects.emplace_back();
	const ibSelectDescription* named = ibSelectOfPath(selects, path);
	if (named == nullptr)
		return nullptr;
	ibSelectDescription& select = selects[static_cast<size_t>(named - selects.data())];

	// THE SAME FIELD is the one whose name in force ends in the same word — ibSelectDescription::Find's
	// rule, asked here of the writable list.
	const ibString leaf = ibNameFromPath(path);
	for (ibFieldDescription& field : select.m_fields)
		if (ibNameFromPath(field.NameInForce()).IsSameAs(leaf, false))
			return &field;

	ibFieldDescription made;
	made.m_path = path;
	select.m_fields.push_back(std::move(made));
	return &select.m_fields.back();
}

// ===========================================================================
//  Resources — the aggregates the levels fold
// ===========================================================================
bool ibResourceDescriptionMemory::ReadNode(const ibDataNode& node, std::vector<ibResourceDescription>& resources)
{
	resources.clear();
	for (const ibDataNode& child : node.Children()) {
		if (child.GetClsid() != g_resourceNodeClsid)
			continue;
		const ibString path = child.GetValue<ibString>(kResourcePath);
		if (path.IsEmpty())
			continue;
		// The alias is OPTIONAL and absence reads back as absence — a file written before resources
		// could be named holds no such key and means "name it after the argument", which is what an
		// empty alias says.
		ibResourceDescription resource;
		resource.m_func  = child.GetValue<ibString>(kResourceFunc);
		resource.m_path  = path;
		resource.m_alias = child.GetValue<ibString>(kResourceAlias);
		resource.m_scope = child.GetValue<ibString>(kResourceScope);
		resources.push_back(std::move(resource));
	}
	return true;
}

bool ibResourceDescriptionMemory::WriteNode(ibDataNode& node, const std::vector<ibResourceDescription>& resources)
{
	for (size_t i = 0; i < resources.size(); ++i) {
		if (resources[i].m_path.IsEmpty())
			continue;
		ibDataNode& sub = node.AddChild(g_resourceNodeClsid, static_cast<ibMetaID>(i));
		sub.SetValue<ibString>(kResourceFunc, resources[i].m_func);   // empty = the path IS the expression
		sub.SetValue<ibString>(kResourcePath, resources[i].m_path);
		if (!resources[i].m_alias.IsEmpty())
			sub.SetValue<ibString>(kResourceAlias, resources[i].m_alias);
		if (!resources[i].m_scope.IsEmpty())
			sub.SetValue<ibString>(kResourceScope, resources[i].m_scope);
	}
	return true;
}

// ===========================================================================
//  Fields — what each of them is called
// ===========================================================================
namespace {

// The field entries of ONE select — read and written under its own node, which is what keeps two
// selects' identically-named fields apart.
void ReadSelectFields(const ibDataNode& node, std::vector<ibFieldDescription>& fields)
{
	fields.clear();
	for (const ibDataNode& child : node.Children()) {
		if (child.GetClsid() != g_fieldInfoNodeClsid)
			continue;
		const ibString path = child.GetValue<ibString>(kFieldInfoPath);
		if (path.IsEmpty())
			continue;   // an entry about no field is not an entry
		ibFieldDescription field;
		field.m_path     = path;
		field.m_name     = child.GetValue<ibString>(kFieldInfoName);
		field.m_useTitle = child.GetValue<bool>(kFieldInfoUseTitle);
		field.m_title    = child.GetValue<ibString>(kFieldInfoTitle);
		// THE ROLE IS WRITTEN ONLY WHEN A PERSON SAID ONE — its presence is what `m_useRole` means, so an
		// entry written before roles existed reads as "the source's", which is what it always was.
		if (child.Has(kFieldInfoRole)) {
			const std::int32_t role = child.GetValue<std::int32_t>(kFieldInfoRole);
			if (role >= static_cast<std::int32_t>(ibBalanceRole::None) && role <= static_cast<std::int32_t>(ibBalanceRole::Closing)) {
				field.m_useRole = true;
				field.m_role    = static_cast<ibBalanceRole>(role);
				if (field.m_role == ibBalanceRole::Moment)
					field.m_periodRank = child.GetValue<std::int32_t>(kFieldInfoPeriodRank);   // absent reads as 0: in order
			}
		}
		if (auto appearance = child.FindChild(kFieldInfoAppearance))
			ReadAppearance(*appearance, field.m_appearance);
		fields.push_back(std::move(field));
	}
}

// DOES THIS FIELD SAY ANYTHING BEYOND ITS OWN NAME? A title nobody took over and a name that is the
// path's own leaf are both "nothing was said" — and writing them would freeze a caption that is
// supposed to keep following the name.
bool SaysAnything(const ibFieldDescription& field)
{
	return !field.m_path.IsEmpty()
	    && (field.m_useTitle || field.m_useRole || !field.m_appearance.IsEmpty() || !field.m_name.IsEmpty());
}

bool SaysAnything(const std::vector<ibFieldDescription>& fields)
{
	for (const ibFieldDescription& field : fields)
		if (SaysAnything(field))
			return true;
	return false;
}

void WriteSelectFields(ibDataNode& node, const std::vector<ibFieldDescription>& fields)
{
	for (size_t i = 0; i < fields.size(); ++i) {
		if (!SaysAnything(fields[i]))
			continue;
		ibDataNode& sub = node.AddChild(g_fieldInfoNodeClsid, static_cast<ibMetaID>(i));
		sub.SetValue<ibString>(kFieldInfoPath, fields[i].m_path);
		if (!fields[i].m_name.IsEmpty())
			sub.SetValue<ibString>(kFieldInfoName, fields[i].m_name);
		if (fields[i].m_useTitle) {
			sub.SetValue<bool>(kFieldInfoUseTitle, true);
			sub.SetValue<ibString>(kFieldInfoTitle, fields[i].m_title);
		}
		if (fields[i].m_useRole)
			sub.SetValue<std::int32_t>(kFieldInfoRole, static_cast<std::int32_t>(fields[i].m_role));
		if (fields[i].m_useRole && fields[i].m_periodRank > 0)
			sub.SetValue<std::int32_t>(kFieldInfoPeriodRank, fields[i].m_periodRank);
		if (!fields[i].m_appearance.IsEmpty())
			WriteAppearance(sub.Child(kFieldInfoAppearance), fields[i].m_appearance);
	}
}

} // namespace

bool ibSelectDescriptionMemory::ReadNode(const ibDataNode& node, std::vector<ibSelectDescription>& selects)
{
	selects.clear();
	for (const ibDataNode& child : node.Children()) {
		if (child.GetClsid() != g_selectNodeClsid)
			continue;
		ibSelectDescription select;
		select.m_id   = child.GetValue<ibString>(kSelectId);
		select.m_name = child.GetValue<ibString>(kSelectName);
		ReadSelectFields(child, select.m_fields);
		selects.push_back(std::move(select));
	}
	return true;
}

bool ibSelectDescriptionMemory::WriteNode(ibDataNode& node, const std::vector<ibSelectDescription>& selects)
{
	for (size_t i = 0; i < selects.size(); ++i) {
		// ⚠ A SELECT THAT SAID NOTHING IS NOT WRITTEN — it is not a fact about the report, it is the
		// absence of one, and the selects themselves live in the query text. What IS written stays
		// written: an id, once minted, is what every path refers to.
		if (selects[i].m_name.IsEmpty() && !SaysAnything(selects[i].m_fields))
			continue;
		ibDataNode& sub = node.AddChild(g_selectNodeClsid, static_cast<ibMetaID>(i));
		if (!selects[i].m_id.IsEmpty())
			sub.SetValue<ibString>(kSelectId, selects[i].m_id);
		if (!selects[i].m_name.IsEmpty())
			sub.SetValue<ibString>(kSelectName, selects[i].m_name);
		WriteSelectFields(sub, selects[i].m_fields);
	}
	return true;
}

// ===========================================================================
//  Variants — settings + structure + the values they were saved with
// ===========================================================================
bool ibVariantDescriptionMemory::ReadNode(const ibDataNode& node,
	std::vector<ibVariantDescription>& variants)
{
	std::vector<ibVariantDescription> read;
	for (const ibDataNode& child : node.Children()) {
		if (child.GetClsid() != g_variantNodeClsid)
			continue;   // the filter tree is a child of this node too — it is not a variant
		// ⭐ A VARIANT IS A NAME, A SYNONYM AND A SETTING — the whole of it, and the setting writes
		// itself whole (its outputs included). The parameters that used to be read here were a
		// second list beside the composition's own and went with the member.
		ibVariantDescription variant;
		variant.m_name    = child.GetValue<ibString>(kVariantName);
		variant.m_synonym = child.GetValue<ibString>(kVariantSynonym);   // absent in an older file
		ibSettingsDescriptionMemory::ReadNode(child, variant.m_settings);
		read.push_back(variant);
	}
	if (read.empty())
		return false;   // an older record — one set of settings, written before variants existed

	variants = read;
	return true;
}

bool ibVariantDescriptionMemory::WriteNode(ibDataNode& node,
	const std::vector<ibVariantDescription>& variants)
{
	for (size_t i = 0; i < variants.size(); ++i) {
		ibDataNode& sub = node.AddChild(g_variantNodeClsid, static_cast<ibMetaID>(i));
		sub.SetValue<ibString>(kVariantName, variants[i].m_name);
		sub.SetValue<ibString>(kVariantSynonym, variants[i].m_synonym);
		ibSettingsDescriptionMemory::WriteNode(sub, variants[i].m_settings);
	}
	return true;
}

// ⭐ THE VALUE FORM — the door a PROPERTY goes through, and the reason its read and write are one
// line each (ibSourceDescriptionMemory's shape). The value carries the description's own node.
bool ibCompositionDescriptionMemory::ReadNode(const ibDataValue& value, ibCompositionDescription& composition)
{
	if (value.Kind() != ibDataKind::Child)
		return false;
	return ReadNode(*value.AsChild(), composition);
}

bool ibCompositionDescriptionMemory::WriteNode(ibDataValue& value, const ibCompositionDescription& composition)
{
	std::shared_ptr<ibDataNode> packed = std::make_shared<ibDataNode>();
	if (!WriteNode(*packed, composition))
		return false;
	value = ibDataValue::Child(packed);
	return true;
}

// ⭐ THE NAMES THE IDS WERE MADE FROM — see the note in the header. One list, built from the same
// literals the constants above use, so a name and its id cannot part company: each entry is the
// constant itself beside the word it was spelled with.
// ⭐⭐ ONE TABLE, READ BOTH WAYS. A second list would be a second thing to keep in step, and the day
// it fell behind the failure would be a node silently arriving as type 0 — which is exactly the
// failure this pair exists to end.
static const std::pair<ibClassID, const ibChar*>* ibCompositionNodeTable(size_t& count)
{
	static const std::pair<ibClassID, const ibChar*> s_names[] = {
		{ g_variantNodeClsid,   ibT("CompositionVariant")    },
		{ g_outputNodeClsid,    ibT("CompositionOutput")     },
		{ g_levelNodeClsid,     ibT("CompositionLevel")      },
		{ g_fieldNodeClsid,     ibT("CompositionField")      },
		{ g_filterNodeClsid,    ibT("CompositionFilterNode") },
		{ g_orderNodeClsid,     ibT("CompositionOrderLine")  },
		{ g_groupNodeClsid,     ibT("CompositionGroupLine")  },
		{ g_parameterNodeClsid, ibT("CompositionParameter")  },
		{ g_resourceNodeClsid,  ibT("CompositionResource")   },
		{ g_selectNodeClsid,    ibT("CompositionSelect")     },
		{ g_fieldInfoNodeClsid, ibT("CompositionFieldInfo")  },
		{ g_appearanceNodeClsid, ibT("CompositionAppearance") },
	};

	count = sizeof(s_names) / sizeof(s_names[0]);
	return s_names;
}

ibString ibCompositionNodeName(ibClassID clsid)
{
	size_t count = 0;
	const std::pair<ibClassID, const ibChar*>* const names = ibCompositionNodeTable(count);

	for (size_t i = 0; i < count; ++i)
		if (names[i].first == clsid)
			return names[i].second;

	return ibEmptyString;
}

// ⭐⭐ AND BACK, WHICH IS WHAT MAKES A ROUND TRIP POSSIBLE. An answer renders these types as WORDS so
// a reader sees `"NodeType": "CompositionParameter"` instead of seventeen digits — and until this
// existed, that answer could not be handed back: the JSON reader turned an unknown word into clsid 0,
// so a schema read, edited and returned lost every part it was made of, silently. The composition
// then had no parameters, no outputs and no fields, and refused as though the caller had sent an
// empty one (measured 2026-09-06, building a two-output schema by hand).
//
// ⚠ IT IS THE SAME TABLE. The names ARE the literals the ids are made from, so this cannot drift
// from the writer without both being edited at once.
ibClassID ibCompositionNodeClsid(const ibString& name)
{
	if (name.IsEmpty())
		return ibClassID(0);

	size_t count = 0;
	const std::pair<ibClassID, const ibChar*>* const names = ibCompositionNodeTable(count);

	for (size_t i = 0; i < count; ++i)
		if (name == names[i].second)
			return names[i].first;

	return ibClassID(0);
}
