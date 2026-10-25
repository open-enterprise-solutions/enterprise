#include "core/serialize/fileProvider.h"
#include "core/exception.h"
#include "core/fileSystem.h"
#include "core/fiber/fiber.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iterator>
#include <set>
#include <limits>
#include <string_view>

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

void Refuse(const char* message)
{
	ibCoreException::Error(_("Configuration files: %s"), wxString::FromUTF8(message));
}

void CheckDepth(unsigned depth)
{
	if (depth > 128 || ibFiber::StackLow())
		Refuse("the structure is nested too deeply");
}

std::string Text(const ibString& text)
{
	const wxString value = text;
	const wxScopedCharBuffer utf8 = value.utf8_str();
	return std::string(utf8.data() ? utf8.data() : "", utf8.length());
}

ibString String(const Json& value)
{
	const std::string& text = value.get_ref<const std::string&>();
	const wxString decoded = wxString::FromUTF8(text.data(), text.size());
	if (!text.empty() && decoded.empty())
		Refuse("invalid UTF-8 text");
	return decoded;
}

// Only files under this export may be read or written. In particular, a repository
// supplied by somebody else cannot turn an asset reference into an arbitrary path.
fs::path Path(const fs::path& directory, const std::string& name)
{
	const fs::path relative = fs::u8path(name);
	if (relative.empty() || relative.is_absolute() || relative.has_root_name()
		|| name.find('\\') != std::string::npos || name.find(':') != std::string::npos
		|| name.find('\0') != std::string::npos)
		Refuse("invalid relative file name");
	fs::path path = directory;
	if (fs::is_symlink(fs::symlink_status(path)))
		Refuse("an export directory cannot be a symbolic link");
	for (const fs::path& component : relative) {
		if (component == ".." || component == "." || component.empty())
			Refuse("invalid relative file name");
		path /= component;
		if (fs::is_symlink(fs::symlink_status(path)))
			Refuse("a configuration file cannot be a symbolic link");
	}
	return path;
}

std::string ReadFile(const fs::path& path)
{
	if (!fs::is_regular_file(path))
		Refuse("a referenced file is missing or is not a regular file");
	std::ifstream input(path, std::ios::binary);
	if (!input)
		Refuse("a referenced file could not be opened");
	std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	if (input.bad())
		Refuse("a referenced file could not be read");
	return text;
}

void WriteFile(const fs::path& path, const char* data, size_t length)
{
	fs::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output)
		Refuse("a file could not be opened for writing");
	output.write(data, static_cast<std::streamsize>(length));
	output.close();
	if (!output)
		Refuse("a file could not be written");
}

Json Parse(const std::string& text)
{
	return Json::parse(text, [](int depth, Json::parse_event_t, Json&) {
		if (depth > 512 || ibFiber::StackLow())
			Refuse("the JSON is nested too deeply");
		return true;
	});
}

// Entry order, duplicate names and the distinction between fields and properties
// are data too. Arrays of [name, value] keep all three intact.
struct FileTree {
	fs::path m_directory;
	std::set<std::string> m_fileArray;

	void Put(const std::string& name, const char* data, size_t size)
	{
		if (!m_fileArray.insert(name).second)
			Refuse("two objects have the same file identity");
		WriteFile(Path(m_directory, name), data, size);
	}

	std::string Take(const std::string& name)
	{
		if (!m_fileArray.insert(name).second)
			Refuse("a file is referenced more than once");
		return ReadFile(Path(m_directory, name));
	}

	Json WriteValue(const ibDataValue& value, const std::string& stem, unsigned depth)
	{
		CheckDepth(depth);
		Json out = Json::object();
		switch (value.Kind()) {
		case ibDataKind::Empty: out["kind"] = "empty"; break;
		case ibDataKind::Bool: out["kind"] = "bool"; out["value"] = value.AsBool(); break;
		case ibDataKind::Number:
			out["kind"] = "number"; out["value"] = Text(value.AsNumber().ToString()); break;
		case ibDataKind::Date:
			out["kind"] = "date"; out["value"] = value.AsDate().GetValue(); break;
		case ibDataKind::String: {
			out["kind"] = "string";
			const std::string text = Text(value.AsString());
			if (text.find('\n') != std::string::npos || text.find('\r') != std::string::npos) {
				out["file"] = stem + ".txt";
				Put(stem + ".txt", text.data(), text.size());
			}
			else out["value"] = text;
			break;
		}
		case ibDataKind::Binary: {
			out["kind"] = "binary"; out["file"] = stem + ".bin";
			const ibMemoryBuffer& bytes = value.AsBinary();
			Put(stem + ".bin", static_cast<const char*>(bytes.GetData()), bytes.GetDataLen());
			break;
		}
		case ibDataKind::Child:
			out["kind"] = "child";
			out["value"] = value.AsChild() ? WriteNode(*value.AsChild(), stem, depth + 1) : Json();
			break;
		case ibDataKind::Array:
			out["kind"] = "array"; out["value"] = Json::array();
			for (const ibDataValue& item : value.AsArray())
				out["value"].push_back(WriteValue(item, stem + "-" + std::to_string(out["value"].size()), depth + 1));
			break;
		}
		return out;
	}

	Json WriteNode(const ibDataNode& node, const std::string& stem, unsigned depth)
	{
		CheckDepth(depth);
		Json out = {{"class", node.GetClsid()}, {"id", node.GetMetaId()},
			{"fields", Json::array()}, {"properties", Json::array()}, {"children", Json::array()}};
		for (const auto& entry : node.Fields())
			out["fields"].push_back(Json::array({Text(entry.first),
				WriteValue(entry.second, stem + "/field-" + std::to_string(out["fields"].size()), depth + 1)}));
		for (const auto& entry : node.Properties())
			out["properties"].push_back(Json::array({Text(entry.first),
				WriteValue(entry.second, stem + "/property-" + std::to_string(out["properties"].size()), depth + 1)}));
		if (node.HasRawData()) {
			out["raw"] = stem + "/raw.bin";
			Put(stem + "/raw.bin", static_cast<const char*>(node.RawData().GetData()), node.RawData().GetDataLen());
		}
		for (const ibDataNode& child : node.Children()) {
			// Metadata declares its persistent identity. Other structured children are
			// positional within their value, never named by a process-local node id.
			std::string key = "item-" + std::to_string(out["children"].size());
			if (const ibDataValue* guid = child.FindField("Guid")) {
				const std::string text = Text(guid->AsString());
				const ibGuid identity{std::string_view(text)};
				if (!identity.isValid())
					Refuse("a metadata object has no valid GUID");
				key = Text(identity.str());
			}
			const std::string childStem = stem + "/" + key;
			const std::string name = childStem + "/node.json";
			const std::string text = WriteNode(child, childStem, depth + 1).dump(2) + "\n";
			Put(name, text.data(), text.size());
			out["children"].push_back(name);
		}
		return out;
	}

	ibDataValue ReadValue(const Json& value, unsigned depth)
	{
		CheckDepth(depth);
		const std::string kind = value.at("kind").get<std::string>();
		if (kind == "empty") return ibDataValue();
		if (kind == "bool") return ibDataValue::Bool(value.at("value").get<bool>());
		if (kind == "number") {
			ibNumber number;
			if (!number.FromString(String(value.at("value"))))
				Refuse("invalid decimal number");
			return ibDataValue::Number(number);
		}
		if (kind == "date") {
			if (!value.at("value").is_number_integer()
				|| (value.at("value").is_number_unsigned() && value.at("value").get<std::uint64_t>() >
					static_cast<std::uint64_t>(std::numeric_limits<long long>::max())))
				Refuse("a date must hold a signed integer count");
			return ibDataValue::Date(ibDateTime(value.at("value").get<long long>()));
		}
		if (kind == "string") {
			if (value.contains("file"))
				return ibDataValue::String(String(Take(value.at("file").get<std::string>())));
			return ibDataValue::String(String(value.at("value")));
		}
		if (kind == "binary") {
			const std::string bytes = Take(value.at("file").get<std::string>());
			ibMemoryBuffer buffer;
			buffer.AppendData(bytes.data(), bytes.size());
			return ibDataValue::Binary(buffer);
		}
		if (kind == "child") {
			if (value.at("value").is_null()) return ibDataValue::Child(nullptr);
			auto node = std::make_shared<ibDataNode>();
			ReadNode(value.at("value"), *node, depth + 1);
			return ibDataValue::Child(node);
		}
		if (kind == "array") {
			if (!value.at("value").is_array()) Refuse("an array must hold a list");
			std::vector<ibDataValue> itemArray;
			for (const Json& item : value.at("value")) itemArray.push_back(ReadValue(item, depth + 1));
			return ibDataValue::Array(std::move(itemArray));
		}
		Refuse("unknown value kind");
		return ibDataValue();
	}

	void ReadNode(const Json& value, ibDataNode& node, unsigned depth)
	{
		CheckDepth(depth);
		if (!value.at("class").is_number_unsigned() || !value.at("id").is_number_integer())
			Refuse("invalid node identity");
		if (value.at("id").is_number_unsigned() && value.at("id").get<std::uint64_t>() >
			static_cast<std::uint64_t>(std::numeric_limits<ibMetaID>::max()))
			Refuse("metadata id is out of range");
		const long long id = value.at("id").get<long long>();
		if (id < std::numeric_limits<ibMetaID>::min() || id > std::numeric_limits<ibMetaID>::max())
			Refuse("metadata id is out of range");
		node.SetClsid(value.at("class").get<ibClassID>());
		node.SetMetaId(static_cast<ibMetaID>(id));
		for (const char* area : {"fields", "properties"}) {
			if (!value.at(area).is_array()) Refuse("an entry area must hold a list");
			for (const Json& entry : value.at(area)) {
				if (!entry.is_array() || entry.size() != 2) Refuse("invalid named entry");
				ibDataValue item = ReadValue(entry.at(1), depth + 1);
				if (std::string(area) == "fields") node.AddField(String(entry.at(0)), std::move(item));
				else node.SetProperty(String(entry.at(0)), std::move(item));
			}
		}
		if (value.contains("raw")) {
			const std::string bytes = Take(value.at("raw").get<std::string>());
			ibMemoryBuffer buffer;
			buffer.AppendData(bytes.data(), bytes.size());
			node.SetRawData(buffer);
		}
		if (!value.at("children").is_array()) Refuse("children must hold a list");
		for (const Json& child : value.at("children")) {
			ibDataNode& target = node.AddChild(0, 0);
			ReadNode(Parse(Take(child.get<std::string>())), target, depth + 1);
		}
	}
};

} // namespace

bool ibFileProvider::Write(const ibDataNode& root, ibWriterMemory& writer) const
{
	try {
		FileTree tree{ibPathOf(m_directory), {}};
		Json manifest = {{"format", "OES-Configuration"}, {"version", 1},
			{"root", tree.WriteNode(root, "objects", 0)}};
		const std::string text = manifest.dump(2) + "\n";
		writer.w(text.data(), static_cast<std::uint32_t>(text.size()));
		return true;
	}
	catch (const Json::exception& error) { Refuse(error.what()); }
	catch (const fs::filesystem_error& error) { Refuse(error.what()); }
	return false;
}

bool ibFileProvider::Read(ibReaderMemory& reader, ibDataNode& root) const
{
	try {
		const std::string text(static_cast<const char*>(reader.pointer()), reader.elapsed());
		const Json manifest = Parse(text);
		if (manifest.at("format") != "OES-Configuration" || manifest.at("version") != 1)
			Refuse("unsupported format or version");
		FileTree tree{ibPathOf(m_directory), {"configuration.json"}};
		ibDataNode fresh;
		tree.ReadNode(manifest.at("root"), fresh, 0);
		root = std::move(fresh);
		reader.advance(reader.elapsed());
		return true;
	}
	catch (const Json::exception& error) { Refuse(error.what()); }
	catch (const fs::filesystem_error& error) { Refuse(error.what()); }
	return false;
}

bool ibFileProvider::Write(const ibDataNode& root) const
{
	const fs::path target = ibPathOf(m_directory);
	fs::path stage = target;
	stage += fs::u8path(".stage-" + Text(ibGuid(ibGuid::newGuid()).str()));
	fs::path backup = stage;
	backup += ".previous";
	try {
		if (target.filename().empty() || target.filename() == "." || target.filename() == ".."
			|| fs::is_symlink(fs::symlink_status(target)))
			Refuse("choose a dedicated export directory");
		if (fs::exists(target) && !fs::is_empty(target)) {
			// Never replace somebody's source repository, database, or unrelated files.
			const Json manifest = Parse(ReadFile(Path(target, "configuration.json")));
			if (manifest.at("format") != "OES-Configuration" || manifest.at("version") != 1)
				Refuse("unsupported format or version");
			FileTree previousTree{target, {"configuration.json"}};
			ibDataNode previous;
			previousTree.ReadNode(manifest.at("root"), previous, 0);
			for (const auto& entry : fs::recursive_directory_iterator(target)) {
				const std::string name = entry.path().lexically_relative(target).generic_string();
				if (fs::is_symlink(entry.symlink_status()))
					Refuse("the export directory contains unrelated files");
				if (entry.is_directory()) {
					const std::string prefix = name + "/";
					const auto found = previousTree.m_fileArray.lower_bound(prefix);
					if (found == previousTree.m_fileArray.end() || found->compare(0, prefix.size(), prefix) != 0)
						Refuse("the export directory contains unrelated files");
				}
				else if (previousTree.m_fileArray.count(name) == 0)
					Refuse("the export directory contains unrelated files");
			}
		}
		fs::create_directories(stage);
		ibWriterMemory writer;
		ibFileProvider provider(wxString::FromUTF8(stage.u8string()));
		provider.Write(root, writer);
		WriteFile(stage / "configuration.json", reinterpret_cast<const char*>(writer.pointer()), writer.size());
		if (fs::exists(target)) fs::rename(target, backup);
		try { fs::rename(stage, target); }
		catch (const fs::filesystem_error&) {
			if (fs::exists(backup)) fs::rename(backup, target);
			throw;
		}
		// The new manifest is published. Cleanup must not turn a successful export
		// into a failure; an undeletable backup is left for the person to inspect.
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
		Refuse(error.what());
	}
	return false;
}

bool ibFileProvider::Read(ibDataNode& root) const
{
	try {
		const std::string text = ReadFile(Path(ibPathOf(m_directory), "configuration.json"));
		ibMemoryBuffer bytes;
		bytes.AppendData(text.data(), text.size());
		ibReaderMemory reader(bytes);
		return Read(reader, root);
	}
	catch (const fs::filesystem_error& error) { Refuse(error.what()); }
	return false;
}
