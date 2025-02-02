#ifndef _META_CTOR_H__
#define _META_CTOR_H__

//metaobject register document, form, etc ... 
template <class T>
class CMetaTypeCtor : public IValueTypeCtor {

public:

	CMetaTypeCtor(const wxString& className, const class_identifier_t& clsid) :
		IValueTypeCtor(className, CLASSINFO(T), clsid) {}

	virtual wxIcon GetClassIcon() const {
		return T::GetIconGroup();
	}

	virtual eCtorObjectType GetObjectTypeCtor() const {
		return eCtorObjectType::eCtorObjectType_object_metadata;
	}

	virtual void CallEvent(eCtorObjectTypeEvent event) {
		if (event == eCtorObjectTypeEvent::eCtorObjectTypeEvent_Register)
			T::OnRegisterObject(GetClassName(), this);
		else if (event == eCtorObjectTypeEvent::eCtorObjectTypeEvent_UnRegister)
			T::OnUnRegisterObject(GetClassName());
	}

	virtual CValue* CreateObject() const {
		return new T();
	}
};

#define METADATA_TYPE_REGISTER(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_m_), new CMetaTypeCtor<class_info>(wxT(class_name), clsid))

#endif