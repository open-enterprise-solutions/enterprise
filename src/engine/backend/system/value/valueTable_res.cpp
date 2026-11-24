#include "valueTable.h"

#include "backend/backend_picture.h"
#include "core/picturePredefined.h"   // g_picTableCLSID — the core's drawing of a table, a tabular section's as well

ibPicture ibValueModelTable::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueModelTable::GetIconGroup()
{
	return ibBackendPicture::GetPicture(g_picTableCLSID);
}
