#include "advpropForm.h"
#include "advpropHyperLink.h"

#include "backend/propertyManager/property/propertyForm.h"
#include "frontend/propertyManager/property/private/prop.h"

// register frontend property 
class ibPropertyFormLoader
{
public:
	ibPropertyFormLoader()
	{
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(ibPGHyperLinkProperty, ibPropertyForm::ms_propertyForm);
	}
}s_dateLoaderForm;