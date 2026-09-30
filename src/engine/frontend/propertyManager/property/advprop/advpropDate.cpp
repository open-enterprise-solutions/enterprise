
#include <wx/propgrid/advprops.h>

#include "backend/propertyManager/property/propertyDate.h"
#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"

// register frontend property 
class ibPropertyDateLoader
{
public:
	ibPropertyDateLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyDate* prop) -> wxPGProperty* {
			// The empty date crosses as an invalid wxDateTime: a picker with no date.
			return new wxDateProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsDateTime().ToWxDateTime());
		});
	}
}s_dateLoaderDate;