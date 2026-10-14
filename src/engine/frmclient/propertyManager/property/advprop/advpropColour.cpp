
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
		ibPropertyRegistry::Register(wxT("ibPropertyColour"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new wxColourProperty(property->GetLabel(), property->GetName(),
				typeConv::StringToColour(property->GetNodeValue().AsString()));
		},
		[](const wxVariant& value) {
			wxColour colour;
			colour << value;
			return ibDataValue::String(typeConv::ColourToString(colour));
		});
	}
}s_enumLoader;