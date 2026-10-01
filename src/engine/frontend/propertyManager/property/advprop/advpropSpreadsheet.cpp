#include "advpropHyperLink.h"

#include "backend/propertyManager/property/propertySpreadsheet.h"
#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"

// register frontend property 
class ibPropertySpreadsheetLoader
{
public:
	ibPropertySpreadsheetLoader()
	{
		ibPropertyRegistry::Register([](ibPropertySpreadsheet* prop) -> wxPGProperty* {
			return new ibPGHyperLinkProperty(prop->GetPropertyObject(), prop->GetLabel(), prop->GetName(), prop->GetValue());
		});
	}
}s_dateLoaderSpreadsheet;