#ifndef _TABLE_ATTRIBUTES_H__
#define _TABLE_ATTRIBUTES_H__

class ITableAttribute {
public:
	//override base objects 
	virtual std::vector<IMetaAttributeObject*> GetGenericAttributes() const = 0;
};

#endif