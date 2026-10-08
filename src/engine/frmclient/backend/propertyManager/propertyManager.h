#ifndef __PROPERTY_MANAGER_H__
#define __PROPERTY_MANAGER_H__

// THE PROPERTIES OF AN OBJECT — the engine's property manager (backend/propertyManager/propertyManager.h), the types a
// client's object shows in its inspector: the spreadsheet editor's cells and sheet. The rest are the designer's — a
// metaobject's, a form's — and the server's to show.

//base property
#include "frmclient/backend/propertyManager/property/propertyBoolean.h"
#include "frmclient/backend/propertyManager/property/propertyString.h"

//enum property
#include "frmclient/backend/propertyManager/property/propertyEnum.h"

//advanced property
#include "frmclient/backend/propertyManager/property/propertyFormat.h"
#include "frmclient/backend/propertyManager/property/propertyFont.h"
#include "frmclient/backend/propertyManager/property/propertyColour.h"

#endif
