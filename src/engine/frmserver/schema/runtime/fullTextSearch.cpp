#include "fullTextSearch.h"

#include "backend/backend_exception.h"
#include "backend/fullTextSearch/fullTextIndex.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"

#include <algorithm>

namespace {

// The window's own wording of what a command is, for a refusal.
ibProtocol::ibSchema::ibCommand CommandOf(std::int32_t command)
{
	return static_cast<ibProtocol::ibSchema::ibCommand>(command);
}

}

//////////////////////////////////////////////////////////////////////
//  Administration
//////////////////////////////////////////////////////////////////////

bool ibSchemaFullTextSearch::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_DataAdministration();
}

void ibSchemaFullTextSearch::Build(const ibSession& session, const ibDataNode& /*args*/, ibDataNode& result) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	const bool enabled = ibFullTextIndex::GetMode() == ibFullTextSearchMode::ibFullTextSearchMode_Enable;
	result.SetValue(ibProtocol::ibSchema::ibFullTextSearch::Enabled, enabled);
	result.SetValue(ibProtocol::ibSchema::ibFullTextSearch::IndexTrue, ibFullTextIndex::IsIndexTrue(metaData));

	const ibDateTime updated = ibFullTextIndex::GetUpdateDate(metaData);
	result.SetValue(ibProtocol::ibSchema::ibFullTextSearch::Updated, updated.IsEmpty() ? ibString() : updated.ToString());

	const ibFullTextIndex::ibProgress progress = ibFullTextIndex::GetProgress(metaData);
	result.SetValue(ibProtocol::ibSchema::ibFullTextSearch::Indexed, ibDataValue::Int(progress.m_indexed));
	result.SetValue(ibProtocol::ibSchema::ibFullTextSearch::Total, ibDataValue::Int(progress.m_total));
}

std::function<void()> ibSchemaFullTextSearch::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	switch (CommandOf(command)) {
	case ibProtocol::ibSchema::ibCommand::Set: {
		bool enabled = false;
		args.GetValue(ibProtocol::ibSchema::ibFullTextSearch::Enabled, enabled);
		return [enabled]() {
			ibFullTextIndex::SetMode(enabled ? ibFullTextSearchMode::ibFullTextSearchMode_Enable
				: ibFullTextSearchMode::ibFullTextSearchMode_Disable);
		};
	}
	case ibProtocol::ibSchema::ibCommand::Start:
		return [metaData]() { ibFullTextIndex::Update(metaData, true); };
	case ibProtocol::ibSchema::ibCommand::Clear:
		return []() { ibFullTextIndex::Clear(); };
	case ibProtocol::ibSchema::ibCommand::Open:
	case ibProtocol::ibSchema::ibCommand::Stop:
	case ibProtocol::ibSchema::ibCommand::Save:
	case ibProtocol::ibSchema::ibCommand::Kick:
	case ibProtocol::ibSchema::ibCommand::Reload:
	case ibProtocol::ibSchema::ibCommand::Release:
	case ibProtocol::ibSchema::ibCommand::Remove:
	case ibProtocol::ibSchema::ibCommand::Create:
	case ibProtocol::ibSchema::ibCommand::Load:
	case ibProtocol::ibSchema::ibCommand::Merge:
		break;
	}
	refusal = ibProtocol::ibRefusal::NotFound;
	error = ibString::Format(ibT("the full-text search administration has no command %d"), command);
	return nullptr;
}

//////////////////////////////////////////////////////////////////////
//  Find
//////////////////////////////////////////////////////////////////////

void ibSchemaSearch::Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const
{
	const bool enabled = ibFullTextIndex::GetMode() == ibFullTextSearchMode::ibFullTextSearchMode_Enable;
	result.SetValue(ibProtocol::ibSchema::ibFullTextSearch::Enabled, enabled);

	ibString text;
	args.GetValue(ibProtocol::ibSchema::ibFullTextSearch::Text, text);
	if (!enabled || text.IsEmpty())
		return;

	// The engine's refusals - no word in the text - are the person's, as the script's are.
	const std::vector<ibFullTextIndex::ibHit> hits = ibFullTextIndex::Search(session.GetMetaData(), text);
	result.SetValue(ibProtocol::ibSchema::ibFullTextSearch::Total, ibDataValue::Int(static_cast<std::int64_t>(hits.size())));

	ibDataNode& rows = result.Child(ibProtocol::ibSchema::ibFullTextSearch::Hits);
	const std::size_t shown = std::min(hits.size(), static_cast<std::size_t>(kShown));
	for (std::size_t idx = 0; idx < shown; idx++) {
		const ibFullTextIndex::ibHit& hit = hits[idx];
		const ibValuePtr<ibValueReferenceDataObject> reference(ibValueReferenceDataObject::Create(hit.m_metaObject, hit.m_key));
		ibDataNode& row = rows.AddChild(0, static_cast<ibMetaID>(idx));
		row.SetValue(ibProtocol::ibSchema::ibFullTextSearch::Title, reference->GetString());
		row.SetValue(ibProtocol::ibSchema::ibFullTextSearch::Metadata, hit.m_metaObject->GetSynonym());
		row.SetValue(ibProtocol::ibSchema::ibFullTextSearch::MetaId, static_cast<std::int32_t>(hit.m_metaObject->GetMetaID()));
		row.SetValue(ibProtocol::ibSchema::ibFullTextSearch::Key, hit.m_key.str());
	}
}

std::function<void()> ibSchemaSearch::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	if (CommandOf(command) != ibProtocol::ibSchema::ibCommand::Open) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = ibString::Format(ibT("Find has no command %d"), command);
		return nullptr;
	}

	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	std::int32_t metaId = 0;
	ibString key;
	args.GetValue(ibProtocol::ibSchema::ibFullTextSearch::MetaId, metaId);
	args.GetValue(ibProtocol::ibSchema::ibFullTextSearch::Key, key);

	// Only what a search could have offered this person: a metaobject the index holds, that they may read.
	const ibValueMetaObjectRecordDataMutableRef* const metaObject = metaData != nullptr
		? dynamic_cast<const ibValueMetaObjectRecordDataMutableRef*>(metaData->FindAnyObjectByFilter(metaId)) : nullptr;
	const ibGuid guid(key);
	if (metaObject == nullptr || ibFullTextIndex::GetIndexedAttributes(metaObject).empty() || !metaObject->AccessRight_Read() || !guid.isValid()) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = ibString::Format(ibT("Find offers no record %s of %d"), key, metaId);
		return nullptr;
	}

	return [metaObject, guid]() {
		const ibValuePtr<ibValueRecordDataObjectRef> object = metaObject->CreateObjectValue(guid);
		if (!object)
			ibBackendCoreException::Error(_("Find: %s %s could not be read"), metaObject->GetFullName(), guid.str());
		object->ShowFormValue();
	};
}
