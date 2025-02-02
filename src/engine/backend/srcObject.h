#ifndef _SRC_OBJECT_H_
#define _SRC_OBJECT_H_

class ISourceObject {
public:
	//get metaData from object 
	virtual IMetaObjectGenericData* GetSourceMetaObject() const = 0;
	//Get ref class 
	virtual class_identifier_t GetSourceClassType() const = 0;
};

#endif 