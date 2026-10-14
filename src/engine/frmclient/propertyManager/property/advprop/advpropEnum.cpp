
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
		ibPropertyRegistry::Register(wxT("ibPropertyEnum"), [](const ibDataNode& row) -> wxPGProperty* {
			wxPGChoices choices = ibPropertyRegistry::GetChoices(row);
			return new wxEnumProperty(ibPropertyRegistry::GetLabel(row), ibPropertyRegistry::GetName(row),
				choices, static_cast<int>(ibPropertyRegistry::GetValue(row).AsInt()));
		});
	}
}g_enumLoader;