#ifndef __META_LANGUAGE_H__
#define __META_LANGUAGE_H__

#include "metaObject.h"

class BACKEND_API ibValueMetaObjectLanguage : public ibValueMetaObject {
	public:

	ibString GetLangCode() const { return m_propertyCode->GetValueAsString(); }
	void SetLangCode(const ibString& strCode) { m_propertyCode->SetValue(strCode); }

	ibValueMetaObjectLanguage(const ibString& name = ibEmptyString, const ibString& synonym = ibEmptyString, const ibString& comment = ibEmptyString);

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	//events: 
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	//after and before for designer 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	/**
	* Property events
	*/
	virtual bool OnPropertyChanging(ibProperty* property, const ibVariant& newValue);

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	bool IsValidCode(const ibString& strLangCode);

private:
	ibPropertyUString* m_propertyCode = ibPropertyObject::CreateProperty<ibPropertyUString>(m_categoryContext, ibT("Code"), _("Code"),
		_("The language's code (en, ru, uk and so on): the key its texts are stored under in multi-language strings (synonyms, captions, spreadsheet cells) and the code the session's interface language is set by when this language is the configuration's default. Unique among the configuration's languages - a pasted duplicate gets a number appended."),
		ibT("en"));
};

#endif 