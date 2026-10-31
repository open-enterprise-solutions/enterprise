#include "metaData.h"

#include "backend/backend_mainFrame.h"   // ibBackendDocFrame — the configuration's own say how they went
#include "backend/metadataConfiguration.h"
#include "backend/metadataDataProcessor.h"   // ibMetaDataDataProcessor — a data processor read from its file, written into one
#include "backend/metadataReport.h"          // ibMetaDataReport — …a report
#include "backend/metaCollection/metaIntrospect.h"   // ibFindMetaObjectById — an object by its id, anywhere in the tree
#include "backend/session/session.h"
#include "backend/temp/tempStorage.h"
#include "core/memory/memoryStream.h"   // ibWriterMemory / ibReaderMemory — a copy's payload
#include "core/serialize/dataBuilder.h"

#include "frmserver/docView/docView.h"   // docManager — the client's, where its editors open
#include "frmserver/docView/templates/designer/docViewMetaFile.h"   // the base's configuration browsed — Browse
#include "frmserver/client/clientChildFrame.h"
#include "frmserver/client/clientFrame.h"   // the tab an external file's tree is in; the rows the tree shows next
#include "frmserver/win/dlgs/applyChange.h"   // the restructure's ledger, put to the person — Apply

namespace {

// Where a new object goes, and what it is — Parent (0: the configuration's own) and Type, a class it takes. Refused,
// false, otherwise: a schema acts only on what it offered.
bool FindPlace(ibMetaData* metaData, const ibDataNode& args, ibValueMetaObject*& parent,
	ibClassID& clsid, ibProtocol::ibRefusal& refusal, ibString& error)
{
	parent = args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibMetaData::Parent) != 0
		? ibSchemaMetaData::FindObject(metaData, args, ibProtocol::ibSchema::ibMetaData::Parent, refusal, error)
		: metaData->GetCommonMetaObject();
	if (parent == nullptr) {
		if (refusal == ibProtocol::ibRefusal::None) {
			refusal = ibProtocol::ibRefusal::NotFound;
			error = ibT("the configuration has no root");
		}
		return false;
	}

	unsigned long long type = 0;
	args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::Type).ToULongLong(&type);
	clsid = static_cast<ibClassID>(type);
	if (clsid == 0 || !parent->FilterChild(clsid)) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibString::Format(ibT("'%s' takes no such class"), parent->GetName());
		return false;
	}
	return true;
}

// THE CONFIGURATION ITSELF, its base behind it — what the desktop's Configuration menu acted on, by the right it was
// enabled by (data administration). Refused, null: an external file's tree has no base, or the person no such right.
ibMetaDataConfigurationBase* FindConfiguration(const ibSession& session, const ibMetaData* metaData,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	ibMetaDataConfigurationBase* const configuration = session.GetMetaData();
	if (metaData != configuration) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibT("an external file has no base");
		return nullptr;
	}
	if (!configuration->AccessRight_DataAdministration()) {
		refusal = ibProtocol::ibRefusal::NoRight;
		error = ibT("no right to the configuration's base");
		return nullptr;
	}
	return configuration;
}

// A DATA PROCESSOR OR A REPORT IS KEPT IN A FILE APART — the desktop's tree read one from it and wrote it into one
// (ibConfigurationTree::InsertItem, ReplaceItem, SaveItem), through these readers. One of that class read from that file of
// the session's temporary storage into the configuration, under its root. Null: the file is not one — and what the reader
// had put in before reading is taken away again, so a bad file leaves nothing behind.
ibValueMetaObject* ReadExternal(ibMetaData* metaData, const ibClassID& clsid, const ibString& file)
{
	ibValueMetaObject* read = nullptr;
	bool loaded = false;
	if (clsid == g_metaDataProcessorCLSID) {
		// A reader only — what it reads lives on in this configuration, so it is never closed (the desktop's InsertItem).
		const std::shared_ptr<ibMetaDataDataProcessor> reader = std::make_shared<ibMetaDataDataProcessor>(metaData);
		read = reader->GetDataProcessor();
		loaded = reader->LoadFromTempFile(file);
	}
	else {
		const std::shared_ptr<ibMetaDataReport> reader = std::make_shared<ibMetaDataReport>(metaData);
		read = reader->GetReport();
		loaded = reader->LoadFromTempFile(file);
	}
	if (loaded)
		return read;
	metaData->GetCommonMetaObject()->RemoveChild(read);
	return nullptr;
}

// …and that one written into that file.
bool WriteExternal(ibMetaData* metaData, ibValueMetaObject* object, const ibString& file)
{
	// A writer over the configuration's own object — never closed, for the object stays (the desktop's SaveItem).
	if (object->GetClassType() == g_metaDataProcessorCLSID)
		return std::make_shared<ibMetaDataDataProcessor>(metaData, static_cast<ibValueMetaObjectDataProcessor*>(object))->SaveToTempFile(file);
	return std::make_shared<ibMetaDataReport>(metaData, static_cast<ibValueMetaObjectReport*>(object))->SaveToTempFile(file);
}

} // namespace

bool ibSchemaMetaData::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_Administration();
}

void ibSchemaMetaData::Build(const ibSession& /*session*/, const ibDataNode& /*args*/,
	ibDataNode& /*result*/) const
{
}

std::function<void()> ibSchemaMetaData::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	ibMetaData* const metaData = FindMetaData(session, args, refusal, error);
	if (metaData == nullptr)
		return nullptr;

	switch (static_cast<ibProtocol::ibSchema::ibMetaData::ibCommand>(command)) {
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Open: {
		// …or by the module's address — where a runtime stopped or failed (the desktop's ibConfigurationTree::EditModule).
		const ibString module = args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::Module);
		if (!module.IsEmpty()) {
			ibValueMetaObject* const object = metaData->FindAnyObjectByFilter(ibGuid(module), true);
			if (object == nullptr || object->IsDeleted()) {
				refusal = ibProtocol::ibRefusal::NotFound;
				error = ibString::Format(ibT("nothing in the configuration is the module %s"), module);
				return nullptr;
			}
			return [object]() { docManager->OpenForm(object); };
		}
		ibValueMetaObject* const object = FindObject(metaData, args, ibProtocol::ibSchema::ibMetaData::Id, refusal, error);
		if (object == nullptr)
			return nullptr;
		return [object]() { docManager->OpenForm(object); };
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Create: {
		ibValueMetaObject* parent = nullptr;
		ibClassID clsid = 0;
		if (!FindPlace(metaData, args, parent, clsid, refusal, error))
			return nullptr;
		// Made, and its rows shown with the answer: the tree selects its row without reading them, then opens its editor
		// (the client's CreateItem, the desktop's ibConfigurationTree::CreateItem — NewItem, then OpenObjectForm) — made
		// while the row is shown, the editor shows its own.
		const std::int32_t tabId = args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibMetaData::Tab);
		return [metaData, clsid, parent, tabId]() {
			ibValueMetaObject* const created = metaData->CreateMetaObject(clsid, parent);
			ibClientFrame* const frame = ibClientFrame::GetFrame();
			if (created != nullptr && frame != nullptr)
				frame->ShowProperties(created, tabId);
		};
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Remove: {
		ibValueMetaObject* const object = FindObject(metaData, args, ibProtocol::ibSchema::ibMetaData::Id, refusal, error);
		if (object == nullptr)
			return nullptr;
		// …and the rows of the object the tree goes to after it (Select — the next row, else the one before, else the
		// parent: the client's RemoveItem), shown with the answer.
		const std::int32_t tabId = args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibMetaData::Tab);
		const ibMetaID select = args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibMetaData::Select);
		return [metaData, object, tabId, select]() {
			metaData->RemoveMetaObject(object);
			ibValueMetaObject* const selected = select > 0 ? ibFindMetaObjectById(metaData, select) : nullptr;
			ibClientFrame* const frame = ibClientFrame::GetFrame();
			if (selected != nullptr && !selected->IsDeleted() && frame != nullptr)
				frame->ShowProperties(selected, tabId);
		};
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Move: {
		ibValueMetaObject* const object = FindObject(metaData, args, ibProtocol::ibSchema::ibMetaData::Id, refusal, error);
		if (object == nullptr)
			return nullptr;
		ibValueMetaObject* const parent = object->GetParent();
		const std::int32_t position = args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibMetaData::Position);
		if (parent == nullptr || position < 0 || static_cast<unsigned int>(position) >= parent->GetChildCount()) {
			refusal = ibProtocol::ibRefusal::BadParameter;
			error = ibString::Format(ibT("'%s' has no place %d"), object->GetName(), position);
			return nullptr;
		}
		// The door moves it and announces `Moved`; the rows follow.
		return [parent, object, position]() { parent->ChangeChildPosition(object, static_cast<unsigned int>(position)); };
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Copy: {
		ibValueMetaObject* const object = FindObject(metaData, args, ibProtocol::ibSchema::ibMetaData::Id, refusal, error);
		if (object == nullptr)
			return nullptr;
		ibWriterMemory writer;
		if (!metaData->CopyMetaObject(object, writer)) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = ibString::Format(ibT("'%s' could not be copied"), object->GetName());
			return nullptr;
		}
		ibTempFile file;
		if (!file.Create(args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::File), true)) {
			refusal = ibProtocol::ibRefusal::NotFound;
			error = ibT("no such file");
			return nullptr;
		}
		const ibMemoryBuffer bytes = writer.buffer();
		if (!file.Write(bytes.GetData(), bytes.GetDataLen())) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = ibT("the copy was not written");
			return nullptr;
		}
		// Done in the asking — nothing in the configuration changed.
		return []() {};
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Paste: {
		ibValueMetaObject* parent = nullptr;
		ibClassID clsid = 0;
		if (!FindPlace(metaData, args, parent, clsid, refusal, error))
			return nullptr;
		const ibTempFile file(args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::File));
		if (!file.IsOpened()) {
			refusal = ibProtocol::ibRefusal::NotFound;
			error = ibT("no such file");
			return nullptr;
		}
		ibMemoryBuffer bytes;
		ibMemoryBuffer part;
		for (int index = 0; file.ReadPart(index, part); ++index)
			bytes.AppendData(part.GetData(), part.GetDataLen());
		// The shell, the read, the announcement and the cleanup of a bad payload are all the door's.
		return [metaData, clsid, parent, bytes]() {
			ibReaderMemory reader(bytes);
			metaData->PasteMetaObject(clsid, parent, reader);
		};
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Save: {
		// The designer's Save (ibConfigurationTree::Save) — the configuration written into its base. An external file is
		// saved by its tab's Save, as the desktop's document saved it: into the file the person keeps (ibTabDocument::Save).
		ibMetaDataConfigurationBase* const configuration = session.GetMetaData();
		if (metaData != configuration) {
			refusal = ibProtocol::ibRefusal::BadParameter;
			error = ibT("an external file is saved by its tab");
			return nullptr;
		}
		return [configuration]() { configuration->SaveDatabase(); };
	}
	// THE CONFIGURATION'S OWN — the desktop's Configuration menu (designer/mainFrame, OnOpenConfiguration,
	// OnRollbackConfiguration, OnUpdateConfiguration, OnConfiguration), each the engine's verb. Each that replaces the
	// configuration is the work's: its Closed takes this client's editors over it down (the tree's, on its own session
	// only). And what the desktop said in a message box when it was done the server asks the person — only it knows how
	// it went.
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Browse: {
		ibMetaDataConfigurationBase* const configuration = FindConfiguration(session, metaData, refusal, error);
		if (configuration == nullptr)
			return nullptr;
		ibMetaDataConfigurationBase* const database = configuration->GetConfiguration();
		if (database == nullptr || database->GetCommonMetaObject() == nullptr) {
			refusal = ibProtocol::ibRefusal::NotFound;
			error = ibT("the base has no configuration of its own");
			return nullptr;
		}
		// The desktop's OnOpenConfiguration: the browser over the database's copy raised when it is open — found by that
		// copy's root, its metaobject — else one opened. Read-only as the desktop's view was, and here the copy itself
		// says so: every door that would change it refuses (ibMetaData::IsEditable). Nothing changes it in place — Apply
		// puts a new one.
		return [database]() {
			ibValueMetaObject* const root = database->GetCommonMetaObject();
			for (ibDocument* const doc : docManager->GetDocumentsVector()) {
				ibMetadataBrowserDocument* const browser = wxDynamicCast(doc, ibMetadataBrowserDocument);
				if (browser != nullptr && browser->GetMetaObject() == root) {
					browser->Activate();
					return;
				}
			}
			database->SetReadOnly(true);
			ibMetadataBrowserDocument* const document = new ibMetadataBrowserDocument(
				std::static_pointer_cast<ibMetaDataConfigurationBase>(database->shared_from_this()));
			docManager->AddDocument(document);
			document->SetTitle(root->GetModuleName());
			document->SetMetaObject(root);
			if (!document->OnCreate(root->GetModuleName(), ibDOC_NEW))
				document->DeleteAllViews();
		};
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Rollback: {
		ibMetaDataConfigurationBase* const configuration = FindConfiguration(session, metaData, refusal, error);
		if (configuration == nullptr)
			return nullptr;
		return [configuration]() {
			ibString refusal;
			const bool rolledBack = configuration->RollbackConfiguration(refusal);
			ibBackendDocFrame* const frame = ibSession::CurrentFrame();
			if (frame == nullptr)
				return;
			if (rolledBack)
				frame->ShowModalMessage(_("Successfully rolled back to database configuration!"), ibString(), wxOK | wxCENTRE);
			else if (!refusal.IsEmpty())
				frame->ShowModalMessage(refusal, ibString(), wxOK | wxCENTRE | wxICON_WARNING);
		};
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Apply: {
		ibMetaDataConfigurationBase* const configuration = FindConfiguration(session, metaData, refusal, error);
		if (configuration == nullptr)
			return nullptr;
		// …and the last question is the ledger itself, shown where it has always been shown — the base held until the
		// person answers it.
		return [configuration]() {
			ibString refusal;
			const bool applied = configuration->ApplyConfiguration(refusal,
				[](const ibRestructureInfo& info) { return ibDialogApplyChange::ShowApplyChange(info); });
			ibBackendDocFrame* const frame = ibSession::CurrentFrame();
			if (!applied && !refusal.IsEmpty() && frame != nullptr)
				frame->ShowModalMessage(BackendErrorChain(refusal), _("Error"), wxOK | wxCENTRE | wxICON_ERROR);
		};
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Load:
	case ibProtocol::ibSchema::ibMetaData::ibCommand::LoadFiles: {
		// …or a data processor or a report, from the file it is kept in apart (the desktop's tree's Replace and Insert):
		// that one replaced by the one read (Id), or one read in under that object (Type, Parent).
		if (args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibMetaData::Id) != 0
			|| !args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::Type).IsEmpty()) {
			// The reader puts its object in before it reads — so the configuration's word is asked here, not at its door.
			if (!metaData->IsEditable()) {
				refusal = ibProtocol::ibRefusal::NoRight;
				error = ibT("the configuration is read-only");
				return nullptr;
			}
			ibValueMetaObject* replaced = nullptr;
			ibClassID clsid = 0;
			if (args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibMetaData::Id) != 0) {
				replaced = FindObject(metaData, args, ibProtocol::ibSchema::ibMetaData::Id, refusal, error);
				if (replaced == nullptr)
					return nullptr;
				clsid = replaced->GetClassType();
			}
			else {
				ibValueMetaObject* parent = nullptr;
				if (!FindPlace(metaData, args, parent, clsid, refusal, error))
					return nullptr;
			}
			if (clsid != g_metaDataProcessorCLSID && clsid != g_metaReportCLSID) {
				refusal = ibProtocol::ibRefusal::BadParameter;
				error = ibT("only a data processor or a report is kept in a file");
				return nullptr;
			}
			// A file that is not one leaves the configuration as it was, and the person is told.
			const ibString file = args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::File);
			return [metaData, clsid, replaced, file]() {
				ibValueMetaObject* const read = ReadExternal(metaData, clsid, file);
				if (read == nullptr) {
					if (ibBackendDocFrame* const frame = ibSession::CurrentFrame())
						frame->ShowModalMessage(BackendErrorChain(_("The data processor or report could not be read from the file.")),
							ibString(), wxOK | wxCENTRE | wxICON_ERROR);
					return;
				}
				if (replaced == nullptr)
					return;
				// THE ONE READ TAKES THE REPLACED ONE'S PLACE — the reader puts it in last, and the desktop's tree kept the
				// row where it stood (ReplaceItem repointed it): moved there before the old one goes, so the order of the
				// configuration's objects is the one it was.
				ibValueMetaObject* const parent = replaced->GetParent();
				if (parent != nullptr && read->GetParent() == parent)
					parent->ChangeChildPosition(read, parent->GetChildPosition(replaced));
				metaData->RemoveMetaObject(replaced);
			};
		}
		ibMetaDataConfigurationBase* const configuration = FindConfiguration(session, metaData, refusal, error);
		if (configuration == nullptr)
			return nullptr;
		const ibTempFile file(args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::File));
		if (!file.IsOpened()) {
			refusal = ibProtocol::ibRefusal::NotFound;
			error = ibT("no such file");
			return nullptr;
		}
		ibMemoryBuffer bytes;
		ibMemoryBuffer part;
		for (int index = 0; file.ReadPart(index, part); ++index)
			bytes.AppendData(part.GetData(), part.GetDataLen());
		// Read as the desktop's LoadConfigFromFile reads its file (LoadConfigFromBuffer) — or, LoadFiles, as the files of a
		// directory the client read out of it, one node (ibFileProvider::Collect); a file that is no configuration leaves
		// the one there as it was, and the person is told.
		const bool asFiles = command == static_cast<std::int32_t>(ibProtocol::ibSchema::ibMetaData::ibCommand::LoadFiles);
		return [configuration, bytes, asFiles]() {
			ibDataNode files;
			if (asFiles ? ibBinaryProvider().Read(bytes, files) && configuration->LoadConfigFromFiles(files)
					: configuration->LoadConfigFromBuffer(bytes))
				return;
			if (ibBackendDocFrame* const frame = ibSession::CurrentFrame())
				frame->ShowModalMessage(BackendErrorChain(_("The configuration could not be read from the file.")),
					ibString(), wxOK | wxCENTRE | wxICON_ERROR);
		};
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Unload:
	case ibProtocol::ibSchema::ibMetaData::ibCommand::UnloadFiles: {
		// …or a data processor or a report, written as the file it is kept in apart (Id — the desktop's tree's Save).
		if (args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibMetaData::Id) != 0) {
			ibValueMetaObject* const object = FindObject(metaData, args, ibProtocol::ibSchema::ibMetaData::Id, refusal, error);
			if (object == nullptr)
				return nullptr;
			if (object->GetClassType() != g_metaDataProcessorCLSID && object->GetClassType() != g_metaReportCLSID) {
				refusal = ibProtocol::ibRefusal::BadParameter;
				error = ibT("only a data processor or a report is kept in a file");
				return nullptr;
			}
			if (!WriteExternal(metaData, object, args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::File))) {
				refusal = ibProtocol::ibRefusal::Failed;
				error = ibString::Format(ibT("'%s' was not written"), object->GetName());
				return nullptr;
			}
			// Done in the asking — nothing in the configuration changed.
			return []() {};
		}
		ibMetaDataConfigurationBase* const configuration = FindConfiguration(session, metaData, refusal, error);
		if (configuration == nullptr)
			return nullptr;
		// The configuration's file — or, UnloadFiles, the files of its directory as one node, for the client to write
		// into the directory the person chose (ibFileProvider::Publish).
		ibMemoryBuffer bytes;
		ibDataNode files;
		bool written = false;
		try {
			written = command == static_cast<std::int32_t>(ibProtocol::ibSchema::ibMetaData::ibCommand::UnloadFiles)
				? configuration->SaveConfigToFiles(files) && ibBinaryProvider().Write(files, bytes)
				: configuration->SaveConfigToBuffer(bytes);
		}
		catch (const ibCoreException& err) {
			refusal = ibProtocol::ibRefusal::Exception;
			error = err.GetErrorDescription();
			return nullptr;
		}
		if (!written) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = BackendErrorChain(ibT("the configuration could not be written"));
			return nullptr;
		}
		ibTempFile file;
		if (!file.Create(args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::File), true)) {
			refusal = ibProtocol::ibRefusal::NotFound;
			error = ibT("no such file");
			return nullptr;
		}
		if (!file.Write(bytes.GetData(), bytes.GetDataLen())) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = ibT("the configuration was not written");
			return nullptr;
		}
		// Done in the asking — nothing in the configuration changed.
		return []() {};
	}
	default:
		break;
	}

	refusal = ibProtocol::ibRefusal::NotFound;
	error = ibString::Format(ibT("the configuration tree has no command %d"), command);
	return nullptr;
}

ibMetaData* ibSchemaMetaData::FindMetaData(const ibSession& session, const ibDataNode& args, ibProtocol::ibRefusal& refusal,
	ibString& error)
{
	const std::int32_t tabId = args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibMetaData::Tab);
	if (tabId == 0) {
		ibMetaData* const metaData = session.GetMetaData();
		if (metaData == nullptr) {
			refusal = ibProtocol::ibRefusal::NoSession;
			error = ibT("the session has no configuration");
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
		refusal = ibProtocol::ibRefusal::NotFound;
		error = ibString::Format(ibT("no tab %d shows a configuration"), tabId);
	}
	return metaData;
}

ibValueMetaObject* ibSchemaMetaData::FindObject(ibMetaData* metaData, const ibDataNode& args, const ibString& name,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	const ibMetaID id = args.GetValue<std::int32_t>(name);
	ibValueMetaObject* const object = id > 0 ? ibFindMetaObjectById(metaData, id) : nullptr;
	if (object == nullptr || object->IsDeleted()) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = ibString::Format(ibT("nothing in the configuration has id %d"), id);
		return nullptr;
	}
	return object;
}
