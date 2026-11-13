#ifndef SETTINGS_XML_H
#define SETTINGS_XML_H

/////////////////////////////////////////////////////////////////////////////
// The options file's two shapes, shared by the editor, the font and colour
// settings and the keys: a leaf <tag>text</tag> and a child found by name.
// Header-only: nothing here is exported, every page compiles its own copy.
// Read as the core's tree, written with the core's writer - no wx XML.
/////////////////////////////////////////////////////////////////////////////

#include "core/xml/xmlDocument.h"   // ibXmlNode - what Load reads
#include "core/xml/xmlText.h"       // ibXmlWriter - what Save writes

// Writes <tag>text</tag> where the writer stands.
inline void AddTextNode(ibXmlWriter& writer, const wxString& tag, const wxString& text)
{
	writer.WriteStartElement(tag, ibString());
	writer.WriteText(text);
	writer.WriteEndElement();
}

inline void AddTextNode(ibXmlWriter& writer, const wxString& tag, bool value)
{
	AddTextNode(writer, tag, value ? wxString(wxT("true")) : wxString(wxT("false")));
}

// The first child of the parent with the name, or nullptr.
inline const ibXmlNode* FindChildNode(const ibXmlNode* parent, const wxString& name)
{
	for (const ibXmlNode* node = parent->GetChildren(); node != nullptr; node = node->GetNext()) {
		if (node->GetName() == name)
			return node;
	}
	return nullptr;
}

inline bool IsTrueNode(const ibXmlNode* node)
{
	return node->GetNodeContent().TrimAll().IsSameAs(ibT("true"), false);
}

#endif
