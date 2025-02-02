#include "value_cast.h"
#include "backend/backend_exception.h"
#include "backend/metadataConfiguration.h"
#include "backend/appData.h"

#if defined(_USE_CONTROL_VALUECAST)
inline void ThrowErrorTypeOperation(const wxString& fromType, wxClassInfo* clsInfo)
{
	if (!appData->DesignerMode()) {
		wxString clsName = wxEmptyString;
		if (clsInfo != nullptr) {
			const class_identifier_t& clsid = CValue::GetTypeIDByRef(clsInfo);
			if (IMetaDataConfiguration::Get()) {
				clsName = IMetaDataConfiguration::Get()->GetNameObjectFromID(clsid);
			}
			else {
				clsName = CValue::GetNameObjectFromID(clsid);
			}
		}
		CProcUnit::Raise(); CBackendException::Error(ERROR_TYPE_OPERATION, fromType, clsName);
	}
}
#endif