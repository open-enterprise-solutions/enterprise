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

		// …and the rows that open THEIR OWN OBJECT — a form's Form, a template's Template, a module's Module: the same
		// link, the server names the object it opens (Object), as for an inner module.
		for (const wxChar* className : { wxT("ibPropertyForm"), wxT("ibPropertySpreadsheet"), wxT("ibPropertyModule") })
			ibPropertyRegistry::Register(className, [](ibBackendProperty* property) -> wxPGProperty* {
				return new ibPGHyperLinkProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
					property->GetValue());
			});
	}
}g_moduleLoader;
