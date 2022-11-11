#ifndef _SRC_OBJECT_H_
#define _SRC_OBJECT_H_

class ISourceObject
{
public:
	
	//get metadata from object 
	virtual IMetaObjectWrapperData* GetMetaObject() const = 0;

	//Get ref class 
	virtual CLASS_ID GetClassType() const = 0;
};

#endif 