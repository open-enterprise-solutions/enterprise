#include "core/xml/xmlText.h"
#include "core/exception.h"                 // ibCoreException - the parser's refusals
#include "core/localization/dictionary.h"   // _() - the refusals' words

#include <expat.h>

#include <algorithm>   // std::find_if, std::remove_if, std::reverse
#include <cstring>     // std::strchr

namespace {

// The parser hands names and text over as UTF-8 bytes, and every conversion below reads them so. An expat built
// with XML_UNICODE (wide XML_Char) would hand over something else entirely.
static_assert(sizeof(XML_Char) == 1, "the XML parser reads expat's strings as UTF-8: an XML_UNICODE build is not supported");

// What stands between the URI, the local name and the prefix of a name the parser hands over: not a character a
// name or a URI can hold.
constexpr XML_Char kNsSeparator = '\x01';

const char* const kXmlnsUri = "http://www.w3.org/2000/xmlns/";
// Bound to the prefix `xml` by definition, in every document, declared or not.
const char* const kXmlUri = "http://www.w3.org/XML/1998/namespace";

ibString FromUtf8(const XML_Char* text)
{
	return text != nullptr ? ibString::FromUTF8(text) : ibString();
}

// `uri \1 local \1 prefix`, `uri \1 local` (the default namespace) or `local` (none).
void SplitName(const XML_Char* raw, ibString& uri, ibString& local, ibString& prefix, ibString& qualified)
{
	const XML_Char* first = std::strchr(raw, kNsSeparator);
	if (first == nullptr) {
		local = ibString::FromUTF8(raw);
	}
	else {
		uri = ibString::FromUTF8(raw, static_cast<size_t>(first - raw));
		const XML_Char* second = std::strchr(first + 1, kNsSeparator);
		if (second == nullptr) {
			local = ibString::FromUTF8(first + 1);
		}
		else {
			local = ibString::FromUTF8(first + 1, static_cast<size_t>(second - first - 1));
			prefix = ibString::FromUTF8(second + 1);
		}
	}
	qualified = prefix.IsEmpty() ? local : prefix + ibString(ibT(":")) + local;
}

bool IsXmlSpace(const std::string& text)
{
	for (const char c : text)
		if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
			return false;
	return true;
}

// windows-1251 and cp866, byte -> code point: Python's own codecs, ord(bytes([b]).decode('cp1251')) (and cp866)
// for every byte, a byte the page defines no character for mapped to 0xFFFF.
const char16_t k1251[256] = {
	0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
	0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017, 0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
	0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
	0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
	0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
	0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
	0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
	0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
	0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFF, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
	0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
	0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

const char16_t k866[256] = {
	0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
	0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017, 0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
	0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
	0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
	0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
	0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
	0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
	0x0401, 0x0451, 0x0404, 0x0454, 0x0407, 0x0457, 0x040E, 0x045E, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x2116, 0x00A4, 0x25A0, 0x00A0,
};


// The code point at `at` and how many units it takes. A lone surrogate is itself - which no check below lets by.
char32_t CodePointAt(const ibChar* text, size_t length, size_t at, size_t& units)
{
	const char32_t first = text[at];
	if (first >= 0xD800 && first <= 0xDBFF && at + 1 < length) {
		const char32_t second = text[at + 1];
		if (second >= 0xDC00 && second <= 0xDFFF) {
			units = 2;
			return 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
		}
	}
	units = 1;
	return first;
}

// XML 1.0's Char production: what a document may hold at all, escaped or not.
bool IsXmlChar(char32_t c)
{
	return c == 0x9 || c == 0xA || c == 0xD || (c >= 0x20 && c <= 0xD7FF) || (c >= 0xE000 && c <= 0xFFFD)
		|| (c >= 0x10000 && c <= 0x10FFFF);
}

void AppendReference(ibString& text, char32_t c)
{
	text += ibString::Format(ibT("&#%u;"), static_cast<unsigned int>(c));
}

} // namespace

//////////////////////////////////////////////////////////////////////
// The code pages
//////////////////////////////////////////////////////////////////////

ibXmlCodePage ibXmlCodePage::Find(const ibString& name)
{
	const ibString key = name.Lower();

	ibXmlCodePage page;
	if (key == ibT("utf-8") || key == ibT("utf8")) {
		page.m_kind = kUtf8;
	}
	else if (key == ibT("utf-16") || key == ibT("utf16")) {
		page.m_kind = kUtf16;
	}
	else if (key == ibT("windows-1251") || key == ibT("cp1251")) {
		page.m_kind = kWindows1251;
		page.m_table = k1251;
	}
	else if (key == ibT("cp866") || key == ibT("ibm866")) {
		page.m_kind = kCp866;
		page.m_table = k866;
	}
	return page;   // otherwise kUnknown: a page there is no table for
}

ibString ibXmlCodePage::Supported()
{
	return ibT("UTF-8, UTF-16, windows-1251, cp866");
}

ibString ibXmlCodePage::Name() const
{
	switch (m_kind) {
	case kUtf8:         return ibT("UTF-8");
	case kUtf16:        return ibT("UTF-16");
	case kWindows1251:  return ibT("windows-1251");
	case kCp866:        return ibT("cp866");
	case kUnknown:
	default:            return ibString();
	}
}

char16_t ibXmlCodePage::ToChar(unsigned char byte) const
{
	if (m_table != nullptr)
		return m_table[byte];
	return byte < 0x80 ? static_cast<char16_t>(byte) : char16_t(0xFFFF);   // a UTF kind: only the ASCII half is one byte
}

bool ibXmlCodePage::ToByte(char32_t ch, unsigned char& byte) const
{
	if (ch < 0x80) {
		byte = static_cast<unsigned char>(ch);
		return true;
	}
	// 0xFFFF is the table's own "no character here" - never a character a byte is asked for.
	if (m_table == nullptr || ch == 0xFFFF)
		return false;
	for (int i = 0x80; i <= 0xFF; ++i) {
		if (m_table[i] == ch) {
			byte = static_cast<unsigned char>(i);
			return true;
		}
	}
	return false;
}

bool ibXmlCodePage::Holds(char32_t ch) const
{
	if (m_table == nullptr)
		return m_kind != kUnknown;
	unsigned char byte = 0;
	return ToByte(ch, byte);
}

void ibXmlCodePage::Encode(const ibString& text, const ibString& who, std::string& bytes) const
{
	const ibChar* chars = text.c_str();
	const size_t length = text.Len();
	switch (m_kind) {
	case kUtf8:
		bytes += text.ToUtf8();
		return;
	case kUtf16:
		for (size_t i = 0; i < length; i++) {
			bytes += static_cast<char>(chars[i] & 0xFF);
			bytes += static_cast<char>((chars[i] >> 8) & 0xFF);
		}
		return;
	case kWindows1251:
	case kCp866:
		for (size_t i = 0; i < length; ) {
			size_t units = 0;
			const char32_t c = CodePointAt(chars, length, i, units);
			unsigned char byte = 0;
			if (!ToByte(c, byte))
				ibCoreException::Error(_("%s: the character U+%04X has no byte in %s"), who, static_cast<unsigned int>(c), Name());
			bytes += static_cast<char>(byte);
			i += units;
		}
		return;
	case kUnknown:
	default:
		ibCoreException::Error(_("%s: no encoding to write in"), who);
	}
}

//////////////////////////////////////////////////////////////////////
// The namespaces in scope
//////////////////////////////////////////////////////////////////////

// A chain as deep as the document is nested would otherwise be destroyed link inside link, a stack frame each.
ibXmlParser::ibScope::~ibScope()
{
	std::shared_ptr<ibScope> outer = std::move(m_outer);
	while (outer != nullptr && outer.use_count() == 1)
		outer = std::move(outer->m_outer);   // the link let go of here has no outer of its own left
}

// A prefix declared again keeps its first place and takes the innermost URI; `xmlns=""` is no default namespace at
// all, not one with an empty name.
std::shared_ptr<const ibXmlParser::ibNamespaceArray> ibXmlParser::Resolve(
	const std::vector<std::pair<ibString, ibString>>& declared)
{
	auto resolved = std::make_shared<ibNamespaceArray>();
	resolved->emplace_back(ibString(ibT("xml")), ibString(kXmlUri));
	for (const std::pair<ibString, ibString>& declaration : declared) {
		auto bound = std::find_if(resolved->begin(), resolved->end(),
			[&](const std::pair<ibString, ibString>& pair) { return pair.first == declaration.first; });
		if (bound != resolved->end())
			bound->second = declaration.second;
		else
			resolved->push_back(declaration);
	}
	resolved->erase(std::remove_if(resolved->begin(), resolved->end(),
		[](const std::pair<ibString, ibString>& pair) { return pair.second.IsEmpty(); }), resolved->end());
	return resolved;
}

std::shared_ptr<const ibXmlParser::ibNamespaceArray> ibXmlParser::Resolve(const ibScope* innermost)
{
	std::vector<std::pair<ibString, ibString>> declared;
	for (const ibScope* link = innermost; link != nullptr; link = link->m_outer.get())
		declared.push_back(link->m_declaration);
	std::reverse(declared.begin(), declared.end());
	return Resolve(declared);
}

//////////////////////////////////////////////////////////////////////
// The parser
//////////////////////////////////////////////////////////////////////

// Nothing is thrown through expat: it is C, and an exception crossing its frames is a stack it never unwinds. A
// handler that fails writes down why and stops the parser; Settle raises once expat has returned.
class ibXmlParser::ibExpat {
public:
	ibExpat(ibXmlParser& owner, const ibString& who, ibInput input) : m_owner(owner), m_who(who)
	{
		// From a string the text is characters already, handed over as UTF-8: saying so here is what makes expat
		// ignore an `encoding=` in the declaration, which would read those bytes a second time.
		m_parser = XML_ParserCreateNS(input == kCharacters ? "UTF-8" : nullptr, kNsSeparator);
		if (m_parser == nullptr)
			ibCoreException::Error(_("%s: no memory for the parser"), m_who);

		XML_Parser p = m_parser;
		XML_SetUserData(p, this);
		XML_SetReturnNSTriplet(p, 1);
		XML_SetElementHandler(p, OnStart, OnEnd);
		XML_SetCharacterDataHandler(p, OnText);
		XML_SetCommentHandler(p, OnComment);
		XML_SetProcessingInstructionHandler(p, OnPi);
		XML_SetCdataSectionHandler(p, OnCdataStart, OnCdataEnd);
		XML_SetXmlDeclHandler(p, OnXmlDecl);
		XML_SetNamespaceDeclHandler(p, OnNsStart, OnNsEnd);
		XML_SetDoctypeDeclHandler(p, OnDoctypeStart, OnDoctypeEnd);
		XML_SetUnknownEncodingHandler(p, OnUnknownEncoding, this);

		// No external DTD subset, no parameter entity - and a general entity declared SYSTEM is a refusal where it
		// is used: left without a handler expat would skip it quietly.
		XML_SetParamEntityParsing(p, XML_PARAM_ENTITY_PARSING_NEVER);
		XML_SetExternalEntityRefHandler(p, OnExternalEntity);
		XML_SetSkippedEntityHandler(p, OnSkippedEntity);
		// ...and a document that DEPENDS on declarations outside itself is refused as a whole: expat would drop an
		// entity it cannot resolve in an ATTRIBUTE without calling any handler - `v="1&e;2"` read as "12".
		XML_SetNotStandaloneHandler(p, OnNotStandalone);
	}

	~ibExpat() { XML_ParserFree(m_parser); }

	ibExpat(const ibExpat&) = delete;
	ibExpat& operator=(const ibExpat&) = delete;

	void Feed(const char* data, size_t size, bool last, const ibString& source)
	{
		// expat takes an int; a caller feeds kChunk at a time.
		if (size > kChunk)
			ibCoreException::Error(_("%s: a block of %zu bytes was fed at once"), m_who, size);
		Settle(XML_Parse(m_parser, data, static_cast<int>(size), last ? XML_TRUE : XML_FALSE), source);
	}

	void Resume(const ibString& source) { Settle(XML_ResumeParser(m_parser), source); }

	enum XML_Parsing Parsing() const
	{
		XML_ParsingStatus status;
		XML_GetParsingStatus(m_parser, &status);
		return status.parsing;
	}
	bool Paused() const { return Parsing() == XML_SUSPENDED; }
	bool Finished() const { return Parsing() == XML_FINISHED; }

private:
	typedef ibXmlParser::ibNode ibNode;
	typedef ibXmlParser::ibAttribute ibAttribute;

	void Settle(enum XML_Status status, const ibString& source)
	{
		if (status == XML_STATUS_ERROR) {
			const ibString what = !m_refusal.IsEmpty() ? m_refusal : FromUtf8(XML_ErrorString(XML_GetErrorCode(m_parser)));
			const unsigned long long line = static_cast<unsigned long long>(XML_GetCurrentLineNumber64(m_parser));
			const unsigned long long column = static_cast<unsigned long long>(XML_GetCurrentColumnNumber64(m_parser)) + 1;   // expat counts from 0
			if (source.IsEmpty())
				ibCoreException::Error(_("%s: %s at line %llu, column %llu"), m_who, what, line, column);
			ibCoreException::Error(_("%s: %s at line %llu, column %llu of '%s'"), m_who, what, line, column, source);
		}
		// Paused, the text so far may go on after the pause: it is held, like at the edge of a chunk.
		if (Finished())
			FlushText();
	}

	// Every handler's body runs in here: whatever it raises becomes the parser's refusal.
	template <typename F>
	static void Guard(void* self, F&& body)
	{
		ibExpat& parser = *static_cast<ibExpat*>(self);
		if (parser.m_stopped)
			return;   // expat may still call a handler or two after it was told to stop
		try {
			body(parser);
		}
		catch (const ibCoreException& error) {
			parser.Stop(error.GetErrorDescription());
		}
		catch (const std::exception& error) {
			parser.Stop(ibString::FromUTF8(error.what()));
		}
		catch (...) {
			parser.Stop(_("the parser failed"));
		}
		// Enough is waiting: pause - resumably, and not a failure. Asked a second time while paused, expat would
		// write an error code of its own; hence the status check.
		if (!parser.m_stopped && parser.m_owner.m_queue.size() >= kQueuePause && parser.Parsing() == XML_PARSING)
			XML_StopParser(parser.m_parser, XML_TRUE);
	}

	void Stop(const ibString& why)
	{
		if (m_refusal.IsEmpty())
			m_refusal = why;
		m_stopped = true;
		XML_StopParser(m_parser, XML_FALSE);
	}

	ibNode Begin(ibXmlNodeType type)
	{
		ibNode node;
		node.m_type = type;
		node.m_depth = m_depth;
		node.m_line = static_cast<unsigned long long>(XML_GetCurrentLineNumber64(m_parser));
		node.m_column = static_cast<unsigned long long>(XML_GetCurrentColumnNumber64(m_parser)) + 1;
		node.m_namespaces = m_scope;   // a text begun before the next element's declarations keeps the scope it began in
		return node;
	}

	// The text gathered so far becomes its node: Whitespace when it is nothing else, Text otherwise.
	void FlushText()
	{
		if (!m_pendingOpen)
			return;
		m_pendingNode.m_type = IsXmlSpace(m_pending) ? ibXmlNodeType_Whitespace : ibXmlNodeType_Text;
		m_pendingNode.m_value = ibString::FromUTF8(m_pending.data(), m_pending.size());
		m_owner.m_queue.push_back(std::move(m_pendingNode));
		ForgetPending();
	}

	void ForgetPending()
	{
		m_pendingNode = ibNode();
		if (m_pending.capacity() > kChunk)
			std::string().swap(m_pending);   // a huge text is not kept in memory after it is handed over
		else
			m_pending.clear();
		m_pendingOpen = false;
	}

	static void XMLCALL OnStart(void* self, const XML_Char* name, const XML_Char** atts)
	{
		Guard(self, [&](ibExpat& parser) {
			parser.FlushText();
			ibNode node = parser.Begin(ibXmlNodeType_StartElement);
			SplitName(name, node.m_uri, node.m_local, node.m_prefix, node.m_name);

			// The declarations first: in namespace mode expat takes them out of `atts`, before this handler - their
			// place among the other attributes is not said. They are the innermost links of the scope, the last
			// declared first: taken so, and turned round.
			const ibScope* link = parser.m_scope.get();
			for (size_t i = parser.m_declaring; i > 0 && link != nullptr; --i, link = link->m_outer.get()) {
				const std::pair<ibString, ibString>& declared = link->m_declaration;
				ibAttribute attribute;
				attribute.m_uri = ibString(kXmlnsUri);
				if (declared.first.IsEmpty()) {
					attribute.m_local = attribute.m_name = ibT("xmlns");
				}
				else {
					attribute.m_prefix = ibT("xmlns");
					attribute.m_local = declared.first;
					attribute.m_name = ibString(ibT("xmlns:")) + declared.first;
				}
				attribute.m_value = declared.second;
				node.m_attributeArray.push_back(std::move(attribute));
			}
			std::reverse(node.m_attributeArray.begin(), node.m_attributeArray.end());
			parser.m_declaring = 0;

			for (const XML_Char** at = atts; at[0] != nullptr; at += 2) {
				ibAttribute attribute;
				SplitName(at[0], attribute.m_uri, attribute.m_local, attribute.m_prefix, attribute.m_name);
				attribute.m_value = FromUtf8(at[1]);
				node.m_attributeArray.push_back(std::move(attribute));
			}

			parser.m_startLine = node.m_line;
			parser.m_startColumn = node.m_column;
			parser.m_owner.m_queue.push_back(std::move(node));
			++parser.m_depth;
		});
	}

	static void XMLCALL OnEnd(void* self, const XML_Char* name)
	{
		Guard(self, [&](ibExpat& parser) {
			parser.FlushText();
			--parser.m_depth;
			ibNode node = parser.Begin(ibXmlNodeType_EndElement);
			SplitName(name, node.m_uri, node.m_local, node.m_prefix, node.m_name);

			// `<a/>` and `<a></a>` both come as start-then-end; only the first has no bytes of its own for the end.
			// ⚠ Inside an entity's replacement text expat reports every event at the entity REFERENCE, so there
			// the two cannot be told apart, and an empty element there is not marked.
			// The start node is still in the queue: expat calls both handlers of `<a/>` back to back, a pause
			// between them included, and the queue is read only after the parser returns. Inside an entity's
			// replacement text every event has no bytes of its own - so the end is `<a/>`'s only when the node right
			// before it is its own start.
			std::deque<ibNode>& queue = parser.m_owner.m_queue;
			if (XML_GetCurrentByteCount64(parser.m_parser) == 0 && !queue.empty()
				&& queue.back().m_type == ibXmlNodeType_StartElement && queue.back().m_depth == parser.m_depth) {
				node.m_emptyElement = true;
				node.m_line = parser.m_startLine;       // expat stands AFTER the tag here; the pair is one tag
				node.m_column = parser.m_startColumn;
				queue.back().m_emptyElement = true;
				node.m_attributeArray = queue.back().m_attributeArray;
			}
			parser.m_owner.m_queue.push_back(std::move(node));
		});
	}

	static void XMLCALL OnText(void* self, const XML_Char* text, int length)
	{
		Guard(self, [&](ibExpat& parser) {
			if (!parser.m_pendingOpen) {
				parser.m_pendingNode = parser.Begin(ibXmlNodeType_Text);
				parser.m_pendingOpen = true;
			}
			parser.m_pending.append(text, static_cast<size_t>(length));
		});
	}

	static void XMLCALL OnCdataStart(void* self)
	{
		Guard(self, [&](ibExpat& parser) {
			parser.FlushText();
			parser.m_pendingNode = parser.Begin(ibXmlNodeType_CDATASection);
			parser.m_pendingOpen = true;
		});
	}

	static void XMLCALL OnCdataEnd(void* self)
	{
		Guard(self, [&](ibExpat& parser) {
			// A section of its own, never merged into the text before or after it - and never Whitespace.
			parser.m_pendingNode.m_type = ibXmlNodeType_CDATASection;
			parser.m_pendingNode.m_value = ibString::FromUTF8(parser.m_pending.data(), parser.m_pending.size());
			parser.m_owner.m_queue.push_back(std::move(parser.m_pendingNode));
			parser.ForgetPending();
		});
	}

	static void XMLCALL OnComment(void* self, const XML_Char* data)
	{
		Guard(self, [&](ibExpat& parser) {
			if (parser.m_inDoctype)
				return;   // the document type is not handed over, nor what stands inside it
			parser.FlushText();
			ibNode node = parser.Begin(ibXmlNodeType_Comment);
			node.m_value = FromUtf8(data);
			parser.m_owner.m_queue.push_back(std::move(node));
		});
	}

	static void XMLCALL OnPi(void* self, const XML_Char* target, const XML_Char* data)
	{
		Guard(self, [&](ibExpat& parser) {
			if (parser.m_inDoctype)
				return;
			parser.FlushText();
			ibNode node = parser.Begin(ibXmlNodeType_ProcessingInstruction);
			node.m_name = node.m_local = FromUtf8(target);
			node.m_value = FromUtf8(data);
			parser.m_owner.m_queue.push_back(std::move(node));
		});
	}

	static void XMLCALL OnXmlDecl(void* self, const XML_Char* version, const XML_Char* encoding, int standalone)
	{
		Guard(self, [&](ibExpat& parser) {
			if (version == nullptr)
				return;   // a text declaration of an entity, not the document's
			parser.m_owner.m_version = FromUtf8(version);
			parser.m_owner.m_encoding = FromUtf8(encoding);
			parser.m_owner.m_standalone = standalone == 1;
		});
	}

	static void XMLCALL OnNsStart(void* self, const XML_Char* prefix, const XML_Char* uri)
	{
		Guard(self, [&](ibExpat& parser) {
			// expat declares all of one element's namespaces before its start handler.
			parser.m_scope = std::make_shared<ibScope>(std::move(parser.m_scope), FromUtf8(prefix), FromUtf8(uri));
			++parser.m_declaring;
		});
	}

	// After the element's end: its EndElement is still in its scope. Its declarations are the innermost links -
	// what was declared inside it has ended already - and expat ends each of them once. The scope is then again the
	// very one outside it, not a copy.
	static void XMLCALL OnNsEnd(void* self, const XML_Char* /*prefix*/)
	{
		Guard(self, [&](ibExpat& parser) {
			if (parser.m_scope != nullptr)
				parser.m_scope = parser.m_scope->m_outer;
		});
	}

	static void XMLCALL OnDoctypeStart(void* self, const XML_Char*, const XML_Char*, const XML_Char*, int)
	{
		static_cast<ibExpat*>(self)->m_inDoctype = true;
	}

	static void XMLCALL OnDoctypeEnd(void* self)
	{
		static_cast<ibExpat*>(self)->m_inDoctype = false;
	}

	// An encoding expat does not know by itself (it knows UTF-8, UTF-16, ISO-8859-1 and US-ASCII): the tables above,
	// one character per byte.
	static int XMLCALL OnUnknownEncoding(void* self, const XML_Char* name, XML_Encoding* info)
	{
		ibExpat& parser = *static_cast<ibExpat*>(self);
		try {
			const ibString spelled = FromUtf8(name);
			const ibXmlCodePage page = ibXmlCodePage::Find(spelled);
			if (page.IsSingleByte()) {
				for (int byte = 0; byte < 256; ++byte) {
					const char16_t ch = page.ToChar(static_cast<unsigned char>(byte));
					info->map[byte] = ch == 0xFFFF ? -1 : static_cast<int>(ch);
				}
				info->data = nullptr;
				info->convert = nullptr;
				info->release = nullptr;
				return XML_STATUS_OK;
			}
			if (page.Kind() != ibXmlCodePage::kUnknown)
				parser.m_refusal = ibString::Format(_("the encoding '%s' is read under its own name only - write it '%s'"),
					spelled, page.Name());
			else
				parser.m_refusal = ibString::Format(_("the encoding '%s' is not supported; supported: %s, ISO-8859-1, US-ASCII"),
					spelled, ibXmlCodePage::Supported());
		}
		catch (...) {
			parser.m_refusal = _("the encoding could not be set up");
		}
		return XML_STATUS_ERROR;
	}

	static int XMLCALL OnExternalEntity(XML_Parser p, const XML_Char*, const XML_Char*, const XML_Char* systemId, const XML_Char*)
	{
		ibExpat& parser = *static_cast<ibExpat*>(XML_GetUserData(p));
		try {
			parser.m_refusal = ibString::Format(_("the external entity '%s' is not read - a document may not reach outside itself"),
				FromUtf8(systemId));
		}
		catch (...) {
			parser.m_refusal = _("an external entity is not read");
		}
		return XML_STATUS_ERROR;
	}

	static int XMLCALL OnNotStandalone(void* self)
	{
		ibExpat& parser = *static_cast<ibExpat*>(self);
		try {
			parser.m_refusal = _("the document depends on declarations outside itself (an external DTD or a parameter entity), which are not read");
		}
		catch (...) {
		}
		return XML_STATUS_ERROR;
	}

	// An entity whose declaration lives in an external DTD - which is not read, so neither is it. A second guard:
	// such a document is refused by OnNotStandalone before an entity of it is met.
	static void XMLCALL OnSkippedEntity(void* self, const XML_Char* name, int)
	{
		Guard(self, [&](ibExpat& parser) {
			parser.Stop(ibString::Format(_("the entity '%s' is declared outside the document, which is not read"),
				FromUtf8(name)));
		});
	}

	ibXmlParser&  m_owner;
	ibString      m_who;
	XML_Parser    m_parser = nullptr;

	std::string   m_pending;                    // text so far, as the parser handed it over (UTF-8)
	ibNode        m_pendingNode;                // ...and where it began
	bool          m_pendingOpen = false;
	std::shared_ptr<ibScope> m_scope;           // every declaration not ended; null: none
	size_t        m_declaring = 0;              // ...its innermost this many for the element about to start
	int           m_depth = 0;
	unsigned long long m_startLine = 0, m_startColumn = 0;   // of the last StartElement: an empty one's end is there too
	bool          m_inDoctype = false;
	bool          m_stopped = false;
	ibString      m_refusal;
};

ibXmlParser::ibXmlParser(const ibString& who, ibInput input)
	: m_expat(std::make_unique<ibExpat>(*this, who, input))
{
}

ibXmlParser::~ibXmlParser() = default;

void ibXmlParser::Feed(const char* data, size_t size, bool last, const ibString& source)
{
	m_expat->Feed(data, size, last, source);
}

void ibXmlParser::Resume(const ibString& source)
{
	m_expat->Resume(source);
}

bool ibXmlParser::Paused() const
{
	return m_expat->Paused();
}

bool ibXmlParser::Finished() const
{
	return m_expat->Finished();
}

//////////////////////////////////////////////////////////////////////
// The writer
//////////////////////////////////////////////////////////////////////

ibXmlWriter::ibXmlWriter(const ibString& who, ibXmlCodePage page) : m_who(who), m_page(page)
{
}

// ⭐ ALL OR NOTHING: a write the writer refuses leaves it as it was before the call - no half tag, no half value,
// no element it began - so a script that catches the refusal writes on into a document that still parses. What a
// write can change is the text after its end, the open elements' count, the innermost element and the flags.
template <typename F>
void ibXmlWriter::Atomically(F&& write)
{
	// The innermost element's lists only grow during a write, so their lengths are its snapshot - no copy.
	struct ibMark {
		size_t declared = 0, attributes = 0, used = 0;
		int    madePrefixes = 0;
		bool   hasChildren = false, hasText = false;
	};
	const size_t length = m_text.Len();
	const size_t depth = m_frameArray.size();
	ibMark mark;
	if (depth > 0) {
		const ibFrame& frame = m_frameArray.back();
		mark = { frame.m_declaredArray.size(), frame.m_attributeArray.size(), frame.m_usedArray.size(),
			frame.m_madePrefixes, frame.m_hasChildren, frame.m_hasText };
	}
	const bool written = m_written, tagOpen = m_tagOpen, attributeOpen = m_attributeOpen;
	try {
		write();
	}
	catch (...) {
		// Nothing below allocates: what is undone shrinks, so the refusal that is on its way is the one raised.
		if (m_text.Len() > length)
			m_text.Truncate(length);
		if (m_frameArray.size() > depth)
			m_frameArray.resize(depth);
		if (depth > 0) {
			ibFrame& frame = m_frameArray.back();
			frame.m_declaredArray.resize(mark.declared);
			frame.m_attributeArray.resize(mark.attributes);
			frame.m_usedArray.resize(mark.used);
			frame.m_madePrefixes = mark.madePrefixes;
			frame.m_hasChildren = mark.hasChildren;
			frame.m_hasText = mark.hasText;
		}
		m_pendingArray.clear();
		m_written = written;
		m_tagOpen = tagOpen;
		m_attributeOpen = attributeOpen;
		throw;
	}
}

void ibXmlWriter::CloseStartTag()
{
	if (m_attributeOpen)
		ibCoreException::Error(_("%s: an attribute is open - WriteEndAttribute comes first"), m_who);
	if (m_tagOpen) {
		m_text += ibT('>');
		m_tagOpen = false;
	}
}

// Before an element, a comment, an instruction, a section: the line and the tabs - not before the very first
// thing, and not inside an element that holds text.
void ibXmlWriter::BeforeNode()
{
	CloseStartTag();
	const bool insideText = !m_frameArray.empty() && m_frameArray.back().m_hasText;
	if (!m_frameArray.empty())
		m_frameArray.back().m_hasChildren = true;
	if (m_indent && m_written && !insideText) {
		m_text += ibT('\n');
		m_text.Append(ibT('\t'), m_frameArray.size());
	}
	m_written = true;
}

// What would break the markup is refused: a name is letters, digits and `_ - .`, with one `:` between a prefix and
// a local name - and any character past ASCII the recommendation allows. (Not the whole NameChar production: a
// combining mark first, say, is let by.)
void ibXmlWriter::CheckName(const ibString& name, const ibString& what) const
{
	if (name.IsEmpty())
		ibCoreException::Error(_("%s: %s needs a name"), m_who, what);
	const ibChar* chars = name.c_str();
	const size_t length = name.Len();
	size_t colons = 0;
	for (size_t i = 0; i < length; ) {
		size_t units = 0;
		const char32_t c = CodePointAt(chars, length, i, units);
		if (c < 0x80) {
			const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
			const bool digit = c >= '0' && c <= '9';
			if (!letter && !digit && c != '_' && c != '-' && c != '.' && c != ':')
				ibCoreException::Error(_("%s: '%s' is not a name for %s"), m_who, name, what);
			if (c == ':')
				++colons;
		}
		else if (!IsXmlChar(c)) {
			ibCoreException::Error(_("%s: the character U+%04X is not allowed in XML"), m_who, static_cast<unsigned int>(c));
		}
		else if (!m_page.Holds(c)) {
			ibCoreException::Error(_("%s: the character U+%04X in %s has no byte in %s"), m_who,
				static_cast<unsigned int>(c), what, m_page.Name());
		}
		i += units;
	}
	// Neither the prefix nor the local name begins with a digit, `-` or `.`, and neither is empty.
	const auto badStart = [](ibChar c) { return (c >= '0' && c <= '9') || c == '-' || c == '.' || c == ':'; };
	const int colon = name.Find(ibT(':'));
	if (badStart(chars[0]) || chars[length - 1] == ':' || colons > 1
		|| (colon >= 0 && badStart(chars[static_cast<size_t>(colon) + 1])))
		ibCoreException::Error(_("%s: '%s' is not a name for %s"), m_who, name, what);
}

void ibXmlWriter::AppendEscaped(const ibString& text, ibPlace place)
{
	const ibChar* chars = text.c_str();
	const size_t length = text.Len();
	size_t run = 0;   // where the characters that go as they are begin: they are appended together
	for (size_t i = 0; i < length; ) {
		size_t units = 0;
		const char32_t c = CodePointAt(chars, length, i, units);
		const ibChar* replacement = nullptr;
		switch (c) {
		case '&':  replacement = ibT("&amp;"); break;
		case '<':  replacement = ibT("&lt;"); break;
		case '>':  replacement = ibT("&gt;"); break;
		case '\r': replacement = ibT("&#13;"); break;                 // a reader makes a bare one a line feed...
		case '"':  if (place == kInAttribute) replacement = ibT("&quot;"); break;
		case '\t': if (place == kInAttribute) replacement = ibT("&#9;"); break;   // ...and in an attribute, these a space
		case '\n': if (place == kInAttribute) replacement = ibT("&#10;"); break;
		default:
			if (!IsXmlChar(c))
				ibCoreException::Error(_("%s: the character U+%04X is not allowed in XML"), m_who, static_cast<unsigned int>(c));
			break;
		}
		if (replacement == nullptr && m_page.Holds(c)) {
			i += units;
			continue;
		}
		if (i > run)
			m_text.append(chars + run, i - run);
		if (replacement != nullptr)
			m_text += replacement;
		else
			AppendReference(m_text, c);   // not in the page: a reference reads back as the character
		i += units;
		run = i;
	}
	if (length > run)
		m_text.append(chars + run, length - run);
}

// Checked whole before a character is appended: what is refused leaves no part of itself behind.
void ibXmlWriter::AppendVerbatim(const ibString& text, const ibString& what)
{
	const ibChar* chars = text.c_str();
	const size_t length = text.Len();
	for (size_t i = 0; i < length; ) {
		size_t units = 0;
		const char32_t c = CodePointAt(chars, length, i, units);
		if (!IsXmlChar(c))
			ibCoreException::Error(_("%s: the character U+%04X is not allowed in XML"), m_who, static_cast<unsigned int>(c));
		if (!m_page.Holds(c))
			ibCoreException::Error(_("%s: the character U+%04X in %s has no byte in %s"), m_who,
				static_cast<unsigned int>(c), what, m_page.Name());
		i += units;
	}
	m_text += text;
}

// The innermost declaration of the prefix - `xml` is bound in every document.
bool ibXmlWriter::Bound(const ibString& prefix, ibString& uri) const
{
	for (auto frame = m_frameArray.rbegin(); frame != m_frameArray.rend(); ++frame)
		for (auto declared = frame->m_declaredArray.rbegin(); declared != frame->m_declaredArray.rend(); ++declared)
			if (declared->first == prefix) {
				uri = declared->second;
				return true;
			}
	if (prefix == ibT("xml")) {
		uri = ibString(kXmlUri);
		return true;
	}
	return false;
}

// The innermost prefix that still means the URI here - one declared again further in for another URI does not. For
// an attribute (Lookup with kInAttribute) "" is passed over: the default namespace is no attribute's.
bool ibXmlWriter::LookupPrefix(const ibString& uri, ibString& prefix) const
{
	return Lookup(uri, prefix, kInElement);
}

bool ibXmlWriter::Lookup(const ibString& uri, ibString& prefix, ibPlace place) const
{
	if (uri.IsEmpty())
		return false;
	for (auto frame = m_frameArray.rbegin(); frame != m_frameArray.rend(); ++frame)
		for (auto declared = frame->m_declaredArray.rbegin(); declared != frame->m_declaredArray.rend(); ++declared) {
			if (place == kInAttribute && declared->first.IsEmpty())
				continue;
			ibString bound;
			if (declared->second == uri && Bound(declared->first, bound) && bound == uri) {
				prefix = declared->first;
				return true;
			}
		}
	if (uri == ibString(kXmlUri)) {
		prefix = ibT("xml");
		return true;
	}
	return false;
}

// On the start tag still open: the declaration is written, and the element's scope holds it.
void ibXmlWriter::Declare(const ibString& prefix, const ibString& uri)
{
	if (prefix == ibT("xml") && uri == ibString(kXmlUri))
		return;   // bound so in every document: saying it again is no declaration
	if (prefix == ibT("xml") || prefix == ibT("xmlns"))
		ibCoreException::Error(_("%s: the prefix '%s' is reserved"), m_who, prefix);
	if (uri == ibString(kXmlUri) || uri == ibString(kXmlnsUri))
		ibCoreException::Error(_("%s: the namespace '%s' is reserved"), m_who, uri);
	if (!prefix.IsEmpty() && uri.IsEmpty())
		ibCoreException::Error(_("%s: the prefix '%s' cannot be bound to no namespace"), m_who, prefix);
	if (!prefix.IsEmpty()) {
		CheckName(prefix, _("a prefix"));
		if (prefix.Find(ibT(':')) >= 0)
			ibCoreException::Error(_("%s: '%s' is not a name for %s"), m_who, prefix, _("a prefix"));
	}
	ibFrame& frame = m_frameArray.back();
	// A prefix the element's name or an attribute already written stands under means that namespace on this tag:
	// declared again for another, it would move them into it without a word.
	for (const std::pair<ibString, ibString>& used : frame.m_usedArray)
		if (used.first == prefix && used.second != uri)
			ibCoreException::Error(_("%s: the prefix '%s' already stands for '%s' on this element"), m_who,
				prefix.IsEmpty() ? ibString(ibT("xmlns")) : prefix, used.second);
	for (const std::pair<ibString, ibString>& declared : frame.m_declaredArray)
		if (declared.first == prefix) {
			if (declared.second == uri)
				return;   // said already, the same
			ibCoreException::Error(_("%s: the prefix '%s' is declared on this element already"), m_who, prefix);
		}
	m_text += prefix.IsEmpty() ? ibString(ibT(" xmlns=\"")) : ibString(ibT(" xmlns:")) + prefix + ibString(ibT("=\""));
	AppendEscaped(uri, kInAttribute);
	m_text += ibT('"');
	frame.m_declaredArray.emplace_back(prefix, uri);
}

// The prefix a name is written under in this namespace. What is not bound yet waits in m_pendingArray: it is
// declared on the tag once the name is out (DeclarePending).
ibString ibXmlWriter::PrefixFor(const ibString& name, const ibString& uri, ibPlace place)
{
	const int colon = name.Find(ibT(':'));
	ibString prefix = colon >= 0 ? name.Left(static_cast<size_t>(colon)) : ibString();
	if (colon >= 0) {
		ibString bound;
		if (!uri.IsEmpty() && !(Bound(prefix, bound) && bound == uri))
			m_pendingArray.emplace_back(prefix, uri);
		else if (uri.IsEmpty() && !Bound(prefix, bound))
			ibCoreException::Error(_("%s: the prefix '%s' is not bound to a namespace"), m_who, prefix);
		return prefix;
	}
	// No URI: an element stays in the default namespace in scope, as it would read; an attribute is in none.
	if (uri.IsEmpty())
		return ibString();
	if (Lookup(uri, prefix, place))
		return prefix;
	if (place == kInElement) {
		m_pendingArray.emplace_back(ibString(), uri);
		return ibString();
	}
	ibFrame& frame = m_frameArray.back();
	ibString made, bound;
	do {
		made = ibString::Format(ibT("d%dp%d"), Depth(), ++frame.m_madePrefixes);
	} while (Bound(made, bound));
	m_pendingArray.emplace_back(made, uri);
	return made;
}

// What the prefix will mean on the tag being written: a declaration waiting for it first, else the one in scope.
ibString ibXmlWriter::PendingOrBound(const ibString& prefix) const
{
	for (const std::pair<ibString, ibString>& pending : m_pendingArray)
		if (pending.first == prefix)
			return pending.second;
	ibString uri;
	Bound(prefix, uri);
	return uri;
}

void ibXmlWriter::DeclarePending()
{
	std::vector<std::pair<ibString, ibString>> pending;
	pending.swap(m_pendingArray);
	for (const std::pair<ibString, ibString>& declaration : pending)
		Declare(declaration.first, declaration.second);
}

void ibXmlWriter::WriteXmlDeclaration(const ibString& encodingName)
{
	Atomically([&] {
		if (m_written)
			ibCoreException::Error(_("%s: the XML declaration is the first thing in a document"), m_who);
		m_text += ibT("<?xml version=\"1.0\" encoding=\"");
		AppendEscaped(encodingName, kInAttribute);
		m_text += ibT("\"?>");
		m_written = true;
	});
}

void ibXmlWriter::StartElement(const ibString& name, const ibString& uri)
{
	CheckName(name, _("an element"));
	const int colon = name.Find(ibT(':'));
	const ibString given = colon >= 0 ? name.Left(static_cast<size_t>(colon)) : ibString();
	if (given == ibT("xmlns") || (given == ibT("xml") && !uri.IsEmpty() && uri != ibString(kXmlUri)))
		ibCoreException::Error(_("%s: the prefix '%s' is reserved"), m_who, given);
	BeforeNode();
	m_frameArray.emplace_back();
	const ibString prefix = PrefixFor(name, uri, kInElement);
	const ibString local = colon >= 0 ? name.Mid(static_cast<size_t>(colon) + 1) : name;
	const ibString written = prefix.IsEmpty() ? local : prefix + ibString(ibT(":")) + local;
	m_text += ibT('<');
	m_text += written;
	m_tagOpen = true;
	ibFrame& frame = m_frameArray.back();
	frame.m_name = written;
	const ibString in = PendingOrBound(prefix);
	if (!prefix.IsEmpty() || !in.IsEmpty())
		frame.m_usedArray.emplace_back(prefix, in);
	DeclarePending();
}

void ibXmlWriter::WriteStartElement(const ibString& name, const ibString& uri)
{
	Atomically([&] { StartElement(name, uri); });
}

void ibXmlWriter::WriteStartElement(const ibString& name, const ibString& uri,
	const std::vector<ibXmlParser::ibAttribute>& attributeArray)
{
	Atomically([&] {
		StartElement(name, uri);
		for (const ibXmlParser::ibAttribute& attribute : attributeArray)
			Attribute(attribute.m_name, attribute.m_uri, attribute.m_value);   // declarations among them too
	});
}

void ibXmlWriter::WriteEndElement()
{
	Atomically([&] {
		if (m_frameArray.empty())
			ibCoreException::Error(_("%s: no element is open to end"), m_who);
		if (m_attributeOpen)
			ibCoreException::Error(_("%s: an attribute is open - WriteEndAttribute comes first"), m_who);
		if (m_tagOpen) {
			m_text += ibT("/>");
			m_tagOpen = false;
		}
		else {
			const ibFrame& frame = m_frameArray.back();
			if (m_indent && frame.m_hasChildren && !frame.m_hasText) {
				m_text += ibT('\n');
				m_text.Append(ibT('\t'), m_frameArray.size() - 1);
			}
			m_text += ibT("</");
			m_text += frame.m_name;
			m_text += ibT('>');
		}
		m_frameArray.pop_back();
	});
}

// The start of an attribute's value: its name checked, its namespace declared, the same name twice refused. A
// namespace declaration is no attribute here - WriteAttribute or WriteNamespaceMapping makes one.
void ibXmlWriter::StartAttribute(const ibString& name, const ibString& uri)
{
	if (!m_tagOpen || m_attributeOpen)
		ibCoreException::Error(_("%s: an attribute is written right after its element's start, before anything inside it"), m_who);
	CheckName(name, _("an attribute"));
	const int colon = name.Find(ibT(':'));
	const ibString given = colon >= 0 ? name.Left(static_cast<size_t>(colon)) : ibString();
	if (name == ibT("xmlns") || given == ibT("xmlns"))
		ibCoreException::Error(_("%s: a namespace is declared with WriteNamespaceMapping or WriteAttribute, not as an open attribute"), m_who);
	const ibString prefix = PrefixFor(name, uri, kInAttribute);
	const ibString local = colon >= 0 ? name.Mid(static_cast<size_t>(colon) + 1) : name;
	// The attribute's namespace: two names of one meaning are one attribute.
	const ibString in = prefix.IsEmpty() ? ibString() : PendingOrBound(prefix);
	ibFrame& frame = m_frameArray.back();
	for (const std::pair<ibString, ibString>& written : frame.m_attributeArray)
		if (written.first == in && written.second == local)
			ibCoreException::Error(_("%s: the attribute '%s' is written on this element already"), m_who, name);
	DeclarePending();
	frame.m_attributeArray.emplace_back(in, local);
	if (!prefix.IsEmpty())
		frame.m_usedArray.emplace_back(prefix, in);
	m_text += ibT(' ');
	m_text += prefix.IsEmpty() ? local : prefix + ibString(ibT(":")) + local;
	m_text += ibT("=\"");
	m_attributeOpen = true;
}

void ibXmlWriter::WriteStartAttribute(const ibString& name, const ibString& uri)
{
	Atomically([&] { StartAttribute(name, uri); });
}

void ibXmlWriter::WriteEndAttribute()
{
	Atomically([&] {
		if (!m_attributeOpen)
			ibCoreException::Error(_("%s: no attribute is open to end"), m_who);
		m_text += ibT('"');
		m_attributeOpen = false;
	});
}

// `xmlns` and `xmlns:p` written as attributes are what they are - declarations - and the writer's scope holds them,
// so a name under that prefix afterwards finds it bound.
void ibXmlWriter::Attribute(const ibString& name, const ibString& uri, const ibString& value)
{
	if (name == ibT("xmlns") || name.StartsWith(ibT("xmlns:"))) {
		CheckName(name, _("an attribute"));
		if (!m_tagOpen || m_attributeOpen)
			ibCoreException::Error(_("%s: a namespace is declared on an element's start, before anything inside it"), m_who);
		if (!uri.IsEmpty() && uri != ibString(kXmlnsUri))
			ibCoreException::Error(_("%s: a namespace declaration is in the namespace '%s', not in '%s'"), m_who,
				ibString(kXmlnsUri), uri);
		Declare(name == ibT("xmlns") ? ibString() : name.Mid(6), value);
		return;
	}
	StartAttribute(name, uri);
	AppendEscaped(value, kInAttribute);
	m_text += ibT('"');
	m_attributeOpen = false;
}

void ibXmlWriter::WriteAttribute(const ibString& name, const ibString& uri, const ibString& value)
{
	Atomically([&] { Attribute(name, uri, value); });
}

void ibXmlWriter::WriteText(const ibString& text)
{
	Atomically([&] {
		if (m_attributeOpen) {
			AppendEscaped(text, kInAttribute);
			return;
		}
		CloseStartTag();
		if (!m_frameArray.empty())
			m_frameArray.back().m_hasText = true;
		AppendEscaped(text, kInElement);
		m_written = true;
	});
}

void ibXmlWriter::WriteCDATASection(const ibString& text)
{
	Atomically([&] {
		BeforeNode();
		if (!m_frameArray.empty())
			m_frameArray.back().m_hasText = true;   // a section is text: what follows it gets no line of its own
		m_text += ibT("<![CDATA[");
		// `]]>` would end the section: it is split across two, `]]` in one and `>` in the next.
		const ibString end(ibT("]]>"));
		size_t from = 0;
		for (;;) {
			const size_t at = text.find(end, from);
			if (at == ibString::npos)
				break;
			AppendVerbatim(text.Mid(from, at + 2 - from), _("a CDATA section"));
			m_text += ibT("]]><![CDATA[");
			from = at + 2;
		}
		AppendVerbatim(text.Mid(from), _("a CDATA section"));
		m_text += ibT("]]>");
	});
}

void ibXmlWriter::WriteComment(const ibString& text)
{
	Atomically([&] {
		if (text.find(ibString(ibT("--"))) != ibString::npos || (!text.IsEmpty() && text.Last() == ibT('-')))
			ibCoreException::Error(_("%s: a comment cannot hold '--' or end with '-'"), m_who);
		BeforeNode();
		m_text += ibT("<!--");
		AppendVerbatim(text, _("a comment"));
		m_text += ibT("-->");
	});
}

void ibXmlWriter::WriteProcessingInstruction(const ibString& target, const ibString& data)
{
	Atomically([&] {
		CheckName(target, _("a processing instruction"));
		if (target.Lower() == ibT("xml"))
			ibCoreException::Error(_("%s: 'xml' is the declaration's, not an instruction's name - WriteXMLDeclaration writes it"), m_who);
		if (target.Find(ibT(':')) >= 0)
			ibCoreException::Error(_("%s: '%s' is not a name for %s"), m_who, target, _("a processing instruction"));
		if (data.find(ibString(ibT("?>"))) != ibString::npos)
			ibCoreException::Error(_("%s: a processing instruction cannot hold '?>'"), m_who);
		BeforeNode();
		m_text += ibT("<?");
		m_text += target;
		if (!data.IsEmpty()) {
			m_text += ibT(' ');
			AppendVerbatim(data, _("a processing instruction"));
		}
		m_text += ibT("?>");
	});
}

void ibXmlWriter::WriteNamespaceMapping(const ibString& prefix, const ibString& uri)
{
	Atomically([&] {
		if (!m_tagOpen || m_attributeOpen)
			ibCoreException::Error(_("%s: a namespace is declared on an element's start, before anything inside it"), m_who);
		Declare(prefix, uri);
	});
}

// As it is: not escaped, not checked for markup - but every character of it must be one XML allows and have a byte
// in the target, or the file would be written up to it and stop.
void ibXmlWriter::WriteRaw(const ibString& text)
{
	Atomically([&] {
		if (!m_attributeOpen) {
			CloseStartTag();
			if (!m_frameArray.empty())
				m_frameArray.back().m_hasText = true;   // what it is is not known: no line is put inside it
			m_written = true;
		}
		const ibChar* chars = text.c_str();
		const size_t length = text.Len();
		for (size_t i = 0; i < length; ) {
			size_t units = 0;
			const char32_t c = CodePointAt(chars, length, i, units);
			if (!IsXmlChar(c))
				ibCoreException::Error(_("%s: the character U+%04X is not allowed in XML"), m_who, static_cast<unsigned int>(c));
			if (!m_page.Holds(c))
				ibCoreException::Error(_("%s: the character U+%04X has no byte in %s"), m_who,
					static_cast<unsigned int>(c), m_page.Name());
			i += units;
		}
		m_text += text;
	});
}

std::vector<std::pair<ibString, ibString>> ibXmlWriter::Declared() const
{
	std::vector<std::pair<ibString, ibString>> declared;
	for (const ibFrame& frame : m_frameArray)
		declared.insert(declared.end(), frame.m_declaredArray.begin(), frame.m_declaredArray.end());
	return declared;
}

ibString ibXmlWriter::Take()
{
	ibString taken;
	std::swap(taken, m_text);
	return taken;
}

void ibXmlWriter::Finish() const
{
	if (m_attributeOpen)
		ibCoreException::Error(_("%s: an attribute is still open"), m_who);
	if (!m_frameArray.empty())
		ibCoreException::Error(_("%s: the element '%s' is still open - WriteEndElement for every WriteStartElement"),
			m_who, m_frameArray.back().m_name);
}
