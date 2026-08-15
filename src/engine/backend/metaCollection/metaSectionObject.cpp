#include "metaSectionObject.h"
#include "backend/serialize/dataBuilder.h"

//***********************************************************************
//*                            IntrfaceObject                           *
//***********************************************************************


//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

ibValueMetaObjectSection::ibValueMetaObjectSection(const wxString& name, const wxString& synonym, const wxString& comment) :
	ibValueMetaObject(name, synonym, comment)
{
}

#include "backend/metaData.h"

bool ibValueMetaObjectSection::GetInterfaceItemArrayObject(ibInterfaceCommandSection cmdSection,
	std::vector<ibValueMetaObject*>& array) const
{
	for (const auto object : m_metaData->GetAnyArrayObject()) {

		const ibInterfaceCommandSection& object_type = object->GetCommandSection();

		//create + list 
		if (object->IsSetInterface(m_metaId) && ((ibInterfaceCommandSection_Combined == object_type &&
			(cmdSection == ibInterfaceCommandSection::ibInterfaceCommandSection_Default || cmdSection == ibInterfaceCommandSection::ibInterfaceCommandSection_Create)) || cmdSection == object_type))
		{
			array.emplace_back(object);
		}
	}

	return array.size() > 0;
}

// ibBackendCommandSender — a section id, when reached, HOPS WITHIN ITSELF: a sub-section (descend), its own
// command, or an interface ITEM checked into it. Each result is command-capable / a terminal, so the walk climbs.
// A section has NO runtime of its own — it only ROUTES (this hop); the leaf command carries the mini-runtime (Execute).
bool ibValueMetaObjectSection::GetCommandByHop(const ibCommandHop& hop, ibValue& out)
{
	if (ibValueMetaObjectSection* sub = FindInterfaceObjectByFilter(hop.m_id)) {   // (a) a sub-section -> descend
		out = static_cast<const ibValue*>(sub);
		return true;
	}
	for (ibValueMetaObjectCommand* cmd : GetCommandArrayObject())                  // (b) the section's OWN command
		if (cmd != nullptr && cmd->CompareId(hop.m_id)) {
			out = static_cast<const ibValue*>(cmd);
			return true;
		}
	const ibInterfaceCommandSection areas[] = {                                    // (c) an interface ITEM (any area)
		ibInterfaceCommandSection_Important, ibInterfaceCommandSection_Default,
		ibInterfaceCommandSection_Create, ibInterfaceCommandSection_Report, ibInterfaceCommandSection_Service };
	for (ibInterfaceCommandSection area : areas) {
		std::vector<ibValueMetaObject*> items;
		GetInterfaceItemArrayObject(area, items);
		for (ibValueMetaObject* item : items)
			if (item != nullptr && item->CompareId(hop.m_id)) {
				out = static_cast<const ibValue*>(item);
				return true;
			}
	}
	return false;
}

bool ibValueMetaObjectSection::ReadData(const ibDataNode& node)
{
	m_propertyPicture->SetNodeValue(node.GetProperty(m_propertyPicture->GetName()));
	return true;
}


bool ibValueMetaObjectSection::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyPicture->GetName(), m_propertyPicture->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectSection, "Section", g_metaSectionCLSID);