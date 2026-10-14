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
		ibPropertyRegistry::Register(wxT("ibPropertyInteger"), [](const ibDataNode& row) -> wxPGProperty* {
			return new wxIntProperty(ibPropertyRegistry::GetLabel(row), ibPropertyRegistry::GetName(row),
				static_cast<long>(ibPropertyRegistry::GetValue(row).AsInt()));
		});
		ibPropertyRegistry::Register(wxT("ibPropertyUInteger"), [](const ibDataNode& row) -> wxPGProperty* {
			return new wxUIntProperty(ibPropertyRegistry::GetLabel(row), ibPropertyRegistry::GetName(row),
				static_cast<unsigned long>(ibPropertyRegistry::GetValue(row).AsInt()));
		});
		ibPropertyRegistry::Register(wxT("ibPropertyNumber"), [](const ibDataNode& row) -> wxPGProperty* {
			return new wxStringProperty(ibPropertyRegistry::GetLabel(row), ibPropertyRegistry::GetName(row),
				ibPropertyRegistry::GetValue(row).AsNumber().ToString());
		},
		[](const wxVariant& value) {
			ibNumber number;
			number.FromString(value.GetString());
			return ibDataValue::Number(number);
		});
	}
}g_numberLoader;
