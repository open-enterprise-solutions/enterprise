#include "advpropBoolean.h"

#include <wx/propgrid/advprops.h>

#include "backend/propertyManager/property/propertyBoolean.h"
#include "frontend/propertyManager/property/private/propertyRegistry.h"

// register frontend property
class ibPropertyBooleanLoader
{
public:
	ibPropertyBooleanLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyBoolean* prop) -> wxPGProperty* {
			return new wxBoolProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsBoolean());
		});
	}
}g_boolLoader;