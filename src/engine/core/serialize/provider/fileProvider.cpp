#include "core/serialize/provider/fileProvider.h"
#include "core/serialize/provider/jsonProvider.h"   // ibJsonProvider — a node's one JSON form, each file's text
#include "core/exception.h"
#include "core/fileSystem.h"
#include "core/fiber/fiber.h"
#include "core/localization/dictionary.h"   // _() — the refusals' words

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace {

namespace fs = std::filesystem;

// The file a node's own entries are written in, in its folder; the root's is the manifest at the top.
const ibString kNodeFile = ibT("node.json");
const ibString kManifest = ibT("configuration.json");

// The keys the layout adds to a node's JSON — structural, as ibJsonProvider's own are (`Node…`): the folders its
// children are in, a value kept in a file beside it, and on the root what the directory is.
const ibString kNodeFolders = ibT("NodeFolders");
const ibString kNodeFile_   = ibT("NodeFile");
const ibString kNodeFormat  = ibT("NodeFormat");
const ibString kNodeVersion = ibT("NodeVersion");
const ibString kFormat      = ibT("OES-Configuration");
constexpr int  kVersion     = 1;

// A file's name is the same file whatever its case on the disks a repository is checked out on (Windows, macOS):
// `Goods` and `goods` are one, and two parts named so are refused rather than written into one another.
struct ibNoCaseLess {
	bool operator()(const ibString& one, const ibString& other) const { return one.CmpNoCase(other) < 0; }
};
using ibNameSet = std::set<ibString, ibNoCaseLess>;

void Refuse(const ibString& message)
{
	ibCoreException::Error(_("Configuration files: %s"), message);
}

void CheckDepth(unsigned depth)
{
	if (depth > 128 || ibFiber::StackLow())
		Refuse(_("the structure is nested too deeply"));
}

ibString Join(const ibString& folder, const ibString& name)
{
	return folder.IsEmpty() ? name : folder + ibT("/") + name;
}

// A name that is one file's or folder's on every disk: no separator, no character Windows refuses, no dot (a file
// beside a node is told by its extension), no device name, nothing a path step means.
bool IsFileName(const ibString& name)
{
	if (name.IsEmpty() || name.length() > 120 || name.GetChar(name.length() - 1) == ibT(' ') || name.GetChar(0) == ibT(' '))
		return false;
	for (const ibChar c : name) {
		if (c < 0x20 || c == ibT('.') || c == ibT('/') || c == ibT('\\') || c == ibT(':') || c == ibT('*')
			|| c == ibT('?') || c == ibT('"') || c == ibT('<') || c == ibT('>') || c == ibT('|'))
			return false;
	}
	static const ibString kDevices[] = { ibT("CON"), ibT("PRN"), ibT("AUX"), ibT("NUL"),
		ibT("COM1"), ibT("COM2"), ibT("COM3"), ibT("COM4"), ibT("COM5"), ibT("COM6"), ibT("COM7"), ibT("COM8"), ibT("COM9"),
		ibT("LPT1"), ibT("LPT2"), ibT("LPT3"), ibT("LPT4"), ibT("LPT5"), ibT("LPT6"), ibT("LPT7"), ibT("LPT8"), ibT("LPT9") };
	for (const ibString& device : kDevices)
		if (name.IsSameAs(device, false))
			return false;
	return true;
}

// …and a file the layout names, read back: the names on the way to the value, a dot between, and one extension it writes.
bool IsLaidOutFile(const ibString& name)
{
	ibString stem;
	if (!name.EndsWith(ibT(".txt"), &stem) && !name.EndsWith(ibT(".bin"), &stem))
		return false;
	for (size_t dots = stem.Freq(ibT('.')); ; --dots) {
		ibString rest;
		if (!IsFileName(stem.BeforeFirst(ibT('.'), &rest)))
			return false;
		if (dots == 0)
			return true;
		stem = rest;
	}
}

ibMemoryBuffer Bytes(const ibString& text)
{
	const std::string utf8 = text.ToUTF8();
	ibMemoryBuffer bytes;
	bytes.AppendData(utf8.data(), utf8.size());
	return bytes;
}

ibString Text(const ibMemoryBuffer& bytes, const ibString& path)
{
	const ibString text = ibString::FromUTF8(static_cast<const char*>(bytes.GetData()), bytes.GetDataLen());
	if (bytes.GetDataLen() != 0 && text.IsEmpty())
		Refuse(ibString::Format(_("'%s' is not UTF-8 text"), path));
	return text;
}

ibString PathText(const fs::path& path)
{
	const std::u8string text = path.generic_u8string();
	return ibString::FromUTF8(reinterpret_cast<const char*>(text.data()), text.size());
}

// Only files under the directory may be read or written: a repository somebody else made cannot turn a name in it
// into a path anywhere else.
fs::path Path(const fs::path& directory, const ibString& name)
{
	if (name.IsEmpty() || name.Contains(ibT("\\")) || name.Contains(ibT(":")) || name.Find(ibChar(0)) != -1)
		Refuse(ibString::Format(_("'%s' is not a file name inside the directory"), name));
	fs::path path = directory;
	if (fs::is_symlink(fs::symlink_status(path)))
		Refuse(_("an export directory cannot be a symbolic link"));
	ibString rest = name;
	while (!rest.IsEmpty()) {
		ibString after;
		const ibString step = rest.BeforeFirst(ibT('/'), &after);
		if (step.IsEmpty() || step == ibT(".") || step == ibT(".."))
			Refuse(ibString::Format(_("'%s' is not a file name inside the directory"), name));
		path /= ibPathOf(step);
		if (fs::is_symlink(fs::symlink_status(path)))
			Refuse(_("a configuration file cannot be a symbolic link"));
		rest = after;
	}
	return path;
}

ibMemoryBuffer ReadFile(const fs::path& path, const ibString& name)
{
	if (!fs::is_regular_file(path))
		Refuse(ibString::Format(_("'%s' is missing or is not a file"), name));
	std::ifstream input(path, std::ios::binary);
	const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	if (!input && !input.eof())
		Refuse(ibString::Format(_("'%s' could not be read"), name));
	ibMemoryBuffer buffer;
	buffer.AppendData(bytes.data(), bytes.size());
	return buffer;
}

void WriteFile(const fs::path& path, const ibMemoryBuffer& bytes, const ibString& name)
{
	fs::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(static_cast<const char*>(bytes.GetData()), static_cast<std::streamsize>(bytes.GetDataLen()));
	output.close();
	if (!output)
		Refuse(ibString::Format(_("'%s' could not be written"), name));
}

// THE TREE LAID OUT — each node into its folder.
class ibLayout {
public:
	ibLayout(const ibJsonProvider& json, const std::function<ibString(ibClassID)>& resolveType)
		: m_json(json), m_resolveType(resolveType) {}

	// The node's file: its entries as they are, a text of several lines and a binary set aside into files beside it —
	// at any depth of a value: a module's text is a sub-node's field (`ObjectModule.Module.txt`) — and its children as
	// the folders they are written in.
	ibDataNode Shown(const ibDataNode& node, const ibString& folder, ibNameSet& names, unsigned depth)
	{
		CheckDepth(depth);
		ibDataNode shown(node.GetClsid(), node.GetMetaId());
		if (node.HasRawData())
			shown.SetRawData(node.RawData());

		unsigned index = 0;
		for (const auto& [name, value] : node.Fields()) {
			shown.AddField(name, Aside(value, IsFileName(name) ? name : ibString(),
				ibString::Format(ibT("field-%u"), index), folder, names, depth + 1));
			++index;
		}

		std::vector<ibDataValue> folders;
		unsigned position = 0;
		for (const ibDataNode& child : node.Children()) {
			const ibString name = FolderOf(child, position++);
			if (!names.insert(name).second)
				Refuse(ibString::Format(_("two parts of the configuration are named '%s' - a name told apart only by case "
					"is the same folder"), Join(folder, name)));
			WriteNode(child, Join(folder, name), depth + 1);
			folders.push_back(ibDataValue::String(name));
		}
		if (!folders.empty())
			shown.AddField(kNodeFolders, ibDataValue::Array(std::move(folders)));
		return shown;
	}

	void WriteNode(const ibDataNode& node, const ibString& folder, unsigned depth)
	{
		ibNameSet names{ kNodeFile };
		PutNode(Join(folder, kNodeFile), Shown(node, folder, names, depth));
	}

	void PutNode(const ibString& path, const ibDataNode& shown)
	{
		ibString text;
		m_json.Write(shown, text);
		Put(path, Bytes(text + ibT("\n")));
	}

	ibDataNode m_files;

private:
	// A value with what is set aside replaced by the file it is in (`{"NodeFile": …}`). The file is named by the names
	// on the way to it, a dot between (`named`, empty where one cannot be a file's); where that is not one, or is taken,
	// by the places on the way (`placed`), which no two values share.
	ibDataValue Aside(const ibDataValue& value, const ibString& named, const ibString& placed, const ibString& folder,
		ibNameSet& names, unsigned depth)
	{
		CheckDepth(depth);
		const bool text = value.Kind() == ibDataKind::String
			&& (value.AsString().Contains(ibT("\n")) || value.AsString().Contains(ibT("\r")));
		if (text || value.Kind() == ibDataKind::Binary) {
			const ibString extension = text ? ibT(".txt") : ibT(".bin");
			ibString file = named.IsEmpty() ? ibString() : named + extension;
			if (file.IsEmpty() || !names.insert(file).second) {
				file = placed + extension;
				if (!names.insert(file).second)
					Refuse(ibString::Format(_("two parts of '%s' are named '%s'"), folder, file));
			}
			Put(Join(folder, file), text ? Bytes(value.AsString()) : value.AsBinary());
			ibDataNode kept;
			kept.AddField(kNodeFile_, ibDataValue::String(file));
			return ibDataValue::Child(std::make_shared<ibDataNode>(kept));
		}
		if (value.Kind() == ibDataKind::Child)
			return ibDataValue::Child(std::make_shared<ibDataNode>(AsideNode(*value.AsChild(), named, placed, folder, names, depth)));
		if (value.Kind() == ibDataKind::Array) {
			std::vector<ibDataValue> items;
			for (const ibDataValue& item : value.AsArray()) {
				const ibString at = ibString::Format(ibT("%u"), static_cast<unsigned>(items.size()));
				items.push_back(Aside(item, named.IsEmpty() ? ibString() : named + ibT(".") + at, placed + ibT(".") + at,
					folder, names, depth + 1));
			}
			return ibDataValue::Array(std::move(items));
		}
		return value;
	}

	// …a sub-node of a value, the same way down: its fields by their names, its own children by their places.
	ibDataNode AsideNode(const ibDataNode& node, const ibString& named, const ibString& placed, const ibString& folder,
		ibNameSet& names, unsigned depth)
	{
		CheckDepth(depth);
		ibDataNode aside(node.GetClsid(), node.GetMetaId());
		if (node.HasRawData())
			aside.SetRawData(node.RawData());
		unsigned index = 0;
		for (const auto& [name, value] : node.Fields()) {
			aside.AddField(name, Aside(value, named.IsEmpty() || !IsFileName(name) ? ibString() : named + ibT(".") + name,
				placed + ibString::Format(ibT(".%u"), index), folder, names, depth + 1));
			++index;
		}
		unsigned position = 0;
		for (const ibDataNode& child : node.Children()) {
			const ibString at = ibString::Format(ibT("item-%u"), position++);
			aside.AddChild(AsideNode(child, named.IsEmpty() ? ibString() : named + ibT(".") + at, placed + ibT(".") + at,
				folder, names, depth + 1));
		}
		return aside;
	}

	// Two folders: the type, then the name it is called by — a part with no name of its own by its GUID, or by its place.
	ibString FolderOf(const ibDataNode& child, unsigned position) const
	{
		ibString type = m_resolveType ? m_resolveType(child.GetClsid()) : ibString();
		if (!IsFileName(type))
			type = ibString::Format(ibT("%llu"), static_cast<unsigned long long>(child.GetClsid()));
		for (const char* key : { "Name", "Guid" }) {
			const auto value = child.FindField(key);
			if (value && (value->Kind() == ibDataKind::String || value->Kind() == ibDataKind::Guid) && IsFileName(value->AsString()))
				return Join(type, value->AsString());
		}
		return Join(type, ibString::Format(ibT("item-%u"), position));
	}

	void Put(const ibString& path, const ibMemoryBuffer& bytes)
	{
		m_files.AddField(path, ibDataValue::Binary(bytes));
	}

	const ibJsonProvider& m_json;
	const std::function<ibString(ibClassID)>& m_resolveType;
};

// …AND READ BACK — from wherever the files are: the set, or a disk.
class ibReading {
public:
	ibReading(const ibJsonProvider& json, std::function<ibMemoryBuffer(const ibString&)> source)
		: m_json(json), m_source(std::move(source)) {}

	ibDataNode ReadNode(const ibString& folder, const ibString& file, unsigned depth)
	{
		CheckDepth(depth);
		const ibString path = Join(folder, file);
		const ibString text = Text(Take(path), path);
		ibDataNode shown;
		bool read = false;
		try {
			read = m_json.Read(text, shown);
		}
		catch (const ibCoreException& error) {
			Refuse(ibString::Format(ibT("'%s': %s"), path, error.GetErrorDescription()));
		}
		if (!read)
			Refuse(ibString::Format(_("'%s' is empty"), path));

		ibDataNode node(shown.GetClsid(), shown.GetMetaId());
		if (shown.HasRawData())
			node.SetRawData(shown.RawData());
		std::vector<ibString> folders;
		for (const auto& [name, value] : shown.Fields()) {
			if (name == kNodeFolders) {
				if (value.Kind() != ibDataKind::Array)
					Refuse(ibString::Format(_("'%s': %s must hold a list"), path, kNodeFolders));
				for (const ibDataValue& item : value.AsArray()) {
					if (item.Kind() != ibDataKind::String || !IsFolderName(item.AsString()))
						Refuse(ibString::Format(_("'%s': '%s' names no folder"), path, item.Kind() == ibDataKind::String ? item.AsString() : ibString()));
					folders.push_back(item.AsString());
				}
				continue;
			}
			node.AddField(name, Restored(value, folder, path, depth + 1));
		}
		for (const ibString& child : folders)
			node.AddChild(ReadNode(Join(folder, child), kNodeFile, depth + 1));
		return node;
	}

private:
	// A value with each file it names read back in its place — at any depth, as the layout set it aside (ibLayout::Aside).
	ibDataValue Restored(const ibDataValue& value, const ibString& folder, const ibString& path, unsigned depth)
	{
		CheckDepth(depth);
		if (const std::optional<ibString> kept = KeptFile(value)) {
			if (!IsLaidOutFile(*kept))
				Refuse(ibString::Format(_("'%s': '%s' names no file beside it"), path, *kept));
			const ibString keptPath = Join(folder, *kept);
			const ibMemoryBuffer bytes = Take(keptPath);
			return kept->EndsWith(ibT(".txt")) ? ibDataValue::String(Text(bytes, keptPath)) : ibDataValue::Binary(bytes);
		}
		if (value.Kind() == ibDataKind::Child)
			return ibDataValue::Child(std::make_shared<ibDataNode>(RestoredNode(*value.AsChild(), folder, path, depth)));
		if (value.Kind() == ibDataKind::Array) {
			std::vector<ibDataValue> items;
			for (const ibDataValue& item : value.AsArray())
				items.push_back(Restored(item, folder, path, depth + 1));
			return ibDataValue::Array(std::move(items));
		}
		return value;
	}

	// …a sub-node of a value, the same way down.
	ibDataNode RestoredNode(const ibDataNode& shown, const ibString& folder, const ibString& path, unsigned depth)
	{
		CheckDepth(depth);
		ibDataNode node(shown.GetClsid(), shown.GetMetaId());
		if (shown.HasRawData())
			node.SetRawData(shown.RawData());
		for (const auto& [name, value] : shown.Fields())
			node.AddField(name, Restored(value, folder, path, depth + 1));
		for (const ibDataNode& child : shown.Children())
			node.AddChild(RestoredNode(child, folder, path, depth + 1));
		return node;
	}

	// Each file is read once: a name given twice — a folder that names its own parent among them — is refused.
	ibMemoryBuffer Take(const ibString& path)
	{
		if (!m_taken.insert(path).second)
			Refuse(ibString::Format(_("'%s' is named more than once"), path));
		return m_source(path);
	}

	static std::optional<ibString> KeptFile(const ibDataValue& value)
	{
		// The kind asked first: AsChild() refuses a value of another kind, it does not answer none.
		if (value.Kind() != ibDataKind::Child)
			return std::nullopt;
		// The node held here: AsChild() hands out a copy, and a reference into it would outlive it.
		const std::optional<ibDataNode> kept = value.AsChild();
		const auto file = kept->FindField(kNodeFile_);
		if (!file || file->Kind() != ibDataKind::String || kept->Fields().size() != 1 || !kept->Children().empty())
			return std::nullopt;
		return file->AsString();
	}

	// The type's folder, then the name's — as FolderOf makes it.
	static bool IsFolderName(const ibString& name)
	{
		ibString called;
		const ibString type = name.BeforeFirst(ibT('/'), &called);
		return IsFileName(type) && IsFileName(called);
	}

	const ibJsonProvider& m_json;
	std::function<ibMemoryBuffer(const ibString&)> m_source;
	ibNameSet m_taken;
};

// What the directory is, said on its root and checked before anything in it is believed.
void CheckFormat(const ibDataNode& root)
{
	const auto format = root.FindField(kNodeFormat), version = root.FindField(kNodeVersion);
	if (!format || format->Kind() != ibDataKind::String || format->AsString() != kFormat
		|| !version || version->Kind() != ibDataKind::Number || version->AsInt() != kVersion)
		Refuse(_("the directory holds no configuration export of a version this build reads"));
}

// A type named in a file and known to nobody here — the files were edited, or made by another build. Read on, the node
// would come in with no type at all.
void CheckTypes(const ibDataNode& node)
{
	if (node.GetClsid() == ibClassID(0)) {
		const auto word = node.FindField("NodeType");
		if (word && word->Kind() == ibDataKind::String)
			Refuse(ibString::Format(_("no type is named '%s' here"), word->AsString()));
	}
	for (const ibDataNode& child : node.Children())
		CheckTypes(child);
}

} // namespace

bool ibFileProvider::Write(const ibDataNode& root, ibDataNode& files) const
{
	// A type said by name only where the name reads back as the same type; elsewhere by its number.
	ibJsonProvider json;
	if (m_resolveType && m_lookupType) {
		json.SetTypeResolver([this](ibClassID clsid) {
			const ibString name = m_resolveType(clsid);
			return !name.IsEmpty() && m_lookupType(name) == clsid ? name : ibString();
		});
	}
	ibLayout layout(json, m_resolveType);
	ibNameSet names{ kManifest };
	ibDataNode shown = layout.Shown(root, ibString(), names, 0);
	shown.AddField(kNodeFormat, ibDataValue::String(kFormat));
	shown.AddField(kNodeVersion, ibDataValue::Int(kVersion));
	layout.PutNode(kManifest, shown);
	files = std::move(layout.m_files);
	return true;
}

bool ibFileProvider::Read(const ibDataNode& files, ibDataNode& root) const
{
	std::map<ibString, ibMemoryBuffer, ibNoCaseLess> byName;
	for (const auto& [path, bytes] : files.Fields())
		byName.emplace(path, bytes.AsBinary());

	ibJsonProvider json;
	if (m_lookupType)
		json.SetTypeLookup(m_lookupType);
	ibReading reading(json, [&byName](const ibString& path) {
		const auto found = byName.find(path);
		if (found == byName.end())
			Refuse(ibString::Format(_("'%s' is missing"), path));
		return found->second;
	});
	ibDataNode read = reading.ReadNode(ibString(), kManifest, 0);
	CheckFormat(read);
	CheckTypes(read);
	read.RemoveField(kNodeFormat);
	read.RemoveField(kNodeVersion);
	root = std::move(read);
	return true;
}

bool ibFileProvider::Collect(const ibString& directory, ibDataNode& files)
{
	// The files the export names, followed as a reading follows them — and only those: whatever else the directory
	// holds (a repository's own files) is not the configuration's.
	const fs::path target = ibPathOf(directory);
	ibDataNode taken;
	ibJsonProvider json;
	ibReading reading(json, [&target, &taken](const ibString& path) {
		const ibMemoryBuffer bytes = ReadFile(Path(target, path), path);
		taken.AddField(path, ibDataValue::Binary(bytes));
		return bytes;
	});
	try {
		CheckFormat(reading.ReadNode(ibString(), kManifest, 0));
	}
	catch (const fs::filesystem_error& error) {
		Refuse(ibString::FromUTF8(error.what()));
	}
	files = std::move(taken);
	return true;
}

bool ibFileProvider::Publish(const ibDataNode& files, const ibString& directory)
{
	if (!files.Has(kManifest))
		Refuse(_("there is no configuration export to write"));

	const fs::path target = ibPathOf(directory);
	fs::path stage = target;
	stage += ibPathOf(ibString(ibT(".stage-")) + ibGuid(ibGuid::newGuid()).str());
	fs::path backup = stage;
	backup += ibPathOf(ibT(".previous"));
	try {
		if (target.filename().empty() || target.filename() == "." || target.filename() == ".."
			|| fs::is_symlink(fs::symlink_status(target)))
			Refuse(_("choose a directory of its own for the export"));
		if (fs::exists(target) && !fs::is_empty(target)) {
			// Never over somebody's repository, base or other files: what is there is an earlier export, every file
			// of it named by it.
			if (!fs::is_regular_file(target / ibPathOf(kManifest)))
				Refuse(_("the directory holds files and no export - choose a directory of its own"));
			ibDataNode previous;
			Collect(directory, previous);
			ibNameSet named;
			for (const auto& [path, bytes] : previous.Fields())
				named.insert(path);
			for (const auto& entry : fs::recursive_directory_iterator(target)) {
				const ibString name = PathText(entry.path().lexically_relative(target));
				bool known = false;
				if (fs::is_symlink(entry.symlink_status()))
					known = false;
				else if (entry.is_directory()) {
					const ibString prefix = name + ibT("/");
					const auto found = named.lower_bound(prefix);
					known = found != named.end() && found->Left(prefix.length()).CmpNoCase(prefix) == 0;
				}
				else
					known = named.count(name) != 0;
				if (!known)
					Refuse(ibString::Format(_("the directory holds '%s', which is no part of an export - choose a directory "
						"of its own"), name));
			}
		}
		fs::create_directories(stage);
		for (const auto& [path, bytes] : files.Fields())
			WriteFile(Path(stage, path), bytes.AsBinary(), path);
		if (fs::exists(target))
			fs::rename(target, backup);
		try {
			fs::rename(stage, target);
		}
		catch (const fs::filesystem_error&) {
			if (fs::exists(backup))
				fs::rename(backup, target);
			throw;
		}
		// The new export is in place. A backup that cannot be removed is left for the person to look at; it does not
		// turn the export into a failure.
		std::error_code error;
		fs::remove_all(backup, error);
		return true;
	}
	catch (const ibCoreException&) {
		std::error_code cleanup;
		fs::remove_all(stage, cleanup);
		throw;
	}
	catch (const std::exception& error) {
		std::error_code cleanup;
		fs::remove_all(stage, cleanup);
		Refuse(ibString::FromUTF8(error.what()));
	}
	return false;
}
