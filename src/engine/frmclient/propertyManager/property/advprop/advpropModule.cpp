#include "advpropHyperLink.h"

#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// register frontend property
class ibPropertyModuleLoader
{
public:
	ibPropertyModuleLoader()
	{
		// ibPropertyInnerModule, whichever module: the server names a template without its arguments. The link hands the
		// property's OWNER — the module it opens is the property's value (the module's node), in the owner's configuration.
		ibPropertyRegistry::Register(wxT("ibPropertyInnerModule"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGHyperLinkProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue());
		});

		// …and the row that opens ITS OWN OBJECT — a module's Module: the same link, its value the module's text, so it
		// opens its owner. A form's Form and a template's Template are the desktop's own files
		// (advpropForm.cpp, advpropSpreadsheet.cpp).
		ibPropertyRegistry::Register(wxT("ibPropertyModule"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGHyperLinkProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue());
		});
	}
}g_moduleLoader;
