#include "value_cast.h"
#include "backend/backend_exception.h"
#include "backend/metadataConfiguration.h"
#include "backend/appData.h"

#if defined(_USE_CONTROL_VALUECAST)
void ThrowErrorTypeOperation(const ibVariant* fromValue, const std::type_info& typeInfo)
{
	if (!appData->DesignerMode()) {
		// The type it was — a runtime value's or a property's, named by the class it answers with.
		const ibString fromType = fromValue != nullptr ? ibValue::GetNameObjectFromID(fromValue->GetClassType()) : ibString();
		ibString className = ibEmptyString;

		const ibClassID& clsid = ibValue::GetTypeIDByRef(typeInfo);
		if (clsid != 0) {
			if (auto* md = appEnv::ActiveMetaData()) {
				className = md->GetNameObjectFromID(clsid);
			}
			else {
				className = ibValue::GetNameObjectFromID(clsid);
			}
		}

		ibBackendCoreException::Error(ibScriptError_TypeOperation, fromType, className);
	}
}
#endif