
#include <wx/propgrid/advprops.h>

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// register frontend property
class ibPropertyBooleanLoader
{
public:
	ibPropertyBooleanLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyBoolean"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new wxBoolProperty(property->GetLabel(), property->GetName(), property->GetNodeValue().AsBool());
		});
	}
}g_boolLoader;