#include "metaPictureObject.h"
#include "backend/serialize/dataBuilder.h"

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

bool ibValueMetaObjectPicture::ReadData(const ibDataNode& node)
{
	m_propertyPicture->ReadNodeValue(node.GetProperty(m_propertyPicture->GetName()));
	return true;
}

bool ibValueMetaObjectPicture::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyPicture->GetName(), m_propertyPicture->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectPicture, "Picture", g_metaPictureCLSID);