#include "backend_composition.h"

#include "frmclient/backend/backend_exception.h"     // ibBackendCoreException — a setting that cannot apply is refused
#include "frmclient/backend/backend_localization.h"  // a variant's caption is read in the configuration's language

#include <algorithm>                                  // std::none_of — is this value one the field admits?

// THE TWO DOORS — see the header. (A declared value — `CompositionPredefinedValue` — is the designer's; here it is
// one more the server wrote.)
ibValue ibStoredValue(const ibDataNode& stored, const ibMetaData* WXUNUSED(metaData))
{
	// NOTHING WAS STORED = an empty value, not a failure. A value writes its type into a field of the
	// node, so a node with no fields is a value nobody ever put there.
	if (stored.Fields().empty())
		return ibValue();
	return ibValue::FromNode(stored);
}

// ⭐ NOTHING IS WRITTEN ONLY FOR "NOTHING CHOSEN" — Undefined. `IsEmpty` asked instead, and the empty value of
// a type is a value: a 0, a False, an empty date or an empty reference was dropped here and read back as
// Undefined — `Amount < 0` came back `Amount < Undefined` (2026-09-30, found by the round-trip test).
void ibStoreValue(ibDataNode& stored, const ibValue& value)
{
	stored = ibDataNode();
	if (value.GetType() != ibValueTypes::TYPE_EMPTY)
		value.Serialize(stored);
}

ibDataNode ibStoreValue(const ibValue& value)
{
	ibDataNode stored;
	ibStoreValue(stored, value);
	return stored;
}

// A VARIANT'S CAPTION, in the CONFIGURATION's language — see the note beside the declaration.
wxString ibVariantCaption(const ibVariantDescription& variant)
{
	const wxString caption = ibLocalization::GetTranslateGetRawLocText(ibBackendLocalization::GetUserLanguage(), variant.m_synonym);
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
		// of that type); a value of another type is not. Asked of the value as the server wrote it.
		const ibDataNode& right = item.m_right.m_value;
		const ibTypeDescription& expected = item.m_left.m_type;
		if (!item.m_right.IsField() && !right.Fields().empty() && expected.GetClsidCount() > 0) {
			// Each type of the field as a range (clsid_admits): a family - a subconto of "any reference" -
			// takes its members, and a single type is a range of one.
			const std::vector<ibClassID>& allowed = expected.GetClsidList();
			const ibClassID held = ibValue::FromNode(right).GetClassType();
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
