////////////////////////////////////////////////////////////////////////////
//	Description : the composer's evaluator — a condition read against a row in hand (compositionCondition.h)
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ ONE FILE, BOTH COMPOSERS (Max, 2026-09-30: "the evaluator in a file of the composer of its own, called the
// same way by RAM and by the database"). The engine itself, and what a composer tells it — a row read by field
// (ibComposerRowValueOf), the subtrees «in hierarchy» names (SubtreeOf) and the rules of every storey in force, made
// ready once for a run (ConditionalAppearanceFor). The DB composer's walk and the RAM composer's filter and print ask
// the same functions with the same arguments; the one thing that differs is where the row came from.

#include "dataComposerInternal.h"

#include "backend/query/queryRender.h"             // ibQueryColumnFromPath — a dotted path becomes its hops, once
#include "backend/query/queryConstructorModel.h"   // ibQueryConstructorField — the leaf a path walks to (WalkPath)
#include "backend/query/queryHierarchy.h"          // ibQueryHierarchyScope — what «in hierarchy» admits; how «in» lists
// ibCompositionRulesOf — the ONE place an appearance becomes what a cell is drawn with, so the only one that knows
// what its parameters are made of:
#include "backend/system/value/valueColour.h"   // its colours
#include "backend/system/value/valueFont.h"     // its font
#include "backend/compiler/enumUnit.h"          // its alignment, an enumeration member…
#include "backend/spreadsheetDescription.h"     // …in the sheet's own word, ibSpreadsheetAlignmentHorz
#include "backend/backend_localization.h"       // its Text and Format, in every language
#include "backend/formatString.h"               // what its Format writes a value as

// ⚠ NAMED, NOT INHERITED — MSVC hands these over transitively and GCC / Clang do not.
#include <algorithm>   // std::find_if — a column's own among a line's
#include <iterator>    // std::make_move_iterator, std::prev

//////////////////////////////////////////////////////////////////////
// the engine
//////////////////////////////////////////////////////////////////////

bool ibCompositionCompare(const ibValue& cell, const wxString& op, const ibValue& value)
{
	if (op == wxT("="))                            return cell == value;
	if (op == wxT("<>") || op == wxT("!="))        return cell != value;
	if (op == wxT(">"))                            return cell >  value;
	if (op == wxT(">="))                           return cell >= value;
	if (op == wxT("<"))                            return cell <  value;
	if (op == wxT("<="))                           return cell <= value;
	if (op.CmpNoCase(wxT("LIKE")) == 0) {
		wxString pattern = value.GetString();
		pattern.Replace(wxT("%"), wxT("*"));
		pattern.Replace(wxT("_"), wxT("?"));
		return cell.GetString().Lower().Matches(pattern.Lower());
	}
	return true;   // unknown operator → do not hide anything over a line nobody can read
}

// ⭐ THE SAME COMPARISON, ASKED WITH THE KIND IT IS. A stored condition holds an ibComparisonKind,
// not a spelling, and the string pair that used to translate between them is gone (there was an
// inverse map that read "IN" back as Equal — see list-settings.md § 5a). So the row-side comparison
// answers the kind directly. «In hierarchy» is the one it cannot answer from two values: the subtree is read
// from the catalog, and whoever reads the rows hands it over (ibCompositionSubtreeOf) — here it hides nothing.
bool ibCompositionCompare(const ibValue& cell, ibComparisonKind kind, const ibValue& value)
{
	switch (kind) {
	case ibComparisonKind_Equal:        return cell == value;
	case ibComparisonKind_NotEqual:     return cell != value;
	case ibComparisonKind_Greater:      return cell >  value;
	case ibComparisonKind_GreaterEqual: return cell >= value;
	case ibComparisonKind_Less:         return cell <  value;
	case ibComparisonKind_LessEqual:    return cell <= value;
	case ibComparisonKind_Contains:     return ibCompositionCompare(cell, wxT("LIKE"),
	                                        ibValue(wxT("%") + value.GetString() + wxT("%")));
	// WHETHER THERE IS A VALUE — the cell alone; NULL, an empty reference, "", 0, an empty date and False are none.
	case ibComparisonKind_Filled:       return !cell.IsEmpty();
	case ibComparisonKind_NotFilled:    return cell.IsEmpty();
	case ibComparisonKind_In: {
		// A LIST, OR ONE VALUE STANDING FOR ITSELF — flattened the way the operator flattens it (an array, a
		// table, a set). A value with nothing to list is matched as it is: an empty one is a value to filter by.
		const std::vector<ibValue> named = ibQueryHierarchyNamedValues(value);
		if (named.empty())
			return cell == value;
		for (const ibValue& one : named)
			if (cell == one)
				return true;
		return false;
	}
	default:                            return true;   // InHierarchy — read against the subtree (ReadCondition)
	}
}

// ⭐⭐ THE ONE ENGINE — see the header. Three values inside, so a condition nobody can read here is carried as
// what it is up to the top, and only there becomes the caller's answer.
namespace {
enum class ibConditionReading { False, True, Unknown };

// WHAT THE CALLER KNOWS, carried down the tree as one: the row's values, and the subtrees «in hierarchy» names.
struct ibConditionRow {
	const ibCompositionValueOf&   m_valueOf;
	const ibCompositionSubtreeOf& m_subtreeOf;
};

ibConditionReading ReadCondition(const ibFilterNodeDescription& node, const ibConditionRow& row);

// A GROUP OF CONDITIONS — AND: any false is false; OR: any true is true; NOT: the AND, turned over. A switched-off
// line is not written, in either kind of group; an empty group narrows nothing.
ibConditionReading ReadGroup(ibFilterGroupKind kind, const std::vector<ibFilterNodeDescription>& nodes,
	const ibConditionRow& row)
{
	const bool isOr = kind == ibFilterGroupKind_Or;
	bool asked = false, unknown = false;
	for (const ibFilterNodeDescription& node : nodes) {
		if (!node.m_use)
			continue;
		asked = true;
		const ibConditionReading reading = ReadCondition(node, row);
		if (reading == ibConditionReading::Unknown)
			unknown = true;
		else if (isOr && reading == ibConditionReading::True)
			return ibConditionReading::True;
		else if (!isOr && reading == ibConditionReading::False)
			return kind == ibFilterGroupKind_Not ? ibConditionReading::True : ibConditionReading::False;
	}
	if (!asked)
		return ibConditionReading::True;
	if (unknown)
		return ibConditionReading::Unknown;
	if (isOr)
		return ibConditionReading::False;
	return kind == ibFilterGroupKind_Not ? ibConditionReading::False : ibConditionReading::True;
}

ibConditionReading ReadCondition(const ibFilterNodeDescription& node, const ibConditionRow& row)
{
	if (node.m_kind == ibFilterNodeKind_Group)
		return ReadGroup(node.m_groupKind, node.m_children, row);
	// A CONDITION NAMES A FIELD of the row, compared with a VALUE — or with another field of the same row, read
	// the same way (a table in memory has no server to have answered that one for it).
	const auto valueOf = [&row](const wxString& path) { return row.m_valueOf ? row.m_valueOf(path) : nullptr; };
	const ibValue* cell  = node.m_left.IsField() ? valueOf(node.m_left.m_path) : nullptr;
	// «FILLED» ASKS THE CELL ALONE — no right side is read, and a NULL is its answer (not filled), not an unknown:
	// that is the whole point of asking it (Max, 2026-09-30: a row with no supplier at all is told apart only this way).
	if (!ibComparisonTakesValue(node.m_comparison)) {
		if (cell == nullptr)
			return ibConditionReading::Unknown;   // a field the row does not carry at all
		return ibCompositionCompare(*cell, node.m_comparison, ibValue())
			? ibConditionReading::True : ibConditionReading::False;
	}
	const ibValue* value = node.m_right.IsField() ? valueOf(node.m_right.m_path) : &node.m_right.m_value;
	if (cell == nullptr || value == nullptr)
		return ibConditionReading::Unknown;
	// ⭐ …AND A NULL IS A VALUE THE ROW DOES NOT CARRY — a record's field on a heading (the fold hands a column its
	// level does not hold as NULL: ibSelector::GetValue), a walk through an empty reference, an average of nothing.
	// A stored attribute is never NULL. Compared, it answered with whatever NULL happens to compare as, and a
	// heading was marked on a field it has none of — `Comment <> 'x'` held on every grouping. Unknown, as in SQL.
	if (cell->IsNull() || value->IsNull())
		return ibConditionReading::Unknown;
	// «IN HIERARCHY» — what the named value's subtree admits, as the caller read it (it knows the field's catalog).
	if (node.m_comparison == ibComparisonKind_InHierarchy) {
		const ibQueryHierarchyScope* subtree = row.m_subtreeOf ? row.m_subtreeOf(node.m_left.m_path, *value) : nullptr;
		if (subtree == nullptr)
			return ibConditionReading::Unknown;
		return subtree->Admits(*cell) ? ibConditionReading::True : ibConditionReading::False;
	}
	return ibCompositionCompare(*cell, node.m_comparison, *value)
		? ibConditionReading::True : ibConditionReading::False;
}
}   // namespace

bool ibCompositionFilterHolds(const ibFilterDescription& filter, const ibCompositionValueOf& valueOf,
	const ibCompositionSubtreeOf& subtreeOf, bool whenUnknown)
{
	const ibConditionReading reading = ReadGroup(filter.m_rootKind, filter.m_nodes, ibConditionRow{ valueOf, subtreeOf });
	return reading == ibConditionReading::Unknown ? whenUnknown : reading == ibConditionReading::True;
}

bool ibCompositionSayRules(const std::vector<ibCompositionRule>& rules, const ibCompositionValueOf& valueOf,
	const ibCompositionSubtreeOf& subtreeOf, ibCompositionAttr& line, const ibCompositionSayField& sayField)
{
	bool any = false;
	for (const ibCompositionRule& rule : rules) {
		// A condition this row cannot answer does not mark it: a highlight nobody can explain is noise.
		if (!ibCompositionFilterHolds(rule.m_rule->m_condition, valueOf, subtreeOf, /*whenUnknown*/ false))
			continue;
		any = true;
		if (rule.m_rule->m_fields.empty())
			line.Say(rule.m_attr);
		for (const wxString& field : rule.m_rule->m_fields)
			sayField(field, rule.m_attr);
	}
	return any;
}

// (Out of line, beside the Format it applies: the contract names ibFormatString and does not include it.)
bool ibCompositionAttr::TextOf(const ibValue& value, wxString& text) const
{
	if (m_text) {
		text = *m_text;
		return true;
	}
	if (m_format) {
		text = m_format->Apply(value);
		return true;
	}
	return false;
}

namespace {
// A RULE'S APPEARANCE AS WHAT A CELL IS DRAWN WITH — every value unpacked, its Text read in the language in force,
// its Format parsed: here, once per rule per run, and never per line.
ibCompositionAttr AttrOfAppearance(const ibAppearanceDescription& appearance)
{
	ibCompositionAttr attr;
	for (const ibAppearanceValueDescription& said : appearance.m_values) {
		if (!said.m_use)
			continue;
		ibValue stored = ibStoredValue(said.m_value, nullptr);
		switch (said.m_parameter) {
		case ibAppearanceParameter::BackgroundColour: {
			ibValueColour* colour = nullptr;
			if (stored.ConvertToValue(colour) && colour != nullptr && colour->m_colour.IsOk())
				attr.m_backgroundColour = colour->m_colour;
			break;
		}
		case ibAppearanceParameter::TextColour: {
			ibValueColour* colour = nullptr;
			if (stored.ConvertToValue(colour) && colour != nullptr && colour->m_colour.IsOk())
				attr.m_textColour = colour->m_colour;
			break;
		}
		case ibAppearanceParameter::Font: {
			ibValueFont* font = nullptr;
			if (stored.ConvertToValue(font) && font != nullptr && font->m_font.IsOk())
				attr.m_font = ibCompositionFont::Of(font->m_font, ibDefaultSpreadsheetFont());   // what it changes, and only that
			break;
		}
		case ibAppearanceParameter::HorizontalAlignment: {
			if (stored.IsEmpty())
				break;
			// The sheet's word for it, as wx's HORIZONTAL flag — both drivers speak it: a grid's renderer, and a
			// sheet whose centre also centres vertically, which a cell takes from its column.
			const ibSpreadsheetAlignmentHorz horizontal = stored.ConvertToEnumValue<ibSpreadsheetAlignmentHorz>();
			attr.m_horizontalAlignment = horizontal == ibAlignmentHorz_Right ? wxALIGN_RIGHT
				: horizontal == ibAlignmentHorz_Center ? wxALIGN_CENTER_HORIZONTAL : wxALIGN_LEFT;
			break;
		}
		default:
			break;   // Format and Text are what the cell says — below, once, for both
		}
	}
	// …AND WHAT THE CELL SAYS — its Text where one is ticked, else the Format its value is written in (applied by
	// whoever writes the cell — ibCompositionAttr::TextOf), each read in the language in force.
	const ibValue text = appearance.ValueInForce(ibAppearanceParameter::Text);
	if (!text.IsEmpty())
		attr.m_text = ibTranslateString(text.GetString().ToWxString()).GetString();
	else {
		const wxString written = ibTranslateString(
			appearance.ValueInForce(ibAppearanceParameter::Format).GetString().ToWxString()).GetString();
		std::shared_ptr<ibFormatString> format = std::make_shared<ibFormatString>();
		if (!written.IsEmpty() && ibFormatString::Parse(written, *format))
			attr.m_format = std::move(format);
	}
	return attr;
}
} // namespace

std::vector<ibCompositionRule> ibCompositionRulesOf(const ibConditionalAppearanceDescription& rules)
{
	std::vector<ibCompositionRule> ready;
	for (const ibConditionalAppearanceRuleDescription& rule : rules.m_rules) {
		if (!rule.m_use || rule.m_appearance.IsEmpty())
			continue;
		ibCompositionRule made;
		made.m_rule = &rule;
		made.m_attr = AttrOfAppearance(rule.m_appearance);
		if (!made.m_attr.IsDefault())   // a rule that says nothing a cell shows is not read on any line
			ready.push_back(std::move(made));
	}
	return ready;
}

//////////////////////////////////////////////////////////////////////
// what a composer tells it — the same for both composers
//////////////////////////////////////////////////////////////////////

// A ROW OF THE WALK, READ BY FIELD — the names a person picked from: a column's name, else its alias. A name
// this result does not carry answers nothing, and the caller decides what "nothing" means for it.
ibCompositionValueOf ibComposerRowValueOf(const std::vector<ibQueryLowering::OutputColumn>& schema,
	const std::vector<ibValue>& row)
{
	// ⚠ A PATH IS NOT SPELLED AS ITS COLUMN — `Recorder.Supplier` is read as `RecorderSupplier`; asked by the literal
	// name, every field through a dot was "not in the row" and a rule on one never held (2026-09-30).
	return [&schema, &row](const wxString& path) -> const ibValue* {
		for (size_t i = 0; i < schema.size() && i < row.size(); ++i)
			if (ibComposerColumnAnswersTo(schema[i], path))
				return &row[i];
		return nullptr;
	};
}

ibCompositionSubtreeOf ibDataComposer::SubtreeOf() const
{
	return [this](const wxString& path, const ibValue& named) { return SubtreeAt(path, named); };
}

const ibQueryHierarchyScope* ibDataComposer::SubtreeAt(const wxString& path, const ibValue& named) const
{
	// Keyed by what is NAMED, a list flattened as the operator flattens it — asked before in this run, answered.
	std::pair<wxString, std::vector<ibValue>> key(path, ibQueryHierarchyNamedValues(named));
	const auto known = m_subtrees.find(key);
	if (known != m_subtrees.end())
		return known->second.get();

	// THE PATH AS HOPS, walked to its leaf (WalkPath) — the leaf's TYPE is where the tree lives.
	const ibQueryAstExprPtr column = ibQueryColumnFromPath(path);
	const ibQueryConstructorField leaf = column && !column->m_path.empty() ? WalkPath(column->m_path) : ibQueryConstructorField();
	std::shared_ptr<const ibQueryHierarchyScope> subtree = !leaf.m_type.IsOk() ? nullptr
		: std::make_shared<ibQueryHierarchyScope>(m_metaData, leaf.m_type, key.second, ibQueryDimUnfold::Hierarchy);
	return m_subtrees.emplace(std::move(key), std::move(subtree)).first->second.get();
}

ibDataComposer::ConditionalAppearance ibDataComposer::ConditionalAppearanceFor(const Output& output) const
{
	// Every storey's rules made ready ONCE — a storey that declares none adds nothing, not even an allocation.
	ConditionalAppearance ready;
	ready.m_rules = ibCompositionRulesOf(GetCurrentConditionalAppearanceDesc());
	std::vector<ibCompositionRule> outputs = ibCompositionRulesOf(output.m_settings.m_conditionalAppearance);
	ready.m_rules.insert(ready.m_rules.end(), std::make_move_iterator(outputs.begin()), std::make_move_iterator(outputs.end()));
	for (const std::vector<GroupNode>* axis : { &output.m_rowGroups, &output.m_columnGroups })
		for (const GroupNode& level : *axis) {
			std::vector<ibCompositionRule> own = ibCompositionRulesOf(level.m_settings.m_conditionalAppearance);
			if (!own.empty())
				ready.m_nodes.emplace(&level, std::move(own));
		}
	if (!ready.IsEmpty())
		ready.m_subtreeOf = SubtreeOf();
	return ready;
}

bool ibDataComposer::ConditionalAppearance::AttrFor(const GroupNode* node,
	const std::vector<ibQueryLowering::OutputColumn>& schema, const std::vector<ibValue>& row,
	ibCompositionLineAttr& attr) const
{
	// THE WALK'S BUFFER, REFILLED WHERE IT STANDS — cleared, never freed, so a line allocates nothing once the first
	// has made its room.
	attr.m_line = ibCompositionAttr();
	attr.m_cells.clear();

	// A COLUMN'S OWN, said over what it already has — the column found among the schema's by the name a person picked
	// it by; kept only for the columns a rule of their own held on.
	const ibCompositionValueOf  valueOf  = ibComposerRowValueOf(schema, row);
	const ibCompositionSayField sayField = [&schema, &attr](const wxString& field, const ibCompositionAttr& said) {
		for (size_t i = 0; i < schema.size(); ++i) {
			if (!ibComposerColumnAnswersTo(schema[i], field))
				continue;
			auto own = std::find_if(attr.m_cells.begin(), attr.m_cells.end(),
				[i](const std::pair<size_t, ibCompositionAttr>& cell) { return cell.first == i; });
			if (own == attr.m_cells.end()) {
				attr.m_cells.emplace_back(i, ibCompositionAttr());
				own = std::prev(attr.m_cells.end());
			}
			own->second.Say(said);
		}
	};
	bool any = ibCompositionSayRules(m_rules, valueOf, m_subtreeOf, attr.m_line, sayField);
	const auto nodeRules = node != nullptr ? m_nodes.find(node) : m_nodes.end();
	if (nodeRules != m_nodes.end())
		any = ibCompositionSayRules(nodeRules->second, valueOf, m_subtreeOf, attr.m_line, sayField) || any;
	if (!any)
		return false;

	// …and each column's own over the line's, now the whole line is said — a deeper storey's line never paints over a
	// column's own.
	for (std::pair<size_t, ibCompositionAttr>& cell : attr.m_cells) {
		ibCompositionAttr own = std::move(cell.second);
		cell.second = attr.m_line;
		cell.second.Say(own);
	}
	return true;
}
