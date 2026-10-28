#include "advpropHyperLink.h"

#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "protocol/protocol.h"   // ibProtocol::ibSchema::ibProperties::Object — what the link opens

// register frontend property
class ibPropertyModuleLoader
{
public:
	ibPropertyModuleLoader()
	{
		// ibPropertyInnerModule, whichever module: the server names a template without its arguments. The link opens the
		// object the row names — the module — in its owner's configuration.
		ibPropertyRegistry::Register(wxT("ibPropertyInnerModule"), [](ibBackendProperty* property) -> wxPGProperty* {
			ibDataNode row;
			property->GetProtocolValue(row);
			const ibPropertyObject* const owner = property->GetPropertyObject();   // CONST: the non-const GetMetaData wxFAILs
			return new ibPGHyperLinkProperty(static_cast<ibMetaID>(row.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Object)),
				*owner->GetMetaData(), property->GetLabel(), property->GetName(), property->GetValue());
		});

		// …and a module's Module: the same link, the row naming the module itself. A form's Form and a template's Template
		// are the desktop's own files (advpropForm.cpp, advpropSpreadsheet.cpp).
		ibPropertyRegistry::Register(wxT("ibPropertyModule"), [](ibBackendProperty* property) -> wxPGProperty* {
			ibDataNode row;
			property->GetProtocolValue(row);
			const ibPropertyObject* const owner = property->GetPropertyObject();
			return new ibPGHyperLinkProperty(static_cast<ibMetaID>(row.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Object)),
				*owner->GetMetaData(), property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}g_moduleLoader;
