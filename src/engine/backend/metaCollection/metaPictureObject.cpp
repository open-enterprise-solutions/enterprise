#include "metaPictureObject.h"

//***********************************************************************
//*                            IntrfaceObject                           *
//***********************************************************************


//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

ibValueMetaObjectPicture::ibValueMetaObjectPicture(const wxString& name, const wxString& synonym, const wxString& comment) :
	ibValueMetaObject(name, synonym, comment)
{
}

bool ibValueMetaObjectPicture::LoadData(ibReaderMemory& reader)
{
	return m_propertyPicture->LoadData(reader);
}

bool ibValueMetaObjectPicture::SaveData(ibWriterMemory& writer)
{
	return m_propertyPicture->SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectPicture, "Picture", g_metaPictureCLSID);