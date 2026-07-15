#include "advpropEnum.h"

#include <wx/propgrid/props.h>   // wxEnumProperty / wxPGChoices — this file's own, not the backend's

#include "backend/propertyManager/property/propertyEnum.h"
#include "frontend/propertyManager/property/private/propertyRegistry.h"

// register frontend property
class ibPropertyEnumLoader
{
public:
	ibPropertyEnumLoader()
	{
		// ibPropertyEnumBase, not ibPropertyEnum<T>: the template has 22 instantiations and
		// the dynamic_cast matches all of them through the base. Hence the late priority —
		// a base swallows anything derived from it, so concrete makers must be tried first.
		ibPropertyRegistry::Register([](ibPropertyEnumBase* prop) -> wxPGProperty* {
			wxPGChoices ch;
			const ibPropertyChoiceList choices = prop->GetEnumList();
			for (unsigned int idx = 0; idx < choices.GetCount(); idx++)
				ch.Add(choices.GetLabel(idx), choices.GetId(idx));
			return new wxEnumProperty(prop->GetLabel(), prop->GetName(), ch, prop->GetValueAsInteger());
		}, ibPropertyRegistry::Priority_Base);
	}
}g_enumLoader;