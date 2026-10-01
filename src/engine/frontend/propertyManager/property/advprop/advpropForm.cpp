#include "advpropHyperLink.h"

#include "backend/propertyManager/property/propertyForm.h"
#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"

// register frontend property 
class ibPropertyFormLoader
{
public:
	ibPropertyFormLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyForm* prop) -> wxPGProperty* {
			return new ibPGHyperLinkProperty(prop->GetPropertyObject(), prop->GetLabel(), prop->GetName(), prop->GetValue());
		});
	}
}s_dateLoaderForm;