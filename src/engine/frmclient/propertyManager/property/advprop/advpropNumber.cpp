#include "advpropNumber.h"
#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// (The desktop's ibPGNumberProperty — the decimal's cell — is not here: the thin client holds no decimal. A decimal
//  property is shown as the text the server's exact decimal writes, and goes back read as one.)

// register frontend property
class ibPropertyNumberLoader
{
public:
	ibPropertyNumberLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyInteger"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new wxIntProperty(property->GetLabel(), property->GetName(),
				static_cast<long>(property->GetNodeValue().AsInt()));
		});
		ibPropertyRegistry::Register(wxT("ibPropertyUInteger"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new wxUIntProperty(property->GetLabel(), property->GetName(),
				static_cast<unsigned long>(property->GetNodeValue().AsInt()));
		});
		ibPropertyRegistry::Register(wxT("ibPropertyNumber"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new wxStringProperty(property->GetLabel(), property->GetName(),
				property->GetNodeValue().AsNumber().ToString());
		},
		[](const wxVariant& value) {
			ibNumber number;
			number.FromString(value.GetString());
			return ibDataValue::Number(number);
		});
	}
}g_numberLoader;
