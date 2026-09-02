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

#include "backend/serialize/dataBuilder.h"         // ibDataNode / ibDataValue
#include "backend/metaData.h"                      // ibMetaData::Deserialize — the door a caller HANDS IN

#include "backend/backend_exception.h"             // ibBackendCoreException — a setting that cannot apply is refused
#include "backend/compiler/valueSerialization.h"    // ibReadNodeType — whose value is in this node
#include "backend/system/value/composition/valueComposerField.h"   // the declared value this tier vends

#include <algorithm>                               // std::find — is this value one the field admits?

// ===========================================================================

// ===========================================================================
//  Structure — the outputs of a variant, their levels, and the fields inside
// ===========================================================================
namespace {
// The node a variant is written into. Not a registered value type — a variant is a record inside
// the composition, not something a script hands around.
constexpr ibClassID g_variantNodeClsid = make_clsid("CompositionVariant", ibClassKind_None);
const wxString  kVariantName       = wxT("Name");
// …AND WHAT A PICKER SHOWS. A variant is a name, a SYNONYM and a setting; the synonym is what a
// person reads in the list, and empty means "derive it from the name" (the rule every metaobject's
// synonym follows).
const wxString  kVariantSynonym    = wxT("Synonym");

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
const wxString  kStructureNode  = wxT("Structure");
const wxString  kRowsNode       = wxT("Rows");
const wxString  kColumnsNode    = wxT("Columns");
const wxString  kSelectedNode   = wxT("Selected");
// ⚠ THE READER'S OWN TABLE NEEDS A NAME OF ITS OWN. A LEVEL writes two things into one node — its
// own selected fields (`kSelectedNode`) and its settings, through the settings pair — so if the
// settings wrote their table under the same name, the two would land on top of each other. Today a
// level's settings carry no fields and nothing collides; a name apart is what keeps that true when
// somebody fills them.
const wxString  kSelectedFieldsNode = wxT("SelectedFields");
const wxString  kPathName       = wxT("Path");
const wxString  kKindName       = wxT("Kind");
// A LEVEL'S OWN SORT AND FILTER — written inside the level, because that is where they belong.
//
// (⛔ A SECOND SPELLING OF THE SORT SECTION STOOD HERE — `g_sortNodeClsid`, `kSortNode = "Sort"`,
//  `kAscendingName = "Ascending"` — declared and used ZERO times, eight lines from the live pair.
//  On disk the section is `kOrderNode = "Order"` with lines of class `CompositionOrderLine`, and
//  `kDirName` already spells "Ascending". A rename was started at the constants and never carried
//  through; what it left behind was a dead vocabulary that reads as if it were the format.)
const wxString  kFilterNode     = wxT("Filter");
// WHAT THE LEVEL IS — a grouping or the detail records. Absent in a file written before detail
// levels existed, and absence reads back as Grouping, which is what every level in such a file is.
const wxString  kLevelKindName  = wxT("LevelKind");
const wxString  kOutputKindName = wxT("OutputKind");
// WHAT UNFOLDS UNDER A NODE — written INSIDE it, so where a node is written is which node it belongs
// to. The axes on an output (Rows / Columns) are the same shape at the top; this is every storey below.
const wxString  kChildrenNode   = wxT("Children");

// The axis writer / reader, declared here because a LEVEL uses them for its own children — a node
// and the run of nodes under it are mutually recursive, which is what a tree is.
void WriteLevels(ibDataNode& parent, const wxString& name, const std::vector<ibLevelDescription>& levels);
void ReadLevels(const ibDataNode& parent, const wxString& name,
                std::vector<ibLevelDescription>& levels, const ibMetaData* metaData);

void WriteFieldList(ibDataNode& node, const wxString& name, const std::vector<wxString>& list)
{
	if (list.empty())
		return;   // an empty set writes nothing — absence reads back as absence
	ibDataNode& sub = node.Child(name);
	for (size_t i = 0; i < list.size(); ++i)
		sub.AddChild(g_fieldNodeClsid, static_cast<ibMetaID>(i)).SetValue<wxString>(kPathName, list[i]);
}

void ReadFieldList(const ibDataNode& node, const wxString& name, std::vector<wxString>& list)
{
	list.clear();
	const ibDataNode* sub = node.FindChild(name);
	if (sub == nullptr)
		return;
	for (const ibDataNode& child : sub->Children())
		if (child.GetClsid() == g_fieldNodeClsid)
			list.push_back(child.GetValue<wxString>(kPathName));
}

// ⭐⭐ THE SELECTED-FIELDS TABLE — the same node shape as a plain field list, plus the row's KIND.
//
// ⚠ AND THE KIND IS WRITTEN ONLY WHEN IT IS NOT A FIELD. Everything saved before this is a list of
// paths, and a path with no kind beside it IS a field — so old settings read back unchanged, and a
// table of ordinary fields keeps writing exactly what it wrote before. `Auto` carries no path: it
// names nothing, it says WHERE what the storey above chose lands.
void WriteSelectedList(ibDataNode& node, const wxString& name,
                       const std::vector<ibSelectedFieldDescription>& list)
{
	if (list.empty())
		return;   // an empty table writes nothing — absence reads back as absence, and absence inherits
	ibDataNode& sub = node.Child(name);
	for (size_t i = 0; i < list.size(); ++i) {
		ibDataNode& row = sub.AddChild(g_fieldNodeClsid, static_cast<ibMetaID>(i));
		row.SetValue<wxString>(kPathName, list[i].m_path);
		// ⚠ `kKindName` is spelled on a node of ANOTHER class above (a filter group's); a field node
		// has no kind of its own, so the word is free here and means what it says.
		if (list[i].m_kind != ibSelectedFieldKind_Field)
			row.SetValue<s32>(kKindName, static_cast<s32>(list[i].m_kind));
	}
}

void ReadSelectedList(const ibDataNode& node, const wxString& name,
                      std::vector<ibSelectedFieldDescription>& list)
{
	list.clear();
	const ibDataNode* sub = node.FindChild(name);
	if (sub == nullptr)
		return;
	for (const ibDataNode& child : sub->Children()) {
		if (child.GetClsid() != g_fieldNodeClsid)
			continue;
		ibSelectedFieldDescription row;
		// A record that predates the kind has none — and it is a FIELD, which is what every one of
		// them was. (ibDataNode answers a missing value with the type's default, and 0 IS Field.)
		row.m_kind = static_cast<ibSelectedFieldKind>(child.GetValue<s32>(kKindName));
		row.m_path = child.GetValue<wxString>(kPathName);
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
	node.SetValue<s32>(kLevelKindName, static_cast<s32>(level.m_kind));
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

bool ibLevelDescriptionMemory::ReadNode(const ibDataNode& node, ibLevelDescription& level, const ibMetaData* metaData)
{
	level.m_kind = static_cast<ibCompositionLevelKind>(node.GetValue<s32>(kLevelKindName));
	ReadSelectedList(node, kSelectedNode, level.m_selected);

	// ITS SETTINGS, back the way they were written — through the one pair. The filter comes back as
	// the LINES it is; the expression the engine runs is built from them once the composer is in
	// hand, because building it needs the parameters the composer names.
	ibSettingsDescriptionMemory::ReadNode(node, level.m_settings, metaData);

	// …AND WHAT UNFOLDS UNDER IT, read from where it was written — inside this node.
	ReadLevels(node, kChildrenNode, level.m_children, metaData);
	return true;
}

namespace {

// THE AXIS — a run of levels under one name (rows, columns). It combines levels and knows nothing
// of what a level is made of.
void WriteLevels(ibDataNode& parent, const wxString& name, const std::vector<ibLevelDescription>& levels)
{
	if (levels.empty())
		return;
	ibDataNode& axis = parent.Child(name);
	for (size_t i = 0; i < levels.size(); ++i)
		ibLevelDescriptionMemory::WriteNode(axis.AddChild(g_levelNodeClsid, static_cast<ibMetaID>(i)), levels[i]);
}

void ReadLevels(const ibDataNode& parent, const wxString& name, std::vector<ibLevelDescription>& levels,
	const ibMetaData* metaData)
{
	levels.clear();
	const ibDataNode* axis = parent.FindChild(name);
	if (axis == nullptr)
		return;
	for (const ibDataNode& node : axis->Children()) {
		if (node.GetClsid() != g_levelNodeClsid)
			continue;
		ibLevelDescription level;
		ibLevelDescriptionMemory::ReadNode(node, level, metaData);
		levels.push_back(std::move(level));
	}
}

} // namespace

// ===========================================================================
//  AN OUTPUT — what a report shows: its rows, its columns, and what it reads
// ===========================================================================
bool ibOutputDescriptionMemory::WriteNode(ibDataNode& node, const ibOutputDescription& output)
{
	node.SetValue<wxString>(kVariantName, output.m_name);
	// ⭐ AND WHAT IT IS. Stored because it is a DECISION — "add grouping" or "add table" — and a
	// table that has not been filled in yet is empty on both axes: read back off the content, it
	// would come up a grouping and the person's second axis would have nowhere to go.
	node.SetValue<s32>(kOutputKindName, static_cast<s32>(output.m_kind));
	WriteSelectedList(node, kSelectedNode, output.m_selected);
	// ITS SETTINGS, written the way a level writes them — one storey up, same shape, same pair.
	ibSettingsDescriptionMemory::WriteNode(node, output.m_settings);
	// AND ITS TWO AXES. A grouping output has rows; a TABLE has rows AND columns, and that is the
	// whole of the difference — each axis is a run of ROOT nodes, and what unfolds under one is on it.
	WriteLevels(node, kRowsNode, output.m_rowGroups);
	WriteLevels(node, kColumnsNode, output.m_columnGroups);
	return true;
}

bool ibOutputDescriptionMemory::ReadNode(const ibDataNode& node, ibOutputDescription& output, const ibMetaData* metaData)
{
	output.m_name          = node.GetValue<wxString>(kVariantName);
	// (The "Source" property an older record carries is READ BY NOBODY — see ibOutputDescription.)
	ReadSelectedList(node, kSelectedNode, output.m_selected);
	ibSettingsDescriptionMemory::ReadNode(node, output.m_settings, metaData);
	ReadLevels(node, kRowsNode, output.m_rowGroups, metaData);
	ReadLevels(node, kColumnsNode, output.m_columnGroups, metaData);
	// ⚠ A RECORD WRITTEN BEFORE THE KIND EXISTED reads back 0 — Grouping — and that is right for
	// every one of them except a table, which could not be authored then. So the content answers
	// where the record is silent: a stored column axis means a table, whatever the number says.
	output.m_kind = static_cast<ibCompositionOutputKind>(node.GetValue<s32>(kOutputKindName));
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

bool ibStructureDescriptionMemory::ReadNode(const ibDataNode& node, std::vector<ibOutputDescription>& outputs, const ibMetaData* metaData)
{
	const ibDataNode* structure = node.FindChild(kStructureNode);
	if (structure == nullptr)
		return false;   // written before there was a structure — the caller keeps what it had

	std::vector<ibOutputDescription> read;
	for (const ibDataNode& sub : structure->Children()) {
		if (sub.GetClsid() != g_outputNodeClsid)
			continue;
		ibOutputDescription output;
		ibOutputDescriptionMemory::ReadNode(sub, output, metaData);
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

// ⭐⭐ A FILTER VALUE COMES BACK THROUGH THE CONFIGURATION'S OWN DOOR, AND THE DOOR IS HANDED IN.
// A filter compares against references and enum members, whose types exist only in the metadata's
// own registry; asking the value factory for one raises "Unknown value type '<id>' in the data" on
// a filter that was saved perfectly well. `ibMetaData::Deserialize` IS that door.
//
// ⚠ BUT A DESCRIPTION DOES NOT REACH FOR IT (Max, 2026-08-23: "the metadata leaked into the
// description" — it was calling `activeMetaData`). Nothing here resolves anything, and reaching for
// the ACTIVE configuration is the worst way to break that rule: two are open in the designer at
// once, so a description read while the other one is active comes back about somebody else's
// metadata. Whoever reads a description knows which configuration it belongs to and says so; null
// means there is none (tests, headless tools) and the value factory answers alone.
ibValue ReadStoredValue(const ibDataNode& node, const ibMetaData* metaData) {
	return metaData != nullptr ? metaData->Deserialize(node) : ibValue::FromNode(node);
}
} // namespace

// THE TWO DOORS — see compositionDescription.h. A stored node becomes a value only when somebody
// asks, with the configuration to ask against; a value becomes a node by packing itself.
ibValue ibStoredValue(const ibDataNode& stored, const ibMetaData* metaData)
{
	// NOTHING WAS STORED = an empty value, not a failure. A value writes its type into a field of the
	// node, so a node with no fields is a value nobody ever put there.
	if (stored.Fields().empty())
		return ibValue();

	// ⭐⭐ ITS OWN VALUE, BUILT BY ITS OWNER. `CompositionPredefinedValue` is a SYSTEM type — the value
	// factory does not create those, and nothing writes `New` of it. It belongs to this tier: the
	// designer's window makes one when a declared value is chosen, and here is where the store makes
	// it back out of what it kept. Everything else goes through the configuration's own door.
	if (ibReadNodeType(stored) == g_compositionPredefinedCLSID) {
		ibValueCompositionPredefined* declared = new ibValueCompositionPredefined();
		declared->Deserialize(stored);
		return ibValue(declared);
	}

	return ReadStoredValue(stored, metaData);
}

void ibStoreValue(ibDataNode& stored, const ibValue& value)
{
	stored = ibDataNode();
	if (!value.IsEmpty())
		value.Serialize(stored);
}

namespace {

// ⚠ AN OPAQUE KEY, NOT A NAME. The tier says **sort** everywhere (ibSortDescription, m_sort,
// SetSort); this string is what the bytes on disk already say, and renaming it would make every
// saved setting unreadable to buy a word. Same call the CLSID keys took when their concepts were
// renamed (`MD_SKTB` after subconto → account dimension).
const wxChar* const kOrderNode = wxT("Order");
const wxChar* const kGroupNode = wxT("Group");
const wxChar* const kFieldName = wxT("Field");
const wxChar* const kDirName   = wxT("Ascending");

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
const wxChar* const kNodeKindName = wxT("Node");
const wxChar* const kUseName      = wxT("Use");
const wxChar* const kDisplayName  = wxT("Display");
const wxChar* const kTextName     = wxT("Text");
// ⚠ NAMED APART FROM THE LEVEL'S `kKindName` ABOVE, which is a different field of a different node
// that happens to be spelled the same on disk. Two anonymous namespaces in one file are ONE
// namespace, so the collision is a compile error rather than a subtle mix-up — but the reason the
// two names exist is worth saying: a grouping LINE's unfold and a LEVEL's kind are not one thing.
const wxChar* const kGroupKindName = wxT("Kind");
// BY PERIODS — the three parts of one answer, written only when the line has one (see the writer).
const wxChar* const kPeriodUnitName = wxT("PeriodUnit");
const wxChar* const kPeriodFromName = wxT("PeriodFrom");
const wxChar* const kPeriodToName   = wxT("PeriodTo");

// THE LIST'S OWN HALF — the main table, written only when there is one.
const wxChar* const kMainTableName = wxT("MainTable");
// …AND THE QUERY, which is the composition's real source. Kept under the name the composition's own
// property already used, so a record written before this file existed reads back unchanged.
const wxChar* const kQueryName     = wxT("Query");

} // namespace


// ===========================================================================
//  Filter — the nodes, flat, in display order
// ===========================================================================
//
// ⭐ ONE NODE PER LINE, and the tree is the parent index. The filter used to be written as a live
// object packing itself (a root ibValueFilterGroup with children), which meant reading it back was
// creating objects — through the configuration's door, because a condition compares against
// references and enum members. As plain lines there is nothing to create: only the VALUE on the
// right-hand side is a value, and it goes through that same door on its own.
//
// A FILTER VALUE COMES BACK THROUGH THE CONFIGURATION'S OWN DOOR. It compares against references and
// enum members, whose types exist only in the metadata's own registry; asking the value factory for
// one raises "Unknown value type '<id>' in the data" on a filter that was saved perfectly well.
// ibMetaData::Deserialize IS that door.
namespace {
void ibWriteOperand(ibDataNode& node, const wxString& prefix, const ibFilterOperandDescription& side)
{
	node.SetValue<wxString>(prefix + wxT("Path"), side.m_path);
	node.SetValue<wxString>(prefix + wxT("Text"), side.m_presentation);
	node.SetValue<s32>(prefix + wxT("Leaf"), static_cast<s32>(side.m_leafId));
	// The TYPE is deliberately NOT packed — it is derived from the source the field is bound to, and
	// a saved setting outlives the schema it was saved against.
	if (!side.m_value.IsEmpty())
		side.m_value.Serialize(node.Child(prefix + wxT("Value")));
}

void ibReadOperand(const ibDataNode& node, const wxString& prefix, ibFilterOperandDescription& side,
	const ibMetaData* metaData)
{
	side.m_path         = node.GetValue<wxString>(prefix + wxT("Path"));
	side.m_presentation = node.GetValue<wxString>(prefix + wxT("Text"));
	side.m_leafId       = static_cast<ibMetaID>(node.GetValue<s32>(prefix + wxT("Leaf")));
	side.m_type         = ibTypeDescription();
	side.m_value        = ibValue();
	if (const ibDataNode* value = node.FindChild(prefix + wxT("Value")))
		side.m_value = ReadStoredValue(*value, metaData);
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
		line.SetValue<s32>(kNodeKindName, static_cast<s32>(item.m_kind));
		line.SetValue<s32>(kUseName, item.m_use ? 1 : 0);
		line.SetValue<s32>(kDisplayName, static_cast<s32>(item.m_display));
		line.SetValue<wxString>(kTextName, item.m_presentation);
		if (item.m_kind == ibFilterNodeKind_Group) {
			line.SetValue<s32>(kGroupKindName, static_cast<s32>(item.m_groupKind));
			ibWriteFilterNodes(line, item.m_children);
		}
		else {
			line.SetValue<s32>(kGroupKindName, static_cast<s32>(item.m_comparison));
			ibWriteOperand(line, wxT("L"), item.m_left);
			ibWriteOperand(line, wxT("R"), item.m_right);
		}
	}
}

void ibReadFilterNodes(const ibDataNode& from, std::vector<ibFilterNodeDescription>& nodes,
	const ibMetaData* metaData)
{
	for (const ibDataNode& line : from.Children()) {
		ibFilterNodeDescription item;
		item.m_kind    = static_cast<ibFilterNodeKind>(line.GetValue<s32>(kNodeKindName));
		item.m_use     = line.GetValue<s32>(kUseName) != 0;
		item.m_display = static_cast<ibFilterDisplayMode>(line.GetValue<s32>(kDisplayName));
		item.m_presentation = line.GetValue<wxString>(kTextName);
		if (item.m_kind == ibFilterNodeKind_Group) {
			item.m_groupKind = static_cast<ibFilterGroupKind>(line.GetValue<s32>(kGroupKindName));
			ibReadFilterNodes(line, item.m_children, metaData);
		}
		else {
			item.m_comparison = static_cast<ibComparisonKind>(line.GetValue<s32>(kGroupKindName));
			ibReadOperand(line, wxT("L"), item.m_left, metaData);
			ibReadOperand(line, wxT("R"), item.m_right, metaData);
		}
		nodes.push_back(item);
	}
}
} // namespace

bool ibFilterDescriptionMemory::ReadNode(const ibDataNode& node, ibFilterDescription& filter, const ibMetaData* metaData)
{
	filter.Clear();
	const ibDataNode* sub = node.FindChild(kFilterNode);
	if (sub == nullptr)
		return true;   // no filter written is not a failure — it is a composition without one

	filter.m_rootKind = static_cast<ibFilterGroupKind>(sub->GetValue<s32>(kKindName));
	ibReadFilterNodes(*sub, filter.m_nodes, metaData);
	return true;
}

bool ibFilterDescriptionMemory::WriteNode(ibDataNode& node, const ibFilterDescription& filter)
{
	if (!filter.IsOk())
		return true;   // nothing to write, and absence reads back as absence

	ibDataNode& sub = node.Child(kFilterNode);
	sub.SetValue<s32>(kKindName, static_cast<s32>(filter.m_rootKind));
	ibWriteFilterNodes(sub, filter.m_nodes);
	return true;
}

// ===========================================================================
//  Sort — a path and a direction, per line
// ===========================================================================
bool ibSortDescriptionMemory::ReadNode(const ibDataNode& node, ibSortDescription& sort)
{
	sort.Clear();
	const ibDataNode* order = node.FindChild(kOrderNode);
	if (order == nullptr)
		return true;
	for (const ibDataNode& line : order->Children()) {
		const wxString field = line.GetValue<wxString>(kFieldName);
		if (field.IsEmpty())
			continue;
		sort.Append(field, line.GetValue<s32>(kDirName) != 0);
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
		line.SetValue<wxString>(kFieldName, sort.m_lines[i].m_path);
		line.SetValue<s32>(kDirName, sort.m_lines[i].m_ascending ? 1 : 0);
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
	const ibDataNode* sub = node.FindChild(kGroupNode);
	if (sub == nullptr)
		return true;
	for (const ibDataNode& line : sub->Children()) {
		const wxString field = line.GetValue<wxString>(kFieldName);
		if (field.IsEmpty())
			continue;
		group.Append(field, static_cast<ibQueryDimUnfold>(line.GetValue<s32>(kGroupKindName)));
		// AND ITS PERIODICITY, when it has one. A record written before this existed has no unit,
		// which reads back as "not by periods" — the same answer the member's own IsOk gives.
		ibGroupPeriodsDescription& periods = group.m_lines.back().m_periods;
		periods.m_unit = line.GetValue<wxString>(kPeriodUnitName);
		periods.m_from = line.GetValue<wxString>(kPeriodFromName);
		periods.m_to   = line.GetValue<wxString>(kPeriodToName);
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
		line.SetValue<wxString>(kFieldName, group.m_lines[i].m_path);
		line.SetValue<s32>(kGroupKindName, static_cast<s32>(group.m_lines[i].m_kind));
		// ⭐ THE PERIODICITY, WRITTEN ONLY WHEN THERE IS ONE — three empty properties on every
		// ordinary grouping line would be three ways for a file to look different without meaning
		// anything different, and equality is what "modified" is decided by.
		const ibGroupPeriodsDescription& periods = group.m_lines[i].m_periods;
		if (periods.IsOk()) {
			line.SetValue<wxString>(kPeriodUnitName, periods.m_unit);
			if (!periods.m_from.IsEmpty()) line.SetValue<wxString>(kPeriodFromName, periods.m_from);
			if (!periods.m_to.IsEmpty())   line.SetValue<wxString>(kPeriodToName, periods.m_to);
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
bool ibSettingsDescriptionMemory::ReadNode(const ibDataNode& node, ibSettingsDescription& settings, const ibMetaData* metaData)
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
	const bool ok = ibFilterDescriptionMemory::ReadNode(node, settings.m_filter, metaData)
		&& ibSortDescriptionMemory::ReadNode(node, settings.m_sort)
		&& ibGroupDescriptionMemory::ReadNode(node, settings.m_group);
	ibStructureDescriptionMemory::ReadNode(node, settings.m_structure, metaData);
	// ⭐ AND THE FIELDS THE READER CHOSE — a setting like the three above. Absent from every record
	// written before 2026-08-28, and absence is a legitimate answer: nothing was chosen.
	ReadSelectedList(node, kSelectedFieldsNode, settings.m_selected);
	// ⭐ …AND THE VALUES THE READER FILLED IN. Only the parameters the author offered them ever land
	// here, and only the name and the packed value travel: the declaration stays with the author.
	ibParameterDescriptionMemory::ReadNode(node, settings.m_parameters, metaData);
	return ok;
}

bool ibSettingsDescriptionMemory::WriteNode(ibDataNode& node, const ibSettingsDescription& settings)
{
	WriteSelectedList(node, kSelectedFieldsNode, settings.m_selected);   // see ReadNode
	// …and the reader's parameter values, written as a VARIANT's are: name and value, no declaration.
	ibParameterDescriptionMemory::WriteNode(node, settings.m_parameters, /*full*/false);
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
bool ibCompositionDescriptionMemory::ReadNode(const ibDataNode& node, ibCompositionDescription& composition, const ibMetaData* metaData)
{
	// THE MAIN TABLE IS A NUMBER, and its absence is a number too: a record written by a report (or
	// by a list before one was picked) simply has no such property, and wxNOT_FOUND is what "nothing
	// picked" IS. Nothing is resolved here — that is the whole reason the id is stored at all.
	composition.m_mainTable = wxNOT_FOUND;
	node.GetValue<s32>(kMainTableName, composition.m_mainTable);   // …left as it is when the name is absent
	composition.m_query     = node.GetValue<wxString>(kQueryName);

	// ⭐ THE VARIANTS ARE THE SETTINGS. A record written before they existed carries ONE set and no
	// variant nodes at all — it is read into the FIRST variant, which is where the settings live now,
	// so nobody's saved work turns into a migration and there is no second member to land it in.
	if (!ibVariantDescriptionMemory::ReadNode(node, composition.m_variants, metaData)) {
		composition.m_variants.assign(1, ibVariantDescription());
		ibSettingsDescriptionMemory::ReadNode(node, composition.m_variants[0].m_settings, metaData);
	}
	if (composition.m_variants.empty())
		composition.m_variants.emplace_back();   // a record can say anything; the invariant is ours

	// ⚠ AND THE DOOR GOES IN HERE TOO. A parameter holds references and enum members like a filter's
	// right side does; read without the configuration they are read against, the value factory raises
	// "Unknown value type '<id>' in the data" on a composition that was saved perfectly well — the id
	// being a metaobject's, which only the metadata knows how to build (Max, 2026-08-28).
	ibParameterDescriptionMemory::ReadNode(node, composition.m_parameters, metaData);
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
		node.SetValue<s32>(kMainTableName, static_cast<s32>(composition.m_mainTable));
	node.SetValue<wxString>(kQueryName, composition.m_query);

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
const wxString  kParamName       = wxT("Name");
const wxString  kParamExpression = wxT("Expression");
const wxString  kParamUser       = wxT("ForUser");
const wxString  kParamFromQuery  = wxT("FromQuery");
const wxString  kParamValue      = wxT("Value");
const wxString  kParamType       = wxT("Type");

constexpr ibClassID g_resourceNodeClsid = make_clsid("CompositionResource", ibClassKind_None);
const wxString  kResourceFunc = wxT("Func");
const wxString  kResourcePath = wxT("Path");
const wxString  kResourceAlias = wxT("Alias");
// The grouping this figure is computed over (`OVER Item`). Optional, and absence reads back as
// absence: every settings file written before areas existed holds no such key and means "the area
// comes from the ladder", which is exactly what an empty scope says.
const wxString  kResourceScope = wxT("Scope");

// THE PACKAGES — a node per select somebody has said something about, each holding its own field
// entries. The fields themselves are not stored: they are whatever the select projects, so a list
// of them here would be a copy that goes stale the moment the text is edited.
//
// ⚠ ITS OWN CLSID, not the one the plain field LISTS use (g_fieldNodeClsid, one file up): those
// nodes carry a path and nothing else, and a reader walking children by class must not meet two
// different records answering to one name.
constexpr ibClassID g_selectNodeClsid    = make_clsid("CompositionSelect", ibClassKind_None);
const wxString  kSelectId   = wxT("Id");
const wxString  kSelectName = wxT("Name");

constexpr ibClassID g_fieldInfoNodeClsid = make_clsid("CompositionFieldInfo", ibClassKind_None);
const wxString  kFieldInfoPath     = wxT("Path");
const wxString  kFieldInfoName     = wxT("Name");
const wxString  kFieldInfoTitle    = wxT("Title");
const wxString  kFieldInfoUseTitle = wxT("UseTitle");
} // namespace

bool ibParameterDescriptionMemory::ReadNode(const ibDataNode& node,
	std::vector<ibParameterDescription>& parameters, const ibMetaData* metaData)
{
	std::vector<ibParameterDescription> read;
	for (const ibDataNode& child : node.Children()) {
		if (child.GetClsid() != g_parameterNodeClsid)
			continue;   // variants and resources are children here too
		ibParameterDescription parameter;
		parameter.m_name = child.GetValue<wxString>(kParamName);
		if (parameter.m_name.IsEmpty())
			continue;   // a nameless parameter answers nothing
		parameter.m_expression   = child.GetValue<wxString>(kParamExpression);
		parameter.m_userSettable = child.GetValue<s32>(kParamUser) != 0;
		parameter.m_fromQuery    = child.GetValue<s32>(kParamFromQuery) != 0;
		// ⭐⭐ THE NODE IS TAKEN AS IT IS. Nothing is built here: a parameter's value is a BLOB in this
		// tier, and it is read into a runtime value by whoever runs the composition, against the
		// configuration it runs in. Building one here made a reference — a runtime object — in the
		// middle of a configuration load, where the type it names may still be three branches away
		// (Max, 2026-08-29: "the schema keeps the serialised node, and the runtime reads it and
		// writes it back itself").
		if (const ibDataNode* value = child.FindChild(kParamValue))
			parameter.m_value = *value;
		ibTypeDescriptionMemory::ReadNode(child.GetProperty(kParamType), parameter.m_type, metaData);
		read.push_back(parameter);
	}
	if (!read.empty())
		parameters = read;
	return true;
}

bool ibParameterDescriptionMemory::WriteNode(ibDataNode& node,
	const std::vector<ibParameterDescription>& parameters, bool full, const ibMetaData* metaData)
{
	for (size_t i = 0; i < parameters.size(); ++i) {
		const ibParameterDescription& parameter = parameters[i];
		ibDataNode& sub = node.AddChild(g_parameterNodeClsid, static_cast<ibMetaID>(i));
		sub.SetValue<wxString>(kParamName, parameter.m_name);
		sub.SetValue<wxString>(kParamExpression, parameter.m_expression);
		// …AND GOES BACK AS IT IS. It was packed by whoever put it there — the designer choosing a
		// declared value, the runtime writing one back — so writing is copying, byte for byte.
		sub.Child(kParamValue) = parameter.m_value;
		if (!full)
			continue;   // a VARIANT keeps only what it owns — see the header
		sub.SetValue<s32>(kParamUser, parameter.m_userSettable ? 1 : 0);
		sub.SetValue<s32>(kParamFromQuery, parameter.m_fromQuery ? 1 : 0);
		ibDataValue declared;
		ibTypeDescriptionMemory::WriteNode(declared, parameter.m_type, metaData);
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
wxString ibTitleFromName(const wxString& name)
{
	if (name.IsEmpty() || name.Find(wxT(' ')) != wxNOT_FOUND)
		return name;

	wxString title;
	title.reserve(name.length() + 4);
	for (size_t i = 0; i < name.length(); ++i) {
		const wxUniChar ch   = name[i];
		const wxUniChar prev = i > 0 ? name[i - 1] : wxUniChar(0);
		const wxUniChar next = i + 1 < name.length() ? name[i + 1] : wxUniChar(0);
		const bool startsWord = i > 0 && wxIsupper(ch)
			&& (!wxIsupper(prev) || (next != 0 && wxIslower(next)));
		if (startsWord)
			title += wxT(' ');
		title += ch;
	}
	return title;
}

wxString ibNameFromPath(const wxString& path)
{
	const int dot = path.Find(wxT('.'), /*fromEnd*/true);
	return dot == wxNOT_FOUND ? path : path.Mid(dot + 1);
}

const ibSelectDescription* ibSelectOfPath(const std::vector<ibSelectDescription>& selects, const wxString& path)
{
	const int dot = path.Find(wxT('.'), /*fromEnd*/true);
	if (dot != wxNOT_FOUND) {
		// THE QUALIFIER — matched by IDENTITY first and by name second (ibSelectDescription::Matches).
		// Today every stored path carries a name, because a path is the text that goes into the
		// query; asked this way, the day a path carries an id instead, nothing here changes.
		const wxString qualifier = path.Left(dot);
		for (const ibSelectDescription& select : selects)
			if (select.Matches(qualifier))
				return &select;
	}
	// UNQUALIFIED CAN ONLY MEAN THE ONE THERE IS — which is precisely why a second select makes the
	// name compulsory: with two of them an unqualified path names nothing in particular.
	return selects.size() == 1 ? &selects.front() : nullptr;
}

wxString ibTitleForPath(const std::vector<ibSelectDescription>& selects, const wxString& path)
{
	const wxString leaf = ibNameFromPath(path);
	if (const ibSelectDescription* select = ibSelectOfPath(selects, path))
		if (const ibFieldDescription* field = select->Find(leaf))
			return field->TitleInForce();
	return ibTitleFromName(leaf);
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
		const wxString path = child.GetValue<wxString>(kResourcePath);
		if (path.IsEmpty())
			continue;
		// The alias is OPTIONAL and absence reads back as absence — a file written before resources
		// could be named holds no such key and means "name it after the argument", which is what an
		// empty alias says.
		ibResourceDescription resource;
		resource.m_func  = child.GetValue<wxString>(kResourceFunc);
		resource.m_path  = path;
		resource.m_alias = child.GetValue<wxString>(kResourceAlias);
		resource.m_scope = child.GetValue<wxString>(kResourceScope);
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
		sub.SetValue<wxString>(kResourceFunc, resources[i].m_func);   // empty = the path IS the expression
		sub.SetValue<wxString>(kResourcePath, resources[i].m_path);
		if (!resources[i].m_alias.IsEmpty())
			sub.SetValue<wxString>(kResourceAlias, resources[i].m_alias);
		if (!resources[i].m_scope.IsEmpty())
			sub.SetValue<wxString>(kResourceScope, resources[i].m_scope);
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
		const wxString path = child.GetValue<wxString>(kFieldInfoPath);
		if (path.IsEmpty())
			continue;   // an entry about no field is not an entry
		ibFieldDescription field;
		field.m_path     = path;
		field.m_name     = child.GetValue<wxString>(kFieldInfoName);
		field.m_useTitle = child.GetValue<bool>(kFieldInfoUseTitle);
		field.m_title    = child.GetValue<wxString>(kFieldInfoTitle);
		fields.push_back(std::move(field));
	}
}

// DOES THIS FIELD SAY ANYTHING BEYOND ITS OWN NAME? A title nobody took over and a name that is the
// path's own leaf are both "nothing was said" — and writing them would freeze a caption that is
// supposed to keep following the name.
bool SaysAnything(const ibFieldDescription& field)
{
	return !field.m_path.IsEmpty() && (field.m_useTitle || !field.m_name.IsEmpty());
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
		sub.SetValue<wxString>(kFieldInfoPath, fields[i].m_path);
		if (!fields[i].m_name.IsEmpty())
			sub.SetValue<wxString>(kFieldInfoName, fields[i].m_name);
		if (fields[i].m_useTitle) {
			sub.SetValue<bool>(kFieldInfoUseTitle, true);
			sub.SetValue<wxString>(kFieldInfoTitle, fields[i].m_title);
		}
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
		select.m_id   = child.GetValue<wxString>(kSelectId);
		select.m_name = child.GetValue<wxString>(kSelectName);
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
			sub.SetValue<wxString>(kSelectId, selects[i].m_id);
		if (!selects[i].m_name.IsEmpty())
			sub.SetValue<wxString>(kSelectName, selects[i].m_name);
		WriteSelectFields(sub, selects[i].m_fields);
	}
	return true;
}

// ===========================================================================
//  Variants — settings + structure + the values they were saved with
// ===========================================================================
bool ibVariantDescriptionMemory::ReadNode(const ibDataNode& node,
	std::vector<ibVariantDescription>& variants, const ibMetaData* metaData)
{
	std::vector<ibVariantDescription> read;
	for (const ibDataNode& child : node.Children()) {
		if (child.GetClsid() != g_variantNodeClsid)
			continue;   // the filter tree is a child of this node too — it is not a variant
		// ⭐ A VARIANT IS A NAME, A SYNONYM AND A SETTING — the whole of it, and the setting writes
		// itself whole (its outputs included). The parameters that used to be read here were a
		// second list beside the composition's own and went with the member.
		ibVariantDescription variant;
		variant.m_name    = child.GetValue<wxString>(kVariantName);
		variant.m_synonym = child.GetValue<wxString>(kVariantSynonym);   // absent in an older file
		ibSettingsDescriptionMemory::ReadNode(child, variant.m_settings, metaData);
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
		sub.SetValue<wxString>(kVariantName, variants[i].m_name);
		sub.SetValue<wxString>(kVariantSynonym, variants[i].m_synonym);
		ibSettingsDescriptionMemory::WriteNode(sub, variants[i].m_settings);
	}
	return true;
}

// ⭐ THE VALUE FORM — the door a PROPERTY goes through, and the reason its read and write are one
// line each (ibSourceDescriptionMemory's shape). The value carries the description's own node.
bool ibCompositionDescriptionMemory::ReadNode(const ibDataValue& value, ibCompositionDescription& composition, const ibMetaData* metaData)
{
	if (value.Kind() != ibDataKind::Child)
		return false;
	const std::shared_ptr<ibDataNode>& packed = value.AsChild();
	return packed != nullptr && ReadNode(*packed, composition, metaData);
}

bool ibCompositionDescriptionMemory::WriteNode(ibDataValue& value, const ibCompositionDescription& composition)
{
	std::shared_ptr<ibDataNode> packed = std::make_shared<ibDataNode>();
	if (!WriteNode(*packed, composition))
		return false;
	value = ibDataValue::Child(packed);
	return true;
}

// ===========================================================================
//  Validation — a setting that cannot be applied is refused, not dropped
// ===========================================================================
namespace {
void ibValidateFilterNodes(const std::vector<ibFilterNodeDescription>& nodes)
{
	for (const ibFilterNodeDescription& item : nodes) {
		if (!item.m_use)
			continue;   // switched off — as if it were not written
		if (item.m_kind == ibFilterNodeKind_Group) {
			ibValidateFilterNodes(item.m_children);
			continue;
		}

		// A CONDITION NEEDS A LEFT-HAND SIDE. Without one there is nothing to compare.
		if (!item.m_left.IsField() && item.m_left.m_value.IsEmpty())
			ibBackendCoreException::Error(_("A filter condition has no field chosen"));

		// AND A VALUE THE FIELD CAN HOLD. An empty right side is legal (it compares against the
		// empty value of that type); a value of another type is not.
		const ibValue& right = item.m_right.m_value;
		const ibTypeDescription& expected = item.m_left.m_type;
		if (!item.m_right.IsField() && !right.IsEmpty() && expected.GetClsidCount() > 0) {
			const std::vector<ibClassID>& allowed = expected.GetClsidList();
			if (std::find(allowed.begin(), allowed.end(), right.GetClassType()) == allowed.end())
				ibBackendCoreException::Error(
					_("The value of condition '%s' does not fit the field's type"), item.m_left.m_path);
		}
	}
}
} // namespace

void ibValidateSettings(const ibSettingsDescription& settings)
{
	ibValidateFilterNodes(settings.m_filter.m_nodes);

	// A SORT OR A GROUPING WITHOUT A FIELD is the same kind of half-written line.
	for (const ibSortLineDescription& line : settings.m_sort.m_lines)
		if (line.m_path.IsEmpty())
			ibBackendCoreException::Error(_("A sort line has no field chosen"));
	for (const ibGroupLineDescription& line : settings.m_group.m_lines)
		if (line.m_path.IsEmpty())
			ibBackendCoreException::Error(_("A grouping line has no field chosen"));
}

// ⭐ THE NAMES THE IDS WERE MADE FROM — see the note in the header. One list, built from the same
// literals the constants above use, so a name and its id cannot part company: each entry is the
// constant itself beside the word it was spelled with.
wxString ibCompositionNodeName(ibClassID clsid)
{
	static const std::pair<ibClassID, const wxChar*> s_names[] = {
		{ g_variantNodeClsid,   wxT("CompositionVariant")    },
		{ g_outputNodeClsid,    wxT("CompositionOutput")     },
		{ g_levelNodeClsid,     wxT("CompositionLevel")      },
		{ g_fieldNodeClsid,     wxT("CompositionField")      },
		{ g_filterNodeClsid,    wxT("CompositionFilterNode") },
		{ g_orderNodeClsid,     wxT("CompositionOrderLine")  },
		{ g_groupNodeClsid,     wxT("CompositionGroupLine")  },
		{ g_parameterNodeClsid, wxT("CompositionParameter")  },
		{ g_resourceNodeClsid,  wxT("CompositionResource")   },
		{ g_selectNodeClsid,    wxT("CompositionSelect")     },
		{ g_fieldInfoNodeClsid, wxT("CompositionFieldInfo")  },
	};

	for (const auto& entry : s_names)
		if (entry.first == clsid)
			return entry.second;

	return wxEmptyString;
}
