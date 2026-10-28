#include "advpropHyperLink.h"

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "protocol/protocol.h"   // ibProtocol::ibSchema::ibProperties::Object — what the link opens

// register frontend property
class ibPropertyFormLoader
{
public:
	ibPropertyFormLoader()
	{
		// ibPropertyForm — a form metaobject's layout and module, not edited in the grid: the link opens the object the
		// row names — the form.
		ibPropertyRegistry::Register(wxT("ibPropertyForm"), [](ibBackendProperty* property) -> wxPGProperty* {
			ibDataNode row;
			property->GetProtocolValue(row);
			const ibPropertyObject* const owner = property->GetPropertyObject();   // CONST: the non-const GetMetaData wxFAILs
			return new ibPGHyperLinkProperty(static_cast<ibMetaID>(row.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Object)),
				*owner->GetMetaData(), property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}s_dateLoaderForm;
