#ifndef __META_CONTEXT_H__
#define __META_CONTEXT_H__

#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "core/serialize/dataBuilder.h"   // ibDataValue — node value (String guid)

#pragma region __property_standart_h__

// The holder of a PREDEFINED ATTRIBUTE - one every object of its kind has (Ref, Code, Description, Period, Recorder, …), made
// with the kind and shown in its Predefined attributes (m_categoryPredefined).
template <typename T = class ibValueMetaObjectAttributePredefined>
class ibPropertyPredefined : public ibProperty {
public:

	template <typename... Args>
	ibPropertyPredefined(ibPropertyCategory* cat, Args&&... args)
		: ibProperty(cat,
			std::get<0>(std::forward_as_tuple(args...)),
			std::get<1>(std::forward_as_tuple(args...)),
			ibVariant()), m_metaObject(nullptr)
	{
		ibValueMetaObject* parent =
			static_cast<ibValueMetaObject*>(m_owner);
		wxASSERT(parent);
		m_metaObject = parent->CreateMetaObjectAndSetParent<T>(args...);
		m_propHelp = m_metaObject->GetComment();   // a third argument is the attribute's comment, and the property's help
	}

	// The help is the held attribute's comment — what it says about itself is what the property shows.
	ibPropertyPredefined(ibPropertyCategory* cat, T* metaObject)
		: ibProperty(cat, metaObject->GetName(), metaObject->GetSynonym(), metaObject->GetComment(), ibVariant()), m_metaObject(metaObject)
	{
	}

	virtual ~ibPropertyPredefined() {}

	// get meta object 
	T* GetMetaObject() const { return m_metaObject; }

	// get meta object via pointer 
	T* operator->() { return GetMetaObject(); }


	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) { return false; }
	virtual bool GetDataValue(ibValue& pvarPropVal) const {
		pvarPropVal = m_metaObject;
		return true;
	}

	//per-type node value — the held metaobject's whole node (a Child sub-node)
	virtual bool ReadNodeValue(const ibDataValue& value) override {
		const auto child = value.AsChild();
		if (child) m_metaObject->LoadNode(*child);
		return true;
	}
	virtual bool WriteNodeValue(ibDataValue& value) const override {
		auto child = std::make_shared<ibDataNode>();
		m_metaObject->SaveNode(*child);
		value = ibDataValue::Child(child);
		return true;
	}

	//copy & paste — preserves the predefined child's CommonGuid
	virtual bool CopyNodeValue(ibDataValue& value) const override {
		value = ibDataValue::String(m_metaObject->GetCommonGuid().str());
		return true;
	}
	virtual bool PasteNodeValue(const ibDataValue& value) override {
		m_metaObject->SetCommonGuid(ibGuid(value.AsString()));
		return true;
	}

	// NO ROW — it carries composition (an inner attribute: Ref, Code, Description, …; a table), not a value to edit, and
	// the inspector draws none.
	virtual bool GetProtocolValue(ibDataNode& row, ibReader reader) override { return false; }

private:

	ibValuePtr<T> m_metaObject;
};

// ⭐ …AND ONE WITH A FACE (#646). An inner attribute whose type the configuration chooses - a catalog's code and
// description, a document's number, a tabular section's line number - is shown on its owner as that TYPE, one of those
// it is offered, and its length: a string or a number for a code, a description and a document's number; a number alone
// for a line number, which the platform counts. The attribute stays what it is and writes nothing of its own
// (ibValueMetaObjectAttributePredefined::WriteData says why); what was chosen is written by this holder, into the
// attribute's node beside the rest, so the file and the inspector hand over ONE shape - both come in through
// ReadNodeValue. Asked for as a property of the owner instead, the length would be one more property for each thing an
// attribute can be told.
class BACKEND_API ibPropertyPredefinedType : public ibPropertyPredefined<> {
public:

	ibPropertyPredefinedType(ibPropertyCategory* cat, ibValueMetaObjectAttributePredefined* metaObject,
		std::vector<ibClassID> offered = { g_valueStringCLSID, g_valueNumberCLSID })
		: ibPropertyPredefined<>(cat, metaObject), m_offered(std::move(offered))
	{
	}

	// The attribute's node, and the type chosen in it. A node with no type - a configuration saved before it could be
	// chosen - keeps the one the metatype states; a type it is not offered, or several, is refused.
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

	// A ROW, which the holder of an attribute does not give: the node as it is stored - the client reads the type in it
	// (advpropType.cpp) - and the types it is offered, one at a time.
	virtual bool GetProtocolValue(ibDataNode& row, ibReader reader) override;

private:

	std::vector<ibClassID> m_offered;   // the types it may be, one at a time - what its picker offers
};

#pragma endregion

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

// type ctor for meta
class ibCtorMetaValueType;

//********************************************************************************************
//*                                  Factory & metadata                                      *
//********************************************************************************************


class BACKEND_API ibValueMetaObjectCompositeData
	: public ibValueMetaObject {
	public:

	ibValueMetaObjectCompositeData(
		const ibString& strName = ibEmptyString,
		const ibString& synonym = ibEmptyString,
		const ibString& comment = ibEmptyString
	) : ibValueMetaObject(strName, synonym, comment) {
	}

	virtual ~ibValueMetaObjectCompositeData() {}

	//guid to id 
	ibMetaID GetIdByGuid(const ibGuid& guid) const {
		ibValueMetaObject* metaObject = FindAnyObjectByFilter(guid);
		if (metaObject != nullptr)
			return metaObject->GetMetaID();
		return wxNOT_FOUND;
	}

	ibGuid GetGuidByID(const ibMetaID& id) const {
		ibValueMetaObject* metaObject = FindAnyObjectByFilter(id);
		if (metaObject != nullptr)
			return metaObject->GetGuid();
		return wxNullGuid;
	}

	//runtime support:
	const ibCtorMetaValueType* GetTypeCtor(const enum ibCtorObjectMetaType& refType) const;

#pragma region __generic_h__

	//any attribute 
	std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject() const {
		std::vector<ibValueMetaObjectAttributeBase*> array;
		return GetGenericAttributeArrayObject(array);
	}
	
	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const = 0;

#pragma endregion
#pragma region __array_h__

	//predefined 
	std::vector<ibValueMetaObjectAttributeBase*> GetPredefinedAttributeArrayObject() const {
		std::vector<ibValueMetaObjectAttributeBase*> array;
		FillArrayObjectByPredefinedAttribute(array);
		return array;
	}
	std::vector<ibValueMetaObjectAttributeBase*> GetPredefinedAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		FillArrayObjectByPredefinedAttribute(array);
		return array;
	}

#pragma endregion 
#pragma region __filter_h__

	//predefined
	template <typename _T1>
	ibValueMetaObjectAttributeBase* FindPredefinedAttributeObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectAttributeBase>(id, { g_metaPredefinedAttributeCLSID });
	}

#pragma endregion 

protected:

	ibValueMetaObjectAttributePredefined* CreateBoolean(const ibString& name, const ibString& synonym, const ibString& comment,
		ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, false, ibValueTypes::TYPE_BOOLEAN, useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateBoolean(const ibString& name, const ibString& synonym, const ibString& comment,
		bool fillCheck, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, fillCheck, ibValueTypes::TYPE_BOOLEAN, useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateBoolean(const ibString& name, const ibString& synonym, const ibString& comment,
		bool fillCheck, const bool& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, fillCheck, ibValue(defValue), useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateNumber(const ibString& name, const ibString& synonym, const ibString& comment,
		unsigned char precision, unsigned char scale, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierNumber(precision, scale), false, ibValueTypes::TYPE_NUMBER, useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateNumber(const ibString& name, const ibString& synonym, const ibString& comment,
		unsigned char precision, unsigned char scale, bool fillCheck, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierNumber(precision, scale), fillCheck, ibValueTypes::TYPE_NUMBER, useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateNumber(const ibString& name, const ibString& synonym, const ibString& comment,
		unsigned char precision, unsigned char scale, bool fillCheck, const ibNumber& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierNumber(precision, scale), fillCheck, ibValue(defValue), useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateDate(const ibString& name, const ibString& synonym, const ibString& comment,
		ibDateFractions dateTime, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierDate(dateTime), false, ibValueTypes::TYPE_DATE, useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateDate(const ibString& name, const ibString& synonym, const ibString& comment,
		ibDateFractions dateTime, bool fillCheck, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierDate(dateTime), fillCheck, ibValueTypes::TYPE_DATE, useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateDate(const ibString& name, const ibString& synonym, const ibString& comment,
		ibDateFractions dateTime, bool fillCheck, const ibDateTime& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierDate(dateTime), fillCheck, ibValue(defValue), useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateString(const ibString& name, const ibString& synonym, const ibString& comment,
		unsigned short length, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierString(length), false, ibValueTypes::TYPE_STRING, useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateString(const ibString& name, const ibString& synonym, const ibString& comment,
		unsigned short length, bool fillCheck, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierString(length), fillCheck, ibValueTypes::TYPE_STRING, useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateString(const ibString& name, const ibString& synonym, const ibString& comment,
		unsigned short length, bool fillCheck, const ibString& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierString(length), fillCheck, ibValue(defValue), useItem, selectMode, indexingMode);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	ibValueMetaObjectAttributePredefined* CreateEmptyType(const ibString& name, const ibString& synonym, const ibString& comment,
		ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, false, useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateEmptyType(const ibString& name, const ibString& synonym, const ibString& comment,
		bool fillCheck, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, fillCheck, useItem, selectMode, indexingMode);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	ibValueMetaObjectAttributePredefined* CreateSpecialType(const ibString& name, const ibString& synonym, const ibString& comment,
		const ibClassID& clsid, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, clsid, false, ibValue(), useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateSpecialType(const ibString& name, const ibString& synonym, const ibString& comment,
		const ibClassID& clsid, const ibValue& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, clsid, false, defValue, useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateSpecialType(const ibString& name, const ibString& synonym, const ibString& comment,
		const ibClassID& clsid, bool fillCheck, const ibValue& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, clsid, fillCheck, defValue, useItem, selectMode, indexingMode);
	}

	ibValueMetaObjectAttributePredefined* CreateSpecialType(const ibString& name, const ibString& synonym, const ibString& comment,
		const ibClassID& clsid, const ibTypeDescription::ibTypeData& descr,
		bool fillCheck, const ibValue& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items, ibIndexingMode indexingMode = ibIndexingMode::ibIndexingMode_DontIndex) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, clsid, descr, fillCheck, defValue, useItem, selectMode, indexingMode);
	}

	virtual bool FillArrayObjectByPredefinedAttribute(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		return false;
	}

	// COMMON ATTRIBUTES — the copies a declaration under Common has placed in this object.
	// One function, not a per-class contract like the predefined one above: predefined
	// attributes are named members of each class and every class must list its own, while
	// these are ordinary children and one walk finds them everywhere.
	//
	// It sits beside the predefined fill and merges into the same generic list, so
	// everything downstream — the table builder and its indexes, save and read, the query
	// generator, the form — meets an attribute and asks it the usual questions.
	virtual bool FillArrayObjectByCommonAttribute(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		return FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(
			array, { g_metaCommonAttributeColumnCLSID });
	}

	// THE PREDEFINED ATTRIBUTES' PLACE in the inspector (ibPropertyPredefined) - Ref, Code, Description, Date, Period, … -
	// under the object's Context, apart from its name and caption, which they read as part of. Made here, before any
	// category of the kinds, so it stands right under Context; the inspector leaves it out where it is empty.
	ibPropertyCategory* m_categoryPredefined = ibPropertyObject::CreatePropertyCategory(ibT("Predefined"), _("Predefined attributes"));
};

#endif