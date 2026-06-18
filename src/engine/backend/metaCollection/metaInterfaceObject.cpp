#include "metaInterfaceObject.h"
#include "backend/serialize/dataBuilder.h"

//***********************************************************************
//*                            IntrfaceObject                           *
//***********************************************************************


//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

ibValueMetaObjectInterface::ibValueMetaObjectInterface(const wxString& name, const wxString& synonym, const wxString& comment) :
	ibValueMetaObject(name, synonym, comment)
{
}

#include "backend/metadata.h"

bool ibValueMetaObjectInterface::GetInterfaceItemArrayObject(ibInterfaceCommandSection cmdSection,
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

bool ibValueMetaObjectInterface::ReadData(const ibDataNode& node)
{
	m_propertyPicture->ReadNodeValue(node.GetProperty(m_propertyPicture->GetName()));
	return true;
}


bool ibValueMetaObjectInterface::WriteData(ibDataNode& node)
{
	node.SetProperty(m_propertyPicture->GetName(), m_propertyPicture->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectInterface, "Interface", g_metaInterfaceCLSID);