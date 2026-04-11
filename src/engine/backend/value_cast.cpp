#include "value_cast.h"
#include "backend/backend_exception.h"
#include "backend/metadataConfiguration.h"
#include "backend/appData.h"

#if defined(_USE_CONTROL_VALUECAST)
void ThrowErrorTypeOperation(const wxString& fromType, wxClassInfo* clsInfo)
{
	if (!appData->DesignerMode()) {
		wxString className = wxEmptyString;
		
		if (clsInfo != nullptr) {
			const ibClassID& clsid = ibValue::GetTypeIDByRef(clsInfo);
			if (ibMetaDataConfiguration::Get()) {
				className = ibMetaDataConfiguration::Get()->GetNameObjectFromID(clsid);
			}
			else {
				className = ibValue::GetNameObjectFromID(clsid);
			}
		}
		
		ibBackendCoreException::Error(ERROR_TYPE_OPERATION, fromType, className);
	}
}
#endif