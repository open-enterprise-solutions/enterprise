#ifndef __SRC_OBJECT_H__
#define __SRC_OBJECT_H__

#include "backend/backend.h"   // BACKEND_API — self-sufficient: srcDataObject.h includes this header FIRST,
                               // before anything else defines the export macro (the old srcExplorer.h got it
                               // transitively via queryColumn.h; this base header must not rely on include order).
#include "backend/clsid.h"     // ibClassID (GetSourceClassType) — same reason: don't rely on the includer.

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class BACKEND_API ibValueMetaObjectCompositeData;
class BACKEND_API ibMetaData;   // GetSourceMetaData() returns const ibMetaData* (pointer — fwd-decl is enough)

//********************************************************************************************
//*								      ibSourceObject											 *
//********************************************************************************************

class ibSourceObject {
public:

	virtual ~ibSourceObject() {}

	//get metadata
	virtual const ibMetaData* GetSourceMetaData() const = 0;
	
	//get meta object 
	virtual const ibValueMetaObjectCompositeData* GetSourceMetaObject() const = 0;
	
	//Get ref class 
	virtual ibClassID GetSourceClassType() const = 0;

	//Get presentation 
	virtual wxString GetSourceCaption() const = 0;
};

//********************************************************************************************
//*								      ibTabularObject										 *
//********************************************************************************************

class ibTabularObject {
public:

	virtual ~ibTabularObject() {}

	//get metaData from object 
	virtual const ibValueMetaObjectCompositeData* GetSourceMetaObject() const = 0;

	//Get ref class 
	virtual ibClassID GetSourceClassType() const = 0;
};

#endif 