#include "advpropEnum.h"

#include "backend/propertyManager/property/propertyEnum.h"
#include "frontend/propertyManager/property/private/prop.h"

// register frontend property 
class ibPropertyEnumLoader
{
public:
	ibPropertyEnumLoader()
	{
		ibPropertyEnumBase::ms_propertyEnum = [](const wxString& label, const wxString& name, const wxPGChoices& choices, const int& value) -> wxObject* {
			wxPGChoices ch(choices);
			return new wxEnumProperty(label, name, ch, value);
		};
	}
}g_enumLoader;