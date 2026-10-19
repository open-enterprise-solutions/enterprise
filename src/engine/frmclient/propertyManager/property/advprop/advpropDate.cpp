#include <wx/propgrid/advprops.h>

#include "frmclient/backend/backend_core.h"   // ibDateTime — the date as the server stores it
#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// register frontend property
class ibPropertyDateLoader
{
public:
	ibPropertyDateLoader()
	{
		// ibPropertyDate — the date as the server stores it (a Date node), and back as one.
		ibPropertyRegistry::Register(wxT("ibPropertyDate"), [](ibBackendProperty* property) -> wxPGProperty* {
			// The empty date crosses as an invalid wxDateTime: a picker with no date.
			return new wxDateProperty(property->GetLabel(), property->GetName(), property->GetNodeValue().AsDate().ToWxDateTime());
		},
		[](const wxVariant& value) {
			return ibDataValue::Date(ibDateTime::OfWxDateTime(value.GetDateTime()));
		});
	}
}s_dateLoaderDate;
