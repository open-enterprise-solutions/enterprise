
#include <wx/propgrid/advprops.h>

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/win/typeconv.h"   // the colour as the server writes it

// register frontend property
class ibPropertyColourLoader
{
public:
	ibPropertyColourLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyColour"), [](const ibDataNode& row) -> wxPGProperty* {
			return new wxColourProperty(ibPropertyRegistry::GetLabel(row), ibPropertyRegistry::GetName(row),
				typeConv::StringToColour(ibPropertyRegistry::GetValue(row).AsString()));
		},
		[](const wxVariant& value) {
			wxColour colour;
			colour << value;
			return ibDataValue::String(typeConv::ColourToString(colour));
		});
	}
}s_enumLoader;