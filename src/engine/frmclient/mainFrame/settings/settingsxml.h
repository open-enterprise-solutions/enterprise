#ifndef SETTINGS_XML_H
#define SETTINGS_XML_H

/////////////////////////////////////////////////////////////////////////////
// The options file's two shapes, shared by the editor, the font and colour
// settings and the keys: a leaf <tag>text</tag> and a child found by name.
// Header-only: nothing here is exported, every page compiles its own copy.
/////////////////////////////////////////////////////////////////////////////

#include <wx/xml/xml.h>

// Appends <tag>text</tag> to the parent. wxXmlNode's constructor that takes a
// parent PREPENDS, which would write the file backwards — hence AddChild.
inline void AddTextNode(wxXmlNode* parent, const wxString& tag, const wxString& text)
{
	wxXmlNode* const node = new wxXmlNode(wxXML_ELEMENT_NODE, tag);
	node->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxString(), text));
	parent->AddChild(node);
}

inline void AddTextNode(wxXmlNode* parent, const wxString& tag, bool value)
{
	AddTextNode(parent, tag, value ? wxString(wxT("true")) : wxString(wxT("false")));
}

// The first child of the parent with the name, or nullptr.
inline wxXmlNode* FindChildNode(wxXmlNode* parent, const wxString& name)
{
	for (wxXmlNode* node = parent->GetChildren(); node != nullptr; node = node->GetNext()) {
		if (node->GetName() == name)
			return node;
	}
	return nullptr;
}

inline bool IsTrueNode(const wxXmlNode* node)
{
	return node->GetNodeContent().Trim().Trim(false).IsSameAs(wxT("true"), false);
}

#endif
