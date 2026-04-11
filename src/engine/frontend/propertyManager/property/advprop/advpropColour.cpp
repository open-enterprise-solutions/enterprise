#include "advpropColour.h"

#include <wx/propgrid/advprops.h>

#include "backend/propertyManager/property/propertyColour.h"
#include "frontend/propertyManager/property/private/prop.h"

// register frontend property 
class ibPropertyColourLoader
{
public:
	ibPropertyColourLoader()
	{
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(wxColourProperty, ibPropertyColour::ms_propertyColour);
	}
}s_enumLoader;