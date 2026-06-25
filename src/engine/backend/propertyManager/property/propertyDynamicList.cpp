#include "propertyDynamicList.h"

// get property for grid — frontend slot, wired by ibPG_IMPLEMENT_PROPERTY_CALLBACK
// (advpropDynamicList.cpp) at frontend load. Null in headless builds.
wxObject* (*ibPropertyDynamicList::ms_propertyDynamicList)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&) = nullptr;
