#include "configCompare.h"

#include "backend/metaCollection/metaDiff.h"
#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/temp/tempStorage.h"
#include "backend/backend_mainFrame.h"   // ibBackendDocFrame — the person told how the merge went
#include "core/diagnostics/journal.h"    // the merge's refusals
#include "core/exception.h"              // ibCoreException — a walk that cannot be made is refused
#include "core/memory/memoryStream.h"
#include "core/serialize/dataBuilder.h"

#include "frmserver/schema/designer/metaData.h"

namespace {

using ibMode = ibProtocol::ibSchema::ibConfigCompare::ibMode;
using ibOp   = ibProtocol::ibSchema::ibConfigCompare::ibOp;

ibMemoryBuffer ReadTempFile(const wxString& id, ibProtocol::ibRefusal& refusal, wxString& error)
{
	ibMemoryBuffer bytes;
	const ibTempFile file(id);
	if (!file.IsOpened()) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = wxT("no such file");
		return bytes;
	}
	ibMemoryBuffer part;
	for (int index = 0; file.ReadPart(index, part); ++index)
		bytes.AppendData(part.GetData(), part.GetDataLen());
	return bytes;
}

bool WriteTempFile(const wxString& id, const ibMemoryBuffer& bytes, ibProtocol::ibRefusal& refusal, wxString& error)
{
	ibTempFile file;
	if (!file.Create(id, true)) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = wxT("no such file");
		return false;
	}
	if (!file.Write(bytes.GetData(), bytes.GetDataLen())) {
		refusal = ibProtocol::ibRefusal::Failed;
		error = wxT("the configuration was not written");
		return false;
	}
	return true;
}

// Load an uploaded .oap into a transient file configuration. Caller owns the shared_ptr for the walk/merge lifetime.
std::shared_ptr<ibMetaDataConfigurationFile> LoadFileConfig(const wxString& fileId,
	ibProtocol::ibRefusal& refusal, wxString& error)
{
	const ibMemoryBuffer bytes = ReadTempFile(fileId, refusal, error);
	if (refusal != ibProtocol::ibRefusal::None)
		return nullptr;
	auto holder = ibMetaData::MakeShared<ibMetaDataConfigurationFile>();
	if (!holder->LoadConfigFromBuffer(bytes)) {
		refusal = ibProtocol::ibRefusal::Failed;
		error = wxT("the configuration could not be read from the file");
		return nullptr;
	}
	return holder;
}

struct ibCompareSides {
	ibMetaData* left = nullptr;
	ibMetaData* right = nullptr;
	// Holders keep file configs alive while pointers into their trees are used.
	std::shared_ptr<ibMetaDataConfigurationFile> leftFile;
	std::shared_ptr<ibMetaDataConfigurationFile> rightFile;
};

bool ResolveSides(const ibSession& session, const ibDataNode& args, ibCompareSides& sides,
	ibProtocol::ibRefusal& refusal, wxString& error)
{
	const ibMode mode = static_cast<ibMode>(args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibConfigCompare::Mode));
	ibMetaDataConfigurationBase* const active = session.GetMetaData();
	if (active == nullptr) {
		refusal = ibProtocol::ibRefusal::NoSession;
		error = wxT("the session has no configuration");
		return false;
	}

	switch (mode) {
	case ibMode::File: {
		sides.rightFile = LoadFileConfig(
			args.GetValue<wxString>(ibProtocol::ibSchema::ibConfigCompare::File), refusal, error);
		if (sides.rightFile == nullptr)
			return false;
		sides.left = active;
		sides.right = sides.rightFile.get();
		return true;
	}
	case ibMode::Database: {
		ibMetaDataConfigurationStorage* const storage =
			dynamic_cast<ibMetaDataConfigurationStorage*>(active);
		if (storage == nullptr || storage->GetConfiguration() == nullptr) {
			refusal = ibProtocol::ibRefusal::BadParameter;
			error = wxT("database configuration is not available");
			return false;
		}
		sides.left = storage;
		sides.right = storage->GetConfiguration();
		return true;
	}
	case ibMode::TwoFiles: {
		sides.leftFile = LoadFileConfig(
			args.GetValue<wxString>(ibProtocol::ibSchema::ibConfigCompare::File), refusal, error);
		if (sides.leftFile == nullptr)
			return false;
		sides.rightFile = LoadFileConfig(
			args.GetValue<wxString>(ibProtocol::ibSchema::ibConfigCompare::File2), refusal, error);
		if (sides.rightFile == nullptr)
			return false;
		sides.left = sides.leftFile.get();
		sides.right = sides.rightFile.get();
		return true;
	}
	default:
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = wxT("unknown compare mode");
		return false;
	}
}

void WriteRecords(const std::vector<ibMetaDiffRecord>& records, ibDataNode& result)
{
	ibDataNode& list = result.Child(ibProtocol::ibSchema::ibConfigCompare::Records);
	for (const ibMetaDiffRecord& rec : records) {
		ibDataNode& row = list.AddChild(0, 0);
		row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Status, static_cast<std::int32_t>(rec.m_status));
		row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Parent, rec.m_parentIndex);
		row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Depth, rec.m_depth);
		if (rec.IsGroup()) {
			row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Group,
				wxString::Format(wxT("%llu"), static_cast<unsigned long long>(rec.m_groupClsid)));
			row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Name,
				ibMetaDiffWalker::GroupLabelFor(rec.m_groupClsid));
		}
		else if (rec.IsProperty()) {
			row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Property, rec.m_propertyName);
			row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Left, rec.m_leftValue);
			row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Right, rec.m_rightValue);
		}
		else {
			const ibValueMetaObject* any = rec.GetAnyObject();
			if (any != nullptr) {
				row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Name, any->GetName());
				row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Picture,
					wxString::Format(wxT("%llu"),
						static_cast<unsigned long long>(any->GetClassType())));
			}
			if (rec.m_left != nullptr)
				row.SetValue(ibProtocol::ibSchema::ibConfigCompare::LeftId,
					static_cast<std::int32_t>(rec.m_left->GetMetaID()));
			if (rec.m_right != nullptr)
				row.SetValue(ibProtocol::ibSchema::ibConfigCompare::RightId,
					static_cast<std::int32_t>(rec.m_right->GetMetaID()));
			// Side summaries for object rows (name on each side).
			if (rec.m_left != nullptr)
				row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Left, rec.m_left->GetName());
			if (rec.m_right != nullptr)
				row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Right, rec.m_right->GetName());
		}
	}
}

// The three merge primitives, the desktop's (designer/docManager/templates/docViewConfigCompare.cpp): each answers
// whether it went through, so the merge says what it did.
bool ApplyAdd(ibMetaData* targetMeta, ibMetaData* sourceMeta,
	ibValueMetaObject* source, ibValueMetaObject* targetParent)
{
	if (source == nullptr || targetParent == nullptr || targetMeta == nullptr || sourceMeta == nullptr)
		return false;
	ibWriterMemory writer;
	if (!sourceMeta->CopyMetaObject(source, writer))
		return false;
	ibReaderMemory reader(writer.pointer(), writer.size());
	ibValueMetaObject* const newObj = targetMeta->PasteMetaObject(source->GetClassType(), targetParent, reader);
	if (newObj == nullptr)
		return false;
	newObj->SetCommonGuid(source->GetGuid());
	return true;
}

bool ApplyDelete(ibMetaData* targetMeta, ibValueMetaObject* target)
{
	if (target == nullptr || targetMeta == nullptr)
		return false;
	return targetMeta->RemoveMetaObject(target, target->GetParent());
}

// The paste only when the remove went through, as the desktop's.
bool ApplyReplace(ibMetaData* targetMeta, ibMetaData* sourceMeta,
	ibValueMetaObject* source, ibValueMetaObject* target)
{
	if (source == nullptr || target == nullptr || targetMeta == nullptr || sourceMeta == nullptr)
		return false;
	ibValueMetaObject* parent = target->GetParent();
	if (parent == nullptr)
		return false;
	ibWriterMemory writer;
	if (!sourceMeta->CopyMetaObject(source, writer))
		return false;
	ibReaderMemory reader(writer.pointer(), writer.size());
	const ibGuid sourceGuid = source->GetGuid();
	const ibClassID sourceClsid = source->GetClassType();
	if (!targetMeta->RemoveMetaObject(target, parent))
		return false;
	ibValueMetaObject* const newObj = targetMeta->PasteMetaObject(sourceClsid, parent, reader);
	if (newObj == nullptr)
		return false;
	newObj->SetCommonGuid(sourceGuid);
	return true;
}

ibValueMetaObject* FindById(ibMetaData* meta, std::int32_t id)
{
	return (meta != nullptr && id > 0) ? ibFindMetaObjectById(meta, id) : nullptr;
}

} // namespace

bool ibSchemaConfigCompare::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_Administration();
}

void ibSchemaConfigCompare::Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const
{
	// A side that cannot be read is the person's refusal, not an empty walk — "no differences" would be a lie.
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	wxString error;
	ibCompareSides sides;
	if (!ResolveSides(session, args, sides, refusal, error))
		ibCoreException::Error(wxT("%s"), error);
	const std::vector<ibMetaDiffRecord> records = ibMetaDiffWalker::Walk(
		sides.left != nullptr ? sides.left->GetCommonMetaObject() : nullptr,
		sides.right != nullptr ? sides.right->GetCommonMetaObject() : nullptr);
	WriteRecords(records, result);
}

std::function<void()> ibSchemaConfigCompare::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, wxString& error) const
{
	if (static_cast<ibProtocol::ibSchema::ibCommand>(command) != ibProtocol::ibSchema::ibCommand::Merge) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = wxString::Format(wxT("config compare has no command %d"), command);
		return nullptr;
	}

	ibCompareSides sides;
	if (!ResolveSides(session, args, sides, refusal, error))
		return nullptr;

	const bool pull = args.GetValue<bool>(ibProtocol::ibSchema::ibConfigCompare::Pull);
	const ibMode mode = static_cast<ibMode>(args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibConfigCompare::Mode));

	// Database compare has no Push target (matches the desktop: no right-save callback).
	if (!pull && mode == ibMode::Database) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = wxT("push into the database baseline is not supported; update the database configuration separately");
		return nullptr;
	}

	struct ibOpSpec {
		ibOp op = ibOp::Add;
		std::int32_t leftId = 0;
		std::int32_t rightId = 0;
		std::int32_t parentId = 0;
	};
	// No ops — the rows chosen were all of a kind the tab does not send (a reorder): nothing applied, and said so, as the
	// desktop's "applied to 0".
	std::vector<ibOpSpec> specs;
	if (const ibDataNode* const opsNode = args.FindChild(ibProtocol::ibSchema::ibConfigCompare::Ops)) {
		for (const ibDataNode& node : opsNode->Children()) {
			ibOpSpec spec;
			spec.op = static_cast<ibOp>(node.GetValue<std::int32_t>(ibProtocol::ibSchema::ibConfigCompare::Op));
			spec.leftId = node.GetValue<std::int32_t>(ibProtocol::ibSchema::ibConfigCompare::LeftId);
			spec.rightId = node.GetValue<std::int32_t>(ibProtocol::ibSchema::ibConfigCompare::RightId);
			spec.parentId = node.GetValue<std::int32_t>(ibProtocol::ibSchema::ibConfigCompare::ParentId);
			specs.push_back(spec);
		}
	}

	// Capture holders by value so file configs stay alive through the work lambda.
	auto leftFile = sides.leftFile;
	auto rightFile = sides.rightFile;
	ibMetaData* left = sides.left;
	ibMetaData* right = sides.right;
	const wxString fileId = args.GetValue<wxString>(ibProtocol::ibSchema::ibConfigCompare::File);
	const wxString file2Id = args.GetValue<wxString>(ibProtocol::ibSchema::ibConfigCompare::File2);

	return [pull, mode, specs, left, right, leftFile, rightFile, fileId, file2Id]() {
		ibMetaData* const targetMeta = pull ? left : right;
		ibMetaData* const sourceMeta = pull ? right : left;
		int applied = 0;
		wxString failures;
		for (const ibOpSpec& spec : specs) {
			ibValueMetaObject* const source = FindById(sourceMeta, pull ? spec.rightId : spec.leftId);
			ibValueMetaObject* const target = FindById(targetMeta, pull ? spec.leftId : spec.rightId);
			const wxString name = source != nullptr ? source->GetName() : target != nullptr ? target->GetName() : wxString();
			// One op's failure does not stop the rest — the desktop's per-row try; each one said, in the journal as there,
			// and to the person below.
			try {
				bool done = false;
				switch (spec.op) {
				case ibOp::Add: {
					ibValueMetaObject* parent = FindById(targetMeta, spec.parentId);
					if (parent == nullptr && targetMeta != nullptr)
						parent = targetMeta->GetCommonMetaObject();
					done = ApplyAdd(targetMeta, sourceMeta, source, parent);
					break;
				}
				case ibOp::Delete:
					done = ApplyDelete(targetMeta, target);
					break;
				case ibOp::Replace:
					done = ApplyReplace(targetMeta, sourceMeta, source, target);
					break;
				}
				if (done)
					++applied;
				else
					failures += wxT("\n") + name;
			}
			catch (const ibCoreException& err) {
				ibJournalInfo(wxT("designer"), wxT("[merge] %s: %s"), name, err.GetErrorDescription());
				failures += wxT("\n") + name + wxT(": ") + err.GetErrorDescription();
			}
			catch (...) {
				ibJournalInfo(wxT("designer"), wxT("[merge] unknown error on %s"), name);
				failures += wxT("\n") + name;
			}
		}

		// Push: the right side written back into its file of the session's, which the client takes down.
		bool savedRight = true;
		if (!pull) {
			ibProtocol::ibRefusal refuse = ibProtocol::ibRefusal::None;
			wxString err;
			ibMemoryBuffer bytes;
			ibMetaDataConfigurationBase* rightBase = dynamic_cast<ibMetaDataConfigurationBase*>(right);
			savedRight = rightBase != nullptr && rightBase->SaveConfigToBuffer(bytes)
				&& WriteTempFile(mode == ibMode::TwoFiles ? file2Id : fileId, bytes, refuse, err);
		}

		// HOW IT WENT, said by the one who did it: what was applied, what was not and why.
		wxString message = wxString::Format(_("Merge applied to %d item(s)."), applied);
		if (!failures.IsEmpty())
			message += wxT("\n\n") + _("Not applied:") + failures;
		if (!savedRight)
			message += wxT("\n\n") + _("Warning: failed to save the other-side file.");
		else if (pull)
			message += wxT("\n\n") + _("Use Configuration > Save configuration to persist the changes.");
		if (ibBackendDocFrame* const frame = ibSession::CurrentFrame())
			frame->ShowModalMessage(message, _("Merge"),
				wxOK | wxCENTRE | (failures.IsEmpty() && savedRight ? wxICON_INFORMATION : wxICON_WARNING));

		(void)leftFile;
		(void)rightFile;
	};
}
