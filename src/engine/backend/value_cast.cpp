#include "value_cast.h"
#include "backend/backend_exception.h"
#include "backend/metadataConfiguration.h"
#include "backend/appData.h"

#if defined(_USE_CONTROL_VALUECAST)
void ThrowErrorTypeOperation(const wxString& fromType, const std::type_info& typeInfo)
{
	if (!appData->DesignerMode()) {
		wxString className = wxEmptyString;

		const ibClassID& clsid = ibValue::GetTypeIDByRef(typeInfo);
		if (clsid != 0) {
			if (auto* md = appEnv::ActiveMetaData()) {
				className = md->GetNameObjectFromID(clsid);
			}
			else {
				className = ibValue::GetNameObjectFromID(clsid);
			}
		}

		ibBackendCoreException::Error(ERROR_TYPE_OPERATION, fromType, className);
	}
}
#endif