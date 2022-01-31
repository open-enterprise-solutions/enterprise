#ifndef _ATTRIBUTES_H__
#define _ATTRIBUTES_H__

#include "metadata/metaObjects/metaObject.h"
#include "common/attributeInfo.h"

class IMetaAttributeObject : public IMetaObject,
	public IAttributeInfo {
	wxDECLARE_ABSTRACT_CLASS(IMetaAttributeObject);
public:

	IMetaAttributeObject(const wxString &name = wxEmptyString, const wxString &synonym = wxEmptyString, const wxString &comment = wxEmptyString) :
		IMetaObject(name, synonym, comment), IAttributeInfo(), m_bFillCheck(false)
	{
	}

	virtual IMetadata *GetMetadata() const { return m_metaData; }

	//process choice
	virtual bool ProcessChoice(IValueFrame *ownerValue, meta_identifier_t id = wxNOT_FOUND);
	virtual bool ProcessListChoice(IValueFrame *ownerValue, meta_identifier_t id = wxNOT_FOUND) { return true; }

	virtual wxString GetFieldNameDB() const {
		return wxString::Format("fld%i", m_metaId);
	}

	//get sql type for db 
	virtual wxString GetSQLTypeObject();

	//check if attribute is default 
	virtual bool DefaultAttribute() const = 0;

	//check if attribute is fill 
	virtual bool FillCheck() const { return m_bFillCheck; }

	//events:
	virtual bool OnCreateMetaObject(IMetadata *metaData);
	virtual bool OnDeleteMetaObject();

protected:

	OptionList *GetDateTimeFormat(Property *) {
		OptionList *optList = new OptionList;
		optList->AddOption(_("date"), eDateFractions::eDate);
		optList->AddOption(_("date and time"), eDateFractions::eDateTime);
		optList->AddOption(_("time"), eDateFractions::eTime);
		return optList;
	}

protected:

	bool m_bFillCheck; 

protected:

	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());
};

class CMetaAttributeObject : public IMetaAttributeObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaAttributeObject);
public:

	CMetaAttributeObject();

	//check if attribute is default 
	virtual bool DefaultAttribute() const { return false; }

	//get class name
	virtual wxString GetClassName() const override { return wxT("attribute"); }

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated();

	virtual void OnPropertyCreated(Property *property);
	virtual void OnPropertySelected(Property *property);
	virtual bool OnPropertyChanging(Property *property, const wxString &oldValue);
	virtual void OnPropertyChanged(Property *property);

	//read & save property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;
};

class CMetaDefaultAttributeObject : public IMetaAttributeObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaAttributeObject);
private:
	CMetaDefaultAttributeObject(const wxString &name, const wxString &synonym, const wxString &comment, bool fillCheck)
		: IMetaAttributeObject(name, synonym, comment)
	{
		m_typeDescription.SetDefaultMetatype(eValueTypes::TYPE_BOOLEAN);
		m_bFillCheck = fillCheck;
	}
	CMetaDefaultAttributeObject(const wxString &name, const wxString &synonym, const wxString &comment, CValueQualifierNumber &qNumber, bool fillCheck)
		: IMetaAttributeObject(name, synonym, comment)
	{
		m_typeDescription.SetDefaultMetatype(eValueTypes::TYPE_NUMBER);
		m_typeDescription.SetNumber(qNumber.m_precision, qNumber.m_scale);
		m_bFillCheck = fillCheck;
	}
	CMetaDefaultAttributeObject(const wxString &name, const wxString &synonym, const wxString &comment, CValueQualifierDate &qDate, bool fillCheck)
		: IMetaAttributeObject(name, synonym, comment)
	{
		m_typeDescription.SetDefaultMetatype(eValueTypes::TYPE_DATE);
		m_typeDescription.SetDate(qDate.m_dateTime);
		m_bFillCheck = fillCheck;
	}
	CMetaDefaultAttributeObject(const wxString &name, const wxString &synonym, const wxString &comment, CValueQualifierString &qString, bool fillCheck)
		: IMetaAttributeObject(name, synonym, comment)
	{
		m_typeDescription.SetDefaultMetatype(eValueTypes::TYPE_STRING);
		m_typeDescription.SetString(qString.m_length);
		m_bFillCheck = fillCheck;
	}
public:

	CMetaDefaultAttributeObject()
		: IMetaAttributeObject()
	{
		m_typeDescription.SetDefaultMetatype(eValueTypes::TYPE_STRING);
	}

	static CMetaDefaultAttributeObject *CreateBoolean(const wxString &name, const wxString &synonym, const wxString &comment,
		bool fillCheck = false) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, fillCheck);
	}
	static CMetaDefaultAttributeObject *CreateNumber(const wxString &name, const wxString &synonym, const wxString &comment,
		unsigned char precision, unsigned char scale, bool fillCheck = false) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, CValueQualifierNumber(precision, scale), fillCheck);
	}
	static CMetaDefaultAttributeObject *CreateDate(const wxString &name, const wxString &synonym, const wxString &comment,
		eDateFractions dateTime, bool fillCheck = false) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, CValueQualifierDate(dateTime), fillCheck);
	}
	static CMetaDefaultAttributeObject *CreateString(const wxString &name, const wxString &synonym, const wxString &comment,
		unsigned short length, bool fillCheck = false) {
		return new CMetaDefaultAttributeObject(name, synonym, comment, CValueQualifierString(length), fillCheck);
	}

	//get class name
	virtual wxString GetClassName() const override { return wxT("defaultAttribute"); }
	//check if attribute is default 
	virtual bool DefaultAttribute() const { return true; };
};

#endif