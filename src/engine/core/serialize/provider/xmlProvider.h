#ifndef __SERIALIZE_XML_PROVIDER_H__
#define __SERIALIZE_XML_PROVIDER_H__

////////////////////////////////////////////////////////////////////////////
//	Description : ibXmlProvider — the node tree as XML, over the engine's own XML (core/xml/xmlText.h).
//
//	The third ibFormatProvider, beside the binary packet and the JSON view. Unlike the JSON view it loses
//	nothing: a value says its kind, so a date reads back a date and a string that looks like a number a string.
//
//	  <Node Type="Catalog" Id="5">
//	    <Field Name="Name">Goods</Field>                       a string — the one kind not said
//	    <Field Name="Count" Kind="Number">5</Field>            Bool (true / false), Number, Date (ISO, to the
//	    <Field Name="Picture" Kind="Binary">…</Field>          millisecond), Guid (its text), Binary (base64), Empty
//	    <Field Name="Type"><Node>…</Node></Field>              a sub-node
//	    <Field Name="Forms" Kind="Array"><Item>…</Item></Field>  a list — an item says its kind as a field does
//	    <Raw>…</Raw>                                           the transitional raw block, base64
//	    <Children><Node …/></Children>
//	  </Node>
//
//	A date is written for the years 1 to 9999 - four digits of a year - and refused outside them.
//	A field's name is a value, not an element's name: a field may be called anything (`a:b`, a path) and named
//	twice. A string XML 1.0 cannot hold (a NUL, a lone surrogate) is written `Kind="String" Encoding="base64"`, its
//	UTF-8. A type is named as the JSON view names it — by the resolver, by number where it names none — and looked
//	up by name on Read; a name nobody knows leaves clsid 0 and the word as the node's NodeType field, as the JSON
//	view leaves it (ibJsonProvider::LookupTypes). Malformed or misplaced input throws, with its line.
//
//	⭐ A VALUE'S NODE says its type by XML Schema's own means. What a value writes first - its type, field `t`, the
//	digits of a class id (ibValue::Serialize) - is not written as a field but as `xsi:type`: XML Schema's word for a
//	primitive written as the element's text (one with nothing but its payload `v`, of its own kind), the type's name
//	for the rest - by number where the name would read back as another type; a Null is `xsi:nil="true"`. The node's
//	own type is not said again where it is the value's (an element of a list); where it is none (the root a value is
//	packed into) the node says so with an empty `Type=""`.
//
//	  <Node Type="" xmlns:xs="…/XMLSchema" xmlns:xsi="…/XMLSchema-instance" xsi:type="Array">   declared on the outermost
//	    <Field Name="n" Kind="Number">3</Field>
//	    <Children>
//	      <Node xsi:type="xs:decimal">5</Node>                 xs:string, xs:boolean, xs:dateTime (`T`, the
//	      <Node xsi:nil="true"/>                               milliseconds when there are any)
//	      <Node xsi:type="CatalogRef.Goods">…</Node>           its other fields as any node's
//	    </Children>
//	  </Node>
//
//	Read gives `t` back, first, and the node the value's type where no Type says otherwise, none where it is empty -
//	the node that was written, byte for byte - but for a number's trailing zeros, which no form here keeps: its text
//	is the number's own, so 1518500.10 reads back as 1518500.1, the same number. A type nobody knows, a
//	prefix that does not stand for its namespace, text that is no xs: primitive are refused with their line. The
//	`xsi` prefix is read as written (the tree hands over no list of attributes), and has to stand for XML Schema
//	instance; a type's prefix is looked up where it is declared. The form with `Type` and a field `t` still reads.
////////////////////////////////////////////////////////////////////////////

#include "core/serialize/dataBuilder.h"
#include <functional>

class CORE_API ibXmlProvider : public ibFormatProvider {
public:
	// Nodes and values on one path, counting the root. Past this the text is refused — the JSON view's ceiling.
	static constexpr int kMaxNesting = 256;

	bool Write(const ibDataNode& root, ibWriterMemory& writer) const override;   // UTF-8, with its declaration
	bool Read(ibReaderMemory& reader, ibDataNode& root) const override;          // in the encoding it declares

	// …and as the engine's text.
	bool Write(const ibDataNode& root, ibString& text) const;
	bool Read(const ibString& text, ibDataNode& root) const;

	// clsid <-> name, as ibJsonProvider's.
	void SetTypeResolver(std::function<ibString(ibClassID)> fn) { m_resolveType = std::move(fn); }
	void SetTypeLookup(std::function<ibClassID(const ibString&)> fn) { m_lookupType = std::move(fn); }

private:
	std::function<ibString(ibClassID)>       m_resolveType;
	std::function<ibClassID(const ibString&)> m_lookupType;
};

#endif // !__SERIALIZE_XML_PROVIDER_H__
