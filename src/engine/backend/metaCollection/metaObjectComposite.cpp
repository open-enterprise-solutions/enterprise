#include "metaObjectComposite.h"
#include "backend/metaData.h"


//***********************************************************************
//*							ibValueMetaObjectCompositeData				        *
//***********************************************************************

const ibCtorMetaValueType* ibValueMetaObjectCompositeData::GetTypeCtor(const ibCtorObjectMetaType& refType) const
{
	return m_metaData->GetTypeCtor(this, refType);
}