#include "advpropHyperLink.h"

#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// register frontend property
class ibPropertyModuleLoader
{
public:
	ibPropertyModuleLoader()
	{
		// ibPropertyInnerModule, whichever module: the server names a template without its arguments. The link hands the
		// property's OWNER — the module it opens is the row's (ibPropertySerializable::GetObjectId), in the owner's configuration.
		ibPropertyRegistry::Register(wxT("ibPropertyInnerModule"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGHyperLinkProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue());
		});

		// …and the row that opens ITS OWN OBJECT — a module's Module: the same link, the server names the object it opens
		// (Object), as for an inner module. A form's Form and a template's Template are the desktop's own files
		// (advpropForm.cpp, advpropSpreadsheet.cpp).
		ibPropertyRegistry::Register(wxT("ibPropertyModule"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGHyperLinkProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue());
		});
	}
}g_moduleLoader;
