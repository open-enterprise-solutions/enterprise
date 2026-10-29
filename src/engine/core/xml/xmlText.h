#ifndef __XML_TEXT_H__
#define __XML_TEXT_H__

#include "core/core.h"
#include "core/fstring.h"   // ibString - names, values, refusals
#include "core/xml/xmlNodeType.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// ⭐⭐ XML, READ AND WRITTEN BY THE ENGINE ITSELF - as its JSON is, so that nothing reads XML through wx. The parser
// is expat (src/3rdparty/expat), behind a pointer: no header of the engine names its types. What a script sees of it
// is backend/system/value/valueXml.h, and the node tree as XML is core/serialize/provider/xmlProvider.h; this is the
// half that knows no value of the language.

// ⭐ THE ENCODINGS AN XML FILE IS WRITTEN AND READ IN, named the way a declaration names them. windows-1251 is what a
// tax return's format demands on every machine - which is why this is a fixed table, not the machine's code page
// (TextEncoding.ANSI is UTF-8 on Linux) and not a converter that falls back to Latin-1 without a word on a name it
// does not know.
class CORE_API ibXmlCodePage {
public:
	enum ibKind { kUnknown, kUtf8, kUtf16, kWindows1251, kCp866 };

	static ibXmlCodePage Find(const ibString& name);   // case-insensitive; aliases cp1251, IBM866, utf8, utf16
	// What the writer can produce and the reader accepts; the reader also takes ISO-8859-1 and US-ASCII, which
	// expat decodes by itself.
	static ibString Supported();

	ibKind   Kind() const { return m_kind; }
	ibString Name() const;                             // what a declaration writes

	// Meaningful only when IsSingleByte() - a UTF kind answers the ASCII half only.
	bool     IsSingleByte() const { return m_table != nullptr; }
	char16_t ToChar(unsigned char byte) const;         // 0xFFFF where the page defines no character
	// False: the page has no byte for this code point - the writer then falls back to a `&#N;` reference.
	bool     ToByte(char32_t ch, unsigned char& byte) const;
	bool     Holds(char32_t ch) const;                 // a UTF kind holds every character

	// The text as this page's bytes, appended to `bytes` - UTF-16 little-endian, without a byte-order mark. A
	// character the page has no byte for is refused, `who` beginning the sentence: the writer has made every such
	// character a reference already, so only what a script wrote raw can be one.
	void     Encode(const ibString& text, const ibString& who, std::string& bytes) const;

private:
	ibKind          m_kind = kUnknown;
	const char16_t* m_table = nullptr;   // every character either page holds is in the basic plane
};

// ⭐ expat PUSHES, A CALLER PULLS: input goes in a chunk at a time (Feed) and what the parser made of it waits in
// Queue() - until kQueuePause nodes wait, when the parser PAUSES until they are taken (Resume). A chunk is not a
// bound on its own: a few hundred bytes of nested internal entities expand into a million elements, all of them
// events of one chunk. Text is held back until the next event shows it is over, so a text split by a chunk's edge
// (or an entity, or a pause) is ONE node; a text node is the one thing held whole in memory.
//
// 🛑 NOTHING OUTSIDE THE DOCUMENT IS READ: no external DTD, no parameter entity, no SYSTEM entity - a document that
// needs one is refused, not read with a hole in it (a document from outside asking the machine for its files).
class CORE_API ibXmlParser {
public:
	// ⭐ The declarations of namespaces as the parser meets them: an immutable chain, innermost first. One declaration
	// is one link on top of the scope outside it, and the end of an element puts that very scope back - so a node
	// keeps its scope by holding a pointer, the nodes of one scope share it, and it is resolved only when asked.
	struct CORE_API ibScope {
		ibScope(std::shared_ptr<ibScope> outer, ibString prefix, ibString uri)
			: m_outer(std::move(outer)), m_declaration(std::move(prefix), std::move(uri)) {}
		~ibScope();                                       // lets go of the outer links one by one, not recursively
		std::shared_ptr<ibScope>      m_outer;
		std::pair<ibString, ibString> m_declaration;     // prefix, URI
	};
	// What is in scope at a node: (prefix, URI), one pair per prefix, the default namespace under "", and `xml`
	// always, bound as the Namespaces recommendation binds it.
	typedef std::vector<std::pair<ibString, ibString>> ibNamespaceArray;

	enum ibInput { kBytes, kCharacters };
	// kNamespaces: a name is resolved — its URI, local part and prefix beside it, the declarations in scope (XMLReader's);
	// kAsWritten: a name is what the document wrote (`r:id`) and nothing else is made of it — no URI, local part or
	// prefix, no scope, `xmlns` an attribute like any other, a prefix nobody declared not refused. A reader that keeps
	// the names alone (ibXmlDocument) then pays for no URI on every element and attribute.
	enum ibNames { kNamespaces, kAsWritten };

	struct ibAttribute { ibString m_name, m_local, m_prefix, m_uri, m_value; };
	struct ibNode {
		ibXmlNodeType            m_type = ibXmlNodeType_None;
		ibString                 m_name, m_local, m_prefix, m_uri, m_value;
		// xmlns declarations among them, first; the EndElement of `<a x="1"/>` has them too, that of `</a>` none
		std::vector<ibAttribute> m_attributeArray;
		bool                     m_emptyElement = false;   // <a/> - both nodes of the pair say so
		int                      m_depth = 0;
		// Where the node begins; a node from inside an entity's replacement text says where the REFERENCE is.
		unsigned long long       m_line = 0, m_column = 0;
		// The scope at the node - an EndElement still in its element's; null where nothing is declared.
		std::shared_ptr<const ibScope> m_namespaces;
	};

	static constexpr size_t kChunk = 65536;
	static constexpr size_t kQueuePause = 4096;

	// What is in scope under the innermost link of a chain.
	static std::shared_ptr<const ibNamespaceArray> Resolve(const ibScope* innermost);
	// ...from declarations as they were met, outermost first - one prefix may be declared again further in, and
	// `xmlns=""` takes the default namespace back. For a writer, which keeps its declarations as a list.
	static std::shared_ptr<const ibNamespaceArray> Resolve(const std::vector<std::pair<ibString, ibString>>& declared);

	// `who` begins every refusal ("XMLReader: ... at line N, column M"). kCharacters: the input is a string's,
	// handed over as UTF-8 - an `encoding=` in its declaration is then not applied; kBytes: a file's, read in the
	// encoding its mark or its declaration names. `names` - see ibNames.
	ibXmlParser(const ibString& who, ibInput input, ibNames names = kNamespaces);
	~ibXmlParser();

	// `last` on the final chunk; `source` (a file's name, or empty) is named in a refusal. May leave the parser
	// Paused(): the rest of what it was given is then Resume()'s, and no new chunk is fed before.
	void Feed(const char* data, size_t size, bool last, const ibString& source);
	void Resume(const ibString& source);
	bool Paused() const;
	bool Finished() const;                               // the final chunk is parsed to its end

	std::deque<ibNode>& Queue() { return m_queue; }      // parsed, not yet taken

	// What the declaration said - known once the parser has passed it.
	const ibString& Version() const { return m_version; }
	const ibString& Encoding() const { return m_encoding; }
	bool Standalone() const { return m_standalone; }

private:
	class ibExpat;                                       // the parser and its handlers (xmlText.cpp)
	std::unique_ptr<ibExpat> m_expat;
	std::deque<ibNode>       m_queue;
	ibString                 m_version, m_encoding;
	bool                     m_standalone = false;
};

// ⭐ ...AND WRITTEN. The writer keeps what it writes well-formed by itself: an attribute only while a start tag is
// open and only once, an end only for an element that was started, nothing left open at Finish - and it escapes
// what it is given
// (`&`, `<`, `>`; in an attribute also `"`, a tab and the line breaks, which a reader would otherwise turn into
// spaces). The text it makes is characters; ibXmlCodePage::Encode makes them bytes.
//
// A character the target page has no byte for becomes a `&#N;` reference in text and in an attribute's value - that
// is legal XML and reads back as the character; in a name, a comment, an instruction or a CDATA section there is no
// such thing, and it is refused. A character XML 1.0 does not allow at all (most controls below 0x20, a lone
// surrogate, U+FFFE, U+FFFF) is refused everywhere.
//
// A write it refuses changes nothing: no half tag or half value is left behind, and the writer goes on as before.
//
// ⚠ It writes a FRAGMENT as readily as a document: several elements at the top, or text there, are not refused -
// what is checked is each element, not that the whole has exactly one root. And a name is checked for what breaks
// the markup (ASCII other than letters, digits, `_ - . :`), not against the whole NameChar production.
class CORE_API ibXmlWriter {
public:
	// `who` begins every refusal; `page` is what the text will be encoded in - a writer into a string is UTF-8's.
	ibXmlWriter(const ibString& who, ibXmlCodePage page);

	// One level of tabs per element and a line before each: not inside an element that holds text, where the
	// line and the tabs would become part of that text.
	void SetIndent(bool indent) { m_indent = indent; }
	bool Indent() const { return m_indent; }

	void WriteXmlDeclaration(const ibString& encodingName);    // the first thing written, or refused
	// `name` may be `prefix:local`. A namespace URI not bound in scope is declared on this element: under the
	// name's prefix, or as the default namespace for a name without one. A name without a prefix whose URI is bound
	// to a prefix takes that prefix; a name without either stays in the default namespace in scope.
	void WriteStartElement(const ibString& name, const ibString& uri);
	// ...with its attributes, as ONE write: one refused, the element is not begun either. What a reader hands over.
	void WriteStartElement(const ibString& name, const ibString& uri, const std::vector<ibXmlParser::ibAttribute>& attributeArray);
	void WriteEndElement();                                     // `<a/>` when nothing was written inside
	// An attribute of the start tag still open. A URI not bound to a prefix is declared under the name's prefix, or
	// under one made up (`d1p1`): an attribute is never in the default namespace. `xmlns` and `xmlns:p` are
	// declarations, and are kept as such.
	void WriteAttribute(const ibString& name, const ibString& uri, const ibString& value);
	void WriteStartAttribute(const ibString& name, const ibString& uri);   // WriteText then goes into its value
	void WriteEndAttribute();
	void WriteText(const ibString& text);
	void WriteCDATASection(const ibString& text);              // `]]>` inside is split across two sections
	void WriteComment(const ibString& text);
	void WriteProcessingInstruction(const ibString& target, const ibString& data);
	void WriteNamespaceMapping(const ibString& prefix, const ibString& uri);   // "" - the default namespace
	void WriteRaw(const ibString& text);                        // as it is: not escaped; only its characters checked

	bool LookupPrefix(const ibString& uri, ibString& prefix) const;   // false: no prefix is bound to it
	// The declarations in scope as they were made, outermost first - what ibXmlParser::Resolve takes.
	std::vector<std::pair<ibString, ibString>> Declared() const;
	int Depth() const { return static_cast<int>(m_frameArray.size()); }   // elements open

	// What was written since the last Take, handed over: a writer into a file passes it on a piece at a time.
	ibString Take();
	void Finish() const;                                        // refuses while an element or an attribute is open

private:
	enum ibPlace { kInElement, kInAttribute };   // where a name or a text goes: an attribute's rules are stricter

	struct ibFrame {
		ibString m_name;                                        // as written into the tag
		std::vector<std::pair<ibString, ibString>> m_declaredArray;   // prefix, URI - on this element
		bool     m_hasChildren = false;
		bool     m_hasText = false;
		std::vector<std::pair<ibString, ibString>> m_attributeArray;  // namespace URI, local name - written already
		std::vector<std::pair<ibString, ibString>> m_usedArray;       // prefix, URI - what the tag's names stand under
		int      m_madePrefixes = 0;
	};

	template <typename F> void Atomically(F&& write);          // a refused write is undone (xmlText.cpp)

	void CloseStartTag();
	void BeforeNode();                                          // the line and the tabs, where they belong
	ibString PrefixFor(const ibString& name, const ibString& uri, ibPlace place);
	bool Bound(const ibString& prefix, ibString& uri) const;
	bool Lookup(const ibString& uri, ibString& prefix, ibPlace place) const;   // an attribute's: never ""
	ibString PendingOrBound(const ibString& prefix) const;
	void StartElement(const ibString& name, const ibString& uri);   // the writes' bodies, not undone by themselves
	void StartAttribute(const ibString& name, const ibString& uri);
	void Attribute(const ibString& name, const ibString& uri, const ibString& value);
	void Declare(const ibString& prefix, const ibString& uri);
	void DeclarePending();
	void CheckName(const ibString& name, const ibString& what) const;
	void AppendEscaped(const ibString& text, ibPlace place);
	void AppendVerbatim(const ibString& text, const ibString& what);

	ibString               m_who;
	ibXmlCodePage          m_page;
	ibString               m_text;
	std::vector<ibFrame>   m_frameArray;
	std::vector<std::pair<ibString, ibString>> m_pendingArray;   // decided by PrefixFor, not written yet
	bool                   m_indent = true;
	bool                   m_written = false;                   // anything at all, Take or not
	bool                   m_tagOpen = false;                   // `<a ...` awaiting its `>`
	bool                   m_attributeOpen = false;             // inside WriteStartAttribute
};

#endif // !__XML_TEXT_H__
