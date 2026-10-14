
#include <wx/propgrid/props.h>   // wxEnumProperty / wxPGChoices — this file's own, not the backend's

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// register frontend property
class ibPropertyEnumLoader
{
public:
	ibPropertyEnumLoader()
	{
		// ibPropertyEnum, whatever it enumerates: the server names a template without its arguments. The choices are the
		// row's (the family's verb, GetValueList, asked on the server), so this editor does not know it is an enumeration.
		ibPropertyRegistry::Register(wxT("ibPropertyEnum"), [](ibBackendProperty* property) -> wxPGProperty* {
			wxPGChoices choices = ibPropertyRegistry::GetChoices(property);
			return new wxEnumProperty(property->GetLabel(), property->GetName(),
				choices, static_cast<int>(property->GetNodeValue().AsInt()));
		});
	}
}g_enumLoader;