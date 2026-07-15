
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
			return new wxDateProperty(prop->GetLabel(), prop->GetName(),
				wxDateTime(static_cast<time_t>(prop->GetValueAsDateTime())));
		});
	}
}s_dateLoaderDate;