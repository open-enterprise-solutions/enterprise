#ifndef __META_LANGUAGE_H__
#define __META_LANGUAGE_H__

#include "metaObject.h"

class BACKEND_API ibValueMetaObjectLanguage : public ibValueMetaObject {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectLanguage);
public:

	wxString GetLangCode() const { return m_propertyCode->GetValueAsString(); }
	void SetLangCode(const wxString& strCode) { m_propertyCode->SetValue(strCode); }

	ibValueMetaObjectLanguage(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events: 
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	//after and before for designer 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	/**
	* Property events
	*/
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);

protected:

	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

	bool IsValidCode(const wxString& strLangCode);

private:
	ibPropertyUString* m_propertyCode = ibPropertyObject::CreateProperty<ibPropertyUString>(m_categoryContext, wxT("Code"), _("Code"), wxT("en"));
};

#endif 