#include "variantChoiceLink.h"   // …and, beside these two classes, the two name lookups they draw with
#include "backend/propertyManager/property/propertyChoiceLink.h"   // ibFieldReferenceTypes, ibChoiceOwnerRow — declared there
#include "backend/choiceLinkResolver.h"                    // CanGovern — the reading this offer must agree with
#include "backend/metaData.h"                              // …and the counter a configuration moves when something is removed
#include "backend/metaCollection/partial/commonObject.h"   // the objects and their fields
#include "backend/metaCollection/partial/catalog.h"        // ListOwner and the Owner attribute it declares

#include <algorithm>   // a deleted field taken out of the neighbourhood, a row naming one out of the table

namespace {

// ⭐ THE FIELD THIS PROPERTY SITS ON — asked ONCE, here, because every question below asks it first.
// The property family hands its owner over as an ibPropertyObject, so somebody has to say what that
// owner is; saying it in five places is five chances for the five to stop agreeing (Max, 2026-09-23,
// reading this file: "you have thrown casts about here").
const ibValueMetaObjectAttributeBase* SelfAttribute(const ibPropertyObject* owner)
{
	return dynamic_cast<const ibValueMetaObjectAttributeBase*>(owner);
}

ibMetaID SelfId(const ibPropertyObject* owner)
{
	const ibValueMetaObjectAttributeBase* self = SelfAttribute(owner);
	return self != nullptr ? self->GetMetaID() : 0;
}

// ⭐⭐ THE FIELDS STANDING BESIDE THIS ONE — the neighbours of ITS OWN HOLDER, and that is the whole
// rule. An attribute of an object stands among the object's attributes; a COLUMN of a tabular section
// stands among the other columns of that section, because at run time the holder of a column IS THE ROW
// (choiceLinkResolver.h) and its neighbours are the cells beside it.
//
// 🛑 IT USED TO CLIMB OUT OF THE SECTION to the object, and that is precisely wrong for the commonest
// case there is: a value column is typed by the KIND COLUMN NEXT TO IT, and the list climbing to the
// header did not offer that column at all (Max, 2026-09-23: "your job is simply to get the value FROM
// THE COLUMN - you pick a characteristic, and there is a link to the kind of characteristic beside it").
//
// Both lists here are made of this: a link names one of these fields, a choice parameter takes its
// value from one. One rule, written once, so the two cannot offer different neighbourhoods.
std::vector<ibValueMetaObjectAttributeBase*> HolderAttributes(const ibPropertyObject* owner)
{
	const ibValueMetaObjectAttributeBase* self = SelfAttribute(owner);
	const ibValueMetaObject* holder = self != nullptr ? self->GetParent() : nullptr;
	const ibValueMetaObjectCompositeData* fields = dynamic_cast<const ibValueMetaObjectCompositeData*>(holder);
	if (fields == nullptr)
		return std::vector<ibValueMetaObjectAttributeBase*>();

	std::vector<ibValueMetaObjectAttributeBase*> array = fields->GetGenericAttributeArrayObject();

	// ⭐⭐ AND FOR A COLUMN, THE HEADER'S ATTRIBUTES TOO — "a link by type is simply the choice of a
	// field, of the tabular section or of the header" (Max, 2026-09-23). A column stands in two places
	// at once: among the cells of its row, and under the object those rows belong to.
	//
	// ⚠ NO PATH CONVENTION CHANGES FOR THIS. A field is named by its id, one hop, whichever of the two
	// it is; at run time the holder tries the ROW first and the object after it, which is the same two
	// places in the same order (choiceLinkResolver.cpp, ibChoiceHolder::GetValue).
	// ⚠ AND NO TEST FOR "IS THIS A SECTION" — the climb tests itself. Above an object stands the
	// configuration, which is no composite and answers nothing; only a section has an object up there.
	// A cast written to ask a question another cast already answers is one cast too many.
	if (const ibValueMetaObjectCompositeData* above =
		dynamic_cast<const ibValueMetaObjectCompositeData*>(holder->GetParent()))
		above->GetGenericAttributeArrayObject(array);

	// ⚠ A DELETED FIELD STANDS NOWHERE. Deleting only MARKS a field until the configuration is saved, and the
	// arrays still carry it. The lists below asked it one by one and the names did not, so a row whose source
	// had just been deleted read on the inspector's line as a live `Export` (2026-09-24). Said here, once, for
	// every question this file asks about the neighbourhood.
	array.erase(std::remove_if(array.begin(), array.end(),
		[](const ibValueMetaObjectAttributeBase* field) { return field->IsDeleted(); }), array.end());

	return array;
}

// ⭐⭐ HOW A NEIGHBOUR IS SPELLED — by its name when it stands in this field's own holder, and as
// `<Object>.<Field>` when it is the header's field seen from a column. A section and its object keep
// their names apart, so each may hold a field under the same name (a Warehouse in the header and a
// Warehouse in every row is the ordinary case), and spelled bare the two were two identical lines:
// the designer drew them alike and a caller naming one got whichever came last.
//
// The id is what is stored; this is only how it is said — and every list and every summary here says
// it through this one function, so the designer, the inspector's row and MCP cannot spell it apart.
wxString NeighbourName(const ibPropertyObject* owner, const ibValueMetaObjectAttributeBase* field)
{
	const ibValueMetaObjectAttributeBase* self = SelfAttribute(owner);
	const ibValueMetaObject* holder = field->GetParent();
	if (self == nullptr || holder == nullptr || holder == self->GetParent())
		return field->GetName();
	return holder->GetName() + wxT(".") + field->GetName();
}

// ⭐⭐ WHAT A REFERENCE TYPE NAMES — the metaobject of the kind asked, or none. One look-up, so "what counts
// as a reference" and "what counts as gone" are said once for every question below.
template <typename T>
const T* ReferencedObject(const ibMetaData* metaData, const ibClassID& clsid)
{
	if (metaData == nullptr || !IsReference(clsid))
		return nullptr;
	const T* target = metaData->FindAnyObjectByFilter<T>((ibMetaID)metaID_from_clsid(clsid), true);
	return target != nullptr && !target->IsDeleted() ? target : nullptr;
}

// …and everything THIS field refers to. A composite field refers to more than one thing.
template <typename T>
std::vector<const T*> ReferencedObjects(const ibPropertyObject* owner)
{
	std::vector<const T*> out;
	const ibValueMetaObjectAttributeBase* self = SelfAttribute(owner);
	if (self == nullptr)
		return out;
	for (const ibClassID& clsid : self->GetTypeDesc().GetClsidList()) {
		if (const T* target = ReferencedObject<T>(owner->GetMetaData(), clsid))
			out.push_back(target);
	}
	return out;
}

// CAN `field` HOLD A VALUE OF `other`? A condition holds a field against a value, so a field that cannot
// hold what the other one holds would pair them into a condition that never matches.
bool AcceptsAnyOf(const ibValueMetaObjectAttributeBase* field, const ibValueMetaObjectAttributeBase* other)
{
	for (const ibClassID& held : other->GetTypeDesc().GetClsidList()) {
		if (field->GetTypeDesc().ContainType(held))
			return true;
	}
	return false;
}

}

// The two name lookups the header declares — each asked of the place such a field actually lives.
wxString ibChoiceHolderFieldName(const ibPropertyObject* owner, const ibMetaID& id)
{
	for (const ibValueMetaObjectAttributeBase* field : HolderAttributes(owner)) {
		if (field->GetMetaID() == id)
			return NeighbourName(owner, field);
	}
	return wxEmptyString;
}

void ibChoiceParametersForSource(const ibPropertyObject* owner, const ibMetaID& sourceId,
	std::vector<ibMetaID>& out, ibMetaID* obvious)
{
	if (obvious != nullptr)
		*obvious = 0;

	// The attribute the value will come from — its name and what it holds are the two things asked about.
	const ibValueMetaObjectAttributeBase* source = nullptr;
	for (const ibValueMetaObjectAttributeBase* field : HolderAttributes(owner)) {
		if (field->GetMetaID() == sourceId) { source = field; break; }
	}
	if (source == nullptr)
		return;

	ibMetaID byName = 0;

	for (const ibValueMetaObjectRecordData* target : ReferencedObjects<ibValueMetaObjectRecordData>(owner)) {
		for (const ibValueMetaObjectAttributeBase* field : target->GetGenericAttributeArrayObject()) {
			if (field->IsDeleted() || !AcceptsAnyOf(field, source))
				continue;

			if (byName == 0 && field->GetName().IsSameAs(source->GetName(), false))
				byName = field->GetMetaID();
			else
				out.push_back(field->GetMetaID());
		}
	}

	// …and the one named the same goes first: it is the answer an author already gave by naming them
	// alike, and it stays the answer when several others would also fit.
	if (byName != 0)
		out.insert(out.begin(), byName);

	// THE OBVIOUS ONE, said rather than left to be inferred: the field named the same, else the only
	// candidate there is. Nothing when the answer is a real choice.
	if (obvious != nullptr)
		*obvious = byName != 0 ? byName : (out.size() == 1 ? out.front() : 0);
}

wxString ibChoiceHolderName(const ibPropertyObject* owner)
{
	const ibValueMetaObjectAttributeBase* self = SelfAttribute(owner);
	const ibValueMetaObject* holder = self != nullptr ? self->GetParent() : nullptr;
	return holder != nullptr ? holder->GetName() : wxString();
}

wxString ibChoiceTargetFieldName(const ibPropertyObject* owner, const ibMetaID& id)
{
	// A composite field refers to more than one thing; the parameter belongs to one of them, and which
	// one is answered by finding it rather than by asking the author to say again.
	for (const ibValueMetaObjectRecordData* target : ReferencedObjects<ibValueMetaObjectRecordData>(owner)) {
		for (const ibValueMetaObjectAttributeBase* field : target->GetGenericAttributeArrayObject()) {
			if (field->GetMetaID() == id && !field->IsDeleted())
				return field->GetName();
		}
	}

	return wxEmptyString;
}

// The types this field refers to, as a list to pick from — ReferencedObjects, said as rows.
void ibFieldReferenceTypes(const ibPropertyObject* owner, ibPropertyChoiceList& list)
{
	for (const ibValueMetaObject* named : ReferencedObjects<ibValueMetaObject>(owner))
		list.Add(named->GetMetaID(), named->GetName(), named->GetSynonym(), wxVariant(named->GetMetaID()), named->GetIcon());
}

// ⭐ THE OWNER'S ROW — the whole of "link by owner", and the reason there is no property for it. Three
// things have to be true at once, and each is a question already answered somewhere in the tree: this
// field refers to a catalog, that catalog declares an owner, and a neighbouring field holds one.
//
// 🛑 WRITTEN ONCE, WHEN THE TYPE IS SETTLED — not offered whenever a window opens. The first cut put
// the row in from the dialog "when the table is empty", and promised in its own comment that an author
// who removed it meant it. It could not keep that: an emptied table and a table nobody has opened are
// the same state, so the row came back for ever. After this it is ordinary data the author owns, and
// deleting it sticks (Max, 2026-09-23).
bool ibChoiceOwnerRow(const ibPropertyObject* owner, ibChoiceParameterRowDescription& row)
{
	const ibMetaID selfId = SelfId(owner);

	for (const ibValueMetaObjectCatalog* target : ReferencedObjects<ibValueMetaObjectCatalog>(owner)) {
		// A catalog that owns nothing has an Owner attribute with no types declared — asking it for its
		// clsid list is how the tree itself tells the two apart (catalog.h, GetCatalogOwner).
		const ibValueMetaObjectAttributePredefined* ownerField = target->GetCatalogOwner();
		if (ownerField == nullptr || ownerField->GetClsidCount() == 0)
			continue;

		// …and a neighbour that can supply one: a field whose type meets what the owner may be.
		for (const ibValueMetaObjectAttributeBase* neighbour : HolderAttributes(owner)) {
			if (neighbour->GetMetaID() == selfId || !AcceptsAnyOf(ownerField, neighbour))
				continue;

			row.m_parameter = ownerField->GetMetaID();
			row.m_source.ClearSource();
			row.m_source.AppendSource(neighbour->GetMetaID());
			row.m_onChange = ibChoiceParameterOnChange::Clear;
			return true;
		}
	}

	return false;
}

//*************************************************************************************************
//*                                   The link by type                                            *
//*************************************************************************************************

// Asked on the counter a type description is asked on (ibVariantDataAttribute::DoRefreshTypeDesc): it moves when
// a type comes or goes and when anything is removed, so a configuration that stands still is judged once. What is
// gone is asked with the questions already here — a source by its neighbour's name, empty only when the field is
// genuinely gone; a parameter by the resolver's own deep find. A field standing in no holder has no neighbours to
// ask about, and is left as it is.
void ibVariantDataChoiceLink::RefreshLinkDesc() const
{
	const ibMetaData* metaData = m_ownerProperty != nullptr ? m_ownerProperty->GetMetaData() : nullptr;
	if (!m_linkDesc.IsOk() || metaData == nullptr || ibChoiceHolderName(m_ownerProperty).IsEmpty())
		return;

	const unsigned int object_version = metaData->GetFactoryCountChanges();
	if (object_version == m_object_version)
		return;

	if (ibChoiceHolderFieldName(m_ownerProperty, m_linkDesc.m_source.GetFirst()).IsEmpty())
		m_linkDesc = ibChoiceTypeLinkDescription();
	m_object_version = object_version;
}

// THE GOVERNING FIELD'S NAME — one word on the inspector's row, because that is the question the row
// answers: which field gives this one its type.
//
// A path is shown by its LAST segment for the same reason: the segments before it are how the field was
// reached, not what it is.
//
// ⚠ AND A LINK THAT NAMES SOMETHING GONE SAYS SO. Leaving the row empty there would make a broken link
// look exactly like no link at all, which is the one thing a summary must never do.
wxString ibVariantDataChoiceLink::MakeString() const
{
	const ibChoiceTypeLinkDescription& linkDesc = GetLinkDesc();
	if (!linkDesc.IsOk())
		return wxEmptyString;

	const wxString name = ibChoiceHolderFieldName(m_ownerProperty, linkDesc.m_source.GetLeaf());
	return !name.IsEmpty() ? name : _("<the field it named is gone>");
}

// ⚠ THE METADATA COMES FROM THE PROPERTY'S OWNER, never from the active configuration: several are open
// at once, and the one that matters is the one this property was created inside. Taking the active one
// answers plausibly and wrongly.
ibPropertyChoiceMode ibVariantDataChoiceLink::GetValueList(ibPropertyChoiceList& list) const
{
	const ibPropertyObject* owner = m_ownerProperty;
	const ibMetaData* metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	if (metaData == nullptr)
		return ibPropertyChoiceMode::None;

	const ibMetaID self = SelfId(owner);

	for (const ibValueMetaObjectAttributeBase* field : HolderAttributes(owner)) {
		if (field->GetMetaID() == self)
			continue;   // a field cannot govern itself

		// ⭐ THE SAME ANSWER THE RESOLVER READS BY. Which fields can give a type is said ONCE, beside the
		// reading itself (ibChoiceLinkResolver::CanGovern) — a second rule here is a designer that offers
		// a field the runtime cannot read, and the drift would be silent.
		if (!ibChoiceLinkResolver::CanGovern(field))
			continue;

		ibChoiceTypeLinkDescription linkDesc;
		linkDesc.m_source.AppendSource(field->GetMetaID());

		list.Add(field->GetMetaID(), NeighbourName(owner, field), field->GetSynonym(),
			wxVariant(new ibVariantDataChoiceLink(owner, linkDesc)),
			field->GetIcon());
	}

	return ibPropertyChoiceMode::Single;
}

//*************************************************************************************************
//*                             The choice parameters — the table                                 *
//*************************************************************************************************

void ibVariantDataChoiceParameters::RefreshParametersDesc() const
{
	const ibMetaData* metaData = m_ownerProperty != nullptr ? m_ownerProperty->GetMetaData() : nullptr;
	if (!m_paramsDesc.IsOk() || metaData == nullptr || ibChoiceHolderName(m_ownerProperty).IsEmpty())
		return;

	const unsigned int object_version = metaData->GetFactoryCountChanges();
	if (object_version == m_object_version)
		return;

	std::vector<ibChoiceParameterRowDescription>& rows = m_paramsDesc.m_rows;
	rows.erase(std::remove_if(rows.begin(), rows.end(), [this, metaData](const ibChoiceParameterRowDescription& row) {
		return metaData->FindAnyObjectByFilter(row.m_parameter, true) == nullptr
			|| ibChoiceHolderFieldName(m_ownerProperty, row.m_source.GetFirst()).IsEmpty();
	}), rows.end());
	m_object_version = object_version;
}

// THE PARAMETERS, NAMED RATHER THAN COUNTED. "3 parameters" tells a reader nothing they can act on,
// while the names are what they came to see — and the row is one line, so a long list is shortened
// rather than hidden. The parameter is what the row is ABOUT; where its value comes from is the
// dialog's business, not the summary's.
//
// 🛑 A ROW WHOSE PARAMETER COULD NOT BE NAMED USED TO BE SKIPPED, and with the name looked up in the
// configuration at large that was EVERY predefined field — so a property with rows in it read as an
// empty property. A row that cannot be named is now shown as unnamed: "something is set here" is the
// fact the row exists to carry (Max, 2026-09-23).
wxString ibVariantDataChoiceParameters::MakeString() const
{
	const ibChoiceParametersDescription& paramsDesc = GetParametersDesc();
	if (!paramsDesc.IsOk())
		return wxEmptyString;

	wxString shown;
	unsigned int named = 0;
	for (const ibChoiceParameterRowDescription& row : paramsDesc.m_rows) {
		if (named == 3) {
			shown += wxT(", …");
			break;
		}
		if (named > 0)
			shown += wxT(", ");

		const wxString name = ibChoiceTargetFieldName(m_ownerProperty, row.m_parameter);
		shown += !name.IsEmpty() ? name : _("<gone>");
		named++;
	}

	return shown;
}

// ⭐ THE FIELDS OF WHAT THIS ONE REFERS TO — asked PER TYPE, because a composite field refers to more
// than one thing and they do not have the same fields. A row belongs to one type; offering the union
// would let an author fill a parameter that half the list has never heard of.
void ibVariantDataChoiceParameters::GetParameterList(const ibClassID& ofType, ibPropertyChoiceList& list) const
{
	const ibValueMetaObjectRecordData* target = m_ownerProperty != nullptr
		? ReferencedObject<ibValueMetaObjectRecordData>(m_ownerProperty->GetMetaData(), ofType) : nullptr;
	if (target == nullptr)
		return;

	for (const ibValueMetaObjectAttributeBase* field : target->GetGenericAttributeArrayObject()) {
		if (!field->IsDeleted())
			list.Add(field->GetMetaID(), field->GetName(), field->GetSynonym(), wxVariant(field->GetMetaID()), field->GetIcon());
	}
}

// ⭐ THE FIELDS STANDING BESIDE THIS ONE — HolderAttributes: an object's other attributes, and for a column
// the other columns of its row and then the attributes of the object above it, one hop each.
//
// ⚠ NOT NARROWED, and that is the difference from the link by type. A link needs a field that CARRIES a
// type; a parameter needs a VALUE, and any field has one. Narrowing this list would refuse the ordinary
// case of a date or a number standing in a condition.
void ibVariantDataChoiceParameters::GetSourceList(ibPropertyChoiceList& list) const
{
	const ibPropertyObject* owner = m_ownerProperty;
	const ibMetaID selfId = SelfId(owner);

	for (const ibValueMetaObjectAttributeBase* field : HolderAttributes(owner)) {
		if (field->GetMetaID() == selfId)
			continue;   // a field does not narrow itself
		list.Add(field->GetMetaID(), NeighbourName(owner, field), field->GetSynonym(), wxVariant(field->GetMetaID()), field->GetIcon());
	}
}
