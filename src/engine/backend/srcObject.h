#ifndef __SRC_OBJECT_H__
#define __SRC_OBJECT_H__

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class BACKEND_API ibValueMetaObjectCompositeData;

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