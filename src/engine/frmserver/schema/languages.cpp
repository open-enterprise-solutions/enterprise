#include "languages.h"

#include "backend/metaData.h"
#include "backend/metaCollection/metaLanguageObject.h"
#include "core/exception.h"   // ibCoreException — a tab that shows no configuration, the person's refusal
#include "core/serialize/dataBuilder.h"

#include "frmserver/schema/designer/metaData.h"   // the tab's configuration — as the tree's doors find it

void ibSchemaLanguages::Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const
{
	ibDataNode& languages = result.Child(wxT("Languages"));

	// The configuration of the tab the text is edited in (Tab); none named — the session's. An extension's languages
	// are its owner's.
	ibProtocolRefusal refusal = ibProtocolRefusal::None;
	wxString error;
	const ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
	if (metaData == nullptr) {
		if (refusal == ibProtocolRefusal::NoSession)
			return;
		ibCoreException::Error(wxT("%s"), error);
	}

	ibMetaData* ownerRaw = nullptr;
	metaData->GetOwner(ownerRaw);
	const ibMetaData* const owner = ownerRaw != nullptr ? ownerRaw : metaData;

	ibMetaID idx = 0;
	for (const auto object : owner->GetAnyArrayObject<ibValueMetaObjectLanguage>(g_metaLanguageCLSID)) {
		ibDataNode& row = languages.AddChild(0, idx++);
		row.SetValue(wxT("Guid"), object->GetDocPath());
		row.SetValue(wxT("Title"), object->GetSynonym());
		row.SetValue(wxT("Code"), object->GetLangCode());
	}
}
