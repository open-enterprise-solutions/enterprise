#include "advpropHyperLink.h"

#include "backend/propertyManager/property/propertyModule.h"
#include "backend/metaCollection/metaModuleObject.h"        // ibPropertyInnerModule<T>
#include "backend/metaCollection/metaObjectComposite.h"     // ibPropertyContainer<T>
#include "backend/metaCollection/partial/chartOfAccounts.h" // ibValueMetaObjectSubcontoKindsTable
#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"

// register frontend property
class ibPropertyModuleLoader
{
public:
	ibPropertyModuleLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyModule* prop) -> wxPGProperty* {
			return new ibPGHyperLinkProperty(prop->GetPropertyObject(), prop->GetLabel(), prop->GetName(), prop->GetValue());
		});

		// ibPropertyInnerModule is a TEMPLATE with no non-template base, so unlike
		// ibPropertyEnumBase it cannot be caught by one registration — each instantiation
		// is named. There are two. Note it hands the META OBJECT, not GetPropertyObject():
		// the module property points at the metaobject it edits.
		ibPropertyRegistry::Register([](ibPropertyInnerModule<ibValueMetaObjectModule>* prop) -> wxPGProperty* {
			return new ibPGHyperLinkProperty(prop->GetMetaObject(), prop->GetLabel(), prop->GetName(), prop->GetValue());
		});
		ibPropertyRegistry::Register([](ibPropertyInnerModule<ibValueMetaObjectManagerModule>* prop) -> wxPGProperty* {
			return new ibPGHyperLinkProperty(prop->GetMetaObject(), prop->GetLabel(), prop->GetName(), prop->GetValue());
		});

		// Editor-less BY DESIGN — these carry composition, not an editable value. Declared
		// so Create() answers null quietly instead of asserting on a forgotten Register.
		ibPropertyRegistry::RegisterNoEditor<ibPropertyContainer<>>();
		ibPropertyRegistry::RegisterNoEditor<ibPropertyContainer<ibValueMetaObjectSubcontoKindsTable>>();
	}
}g_moduleLoader;