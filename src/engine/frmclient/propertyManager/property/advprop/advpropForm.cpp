#include "advpropHyperLink.h"

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// register frontend property
class ibPropertyFormLoader
{
public:
	ibPropertyFormLoader()
	{
		// ibPropertyForm — a form metaobject's layout and module, not edited in the grid: the link opens its owner.
		ibPropertyRegistry::Register(wxT("ibPropertyForm"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGHyperLinkProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue());
		});
	}
}s_dateLoaderForm;
