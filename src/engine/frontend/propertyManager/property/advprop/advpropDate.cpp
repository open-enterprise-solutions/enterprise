#include "advpropDate.h"

#include <wx/propgrid/advprops.h>

#include "backend/propertyManager/property/propertyDate.h"
#include "frontend/propertyManager/property/private/prop.h"

// register frontend property 
class ibPropertyDateLoader
{
public:
	ibPropertyDateLoader()
	{
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(wxDateProperty, ibPropertyDate::ms_propertyDate);
	}
}s_dateLoaderDate;