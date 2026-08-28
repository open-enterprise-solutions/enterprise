#include "backend/system/value/composition/valueComposerField.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/compiler/typeCtor.h"
#include "backend/metaData.h"                                        // the configuration a declaration is built against
#include "backend/metaCollection/partial/reference/reference.h"      // …and what it is built INTO, at execution

// ===========================================================================
//  ibValueCompositionField — see valueComposerField.h
// ===========================================================================
ibValueCompositionField::ibValueCompositionField()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {
	m_members.Bind(this, &ibValueCompositionField::FillMembers);
}

ibValueCompositionField::ibValueCompositionField(const wxString& path, const wxString& presentation)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false),
	  m_path(path), m_presentation(presentation) {
	m_members.Bind(this, &ibValueCompositionField::FillMembers);
}

void ibValueCompositionField::FillMembers(ibMemberTable& helper) const {
	helper.AppendConstructor(2, wxT("CompositionField(path : string, presentation? : string)"));

	helper.AppendProp(wxT("Path"));
	helper.AppendProp(wxT("Presentation"));
}

bool ibValueCompositionField::Init(ibValue** paParams, const long lSizeArray) {
	// New CompositionField(path [, presentation])
	if (lSizeArray >= 1 && paParams[0] != nullptr)
		m_path = paParams[0]->GetString();
	if (lSizeArray >= 2 && paParams[1] != nullptr)
		m_presentation = paParams[1]->GetString();
	return true;
}

bool ibValueCompositionField::GetPropVal(const long lPropNum, ibValue& pvarPropVal) {
	switch (lPropNum) {
	case enPath:         pvarPropVal = m_path; return true;
	case enPresentation: pvarPropVal = GetString(); return true;
	}
	return false;
}

bool ibValueCompositionField::SetPropVal(const long lPropNum, const ibValue& varPropVal) {
	switch (lPropNum) {
	case enPath:
		// The TYPE does not follow the path. A path set from script points at
		// whatever the source has there; binding it to a column is the source's
		// job (SetTypeInfo), and pretending otherwise would leave a field
		// carrying the previous field's type.
		m_path = varPropVal.GetString();
		m_leafId = wxNOT_FOUND;
		m_typeDescription = ibTypeDescription();
		return true;
	case enPresentation:
		m_presentation = varPropVal.GetString();
		return true;
	}
	return false;
}

bool ibValueCompositionField::CompareValueEQ(const ibValue& cParam) const {
	// A field is never equal to a string that happens to spell the same path: a
	// filter holding `"Amount"` as text is a different (and wrong) thing from one
	// holding the field, and saying so is what makes that visible. Comparing
	// against anything else must ANSWER false, not raise — `field = 100` is a
	// legitimate comparison to make.
	ibValueCompositionField* rhs = nullptr;
	return cParam.ConvertToValue(rhs) && m_path.IsSameAs(rhs->m_path, false);
}

bool ibValueCompositionField::CompareValueNE(const ibValue& cParam) const {
	return !CompareValueEQ(cParam);
}

bool ibValueCompositionField::DoSerialize(ibDataNode& node) const {
	node.SetValue(wxT("p"), m_path);
	node.SetValue(wxT("n"), m_presentation);
	node.SetValue(wxT("l"), (s32)m_leafId);
	// The TYPE is deliberately NOT packed. It is derived from the source the
	// field is bound to, and a saved setting outlives the schema it was saved
	// against — restoring a stale type would be worse than re-deriving the
	// current one when the field is bound again.
	return true;
}

bool ibValueCompositionField::DoDeserialize(const ibDataNode& node) {
	m_path = node.GetValue<wxString>(wxT("p"));
	m_presentation = node.GetValue<wxString>(wxT("n"));
	m_leafId = (ibMetaID)node.GetValue<s32>(wxT("l"));
	m_typeDescription = ibTypeDescription();
	return true;
}

////////////////////////////////////////////////////////////////////////////
// CompositionPredefinedValue — the declaration, and where it becomes runtime
////////////////////////////////////////////////////////////////////////////

namespace {
const wxString kPredefinedMetaId = wxT("m");    // the same two field names a reference packs itself
const wxString kPredefinedGuid   = wxT("g");    // with — this IS that pair, held rather than resolved
const wxString kPredefinedText   = wxT("t");
}

bool ibValueCompositionPredefined::DoSerialize(ibDataNode& node) const
{
	node.SetValue(kPredefinedMetaId, (s32)m_metaId);
	node.SetValue(kPredefinedGuid, m_guid.str());
	node.SetValue(kPredefinedText, m_written);
	return true;
}

bool ibValueCompositionPredefined::DoDeserialize(const ibDataNode& node)
{
	m_metaId  = (ibMetaID)node.GetValue<s32>(kPredefinedMetaId);
	m_guid    = ibGuid(node.GetValue<wxString>(kPredefinedGuid));
	m_written = node.GetValue<wxString>(kPredefinedText);
	return true;
}

ibValue ibMaterializeCompositionValue(const ibValue& stored, const ibMetaData* metaData)
{
	ibValueCompositionPredefined* declared = nullptr;
	if (!stored.ConvertToValue(declared) || declared == nullptr || metaData == nullptr)
		return stored;   // not a declaration, or nothing to build it against

	// ⭐ AND HERE THE RUNTIME IS MADE — with a composer already running and every metaobject in place,
	// which is the only moment a reference can be built truthfully.
	ibValueReferenceDataObject* reference = ibValueReferenceDataObject::Create(
		metaData, declared->GetMetaId(), declared->GetGuid(), ibReferenceLoad::OnDemand);
	return reference != nullptr ? ibValue(reference) : stored;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueCompositionField, "CompositionField", g_compositionFieldCLSID);

// ⭐ REGISTERED TO BE NAMED, not to be created — the designer's type list renders whatever the
// registry can name, and this entry is how "one of the declared values" gets a line there beside
// Date and Number. Nobody writes `New` of it: it is vended by the tier that owns it.
SYSTEM_TYPE_REGISTER(ibValueCompositionPredefined, "CompositionPredefinedValue", g_compositionPredefinedCLSID);
