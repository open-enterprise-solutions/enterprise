#include "languages.h"

#include "backend/metaData.h"
#include "backend/metaCollection/metaLanguageObject.h"
#include "core/exception.h"   // ibCoreException — a tab that shows no configuration, the person's refusal
#include "core/serialize/dataBuilder.h"

#include "frmserver/schema/designer/metaData.h"   // the tab's configuration — as the tree's doors find it

void ibSchemaLanguages::Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const
{
	ibDataNode& languages = result.Child(ibProtocol::ibSchema::ibLanguages::Languages);

	// The configuration of the tab the text is edited in (Tab); none named — the session's. An extension's languages
	// are its owner's.
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	const ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
	if (metaData == nullptr) {
		if (refusal == ibProtocol::ibRefusal::NoSession)
			return;
		ibCoreException::Error(ibT("%s"), error);
	}

	ibMetaData* ownerRaw = nullptr;
	metaData->GetOwner(ownerRaw);
	const ibMetaData* const owner = ownerRaw != nullptr ? ownerRaw : metaData;

	// The configuration's own language (DefaultLanguage) is told by its code, as the desktop's user record finds it
	// (activeMetaData->GetLangCode()).
	const ibString defaultCode = owner->GetLangCode();
	ibMetaID idx = 0;
	for (const auto object : owner->GetAnyArrayObject<ibValueMetaObjectLanguage>(g_metaLanguageCLSID)) {
		ibDataNode& row = languages.AddChild(0, idx++);
		row.SetValue(ibProtocol::ibSchema::ibLanguages::Guid, object->GetDocPath());
		row.SetValue(ibProtocol::ibSchema::ibLanguages::Title, object->GetSynonym());
		row.SetValue(ibProtocol::ibSchema::ibLanguages::Code, object->GetLangCode());
		if (!defaultCode.IsEmpty() && object->GetLangCode() == defaultCode)
			row.SetValue(ibProtocol::ibSchema::ibLanguages::Default, true);
	}
}
