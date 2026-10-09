#include "frmclient/backend/system/value/composition/valueComposerField.h"

#include "core/serialize/dataBuilder.h"
#include "frmclient/backend/backend_localization.h"   // a presentation read in the reader's language

////////////////////////////////////////////////////////////////////////////
// CompositionField
////////////////////////////////////////////////////////////////////////////

ibValueCompositionField::ibValueCompositionField()
	: ibValue(ibValueTypes::TYPE_VALUE, false) {
}

ibValueCompositionField::ibValueCompositionField(const wxString& path, const wxString& presentation)
	: ibValue(ibValueTypes::TYPE_VALUE, false),
	  m_path(path), m_presentation(presentation) {
}

ibString ibValueCompositionField::GetString() const {
	// The presentation as the reader reads it, else the technical path — the engine's.
	return m_presentation.IsEmpty() ? m_path : ibLocalization::GetTranslateGetRawLocText(ibBackendLocalization::GetUserLanguage(), m_presentation);
}

bool ibValueCompositionField::CompareValueEQ(const ibValue& cParam) const {
	ibValueCompositionField* rhs = nullptr;
	return cParam.ConvertToValue(rhs) && m_path.IsSameAs(rhs->m_path, false);
}

bool ibValueCompositionField::CompareValueNE(const ibValue& cParam) const {
	return !CompareValueEQ(cParam);
}

// The engine's packed form — the path, how it reads, the column it resolved to; the type is the source's to say.
bool ibValueCompositionField::DoSerialize(ibDataNode& node) const {
	node.SetValue(wxT("p"), m_path);
	node.SetValue(wxT("n"), m_presentation);
	node.SetValue(wxT("l"), (s32)m_leafId);
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
// CompositionPredefinedValue
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
