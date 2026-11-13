#include "core/fileConfig.h"

#include "core/fileSystem.h"   // ibPathOf, ibRenameFile, ibRemoveFile

#include <cstdlib>    // getenv
#include <fstream>
#include <iterator>
#include <string>

namespace {

#ifdef __IBMSW__
constexpr const char* kLineEnd = "\r\n";   // the system's own, as wxTextFile wrote it
constexpr bool kPercentVariables = true;    // %NAME% - Windows' own spelling
#else
constexpr const char* kLineEnd = "\n";
constexpr bool kPercentVariables = false;
#endif

// What a line of the file is: a group's, an entry's, or anything else (blank, a comment, a line without `=`).
enum class LineKind { Other, Group, Entry };

struct ParsedLine {
	LineKind kind = LineKind::Other;
	ibString name;    // the group's, or the entry's
	ibString value;   // the entry's, as written
};

bool IsBlank(ibChar c)
{
	return c == ibT(' ') || c == ibT('\t');
}

ParsedLine Parse(const ibString& text)
{
	ParsedLine line;
	size_t at = 0;
	while (at < text.Len() && IsBlank(text[at]))
		++at;
	if (at == text.Len() || text[at] == ibT(';') || text[at] == ibT('#'))
		return line;

	if (text[at] == ibT('[')) {
		const size_t close = text.find(ibT(']'), at + 1);
		if (close == ibString::npos)
			return line;
		line.kind = LineKind::Group;
		line.name = text.Mid(at + 1, close - at - 1);
		line.name.Trim().Trim(false);
		return line;
	}

	const size_t equals = text.find(ibT('='), at);
	if (equals == ibString::npos)
		return line;
	line.kind = LineKind::Entry;
	line.name = text.Mid(at, equals - at);
	line.name.Trim();
	size_t from = equals + 1;
	while (from < text.Len() && IsBlank(text[from]))
		++from;
	line.value = text.Mid(from);
	return line;
}

// A key's group and name: "trade/Path" and "/trade/Path" are Path in [trade], "Port" and "/Port" Port at the top.
void Split(const ibString& key, ibString& group, ibString& name)
{
	const ibString path = key.StartsWith(ibT("/")) ? key.Mid(1) : key;
	const size_t slash = path.rfind(ibT('/'));
	if (slash == ibString::npos) {
		group.clear();
		name = path;
	}
	else {
		group = path.Left(slash);
		name = path.Mid(slash + 1);
	}
}

bool SameName(const ibString& a, const ibString& b)
{
	return a.CmpNoCase(b) == 0;
}

// The environment's value of `name`; false when it is not set.
bool EnvironmentValue(const ibString& name, ibString& value)
{
#ifdef __IBMSW__
	const wchar_t* const set = _wgetenv(name.wc_str());
	if (set == nullptr)
		return false;
	value = ibString(set);
#else
	const char* const set = std::getenv(name.ToUtf8().c_str());
	if (set == nullptr)
		return false;
	value = ibString::FromUTF8(set);
#endif
	return true;
}

bool IsNameChar(ibChar c)
{
	return (c >= ibT('a') && c <= ibT('z')) || (c >= ibT('A') && c <= ibT('Z')) || (c >= ibT('0') && c <= ibT('9'))
		|| c == ibT('_');
}

// The value with its environment variables in their places - wxExpandEnvVars, for the spellings it knew.
ibString ExpandEnvVars(const ibString& text)
{
	ibString result;
	const size_t length = text.Len();
	for (size_t n = 0; n < length; ++n) {
		const ibChar c = text[n];
		if (c == ibT('\\') && n + 1 < length && (text[n + 1] == ibT('$') || (kPercentVariables && text[n + 1] == ibT('%')))) {
			result += text[++n];
			continue;
		}
		if (c != ibT('$') && !(kPercentVariables && c == ibT('%'))) {
			result += c;
			continue;
		}

		ibChar close = 0;
		size_t start = n + 1;
		if (c == ibT('%'))
			close = ibT('%');
		else if (start < length && text[start] == ibT('{')) {
			close = ibT('}');
			++start;
		}
		else if (start < length && text[start] == ibT('(')) {
			close = ibT(')');
			++start;
		}
		size_t end = start;
		while (end < length && IsNameChar(text[end]))
			++end;

		ibString value;
		const bool closed = close == 0 || (end < length && text[end] == close);
		if (end > start && closed && EnvironmentValue(text.Mid(start, end - start), value)) {
			result += value;
			n = close != 0 ? end : end - 1;
		}
		else {
			result += c;   // not a variable, or one that is not set: as written, the rest of it copied as it comes
		}
	}
	return result;
}

// The groups at the top of the file, in its order, each once: [a/b] is a group of a.
std::vector<ibString> TopGroups(const std::vector<ibString>& lines)
{
	std::vector<ibString> groups;
	for (const ibString& text : lines) {
		const ParsedLine line = Parse(text);
		if (line.kind != LineKind::Group)
			continue;
		const ibString top = (line.name.StartsWith(ibT("/")) ? line.name.Mid(1) : line.name).BeforeFirst(ibT('/'));
		if (top.IsEmpty())
			continue;
		bool known = false;
		for (const ibString& group : groups)
			known = known || SameName(group, top);
		if (!known)
			groups.push_back(top);
	}
	return groups;
}

} // namespace

ibFileConfig::ibFileConfig(const ibString& path)
	: m_path(path)
{
	std::ifstream in(ibPathOf(path), std::ios::binary);
	if (!in)
		return;
	const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

	// UTF-8, its mark left out - what wx's converter read a .conf as.
	const size_t skip = bytes.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
	const ibString text = ibString::FromUTF8(bytes.data() + skip, bytes.size() - skip);

	size_t from = 0;
	while (from < text.Len()) {
		size_t end = text.find(ibT('\n'), from);
		if (end == ibString::npos)
			end = text.Len();
		ibString line = text.Mid(from, end - from);
		if (!line.IsEmpty() && line.Last() == ibT('\r'))
			line.RemoveLast();
		m_lines.push_back(line);
		from = end + 1;
	}
}

long ibFileConfig::FindEntry(const ibString& group, const ibString& name) const
{
	// The LAST line that says it, as wx read a key said twice.
	long found = -1;
	ibString current;   // the top of the file, until a group's line
	for (size_t i = 0; i < m_lines.size(); ++i) {
		const ParsedLine line = Parse(m_lines[i]);
		if (line.kind == LineKind::Group)
			current = line.name.StartsWith(ibT("/")) ? line.name.Mid(1) : line.name;
		else if (line.kind == LineKind::Entry && SameName(current, group) && SameName(line.name, name))
			found = static_cast<long>(i);
	}
	return found;
}

bool ibFileConfig::Read(const ibString& key, ibString* value) const
{
	ibString group, name;
	Split(key, group, name);
	const long at = FindEntry(group, name);
	if (at < 0)
		return false;
	*value = ExpandEnvVars(Parse(m_lines[at]).value);
	return true;
}

ibString ibFileConfig::Read(const ibString& key, const ibString& byDefault) const
{
	ibString value;
	return Read(key, &value) ? value : byDefault;
}

bool ibFileConfig::Read(const ibString& key, long* value) const
{
	ibString text;
	long number = 0;
	if (!Read(key, &text) || !text.ToLong(&number))
		return false;
	*value = number;
	return true;
}

bool ibFileConfig::Read(const ibString& key, long* value, long byDefault) const
{
	if (Read(key, value))
		return true;
	*value = byDefault;
	return false;
}

bool ibFileConfig::Write(const ibString& key, const ibString& value)
{
	ibString group, name;
	Split(key, group, name);
	if (name.IsEmpty())
		return false;

	const ibString text = name + ibT("=") + value;
	const long at = FindEntry(group, name);
	if (at >= 0) {
		if (m_lines[at] != text) {
			m_lines[at] = text;
			m_dirty = true;
		}
		return true;
	}

	// A new entry goes after the last of its group's, else right after the group's line; the top's go before the
	// first group. A group the file has not got goes to its end.
	long after = -1;   // the line it goes after; -1 - before the first
	bool hasGroup = group.IsEmpty();
	ibString current;
	for (size_t i = 0; i < m_lines.size(); ++i) {
		const ParsedLine line = Parse(m_lines[i]);
		if (line.kind == LineKind::Group) {
			current = line.name.StartsWith(ibT("/")) ? line.name.Mid(1) : line.name;
			if (SameName(current, group)) {
				hasGroup = true;
				after = static_cast<long>(i);
			}
		}
		else if (line.kind == LineKind::Entry && SameName(current, group))
			after = static_cast<long>(i);
	}
	if (!hasGroup) {
		m_lines.push_back(ibT("[") + group + ibT("]"));
		after = static_cast<long>(m_lines.size()) - 1;
	}
	m_lines.insert(m_lines.begin() + (after + 1), text);
	m_dirty = true;
	return true;
}

bool ibFileConfig::Write(const ibString& key, long value)
{
	return Write(key, ibString::Format(ibT("%ld"), value));
}

bool ibFileConfig::Flush()
{
	if (!m_dirty)
		return true;

	// Into a file beside it, then over it: a write cut short leaves the old file whole - wxTempFile's road.
	const ibString written = m_path + ibT(".tmp");
	{
		std::ofstream out(ibPathOf(written), std::ios::binary | std::ios::trunc);
		for (const ibString& line : m_lines)
			out << line.ToUtf8() << kLineEnd;
		out.close();
		if (!out) {
			ibRemoveFile(written);
			return false;
		}
	}
	if (!ibRenameFile(written, m_path)) {
		ibRemoveFile(written);
		return false;
	}
	m_dirty = false;
	return true;
}

bool ibFileConfig::GetFirstGroup(ibString& group, long& cookie) const
{
	cookie = 0;
	return GetNextGroup(group, cookie);
}

bool ibFileConfig::GetNextGroup(ibString& group, long& cookie) const
{
	const std::vector<ibString> groups = TopGroups(m_lines);
	if (cookie < 0 || static_cast<size_t>(cookie) >= groups.size())
		return false;
	group = groups[static_cast<size_t>(cookie++)];
	return true;
}
