
#include <wx/propgrid/advprops.h>

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// register frontend property
class ibPropertyBooleanLoader
{
public:
	ibPropertyBooleanLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyBoolean"), [](const ibDataNode& row) -> wxPGProperty* {
			return new wxBoolProperty(ibPropertyRegistry::GetLabel(row), ibPropertyRegistry::GetName(row),
				ibPropertyRegistry::GetValue(row).AsBool());
		});
	}
}g_boolLoader;