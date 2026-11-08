#include "metaData.h"

#include "backend/backend_mainFrame.h"   // ibBackendDocFrame — the application's own say how they went
#include "backend/metadataApplication.h"
#include "backend/metadataDataTool.h"        // ibMetaDataDataTool — a data tool read from its file, written into one
#include "backend/metadataReport.h"          // ibMetaDataReport — …a report
#include "backend/metaCollection/metaIntrospect.h"   // ibFindMetaObjectById — an object by its id, anywhere in the tree
#include "backend/session/session.h"
#include "backend/temp/tempStorage.h"
#include "core/memory/memoryStream.h"   // ibWriterMemory / ibReaderMemory — a copy's payload
#include "core/serialize/dataBuilder.h"

#include "frmserver/docView/docView.h"   // docManager — the client's, where its editors open
#include "frmserver/docView/templates/designer/docViewMetaFile.h"   // the base's application browsed — Browse
#include "frmserver/client/clientChildFrame.h"
#include "frmserver/client/clientFrame.h"   // the tab an external file's tree is in; the rows the tree shows next
#include "frmserver/win/dlgs/applyChange.h"   // the restructure's ledger, put to the person — Apply
#include "protocol/protocolDesigner.h"   // ibProtocol::ibSchema::ibMetaData

namespace {

// Where a new object goes, and what it is — Parent (0: the application's own) and Type, a class it takes. Refused,
// false, otherwise: a schema acts only on what it offered.
bool FindPlace(ibMetaData* metaData, const ibDataNode& args, ibValueMetaObject*& parent,
	ibClassID& clsid, ibProtocol::ibRefusal& refusal, ibString& error)
{
	parent = args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibMetaData::Parent) != 0
		? ibSchemaMetaData::FindObject(metaData, args, ibProtocol::ibSchema::ibMetaData::Parent, refusal, error)
		: metaData->GetRootMetaObject();
	if (parent == nullptr) {
		if (refusal == ibProtocol::ibRefusal::None) {
			refusal = ibProtocol::ibRefusal::NotFound;
			error = ibT("the application has no root");
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

// THE APPLICATION ITSELF, its base behind it — what the desktop's Application menu acted on, by the right it was
// enabled by (data administration). Refused, null: an external file's tree has no base, or the person no such right.
ibMetaDataApplicationBase* FindApplication(const ibSession& session, const ibMetaData* metaData,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	ibMetaDataApplicationBase* const application = session.GetMetaData();
	if (metaData != application) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibT("an external file has no base");
		return nullptr;
	}
	if (!application->AccessRight_DataAdministration()) {
		refusal = ibProtocol::ibRefusal::NoRight;
		error = ibT("no right to the application's base");
		return nullptr;
	}
	return application;
}

// A DATA TOOL OR A REPORT IS KEPT IN A FILE APART — the desktop's tree read one from it and wrote it into one
// (ibApplicationTree::InsertItem, ReplaceItem, SaveItem), through these readers. One of that class read from that file of
// the session's temporary storage into the application, under its root.
//
// ⭐ READ AS A FILE IS OPENED, PUT IN AS A COPY IS PASTED (#578). The file's ids are its own and would collide with the
// application's, and so would whatever holds them — a form's button its command, a field its attribute. So the file is
// read on its own, in a tree of its own, and its root is copied and pasted under the application's: the paste gives
// every object its id and re-homes what holds one, as it does for a copy made in the designer. Null: the file is not
// one, or the paste refused it — and the application is as it was.
ibValueMetaObject* ReadExternal(ibMetaData* metaData, const ibClassID& clsid, const ibString& file)
{
	ibWriterMemory copied;
	if (clsid == g_metaDataToolCLSID) {
		const std::shared_ptr<ibMetaDataDataTool> reader = ibMetaData::MakeShared<ibMetaDataDataTool>();
		if (!reader->LoadFromTempFile(file) || !reader->CopyMetaObject(reader->GetDataTool(), copied))
			return nullptr;
	}
	else {
		const std::shared_ptr<ibMetaDataReport> reader = ibMetaData::MakeShared<ibMetaDataReport>();
		if (!reader->LoadFromTempFile(file) || !reader->CopyMetaObject(reader->GetReport(), copied))
			return nullptr;
	}
	ibReaderMemory pasted(copied.pointer(), copied.size());
	return metaData->PasteMetaObject(clsid, metaData->GetRootMetaObject(), pasted);
}

// …and that one written into that file.
bool WriteExternal(ibMetaData* metaData, ibValueMetaObject* object, const ibString& file)
{
	// A writer over the application's own object — never closed, for the object stays (the desktop's SaveItem).
	if (object->GetClassType() == g_metaDataToolCLSID)
		return std::make_shared<ibMetaDataDataTool>(metaData, static_cast<ibValueMetaObjectDataTool*>(object))->SaveToTempFile(file);
	return std::make_shared<ibMetaDataReport>(metaData, static_cast<ibValueMetaObjectReport*>(object))->SaveToTempFile(file);
}

} // namespace

bool ibSchemaMetaData::AccessRight(const ibSession& session) const
{
	const ibMetaDataApplicationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_Administration();
}

void ibSchemaMetaData::Build(const ibSession& /*session*/, const ibDataNode& /*args*/,
	ibDataNode& /*result*/) const
{
}

std::function<void()> ibSchemaMetaData::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	namespace style = ibProtocol::ibRequest::ibMessage::ibStyle;
	ibMetaData* const metaData = FindMetaData(session, args, refusal, error);
	if (metaData == nullptr)
		return nullptr;

	switch (static_cast<ibProtocol::ibSchema::ibMetaData::ibCommand>(command)) {
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Open: {
		// …or by the module's address — where a runtime stopped or failed (the desktop's ibApplicationTree::EditModule).
		const ibString module = args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::Module);
		if (!module.IsEmpty()) {
			// …in the file it is in, when an external data tool's or report's (File — the name its failure gave): the one
			// open in a tab (FindMetadataByPath), as the desktop opened that file's document.
			const ibString file = args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::File);
			const ibBackendDocFrame* const frame = session.GetFrame();
			ibMetaData* const owner = file.IsEmpty() ? metaData
				: frame != nullptr ? frame->FindMetadataByPath(file) : nullptr;
			if (owner == nullptr) {
				refusal = ibProtocol::ibRefusal::NotFound;
				error = ibString::Format(ibT("the file %s is not open"), file);
				return nullptr;
			}
			const ibGuid moduleGuid(module);
			ibValueMetaObject* object = owner->FindAnyObjectByFilter(moduleGuid, true);
			// …and with no File, a file never saved: its failure had no name to give, so the open one that holds the module.
			if (object == nullptr && file.IsEmpty()) {
				if (const ibMetaDataDocument* const unsaved = docManager->FindMetaDataDocument(moduleGuid))
					object = unsaved->GetMetaData()->FindAnyObjectByFilter(moduleGuid, true);
			}
			if (object == nullptr || object->IsDeleted()) {
				refusal = ibProtocol::ibRefusal::NotFound;
				error = ibString::Format(ibT("nothing open is the module %s"), module);
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
		// (the client's CreateItem, the desktop's ibApplicationTree::CreateItem — NewItem, then OpenObjectForm) — made
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
		// What stands on it is asked HERE, before the work: a refusal raised inside the work reaches only the journal
		// (ibClientHost::Settle), so the person saw the object stay with no word why. Answered as a refusal, the
		// client says the sentence. RemoveMetaObject asks again, for its other callers.
		try {
			object->RefuseDelete();
		}
		catch (const ibCoreException& err) {
			refusal = ibProtocol::ibRefusal::Exception;
			error = err.GetErrorDescription();
			return nullptr;
		}
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
		// Done in the asking — nothing in the application changed.
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
		// The designer's Save (ibApplicationTree::Save) — the application written into its base. An external file is
		// saved by its tab's Save, as the desktop's document saved it: into the file the person keeps (ibTabDocument::Save).
		ibMetaDataApplicationBase* const application = session.GetMetaData();
		if (metaData != application) {
			refusal = ibProtocol::ibRefusal::BadParameter;
			error = ibT("an external file is saved by its tab");
			return nullptr;
		}
		return [application]() { application->SaveDatabase(); };
	}
	// THE APPLICATION'S OWN — the desktop's Application menu (designer/mainFrame, OnOpenApplication,
	// OnRollbackApplication, OnUpdateApplication, OnApplication), each the engine's verb. Each that replaces the
	// application is the work's: its Closed takes this client's editors over it down (the tree's, on its own session
	// only). And what the desktop said in a message box when it was done the server asks the person — only it knows how
	// it went.
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Browse: {
		ibMetaDataApplicationBase* const application = FindApplication(session, metaData, refusal, error);
		if (application == nullptr)
			return nullptr;
		ibMetaDataApplicationBase* const database = application->GetApplication();
		if (database == nullptr || database->GetRootMetaObject() == nullptr) {
			refusal = ibProtocol::ibRefusal::NotFound;
			error = ibT("the base has no application of its own");
			return nullptr;
		}
		// The desktop's OnOpenApplication: the browser over the database's copy raised when it is open — found by that
		// copy's root, its metaobject — else one opened. Read-only as the desktop's view was, and here the copy itself
		// says so: every door that would change it refuses (ibMetaData::IsEditable). Nothing changes it in place — Apply
		// puts a new one.
		return [database]() {
			ibValueMetaObject* const root = database->GetRootMetaObject();
			for (ibDocument* const doc : docManager->GetDocumentsVector()) {
				ibMetadataBrowserDocument* const browser = dynamic_cast<ibMetadataBrowserDocument*>(doc);
				if (browser != nullptr && browser->GetMetaObject() == root) {
					browser->Activate();
					return;
				}
			}
			database->SetReadOnly(true);
			ibMetadataBrowserDocument* const document = new ibMetadataBrowserDocument(
				std::static_pointer_cast<ibMetaDataApplicationBase>(database->shared_from_this()));
			docManager->AddDocument(document);
			document->SetTitle(root->GetModuleName());
			document->SetMetaObject(root);
			if (!document->OnCreate(root->GetModuleName(), ibDOC_NEW))
				document->DeleteAllViews();
		};
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Rollback: {
		ibMetaDataApplicationBase* const application = FindApplication(session, metaData, refusal, error);
		if (application == nullptr)
			return nullptr;
		return [application]() {
			ibString refusal;
			const bool rolledBack = application->RollbackApplication(refusal);
			ibBackendDocFrame* const frame = ibSession::CurrentFrame();
			if (frame == nullptr)
				return;
			if (rolledBack)
				frame->ShowModalMessage(_("Successfully reverted to the database's application!"), ibString(), style::OK | style::Centre);
			else if (!refusal.IsEmpty())
				frame->ShowModalMessage(refusal, ibString(), style::OK | style::Centre | style::IconWarning);
		};
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Apply: {
		ibMetaDataApplicationBase* const application = FindApplication(session, metaData, refusal, error);
		if (application == nullptr)
			return nullptr;
		// …and the last question is the ledger itself, shown where it has always been shown — the base held until the
		// person answers it.
		return [application]() {
			ibString refusal;
			const bool applied = application->ApplyApplication(refusal,
				[](const ibRestructureInfo& info) { return ibDialogApplyChange::ShowApplyChange(info); });
			ibBackendDocFrame* const frame = ibSession::CurrentFrame();
			if (!applied && !refusal.IsEmpty() && frame != nullptr)
				frame->ShowModalMessage(BackendErrorChain(refusal), _("Error"), style::OK | style::Centre | style::IconError);
		};
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Load:
	case ibProtocol::ibSchema::ibMetaData::ibCommand::LoadFiles: {
		// …or a data tool or a report, from the file it is kept in apart (the desktop's tree's Replace and Insert):
		// that one replaced by the one read (Id), or one read in under that object (Type, Parent).
		if (args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibMetaData::Id) != 0
			|| !args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::Type).IsEmpty()) {
			// The reader puts its object in before it reads — so the application's word is asked here, not at its door.
			if (!metaData->IsEditable()) {
				refusal = ibProtocol::ibRefusal::NoRight;
				error = ibT("the application is read-only");
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
			if (clsid != g_metaDataToolCLSID && clsid != g_metaReportCLSID) {
				refusal = ibProtocol::ibRefusal::BadParameter;
				error = ibT("only a data tool or a report is kept in a file");
				return nullptr;
			}
			// A file that is not one leaves the application as it was, and the person is told.
			const ibString file = args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::File);
			return [metaData, clsid, replaced, file]() {
				ibValueMetaObject* const read = ReadExternal(metaData, clsid, file);
				if (read == nullptr) {
					if (ibBackendDocFrame* const frame = ibSession::CurrentFrame())
						frame->ShowModalMessage(BackendErrorChain(_("The data tool or report could not be read from the file.")),
							ibString(), style::OK | style::Centre | style::IconError);
					return;
				}
				if (replaced == nullptr)
					return;
				// THE ONE READ TAKES THE REPLACED ONE'S PLACE — the reader puts it in last, and the desktop's tree kept the
				// row where it stood (ReplaceItem repointed it): moved there before the old one goes, so the order of the
				// application's objects is the one it was.
				ibValueMetaObject* const parent = replaced->GetParent();
				if (parent != nullptr && read->GetParent() == parent)
					parent->ChangeChildPosition(read, parent->GetChildPosition(replaced));
				metaData->RemoveMetaObject(replaced);
			};
		}
		ibMetaDataApplicationBase* const application = FindApplication(session, metaData, refusal, error);
		if (application == nullptr)
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
		// Read as the desktop's LoadApplicationFromFile reads its file (LoadApplicationFromBuffer) — or, LoadFiles, as the files of a
		// directory the client read out of it, one node (ibFileProvider::Collect); a file that is no application leaves
		// the one there as it was, and the person is told.
		const bool asFiles = command == static_cast<std::int32_t>(ibProtocol::ibSchema::ibMetaData::ibCommand::LoadFiles);
		return [application, bytes, asFiles]() {
			ibDataNode files;
			if (asFiles ? ibBinaryProvider().Read(bytes, files) && application->LoadApplicationFromFiles(files)
					: application->LoadApplicationFromBuffer(bytes))
				return;
			if (ibBackendDocFrame* const frame = ibSession::CurrentFrame())
				frame->ShowModalMessage(BackendErrorChain(_("The application could not be read from the file.")),
					ibString(), style::OK | style::Centre | style::IconError);
		};
	}
	case ibProtocol::ibSchema::ibMetaData::ibCommand::Unload:
	case ibProtocol::ibSchema::ibMetaData::ibCommand::UnloadFiles: {
		// …or a data tool or a report, written as the file it is kept in apart (Id — the desktop's tree's Save).
		if (args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibMetaData::Id) != 0) {
			ibValueMetaObject* const object = FindObject(metaData, args, ibProtocol::ibSchema::ibMetaData::Id, refusal, error);
			if (object == nullptr)
				return nullptr;
			if (object->GetClassType() != g_metaDataToolCLSID && object->GetClassType() != g_metaReportCLSID) {
				refusal = ibProtocol::ibRefusal::BadParameter;
				error = ibT("only a data tool or a report is kept in a file");
				return nullptr;
			}
			if (!WriteExternal(metaData, object, args.GetValue<ibString>(ibProtocol::ibSchema::ibMetaData::File))) {
				refusal = ibProtocol::ibRefusal::Failed;
				error = ibString::Format(ibT("'%s' was not written"), object->GetName());
				return nullptr;
			}
			// Done in the asking — nothing in the application changed.
			return []() {};
		}
		ibMetaDataApplicationBase* const application = FindApplication(session, metaData, refusal, error);
		if (application == nullptr)
			return nullptr;
		// The application's file — or, UnloadFiles, the files of its directory as one node, for the client to write
		// into the directory the person chose (ibFileProvider::Publish).
		ibMemoryBuffer bytes;
		ibDataNode files;
		bool written = false;
		try {
			written = command == static_cast<std::int32_t>(ibProtocol::ibSchema::ibMetaData::ibCommand::UnloadFiles)
				? application->SaveApplicationToFiles(files) && ibBinaryProvider().Write(files, bytes)
				: application->SaveApplicationToBuffer(bytes);
		}
		catch (const ibCoreException& err) {
			refusal = ibProtocol::ibRefusal::Exception;
			error = err.GetErrorDescription();
			return nullptr;
		}
		if (!written) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = BackendErrorChain(ibT("the application could not be written"));
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
			error = ibT("the application was not written");
			return nullptr;
		}
		// Done in the asking — nothing in the application changed.
		return []() {};
	}
	default:
		break;
	}

	refusal = ibProtocol::ibRefusal::NotFound;
	error = ibString::Format(ibT("the application tree has no command %d"), command);
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
			error = ibT("the session has no application");
		}
		return metaData;
	}

	const ibClientFrame* const frame = dynamic_cast<const ibClientFrame*>(session.GetFrame());
	const ibClientChildFrame* const tab = frame != nullptr ? frame->Tab(frame->FindTab(tabId)) : nullptr;
	// A tree's tab — the file its document holds; a tab of a metaobject (its form, its module) — the application that
	// object is in.
	ibMetaData* metaData = nullptr;
	if (const ibMetaDataDocument* const tree = tab != nullptr ? dynamic_cast<const ibMetaDataDocument*>(tab->GetDocument()) : nullptr)
		metaData = tree->GetMetaData();
	else if (const ibMetaDocument* const document = tab != nullptr ? dynamic_cast<const ibMetaDocument*>(tab->GetDocument()) : nullptr)
		metaData = document->GetMetaObject() != nullptr ? document->GetMetaObject()->GetMetaData() : nullptr;
	if (metaData == nullptr) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = ibString::Format(ibT("no tab %d shows an application"), tabId);
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
		error = ibString::Format(ibT("nothing in the application has id %d"), id);
		return nullptr;
	}
	return object;
}
