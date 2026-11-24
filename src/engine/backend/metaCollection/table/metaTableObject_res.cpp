#include "metaTableObject.h"

#include "backend/backend_picture.h"
#include "core/picturePredefined.h"   // g_picTableCLSID — the core's drawing of a table, a value table's as well

ibPicture ibValueMetaObjectTableData::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectTableData::GetIconGroup()
{
	return ibBackendPicture::GetPicture(g_picTableCLSID);
}
