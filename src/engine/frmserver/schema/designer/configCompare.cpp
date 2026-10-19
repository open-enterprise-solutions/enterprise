#include "configCompare.h"

#include "backend/metaCollection/metaDiff.h"
#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/temp/tempStorage.h"
#include "core/fileSystem/fs.h"
#include "core/serialize/dataBuilder.h"

#include "frmserver/schema/designer/metaData.h"

namespace {

using ibMode = ibProtocol::ibSchema::ibConfigCompare::ibMode;
using ibOp   = ibProtocol::ibSchema::ibConfigCompare::ibOp;

wxMemoryBuffer ReadTempFile(const wxString& id, ibProtocol::ibRefusal& refusal, wxString& error)
{
	wxMemoryBuffer bytes;
	const ibTempFile file(id);
	if (!file.IsOpened()) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = wxT("no such file");
		return bytes;
	}
	wxMemoryBuffer part;
	for (int index = 0; file.ReadPart(index, part); ++index)
		bytes.AppendData(part.GetData(), part.GetDataLen());
	return bytes;
}

bool WriteTempFile(const wxString& id, const wxMemoryBuffer& bytes, ibProtocol::ibRefusal& refusal, wxString& error)
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
	const wxMemoryBuffer bytes = ReadTempFile(fileId, refusal, error);
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
	const ibMode mode = static_cast<ibMode>(args.GetValue<s32>(ibProtocol::ibSchema::ibConfigCompare::Mode));
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
		row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Status, static_cast<s32>(rec.m_status));
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
					static_cast<s32>(rec.m_left->GetMetaID()));
			if (rec.m_right != nullptr)
				row.SetValue(ibProtocol::ibSchema::ibConfigCompare::RightId,
					static_cast<s32>(rec.m_right->GetMetaID()));
			// Side summaries for object rows (name on each side).
			if (rec.m_left != nullptr)
				row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Left, rec.m_left->GetName());
			if (rec.m_right != nullptr)
				row.SetValue(ibProtocol::ibSchema::ibConfigCompare::Right, rec.m_right->GetName());
		}
	}
}

void ApplyAdd(ibMetaData* targetMeta, ibMetaData* sourceMeta,
	ibValueMetaObject* source, ibValueMetaObject* targetParent)
{
	if (source == nullptr || targetParent == nullptr || targetMeta == nullptr || sourceMeta == nullptr)
		return;
	ibWriterMemory writer;
	if (!sourceMeta->CopyMetaObject(source, writer))
		return;
	ibReaderMemory reader(writer.pointer(), writer.size());
	if (ibValueMetaObject* newObj = targetMeta->PasteMetaObject(source->GetClassType(), targetParent, reader))
		newObj->SetCommonGuid(source->GetGuid());
}

void ApplyDelete(ibMetaData* targetMeta, ibValueMetaObject* target)
{
	if (target == nullptr || targetMeta == nullptr)
		return;
	targetMeta->RemoveMetaObject(target, target->GetParent());
}

void ApplyReplace(ibMetaData* targetMeta, ibMetaData* sourceMeta,
	ibValueMetaObject* source, ibValueMetaObject* target)
{
	if (source == nullptr || target == nullptr || targetMeta == nullptr || sourceMeta == nullptr)
		return;
	ibValueMetaObject* parent = target->GetParent();
	if (parent == nullptr)
		return;
	ibWriterMemory writer;
	if (!sourceMeta->CopyMetaObject(source, writer))
		return;
	ibReaderMemory reader(writer.pointer(), writer.size());
	const ibGuid sourceGuid = source->GetGuid();
	const ibClassID sourceClsid = source->GetClassType();
	if (!targetMeta->RemoveMetaObject(target, parent))
		return;
	if (ibValueMetaObject* newObj = targetMeta->PasteMetaObject(sourceClsid, parent, reader))
		newObj->SetCommonGuid(sourceGuid);
}

ibValueMetaObject* FindById(ibMetaData* meta, s32 id)
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
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	wxString error;
	ibCompareSides sides;
	if (!ResolveSides(session, args, sides, refusal, error))
		return;
	const std::vector<ibMetaDiffRecord> records = ibMetaDiffWalker::Walk(
		sides.left != nullptr ? sides.left->GetCommonMetaObject() : nullptr,
		sides.right != nullptr ? sides.right->GetCommonMetaObject() : nullptr);
	WriteRecords(records, result);
}

std::function<void()> ibSchemaConfigCompare::Command(const ibSession& session, s32 command,
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
	const ibMode mode = static_cast<ibMode>(args.GetValue<s32>(ibProtocol::ibSchema::ibConfigCompare::Mode));

	// Database compare has no Push target (matches the desktop: no right-save callback).
	if (!pull && mode == ibMode::Database) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = wxT("push into the database baseline is not supported; update the database configuration separately");
		return nullptr;
	}

	const ibDataNode* const opsNode = args.FindChild(ibProtocol::ibSchema::ibConfigCompare::Ops);
	if (opsNode == nullptr || opsNode->Children().empty()) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = wxT("Merge needs Ops");
		return nullptr;
	}

	struct ibOpSpec {
		ibOp op = ibOp::Add;
		s32 leftId = 0;
		s32 rightId = 0;
		s32 parentLeftId = 0;
		s32 parentRightId = 0;
	};
	std::vector<ibOpSpec> specs;
	for (const ibDataNode& node : opsNode->Children()) {
		ibOpSpec spec;
		spec.op = static_cast<ibOp>(node.GetValue<s32>(ibProtocol::ibSchema::ibConfigCompare::Op));
		spec.leftId = node.GetValue<s32>(ibProtocol::ibSchema::ibConfigCompare::LeftId);
		spec.rightId = node.GetValue<s32>(ibProtocol::ibSchema::ibConfigCompare::RightId);
		spec.parentLeftId = node.GetValue<s32>(ibProtocol::ibSchema::ibConfigCompare::ParentLeftId);
		spec.parentRightId = node.GetValue<s32>(ibProtocol::ibSchema::ibConfigCompare::ParentRightId);
		specs.push_back(spec);
	}

	// Capture holders by value so file configs stay alive through the work lambda.
	auto leftFile = sides.leftFile;
	auto rightFile = sides.rightFile;
	ibMetaData* left = sides.left;
	ibMetaData* right = sides.right;
	const wxString fileId = args.GetValue<wxString>(ibProtocol::ibSchema::ibConfigCompare::File);
	const wxString file2Id = args.GetValue<wxString>(ibProtocol::ibSchema::ibConfigCompare::File2);

	return [pull, mode, specs, left, right, leftFile, rightFile, fileId, file2Id]() {
		int applied = 0;
		for (const ibOpSpec& spec : specs) {
			ibMetaData* targetMeta = pull ? left : right;
			ibMetaData* sourceMeta = pull ? right : left;
			try {
				switch (spec.op) {
				case ibOp::Add: {
					ibValueMetaObject* source = FindById(sourceMeta, pull ? spec.rightId : spec.leftId);
					ibValueMetaObject* parent = FindById(targetMeta,
						pull ? spec.parentLeftId : spec.parentRightId);
					if (parent == nullptr && targetMeta != nullptr)
						parent = targetMeta->GetCommonMetaObject();
					ApplyAdd(targetMeta, sourceMeta, source, parent);
					++applied;
					break;
				}
				case ibOp::Delete: {
					ibValueMetaObject* target = FindById(targetMeta, pull ? spec.leftId : spec.rightId);
					ApplyDelete(targetMeta, target);
					++applied;
					break;
				}
				case ibOp::Replace: {
					ibValueMetaObject* source = FindById(sourceMeta, pull ? spec.rightId : spec.leftId);
					ibValueMetaObject* target = FindById(targetMeta, pull ? spec.leftId : spec.rightId);
					ApplyReplace(targetMeta, sourceMeta, source, target);
					++applied;
					break;
				}
				}
			}
			catch (...) {
				// One op's failure must not abort the rest — matches the desktop's per-row try.
			}
		}

		// Push: persist the right-side file config back into its temp file for the client to download.
		if (!pull) {
			ibProtocol::ibRefusal refuse = ibProtocol::ibRefusal::None;
			wxString err;
			wxMemoryBuffer bytes;
			ibMetaDataConfigurationBase* rightBase = dynamic_cast<ibMetaDataConfigurationBase*>(right);
			if (rightBase != nullptr && rightBase->SaveConfigToBuffer(bytes)) {
				const wxString outId = (mode == ibMode::TwoFiles) ? file2Id : fileId;
				WriteTempFile(outId, bytes, refuse, err);
			}
		}

		(void)applied;
		(void)leftFile;
		(void)rightFile;
	};
}
