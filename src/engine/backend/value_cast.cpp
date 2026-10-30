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

		// Asked of the registry, not of GetTypeIDByRef, which asserts on a miss: the type asked for may have no name to
		// say - the base an enumeration's member is cast to is registered under no class - and the refusal still goes.
		const ibCtorAbstractType* const typeCtor = ibValue::GetAvailableCtor(typeInfo);
		const ibClassID clsid = typeCtor != nullptr ? typeCtor->GetClassType() : 0;
		if (clsid != 0) {
			if (auto* md = appEnv::ActiveMetaData()) {
				className = md->GetNameObjectFromID(clsid);
			}
			else {
				className = ibValue::GetNameObjectFromID(clsid);
			}
		}

		if (className.empty())
			ibBackendCoreException::Error(_("A value of type '%s' does not support this operation"), fromType);
		ibBackendCoreException::Error(_("A value of type '%s' is not a '%s'"), fromType, className);
	}
}
#endif