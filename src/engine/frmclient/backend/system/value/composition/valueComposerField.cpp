#include "frmclient/backend/system/value/composition/valueComposerField.h"

#include "core/serialize/dataBuilder.h"
#include "frmclient/backend/backend_localization.h"   // a presentation read in the reader's language

////////////////////////////////////////////////////////////////////////////
// CompositionField
////////////////////////////////////////////////////////////////////////////

ibValueCompositionField::ibValueCompositionField()
	: ibValue(ibValueTypes::TYPE_VALUE, false) {
}

ibValueCompositionField::ibValueCompositionField(const ibString& path, const ibString& presentation)
	: ibValue(ibValueTypes::TYPE_VALUE, false),
	  m_path(path), m_presentation(presentation) {
}

ibString ibValueCompositionField::GetString() const {
	// The presentation as the reader reads it, else the technical path — the engine's.
	return m_presentation.IsEmpty() ? m_path : ibLocalization::GetTranslateGetRawLocText(ibBackendLocalization::GetUserLanguage(), m_presentation);
}

bool ibValueCompositionField::CompareValueEQ(const ibVariant& cParam) const {
	ibValueCompositionField* rhs = nullptr;
	return cParam.ConvertToValue(rhs) && m_path.IsSameAs(rhs->m_path, false);
}

bool ibValueCompositionField::CompareValueNE(const ibVariant& cParam) const {
	return !CompareValueEQ(cParam);
}

// The engine's packed form — the path, how it reads, the column it resolved to; the type is the source's to say.
bool ibValueCompositionField::DoSerialize(ibDataNode& node) const {
	node.SetValue(ibT("p"), m_path);
	node.SetValue(ibT("n"), m_presentation);
	node.SetValue(ibT("l"), (std::int32_t)m_leafId);
	return true;
}

bool ibValueCompositionField::DoDeserialize(const ibDataNode& node) {
	m_path = node.GetValue<ibString>(ibT("p"));
	m_presentation = node.GetValue<ibString>(ibT("n"));
	m_leafId = (ibMetaID)node.GetValue<std::int32_t>(ibT("l"));
	m_typeDescription = ibTypeDescription();
	return true;
}

////////////////////////////////////////////////////////////////////////////
// CompositionPredefinedValue
////////////////////////////////////////////////////////////////////////////

namespace {
const ibString kPredefinedMetaId = ibT("m");    // the same two field names a reference packs itself
const ibString kPredefinedGuid   = ibT("g");    // with — this IS that pair, held rather than resolved
const ibString kPredefinedText   = ibT("n");    // how it reads — the engine's: not `t`, the header's name for the type
}

bool ibValueCompositionPredefined::DoSerialize(ibDataNode& node) const
{
	node.SetValue(kPredefinedMetaId, (std::int32_t)m_metaId);
	node.SetValue(kPredefinedGuid, m_guid.str());
	node.SetValue(kPredefinedText, m_written);
	return true;
}

bool ibValueCompositionPredefined::DoDeserialize(const ibDataNode& node)
{
	m_metaId  = (ibMetaID)node.GetValue<std::int32_t>(kPredefinedMetaId);
	m_guid    = ibGuid(node.GetValue<ibString>(kPredefinedGuid));
	m_written = node.GetValue<ibString>(kPredefinedText);
	return true;
}
