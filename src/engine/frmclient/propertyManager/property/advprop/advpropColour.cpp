
#include <wx/propgrid/advprops.h>

#include "frmclient/backend/propertyManager/property/propertyColour.h"
#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// register frontend property 
class ibPropertyColourLoader
{
public:
	ibPropertyColourLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyColour* prop) -> wxPGProperty* {
			return new wxColourProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsColour());
		});
	}
}s_enumLoader;