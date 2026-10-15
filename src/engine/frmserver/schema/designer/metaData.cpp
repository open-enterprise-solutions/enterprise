#include "metaData.h"

#include "backend/backend_mainFrame.h"   // ibBackendDocFrame — the configuration's own say how they went
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaIntrospect.h"   // ibFindMetaObjectById — an object by its id, anywhere in the tree
#include "backend/session/session.h"
#include "backend/temp/tempStorage.h"
#include "core/fileSystem/fs.h"   // ibWriterMemory / ibReaderMemory — a copy's payload
#include "core/serialize/dataBuilder.h"

#include "frmserver/docView/docView.h"   // docManager — the client's, where its editors open
#include "frmserver/client/clientChildFrame.h"
#include "frmserver/client/clientFrame.h"   // the tab an external file's tree is in
#include "frmserver/win/dlgs/applyChange.h"   // the restructure's ledger, put to the person — Apply

namespace {

// Where a new object goes, and what it is — Parent (0: the configuration's own) and Type, a class it takes. Refused,
// false, otherwise: a schema acts only on what it offered.
bool FindPlace(ibMetaData* metaData, const ibDataNode& args, ibValueMetaObject*& parent,
	ibClassID& clsid, ibProtocolRefusal& refusal, wxString& error)
{
	parent = args.GetValue<s32>(wxT("Parent")) != 0
		? ibSchemaMetaData::FindObject(metaData, args, wxT("Parent"), refusal, error) : metaData->GetCommonMetaObject();
	if (parent == nullptr) {
		if (refusal == ibProtocolRefusal::None) {
			refusal = ibProtocolRefusal::NotFound;
			error = wxT("the configuration has no root");
		}
		return false;
	}

	unsigned long long type = 0;
	args.GetValue<wxString>(wxT("Type")).ToULongLong(&type);
	clsid = static_cast<ibClassID>(type);
	if (clsid == 0 || !parent->FilterChild(clsid)) {
		refusal = ibProtocolRefusal::BadParameter;
		error = wxString::Format(wxT("'%s' takes no such class"), parent->GetName());
		return false;
	}
	return true;
}

// THE CONFIGURATION ITSELF, its base behind it — what the desktop's Configuration menu acted on, by the right it was
// enabled by (data administration). Refused, null: an external file's tree has no base, or the person no such right.
ibMetaDataConfigurationBase* FindConfiguration(const ibSession& session, const ibMetaData* metaData,
	ibProtocolRefusal& refusal, wxString& error)
{
	ibMetaDataConfigurationBase* const configuration = session.GetMetaData();
	if (metaData != configuration) {
		refusal = ibProtocolRefusal::BadParameter;
		error = wxT("an external file has no base");
		return nullptr;
	}
	if (!configuration->AccessRight_DataAdministration()) {
		refusal = ibProtocolRefusal::NoRight;
		error = wxT("no right to the configuration's base");
		return nullptr;
	}
	return configuration;
}

} // namespace

bool ibSchemaMetaData::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_Administration();
}

void ibSchemaMetaData::Build(const ibSession& WXUNUSED(session), const ibDataNode& WXUNUSED(args),
	ibDataNode& WXUNUSED(result)) const
{
}

std::function<void()> ibSchemaMetaData::Command(const ibSession& session, s32 command,
	const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const
{
	ibMetaData* const metaData = FindMetaData(session, args, refusal, error);
	if (metaData == nullptr)
		return nullptr;

	switch (static_cast<ibProtocolMetaDataCommand>(command)) {
	case ibProtocolMetaDataCommand::Open: {
		// …or by the module's address — where a runtime stopped or failed (the desktop's ibConfigurationTree::EditModule).
		const wxString module = args.GetValue<wxString>(wxT("Module"));
		if (!module.IsEmpty()) {
			ibValueMetaObject* const object = metaData->FindAnyObjectByFilter(ibGuid(module), true);
			if (object == nullptr || object->IsDeleted()) {
				refusal = ibProtocolRefusal::NotFound;
				error = wxString::Format(wxT("nothing in the configuration is the module %s"), module);
				return nullptr;
			}
			return [object]() { docManager->OpenForm(object); };
		}
		ibValueMetaObject* const object = FindObject(metaData, args, wxT("Id"), refusal, error);
		if (object == nullptr)
			return nullptr;
		return [object]() { docManager->OpenForm(object); };
	}
	case ibProtocolMetaDataCommand::Create: {
		ibValueMetaObject* parent = nullptr;
		ibClassID clsid = 0;
		if (!FindPlace(metaData, args, parent, clsid, refusal, error))
			return nullptr;
		// Made, and no more: its editor is the click's to open once its row is selected (the client's CreateItem, the
		// desktop's ibConfigurationTree::CreateItem — NewItem, then OpenObjectForm).
		return [metaData, clsid, parent]() { metaData->CreateMetaObject(clsid, parent); };
	}
	case ibProtocolMetaDataCommand::Remove: {
		ibValueMetaObject* const object = FindObject(metaData, args, wxT("Id"), refusal, error);
		if (object == nullptr)
			return nullptr;
		return [metaData, object]() { metaData->RemoveMetaObject(object); };
	}
	case ibProtocolMetaDataCommand::Move: {
		ibValueMetaObject* const object = FindObject(metaData, args, wxT("Id"), refusal, error);
		if (object == nullptr)
			return nullptr;
		ibValueMetaObject* const parent = object->GetParent();
		const s32 position = args.GetValue<s32>(wxT("Position"));
		if (parent == nullptr || position < 0 || static_cast<unsigned int>(position) >= parent->GetChildCount()) {
			refusal = ibProtocolRefusal::BadParameter;
			error = wxString::Format(wxT("'%s' has no place %d"), object->GetName(), position);
			return nullptr;
		}
		// The door moves it and announces `Moved`; the rows follow.
		return [parent, object, position]() { parent->ChangeChildPosition(object, static_cast<unsigned int>(position)); };
	}
	case ibProtocolMetaDataCommand::Copy: {
		ibValueMetaObject* const object = FindObject(metaData, args, wxT("Id"), refusal, error);
		if (object == nullptr)
			return nullptr;
		ibWriterMemory writer;
		if (!metaData->CopyMetaObject(object, writer)) {
			refusal = ibProtocolRefusal::Failed;
			error = wxString::Format(wxT("'%s' could not be copied"), object->GetName());
			return nullptr;
		}
		ibTempFile file;
		if (!file.Create(args.GetValue<wxString>(wxT("File")), true)) {
			refusal = ibProtocolRefusal::NotFound;
			error = wxT("no such file");
			return nullptr;
		}
		const wxMemoryBuffer bytes = writer.buffer();
		if (!file.Write(bytes.GetData(), bytes.GetDataLen())) {
			refusal = ibProtocolRefusal::Failed;
			error = wxT("the copy was not written");
			return nullptr;
		}
		// Done in the asking — nothing in the configuration changed.
		return []() {};
	}
	case ibProtocolMetaDataCommand::Paste: {
		ibValueMetaObject* parent = nullptr;
		ibClassID clsid = 0;
		if (!FindPlace(metaData, args, parent, clsid, refusal, error))
			return nullptr;
		const ibTempFile file(args.GetValue<wxString>(wxT("File")));
		if (!file.IsOpened()) {
			refusal = ibProtocolRefusal::NotFound;
			error = wxT("no such file");
			return nullptr;
		}
		wxMemoryBuffer bytes;
		wxMemoryBuffer part;
		for (int index = 0; file.ReadPart(index, part); ++index)
			bytes.AppendData(part.GetData(), part.GetDataLen());
		// The shell, the read, the announcement and the cleanup of a bad payload are all the door's.
		return [metaData, clsid, parent, bytes]() {
			ibReaderMemory reader(bytes);
			metaData->PasteMetaObject(clsid, parent, reader);
		};
	}
	case ibProtocolMetaDataCommand::Save: {
		// The designer's Save (ibConfigurationTree::Save) — the configuration written into its base. An external file is
		// saved by its tab's Save, as the desktop's document saved it: into the file the person keeps (ibTabDocument::Save).
		ibMetaDataConfigurationBase* const configuration = session.GetMetaData();
		if (metaData != configuration) {
			refusal = ibProtocolRefusal::BadParameter;
			error = wxT("an external file is saved by its tab");
			return nullptr;
		}
		return [configuration]() { configuration->SaveDatabase(); };
	}
	// THE CONFIGURATION'S OWN — the desktop's Configuration menu (designer/mainFrame, OnRollbackConfiguration,
	// OnUpdateConfiguration, OnConfiguration), each the engine's verb. Each that replaces the configuration is the work's:
	// its Closed takes this client's editors over it down (the tree's, on its own session only). And what the desktop
	// said in a message box when it was done the server asks the person — only it knows how it went.
	case ibProtocolMetaDataCommand::Rollback: {
		ibMetaDataConfigurationBase* const configuration = FindConfiguration(session, metaData, refusal, error);
		if (configuration == nullptr)
			return nullptr;
		return [configuration]() {
			wxString refusal;
			const bool rolledBack = configuration->RollbackConfiguration(refusal);
			ibBackendDocFrame* const frame = ibSession::CurrentFrame();
			if (frame == nullptr)
				return;
			if (rolledBack)
				frame->ShowModalMessage(_("Successfully rolled back to database configuration!"), wxString(), wxOK | wxCENTRE);
			else if (!refusal.IsEmpty())
				frame->ShowModalMessage(refusal, wxString(), wxOK | wxCENTRE | wxICON_WARNING);
		};
	}
	case ibProtocolMetaDataCommand::Apply: {
		ibMetaDataConfigurationBase* const configuration = FindConfiguration(session, metaData, refusal, error);
		if (configuration == nullptr)
			return nullptr;
		// …and the last question is the ledger itself, shown where it has always been shown — the base held until the
		// person answers it.
		return [configuration]() {
			wxString refusal;
			const bool applied = configuration->ApplyConfiguration(refusal,
				[](const ibRestructureInfo& info) { return ibDialogApplyChange::ShowApplyChange(info); });
			ibBackendDocFrame* const frame = ibSession::CurrentFrame();
			if (!applied && !refusal.IsEmpty() && frame != nullptr)
				frame->ShowModalMessage(BackendErrorChain(refusal), wxMessageBoxCaptionStr, wxOK | wxCENTRE | wxICON_ERROR);
		};
	}
	case ibProtocolMetaDataCommand::Load: {
		ibMetaDataConfigurationBase* const configuration = FindConfiguration(session, metaData, refusal, error);
		if (configuration == nullptr)
			return nullptr;
		const ibTempFile file(args.GetValue<wxString>(wxT("File")));
		if (!file.IsOpened()) {
			refusal = ibProtocolRefusal::NotFound;
			error = wxT("no such file");
			return nullptr;
		}
		wxMemoryBuffer bytes;
		wxMemoryBuffer part;
		for (int index = 0; file.ReadPart(index, part); ++index)
			bytes.AppendData(part.GetData(), part.GetDataLen());
		// Read as the desktop's LoadConfigFromFile reads its file (LoadConfigFromBuffer); a file that is no configuration
		// leaves the one there as it was, and the person is told.
		return [configuration, bytes]() {
			if (configuration->LoadConfigFromBuffer(bytes))
				return;
			if (ibBackendDocFrame* const frame = ibSession::CurrentFrame())
				frame->ShowModalMessage(BackendErrorChain(_("The configuration could not be read from the file.")),
					wxString(), wxOK | wxCENTRE | wxICON_ERROR);
		};
	}
	case ibProtocolMetaDataCommand::Unload: {
		ibMetaDataConfigurationBase* const configuration = FindConfiguration(session, metaData, refusal, error);
		if (configuration == nullptr)
			return nullptr;
		wxMemoryBuffer bytes;
		if (!configuration->SaveConfigToBuffer(bytes)) {
			refusal = ibProtocolRefusal::Failed;
			error = BackendErrorChain(wxT("the configuration could not be written"));
			return nullptr;
		}
		ibTempFile file;
		if (!file.Create(args.GetValue<wxString>(wxT("File")), true)) {
			refusal = ibProtocolRefusal::NotFound;
			error = wxT("no such file");
			return nullptr;
		}
		if (!file.Write(bytes.GetData(), bytes.GetDataLen())) {
			refusal = ibProtocolRefusal::Failed;
			error = wxT("the configuration was not written");
			return nullptr;
		}
		// Done in the asking — nothing in the configuration changed.
		return []() {};
	}
	default:
		break;
	}

	refusal = ibProtocolRefusal::NotFound;
	error = wxString::Format(wxT("the configuration tree has no command %d"), command);
	return nullptr;
}

ibMetaData* ibSchemaMetaData::FindMetaData(const ibSession& session, const ibDataNode& args, ibProtocolRefusal& refusal,
	wxString& error)
{
	const s32 tabId = args.GetValue<s32>(wxT("Tab"));
	if (tabId == 0) {
		ibMetaData* const metaData = session.GetMetaData();
		if (metaData == nullptr) {
			refusal = ibProtocolRefusal::NoSession;
			error = wxT("the session has no configuration");
		}
		return metaData;
	}

	const ibClientFrame* const frame = dynamic_cast<const ibClientFrame*>(session.GetFrame());
	const ibClientChildFrame* const tab = frame != nullptr ? frame->Tab(frame->FindTab(tabId)) : nullptr;
	// A tree's tab — the file its document holds; a tab of a metaobject (its form, its module) — the configuration that
	// object is in.
	ibMetaData* metaData = nullptr;
	if (const ibMetaDataDocument* const tree = tab != nullptr ? dynamic_cast<const ibMetaDataDocument*>(tab->GetDocument()) : nullptr)
		metaData = tree->GetMetaData();
	else if (const ibMetaDocument* const document = tab != nullptr ? dynamic_cast<const ibMetaDocument*>(tab->GetDocument()) : nullptr)
		metaData = document->GetMetaObject() != nullptr ? document->GetMetaObject()->GetMetaData() : nullptr;
	if (metaData == nullptr) {
		refusal = ibProtocolRefusal::NotFound;
		error = wxString::Format(wxT("no tab %d shows a configuration"), tabId);
	}
	return metaData;
}

ibValueMetaObject* ibSchemaMetaData::FindObject(ibMetaData* metaData, const ibDataNode& args, const wxString& name,
	ibProtocolRefusal& refusal, wxString& error)
{
	const ibMetaID id = args.GetValue<s32>(name);
	ibValueMetaObject* const object = id > 0 ? ibFindMetaObjectById(metaData, id) : nullptr;
	if (object == nullptr || object->IsDeleted()) {
		refusal = ibProtocolRefusal::NotFound;
		error = wxString::Format(wxT("nothing in the configuration has id %d"), id);
		return nullptr;
	}
	return object;
}
