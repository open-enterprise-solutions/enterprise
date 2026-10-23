#include "predefined.h"

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/partial/commonObject.h"   // ibValueMetaObjectRecordDataHierarchyMutableRef — what declares them
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"

#include "frmserver/schema/designer/metaData.h"   // ibSchemaMetaData — whose tree, which object

namespace {

typedef ibValueMetaObjectRecordDataHierarchyMutableRef ibPredefinedOwner;
typedef ibPredefinedOwner::ibPredefinedValueObject     ibPredefinedItem;

// The object Id names, in Tab's tree — refused, null, when there is no such, or it is one that cannot declare any.
ibPredefinedOwner* FindOwner(const ibSession& session, const ibDataNode& args, ibProtocol::ibRefusal& refusal, wxString& error)
{
	ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
	ibValueMetaObject* const object = metaData != nullptr
		? ibSchemaMetaData::FindObject(metaData, args, ibProtocol::ibSchema::ibPredefined::Id, refusal, error) : nullptr;
	if (object == nullptr)
		return nullptr;

	ibPredefinedOwner* const owner = dynamic_cast<ibPredefinedOwner*>(object);
	if (owner == nullptr) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = wxString::Format(wxT("'%s' cannot have predefined items"), object->GetName());
	}
	return owner;
}

// The item `name` names by its Guid — refused, null, when the owner has none of it.
wxObjectDataPtr<ibPredefinedItem> FindItem(const ibPredefinedOwner* owner, const ibDataNode& args, const wxString& name,
	ibProtocol::ibRefusal& refusal, wxString& error)
{
	const wxString guid = args.GetValue<wxString>(name);
	wxObjectDataPtr<ibPredefinedItem> item = !guid.IsEmpty()
		? owner->FindPredefinedValue(ibGuid(guid)) : wxObjectDataPtr<ibPredefinedItem>();
	if (item == nullptr) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = wxString::Format(wxT("'%s' has no predefined item '%s'"), owner->GetName(), guid);
	}
	return item;
}

// What an item is given — Name, Code, Description, IsFolder and Parent.
struct ibPredefinedFields {
	wxString m_name;
	wxString m_code;
	wxString m_description;
	bool m_isFolder = false;
	wxObjectDataPtr<ibPredefinedItem> m_parent;
};

// …read, and checked as the dialog checked them: a name, one no other item of the owner has (`self` — the item
// edited, or none), and a parent (none: the top) that may hold it — a folder, where the owner keeps folders apart.
// Refused, false, otherwise.
bool ReadFields(const ibPredefinedOwner* owner, const ibDataNode& args, const wxObjectDataPtr<ibPredefinedItem>& self,
	ibPredefinedFields& fields, ibProtocol::ibRefusal& refusal, wxString& error)
{
	fields.m_name = args.GetValue<wxString>(ibProtocol::ibSchema::ibPredefined::Name);
	fields.m_code = args.GetValue<wxString>(ibProtocol::ibSchema::ibPredefined::Code);
	fields.m_description = args.GetValue<wxString>(ibProtocol::ibSchema::ibPredefined::Description);
	fields.m_isFolder = args.GetValue<bool>(ibProtocol::ibSchema::ibPredefined::IsFolder);

	if (fields.m_name.IsEmpty()) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = wxT("a predefined item needs a name");
		return false;
	}

	const wxObjectDataPtr<ibPredefinedItem> named = owner->FindPredefinedValue(fields.m_name);
	if (named != nullptr && (self == nullptr || named->GetPredefinedGuid() != self->GetPredefinedGuid())) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = wxString::Format(wxT("'%s' already has a predefined item named '%s'"), owner->GetName(), fields.m_name);
		return false;
	}

	if (args.GetValue<wxString>(ibProtocol::ibSchema::ibPredefined::Parent).IsEmpty())
		return true;
	fields.m_parent = FindItem(owner, args, ibProtocol::ibSchema::ibPredefined::Parent, refusal, error);
	if (fields.m_parent == nullptr)
		return false;
	if (self != nullptr && fields.m_parent->GetPredefinedGuid() == self->GetPredefinedGuid()) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = wxString::Format(wxT("'%s' cannot sit under itself"), self->GetPredefinedName());
		return false;
	}
	if (owner->HasFolders() && !fields.m_parent->IsPredefinedFolder()) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = wxString::Format(wxT("'%s' is not a folder"), fields.m_parent->GetPredefinedName());
		return false;
	}
	return true;
}

} // namespace

bool ibSchemaPredefined::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_Administration();
}

void ibSchemaPredefined::Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const
{
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	wxString error;
	const ibPredefinedOwner* const owner = FindOwner(session, args, refusal, error);
	if (owner == nullptr)
		return;

	result.SetValue(ibProtocol::ibSchema::ibPredefined::Type, wxString::Format(wxT("%llu"), static_cast<unsigned long long>(owner->GetClassType())));

	// Each item as the engine stores it (WriteData), and the item it sits under by its Guid.
	ibDataNode& items = result.Child(ibProtocol::ibSchema::ibPredefined::Items);
	for (const wxObjectDataPtr<ibPredefinedItem>& item : owner->GetPredefinedValueArray()) {
		ibDataNode& row = items.AddChild(0, static_cast<ibMetaID>(items.Children().size() + 1));
		row.SetValue(ibProtocol::ibSchema::ibPredefined::Guid, item->GetPredefinedGuid().str());
		row.SetValue(ibProtocol::ibSchema::ibPredefined::Name, item->GetPredefinedName());
		row.SetValue(ibProtocol::ibSchema::ibPredefined::Code, item->GetPredefinedCode());
		row.SetValue(ibProtocol::ibSchema::ibPredefined::Description, item->GetPredefinedDescription());
		row.SetValue(ibProtocol::ibSchema::ibPredefined::IsFolder, item->IsPredefinedFolder());
		if (item->GetPredefinedParent() != nullptr)
			row.SetValue(ibProtocol::ibSchema::ibPredefined::Parent, item->GetPredefinedParent()->GetPredefinedGuid().str());
	}
}

std::function<void()> ibSchemaPredefined::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, wxString& error) const
{
	ibPredefinedOwner* const owner = FindOwner(session, args, refusal, error);
	if (owner == nullptr)
		return nullptr;

	switch (static_cast<ibProtocol::ibSchema::ibCommand>(command)) {
	case ibProtocol::ibSchema::ibCommand::Create: {
		ibPredefinedFields fields;
		if (!ReadFields(owner, args, wxObjectDataPtr<ibPredefinedItem>(), fields, refusal, error))
			return nullptr;
		return [owner, fields]() {
			owner->AppendPredefinedValue(fields.m_name, fields.m_code, fields.m_description, fields.m_isFolder, fields.m_parent);
		};
	}
	case ibProtocol::ibSchema::ibCommand::Set: {
		// A Guid the owner has not — one more item of it, as the engine's SetPredefinedValue makes it (the dialog's Copy).
		const ibGuid guid(args.GetValue<wxString>(ibProtocol::ibSchema::ibPredefined::Guid));
		if (!guid.isValid()) {
			refusal = ibProtocol::ibRefusal::BadParameter;
			error = wxT("which predefined item - its Guid");
			return nullptr;
		}
		ibPredefinedFields fields;
		if (!ReadFields(owner, args, owner->FindPredefinedValue(guid), fields, refusal, error))
			return nullptr;
		return [owner, guid, fields]() {
			owner->SetPredefinedValue(guid, fields.m_name, fields.m_code, fields.m_description, fields.m_isFolder, fields.m_parent);
		};
	}
	case ibProtocol::ibSchema::ibCommand::Remove: {
		const wxObjectDataPtr<ibPredefinedItem> item = FindItem(owner, args, ibProtocol::ibSchema::ibPredefined::Guid, refusal, error);
		if (item == nullptr)
			return nullptr;
		const ibGuid guid = item->GetPredefinedGuid();
		return [owner, guid]() { owner->DeletePredefinedValue(guid); };
	}
	default:
		break;
	}

	refusal = ibProtocol::ibRefusal::NotFound;
	error = wxString::Format(wxT("the predefined values editor has no command %d"), command);
	return nullptr;
}
