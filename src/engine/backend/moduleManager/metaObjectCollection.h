#ifndef __META_OBJECT_COLLECTION_H__
#define __META_OBJECT_COLLECTION_H__

#include "backend/compiler/value.h"

class ibMetaData;
class ibValueMetaObject;

void ibValueMetaObjectCollection_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// ⭐ THE METAOBJECTS OF ONE KIND, BY NAME (Max, 2026-11-04) — the one road three namespaces took each on its own: the
// managers of the global context (`Catalogs.Goods`), the metadata (`Metadata.Catalogs.Goods`) and the data sources
// (`Data.Catalogs.Goods`). Each was a Structure filled on every read, name -> what it hands out, and they differed in
// nothing but what it handed out.
//
// So the collection holds the METAOBJECTS, and what one is to a script is the heir's (ElementValue): its manager, the
// object itself, its data source — made when it is asked for, so `Catalogs.Goods` makes one manager, not one for every
// catalog of the configuration. Which objects of the kind are among them is the heir's too, answered as it fills itself.
//
// A name is read after a dot or in brackets, and it is a NAME: case does not matter (`Catalogs.goods`). Two objects may
// share one while a configuration is being edited (it is refused only when saved): the first answers it. Get and [ ]
// take a position as well; Count counts; For Each hands out the elements, not name-and-element pairs.
class BACKEND_API ibValueMetaObjectCollection
	: public ibValueCollection<ibValueMetaObject*, &ibValueMetaObjectCollection_BindNames> {
public:

	// The names, straight off the objects — the member table carries only Count and Get.
	virtual long FindProp(const ibString& strPropName) const override;
	virtual long GetNProps() const override { return (long)m_listValue.size(); }
	virtual const ibString& GetPropName(const long lPropNum) const override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool IsPropReadable(const long lPropNum) const override { return lPropNum >= 0 && lPropNum < GetNProps(); }
	virtual bool IsPropWritable(const long /*lPropNum*/) const override { return false; }

	// [name] as the dot reads; [position] as every collection does.
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue) override;

protected:

	explicit ibValueMetaObjectCollection(ibMetaData* metaData);

	ibMetaData* m_metaData;
};

#endif
