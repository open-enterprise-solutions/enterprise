#include "advpropEnum.h"

#include <wx/propgrid/props.h>   // wxEnumProperty / wxPGChoices — this file's own, not the backend's

#include "backend/propertyManager/property/propertyEnum.h"
#include "frontend/propertyManager/property/private/prop.h"

// register frontend property 
class ibPropertyEnumLoader
{
public:
	ibPropertyEnumLoader()
	{
		ibPropertyEnumBase::ms_propertyEnum = [](const wxString& label, const wxString& name, const ibPropertyChoiceList& choices, const int& value) -> wxObject* {
			wxPGChoices ch;
			for (unsigned int idx = 0; idx < choices.GetCount(); idx++)
				ch.Add(choices.GetLabel(idx), choices.GetId(idx));
			return new wxEnumProperty(label, name, ch, value);
		};
	}
}g_enumLoader;