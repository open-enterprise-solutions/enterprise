#include "advpropHyperLink.h"

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// register frontend property
class ibPropertySpreadsheetLoader
{
public:
	ibPropertySpreadsheetLoader()
	{
		// ibPropertySpreadsheet — a template's sheet, not edited in the grid: the link opens its owner.
		ibPropertyRegistry::Register(wxT("ibPropertySpreadsheet"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGHyperLinkProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue());
		});
	}
}s_dateLoaderSpreadsheet;
