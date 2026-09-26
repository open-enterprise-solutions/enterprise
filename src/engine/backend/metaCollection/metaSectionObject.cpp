#include "metaSectionObject.h"
#include <algorithm>   // std::find — GetInterfaceItemArrayObject() lists an object once
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
#include "metaCommandGroupObject.h"   // ibValueMetaObjectCommandGroup / g_platformCommandGroups

// The declared group a checked item is filed under. Only a command names one — every other kind is filed by
// its area alone — so the kind is asked first and the command second, the way the configuration tree asks a
// command for its sub-commands.
static const ibValueMetaObjectCommandGroup* DeclaredGroupOf(const ibValueMetaObject* object)
{
	if (object->GetClassType() != g_metaCommonCommandCLSID && object->GetClassType() != g_metaCommandCLSID)
		return nullptr;
	return static_cast<const ibValueMetaObjectCommand*>(object)->GetCommandGroup();
}

bool ibValueMetaObjectSection::GetInterfaceItemArrayObject(ibInterfaceCommandSection cmdSection,
	std::vector<ibValueMetaObject*>& array) const
{
	for (const auto object : m_metaData->GetAnyArrayObject()) {

		if (!object->IsSetInterface(m_metaId))
			continue;
		// Filed under a group of its own, so in no platform area — the overload below takes it.
		if (DeclaredGroupOf(object) != nullptr)
			continue;

		const ibInterfaceCommandSection& object_type = object->GetCommandSection();

		//create + list
		if ((ibInterfaceCommandSection_Combined == object_type &&
			(cmdSection == ibInterfaceCommandSection::ibInterfaceCommandSection_Default || cmdSection == ibInterfaceCommandSection::ibInterfaceCommandSection_Create)) || cmdSection == object_type)
		{
			array.emplace_back(object);
		}
	}

	return array.size() > 0;
}

bool ibValueMetaObjectSection::GetInterfaceItemArrayObject(const ibValueMetaObjectCommandGroup* group,
	std::vector<ibValueMetaObject*>& array) const
{
	if (group == nullptr)
		return false;

	for (const auto object : m_metaData->GetAnyArrayObject()) {
		if (object->IsSetInterface(m_metaId) && DeclaredGroupOf(object) == group)
			array.emplace_back(object);
	}

	return array.size() > 0;
}

std::vector<ibValueMetaObject*> ibValueMetaObjectSection::GetInterfaceItemArrayObject() const
{
	std::vector<ibValueMetaObject*> every;
	auto take = [&every](const std::vector<ibValueMetaObject*>& inGroup) {
		for (ibValueMetaObject* object : inGroup)
			if (std::find(every.begin(), every.end(), object) == every.end())
				every.push_back(object);
	};

	for (const ibInterfaceCommandSection area : g_platformCommandGroups) {
		std::vector<ibValueMetaObject*> inArea;
		GetInterfaceItemArrayObject(area, inArea);
		take(inArea);
	}
	for (const ibValueMetaObjectCommandGroup* group : m_metaData->GetAnyArrayObject<ibValueMetaObjectCommandGroup>(g_metaCommandGroupCLSID)) {
		// A group of the FORM command bar is not shown on a section page — its commands stand in a form's
		// submenu. Taken in here, they came out as plain links on the page of the section above, run with no
		// document (found by the audit, 2026-09-22).
		if (group == nullptr || group->IsDeleted() || group->GetCategory() == ibCommandGroupCategory_FormCommandBar)
			continue;
		std::vector<ibValueMetaObject*> inGroup;
		GetInterfaceItemArrayObject(group, inGroup);
		take(inGroup);
	}
	return every;
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
	for (ibValueMetaObject* item : GetInterfaceItemArrayObject())                  // (c) an interface ITEM (any group)
		if (item != nullptr && item->CompareId(hop.m_id)) {
			out = static_cast<const ibValue*>(item);
			return true;
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