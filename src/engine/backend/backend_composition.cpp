#include "backend_composition.h"

#include "backend/metaData.h"                      // ibMetaData::Deserialize — the door a caller HANDS IN
#include "backend/backend_exception.h"             // ibBackendCoreException — a setting that cannot apply is refused
#include "backend/backend_localization.h"          // a variant's caption is read in the configuration's language
#include "backend/compiler/valueSerialization.h"    // ibReadNodeType — whose value is in this node
#include "backend/metadataSerialization.h"          // ibDeserializeValue — a stored value through the configuration
#include "backend/system/value/composition/valueComposerField.h"   // the declared value this tier vends

#include <algorithm>                               // std::none_of — is this value one the field admits?

// THE TWO DOORS — see the header. A stored node becomes a value only when somebody asks, with the
// configuration to ask against; a value becomes a node by packing itself.
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

	// ⭐⭐ A STORED VALUE COMES BACK THROUGH THE CONFIGURATION'S OWN DOOR. A filter compares against references and enum
	// members, whose types exist only in the metadata's own registry; asking the value factory for one raises "Unknown
	// value type '<id>' in the data" on a filter that was saved perfectly well. Whoever restores a value knows which
	// configuration it belongs to and hands it in; none handed in, the door takes the session's.
	return ibDeserializeValue(stored, metaData);
}

// ⭐ NOTHING IS WRITTEN ONLY FOR "NOTHING CHOSEN" — Undefined. `IsEmpty` asked instead, and the empty value of
// a type is a value: a 0, a False, an empty date or an empty reference was dropped here and read back as
// Undefined — `Amount < 0` came back `Amount < Undefined` (2026-09-30, found by the round-trip test).
//
// …AND A VALUE OF A CONFIGURATION'S TYPE IS SAID AS IT READS, beside it (Presentation): a reference's name, an
// enumeration member's word are the configuration's, and whoever shows a setting without one (a thin client's
// settings window) has nothing else to show it by. Written afresh at every write, never read back — the value is
// read through the configuration's door (ibStoredValue).
void ibStoreValue(ibDataNode& stored, const ibValue& value)
{
	stored = ibDataNode();
	if (value.GetType() == ibValueTypes::TYPE_EMPTY)
		return;
	value.Serialize(stored);
	switch (value.GetType()) {
	case ibValueTypes::TYPE_NULL:
	case ibValueTypes::TYPE_BOOLEAN:
	case ibValueTypes::TYPE_NUMBER:
	case ibValueTypes::TYPE_DATE:
	case ibValueTypes::TYPE_STRING:
		break;
	default:
		stored.SetValue<ibString>(ibT("Presentation"), value.GetString());
	}
}

ibDataNode ibStoreValue(const ibValue& value)
{
	ibDataNode stored;
	ibStoreValue(stored, value);
	return stored;
}

// ===========================================================================
//  The read against a configuration — every packed value read back once
// ===========================================================================
namespace {
void CheckStoredValue(const ibSettingsDescription& settings, const ibMetaData* metaData);

void CheckStoredValue(const std::vector<ibFilterNodeDescription>& nodes, const ibMetaData* metaData)
{
	for (const ibFilterNodeDescription& item : nodes) {
		if (item.m_kind == ibFilterNodeKind_Group) {
			CheckStoredValue(item.m_children, metaData);
			continue;
		}
		ibStoredValue(item.m_left.m_value, metaData);    // raises on a type this configuration does not have
		ibStoredValue(item.m_right.m_value, metaData);
	}
}

void CheckStoredValue(const std::vector<ibLevelDescription>& levels, const ibMetaData* metaData)
{
	for (const ibLevelDescription& level : levels) {
		CheckStoredValue(level.m_settings, metaData);
		CheckStoredValue(level.m_children, metaData);
	}
}

void CheckStoredValue(const ibSettingsDescription& settings, const ibMetaData* metaData)
{
	CheckStoredValue(settings.m_filter.m_nodes, metaData);
	for (const ibConditionalAppearanceRuleDescription& rule : settings.m_conditionalAppearance.m_rules)
		CheckStoredValue(rule.m_condition.m_nodes, metaData);
	for (const ibOutputDescription& output : settings.m_structure) {
		CheckStoredValue(output.m_settings, metaData);
		CheckStoredValue(output.m_rowGroups, metaData);
		CheckStoredValue(output.m_columnGroups, metaData);
	}
}

void CheckStoredValue(const ibCompositionDescription& composition, const ibMetaData* metaData)
{
	for (const ibVariantDescription& variant : composition.m_variants)
		CheckStoredValue(variant.m_settings, metaData);
}
} // namespace

bool ibBackendSettingsDescriptionMemory::ReadNode(const ibDataNode& node, ibSettingsDescription& settings, const ibMetaData* metaData)
{
	if (!ibSettingsDescriptionMemory::ReadNode(node, settings))
		return false;
	if (metaData != nullptr)
		CheckStoredValue(settings, metaData);
	return true;
}

bool ibBackendCompositionDescriptionMemory::ReadNode(const ibDataValue& value, ibCompositionDescription& composition, const ibMetaData* metaData)
{
	if (!ibCompositionDescriptionMemory::ReadNode(value, composition))
		return false;
	if (metaData != nullptr)
		CheckStoredValue(composition, metaData);
	return true;
}

bool ibBackendCompositionDescriptionMemory::ReadNode(const ibDataNode& node, ibCompositionDescription& composition, const ibMetaData* metaData)
{
	if (!ibCompositionDescriptionMemory::ReadNode(node, composition))
		return false;
	if (metaData != nullptr)
		CheckStoredValue(composition, metaData);
	return true;
}

// A VARIANT'S CAPTION, in the CONFIGURATION's language — see the note beside the declaration. The
// synonym is stored as a translated text; a language missing from it is answered inside the localizer,
// and a synonym written plainly (no languages in it at all) is its own caption.
ibString ibVariantCaption(const ibVariantDescription& variant)
{
	const ibString caption = ibLocalization::GetTranslateGetRawLocText(ibBackendLocalization::GetUserLanguage(), variant.m_synonym);
	return caption.IsEmpty() ? variant.m_name : caption;
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
		if (!item.m_left.IsField() && item.m_left.m_value.Fields().empty())
			ibBackendCoreException::Error(_("A filter condition has no field chosen"));

		// AND A VALUE THE FIELD CAN HOLD. Nothing stored is legal (it compares against the empty value
		// of that type); a value of another type is not. Asked of the packed value's own type — nothing
		// is built to answer it.
		const ibDataNode& right = item.m_right.m_value;
		const ibTypeDescription& expected = item.m_left.m_type;
		if (!item.m_right.IsField() && !right.Fields().empty() && expected.GetClsidCount() > 0) {
			// Each type of the field as a range (clsid_admits): a family - a subconto of "any reference" -
			// takes its members, and a single type is a range of one. Looked up as it stood, a value the
			// family admits was refused.
			const std::vector<ibClassID>& allowed = expected.GetClsidList();
			const ibClassID held = ibReadNodeType(right);
			if (std::none_of(allowed.begin(), allowed.end(), [held](const ibClassID& type) { return clsid_admits(type, held); }))
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
