#ifndef _META_CTOR_H__
#define _META_CTOR_H__

#include "backend/compiler/typeCtor.h"  // ibCtorValueTypeBase, IB_DISPATCH, ib_clsid_hash

//metaobject register document, form, etc ...
template <class T>
class ibCtorMetaType : public ibCtorValueTypeBase {

public:

	ibCtorMetaType(const wxString& className, const ibClassID& clsid) :ibCtorValueTypeBase(className, typeid(T), clsid) {}

	virtual wxIcon GetClassIcon() const { return T::GetIconGroup(); }
	virtual ibCtorObjectType GetObjectTypeCtor() const { return ibCtorObjectType::ibCtorObjectType_object_metadata; }

	virtual void CallEvent(ibCtorObjectTypeEvent event) {
		if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_Register)
			T::OnRegisterObject(GetClassName(), this);
		else if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_UnRegister)
			T::OnUnRegisterObject(GetClassName());
	}

	virtual ibValue* CreateObject() const { return new T(); }
};

// 3-arg (legacy): explicit clsid.
#define METADATA_TYPE_REGISTER_3(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_m_), new ibCtorMetaType<class_info>(wxT(class_name), clsid))
// 2-arg (new): clsid = ib_clsid_hash(class_name).
#define METADATA_TYPE_REGISTER_2(class_info, class_name)\
METADATA_TYPE_REGISTER_3(class_info, class_name, ib_clsid_hash(class_name))
#define METADATA_TYPE_REGISTER(...) IB_DISPATCH(METADATA_TYPE_REGISTER_, __VA_ARGS__)

#endif