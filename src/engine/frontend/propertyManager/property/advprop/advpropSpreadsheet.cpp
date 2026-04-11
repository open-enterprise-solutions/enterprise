#include "advpropSpreadsheet.h"
#include "advpropHyperLink.h"

#include "backend/propertyManager/property/propertySpreadsheet.h"
#include "frontend/propertyManager/property/private/prop.h"

// register frontend property 
class ibPropertySpreadsheetLoader
{
public:
	ibPropertySpreadsheetLoader()
	{
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(ibPGHyperLinkProperty, ibPropertySpreadsheet::ms_propertySpreadsheet);
	}
}s_dateLoaderSpreadsheet;