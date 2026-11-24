#include "metaLanguageObject.h"

#include "backend/backend_picture.h"
#include "core/picturePredefined.h"   // g_picTranslationCLSID — the core's drawing of a translation, the code editor's too

ibPicture ibValueMetaObjectLanguage::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectLanguage::GetIconGroup()
{
	return ibBackendPicture::GetPicture(g_picTranslationCLSID);
}
