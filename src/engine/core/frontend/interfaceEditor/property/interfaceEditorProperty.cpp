#include "interfaceEditorProperty.h"
#include "frontend/objinspect/objinspect.h"

CInterfaceEditorProperty::CInterfaceEditorProperty()
{
}

CInterfaceEditorProperty::~CInterfaceEditorProperty()
{
	if (objectInspector->GetSelectedObject() == this) {
		objectInspector->ClearProperty();
	}
}
