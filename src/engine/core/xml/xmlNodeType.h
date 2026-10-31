#ifndef __XML_NODE_TYPE_H__
#define __XML_NODE_TYPE_H__

// What a node of a document is - all fifteen kinds, in a fixed order the script's numbers keep: the parser never
// produces some of them (it reports no entity and no notation), and they are here so that code written against the
// whole list compiles. The script's enumeration XMLNodeType is this one.
enum ibXmlNodeType
{
	ibXmlNodeType_None = 1,
	ibXmlNodeType_StartElement,
	ibXmlNodeType_EndElement,
	ibXmlNodeType_Text,
	ibXmlNodeType_Whitespace,
	ibXmlNodeType_CDATASection,
	ibXmlNodeType_Comment,
	ibXmlNodeType_ProcessingInstruction,
	ibXmlNodeType_XMLDeclaration,
	ibXmlNodeType_DocumentTypeDefinition,
	ibXmlNodeType_Attribute,
	ibXmlNodeType_EntityReference,
	ibXmlNodeType_Entity,
	ibXmlNodeType_EndEntity,
	ibXmlNodeType_Notation
};

#endif // !__XML_NODE_TYPE_H__
