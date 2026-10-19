#include "advpropHyperLink.h"

#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// register frontend property
class ibPropertyModuleLoader
{
public:
	ibPropertyModuleLoader()
	{
		// ibPropertyModule — a module metaobject's own text: the link opens its owner.
		ibPropertyRegistry::Register(wxT("ibPropertyModule"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGHyperLinkProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue());
		});

		// ibPropertyInnerModule, whichever module: the server names a template without its arguments. The link hands the
		// property's OWNER — the module it opens is the row's (ibPropertySerializable::GetObjectId), in the owner's configuration.
		ibPropertyRegistry::Register(wxT("ibPropertyInnerModule"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGHyperLinkProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue());
		});
	}
}g_moduleLoader;
