#include "metaInterfaceObject.h"

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

bool ibValueMetaObjectInterface::LoadData(ibReaderMemory& reader)
{
	return m_propertyPicture->LoadData(reader);
}

bool ibValueMetaObjectInterface::SaveData(ibWriterMemory& writer)
{
	return m_propertyPicture->SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectInterface, "Interface", g_metaInterfaceCLSID);