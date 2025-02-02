#ifndef __role_h__
#define __role_h__

#include "backend_core.h"

//*******************************************************************************
class BACKEND_API IMetaObject;
//*******************************************************************************

class Role {
	wxString m_name;
	wxString m_label;
	IMetaObject* m_object; // pointer to the owner object
	bool m_defValue;  // handler function name
	friend class IMetaObject;
private:
	void InitRole(IMetaObject* metaObject, const bool& value = true);
protected:
	Role(const wxString& roleName, const wxString& roleLabel,
		IMetaObject* metaObject, const bool& value = true) :
		m_name(roleName),
		m_label(roleLabel),
		m_object(metaObject),
		m_defValue(value)
	{
		InitRole(metaObject, value);
	}
public:

	bool GetDefValue() const {
		return m_defValue;
	}

	wxString GetName() const {
		return m_name;
	}

	IMetaObject* GetObject() const {
		return m_object;
	}

	wxString GetLabel() const {
		return m_label.IsEmpty() ?
			m_name : m_label;
	}
};

#endif