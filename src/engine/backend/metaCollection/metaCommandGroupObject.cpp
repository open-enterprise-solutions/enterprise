#include "metaCommandGroupObject.h"
#include "metaCommandObject.h"               // ibValueEnumInterfaceCommandSection — the platform groups' captions
#include "backend/serialize/dataBuilder.h"

//***********************************************************************
//*                            Command group                            *
//***********************************************************************

ibValueMetaObjectCommandGroup::ibValueMetaObjectCommandGroup(const wxString& name, const wxString& synonym, const wxString& comment) :
	ibValueMetaObject(name, synonym, comment)
{
}

wxString ibCommandGroupCaption(ibInterfaceCommandSection area)
{
	ibValuePtr<ibValueEnumInterfaceCommandSection> areas = ibValue::CreateObject<ibValueEnumInterfaceCommandSection>();
	for (unsigned int idx = 0; idx < areas->GetEnumCount(); idx++)
		if (areas->GetEnumValue(idx) == area)
			return areas->GetEnumDesc(idx);
	return wxEmptyString;
}

wxString ibCommandGroupCategoryCaption(ibCommandGroupCategory category)
{
	ibValuePtr<ibValueEnumCommandGroupCategory> panels = ibValue::CreateObject<ibValueEnumCommandGroupCategory>();
	for (unsigned int idx = 0; idx < panels->GetEnumCount(); idx++)
		if (panels->GetEnumValue(idx) == category)
			return panels->GetEnumDesc(idx);
	return wxEmptyString;
}

wxString ibCommandGroupLabel(ibInterfaceCommandSection area)
{
	return ibCommandGroupCategoryCaption(ibCommandGroupCategoryOf(area)) + wxT(".") + ibCommandGroupCaption(area);
}

//***********************************************************************
//*                          load & save from DB                        *
//***********************************************************************

bool ibValueMetaObjectCommandGroup::ReadData(const ibDataNode& node)
{
	m_propertyCategory->SetNodeValue(node.GetProperty(m_propertyCategory->GetName()));
	m_propertyPicture->SetNodeValue(node.GetProperty(m_propertyPicture->GetName()));
	m_propertyTooltip->SetNodeValue(node.GetProperty(m_propertyTooltip->GetName()));
	return true;
}

bool ibValueMetaObjectCommandGroup::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyCategory->GetName(), m_propertyCategory->GetNodeValue());
	node.SetProperty(m_propertyPicture->GetName(),  m_propertyPicture->GetNodeValue());
	node.SetProperty(m_propertyTooltip->GetName(),  m_propertyTooltip->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectCommandGroup, "CommandGroup", g_metaCommandGroupCLSID);
ENUM_TYPE_REGISTER(ibValueEnumCommandGroupCategory, "CommandGroupCategory", enum_to_clsid("EN_CGCT"));
