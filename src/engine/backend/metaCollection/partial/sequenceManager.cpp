////////////////////////////////////////////////////////////////////////////
//	Description : sequence manager — the border, read and said
////////////////////////////////////////////////////////////////////////////

#include "sequenceManager.h"

#include "backend/metaData.h"
#include "backend/backend_exception.h"        // ibBackendCoreException — a refusal says WHO refused
#include "backend/system/value/valueMap.h"    // ibValueStructure — the key is said by name
#include "backend/metaCollection/dimension/metaDimensionObject.h"
#include "commonObject.h"
#include "selector/objectSelector.h"

// The registrations are written as a recorder's set, so the set and the key are here as they are for
// any register; the two verbs of a sequence are the border's. `GetBorder` / `SetBorder` are declared
// here and answered in the step that builds the borders table — until then they refuse rather than
// answer a border nothing keeps (docs/private/sequence-arc.md, steps 2 and 5).
enum {
	eCreateRecordSet,
	eCreateRecordKey,
	eSelect,
	eGetBorder,
	eSetBorder,
	eGetListForm,
};

void ibValueManagerDataObjectSequence::FillManagerMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("CreateRecordSet"), wxT("CreateRecordSet()"));
	helper.AppendFunc(wxT("CreateRecordKey"), wxT("CreateRecordKey()"));
	helper.AppendFunc(wxT("Select"), wxT("Select()"));
	// The key is a STRUCTURE, name to value — `GetBorder(New Structure("Organisation", org))`. Left
	// out, a sequence that keeps no dimensions has its one border.
	helper.AppendFunc(wxT("GetBorder"), 1, wxT("GetBorder(Key : structure)"));
	helper.AppendFunc(wxT("SetBorder"), 2, wxT("SetBorder(Border, Key : structure)"));
	helper.AppendFunc(wxT("GetListForm"), 3, wxT("GetListForm(string, owner, guid)"));
}

// ⭐ WHICH BORDER — SAID BY A STRUCTURE, name to value, the way every register reading takes its
// filter (`New Structure("Organisation", org)`). Not by position: a sequence's dimensions are a set,
// not an argument list, and a caller that names them cannot silently pass them in the wrong order.
//
// A NAME THAT IS NOT A DIMENSION IS REFUSED rather than ignored — the same discipline
// ibRegFilterPredicate keeps, and for the same reason: a dropped key is a wrong answer that looks
// right. A dimension the structure leaves out is empty, which is a value like any other: the border
// of "no organisation named" is its own row.
static std::vector<ibValue> KeyFromStructure(const ibValueMetaObjectSequence* seq, const ibValue& filter)
{
	std::vector<ibValue> key;
	ibValueStructure* structure = nullptr;
	const bool given = filter.ConvertToValue(structure) && structure != nullptr;

	for (long at = 0; given && at < structure->GetNProps(); ++at) {
		const wxString name = structure->GetPropName(at);
		bool named = false;
		for (const ibValueMetaObjectDimension* dimension : seq->GetDimensionArrayObject())
			if (dimension != nullptr && stringUtils::CompareString(name, dimension->GetName())) {
				named = true;
				break;
			}
		if (!named)
			ibBackendCoreException::Error(_("the key names '%s', which is not a dimension of '%s'"), name, seq->GetName());
	}

	for (const ibValueMetaObjectDimension* dimension : seq->GetDimensionArrayObject()) {
		ibValue value;
		if (given)
			for (long at = 0; at < structure->GetNProps(); ++at)
				if (structure->GetPropName(at).IsSameAs(dimension->GetName(), false)) {
					structure->GetPropVal(at, value);
					break;
				}
		key.push_back(value);
	}
	return key;
}

bool ibValueManagerDataObjectSequence::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (BuiltinMethodNum(lMethodNum))
	{
	case eCreateRecordSet:
		pvarRetValue = m_metaObject->CreateRecordSetObjectValue();
		return true;
	case eCreateRecordKey:
		pvarRetValue = new ibValueRecordKeyObject(m_metaObject);
		return true;
	case eSelect:
		pvarRetValue = new ibValueSelectorRegisterDataObject(m_metaObject);
		return true;
	case eGetBorder:
		pvarRetValue = ibSequenceBorderGet(m_metaObject,
			KeyFromStructure(m_metaObject, lSizeArray > 0 ? *paParams[0] : ibValue()));
		return true;
	case eSetBorder:
		// The moment first, the key after it: `SetBorder(Border, New Structure("Organisation", org))`.
		// An empty moment takes the border away — the key has nothing anybody has vouched for.
		ibSequenceBorderSet(m_metaObject,
			KeyFromStructure(m_metaObject, lSizeArray > 1 ? *paParams[1] : ibValue()),
			lSizeArray > 0 ? *paParams[0] : ibValue());
		return true;
	case eGetListForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(ibFormRequest(paParams[0]->GetString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}
