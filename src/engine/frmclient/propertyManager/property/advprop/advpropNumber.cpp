#include "advpropNumber.h"
#include "frmclient/backend/propertyManager/property/propertyNumber.h"
#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// (The desktop's ibPGNumberProperty — the decimal's cell — is not here: the thin client holds no decimal.)

// register frontend property
class ibPropertyNumberLoader
{
public:
	ibPropertyNumberLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyInteger* prop) -> wxPGProperty* {
			return new wxIntProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsInteger());
		});
		ibPropertyRegistry::Register([](ibPropertyUInteger* prop) -> wxPGProperty* {
			return new wxUIntProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsUInteger());
		});
	}
}g_numberLoader;
