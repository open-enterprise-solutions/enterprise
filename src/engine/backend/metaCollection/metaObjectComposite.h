#ifndef __META_CONTEXT_H__
#define __META_CONTEXT_H__

#include "backend/metaCollection/attribute/metaAttributeObject.h"

#pragma region __property_standart_h__

//base property for "inner attribute"
template <typename T = class ibValueMetaObjectAttributePredefined>
class ibPropertyContainer : public ibProperty {
public:

	template <typename... Args>
	ibPropertyContainer(ibPropertyCategory* cat, Args&&... args)
		: ibProperty(cat,
			std::get<0>(std::forward_as_tuple(args...)),
			std::get<1>(std::forward_as_tuple(args...)),
			wxNullVariant), m_metaObject(nullptr)
	{
		ibValueMetaObject* parent =
			static_cast<ibValueMetaObject*>(m_owner);
		wxASSERT(parent);
		m_metaObject = parent->CreateMetaObjectAndSetParent<T>(args...);
	}

	ibPropertyContainer(ibPropertyCategory* cat, T* metaObject)
		: ibProperty(cat, metaObject->GetName(), metaObject->GetSynonym(), wxNullVariant), m_metaObject(metaObject)
	{
	}

	virtual ~ibPropertyContainer() {}

	// get meta object 
	T* GetMetaObject() const { return m_metaObject; }

	// get meta object via pointer 
	T* operator->() { return GetMetaObject(); }

	//get property for grid 
	virtual wxObject* GetPGProperty() const { return nullptr; }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) { return false; }
	virtual bool GetDataValue(ibValue& pvarPropVal) const {
		pvarPropVal = m_metaObject;
		return true;
	}

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader) { return false; }
	virtual bool SaveData(ibWriterMemory& writer) { return false; }

	//copy & paste object in control 
	virtual bool PasteData(ibReaderMemory& reader) {
		m_metaObject->SetCommonGuid(reader.r_stringZ());
		return true;
	}

	virtual bool CopyData(ibWriterMemory& writer) {
		writer.w_stringZ(m_metaObject->GetCommonGuid());
		return true;
	}

private:

	ibValuePtr<T> m_metaObject;
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
	wxDECLARE_ABSTRACT_CLASS(ibValueMetaObjectCompositeData);
public:

	ibValueMetaObjectCompositeData(
		const wxString& strName = wxEmptyString,
		const wxString& synonym = wxEmptyString,
		const wxString& comment = wxEmptyString
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

	ibValueMetaObjectAttributePredefined* CreateBoolean(const wxString& name, const wxString& synonym, const wxString& comment,
		ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, false, ibValueTypes::TYPE_BOOLEAN, useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateBoolean(const wxString& name, const wxString& synonym, const wxString& comment,
		bool fillCheck, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, fillCheck, ibValueTypes::TYPE_BOOLEAN, useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateBoolean(const wxString& name, const wxString& synonym, const wxString& comment,
		bool fillCheck, const bool& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, fillCheck, ibValue(defValue), useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateNumber(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned char precision, unsigned char scale, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierNumber(precision, scale), false, ibValueTypes::TYPE_NUMBER, useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateNumber(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned char precision, unsigned char scale, bool fillCheck, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierNumber(precision, scale), fillCheck, ibValueTypes::TYPE_NUMBER, useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateNumber(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned char precision, unsigned char scale, bool fillCheck, const ibNumber& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierNumber(precision, scale), fillCheck, ibValue(defValue), useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateDate(const wxString& name, const wxString& synonym, const wxString& comment,
		ibDateFractions dateTime, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierDate(dateTime), false, ibValueTypes::TYPE_DATE, useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateDate(const wxString& name, const wxString& synonym, const wxString& comment,
		ibDateFractions dateTime, bool fillCheck, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierDate(dateTime), fillCheck, ibValueTypes::TYPE_DATE, useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateDate(const wxString& name, const wxString& synonym, const wxString& comment,
		ibDateFractions dateTime, bool fillCheck, const wxDateTime& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierDate(dateTime), fillCheck, ibValue(defValue), useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateString(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned short length, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierString(length), false, ibValueTypes::TYPE_STRING, useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateString(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned short length, bool fillCheck, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierString(length), fillCheck, ibValueTypes::TYPE_STRING, useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateString(const wxString& name, const wxString& synonym, const wxString& comment,
		unsigned short length, bool fillCheck, const wxString& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, ibQualifierString(length), fillCheck, ibValue(defValue), useItem, selectMode);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	ibValueMetaObjectAttributePredefined* CreateEmptyType(const wxString& name, const wxString& synonym, const wxString& comment,
		ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, false, useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateEmptyType(const wxString& name, const wxString& synonym, const wxString& comment,
		bool fillCheck, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, fillCheck, useItem, selectMode);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	ibValueMetaObjectAttributePredefined* CreateSpecialType(const wxString& name, const wxString& synonym, const wxString& comment,
		const ibClassID& clsid, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, clsid, false, ibValue(), useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateSpecialType(const wxString& name, const wxString& synonym, const wxString& comment,
		const ibClassID& clsid, const ibValue& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, clsid, false, defValue, useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateSpecialType(const wxString& name, const wxString& synonym, const wxString& comment,
		const ibClassID& clsid, bool fillCheck, const ibValue& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, clsid, fillCheck, defValue, useItem, selectMode);
	}

	ibValueMetaObjectAttributePredefined* CreateSpecialType(const wxString& name, const wxString& synonym, const wxString& comment,
		const ibClassID& clsid, const ibTypeDescription::ibTypeData& descr,
		bool fillCheck, const ibValue& defValue, ibItemMode useItem = ibItemMode::ibItemMode_Item, ibSelectMode selectMode = ibSelectMode::ibSelectMode_Items) {
		return ibValueMetaObject::CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(name, synonym, comment, clsid, descr, fillCheck, defValue, useItem, selectMode);
	}

	virtual bool FillArrayObjectByPredefinedAttribute(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		return false;
	}
};

#endif