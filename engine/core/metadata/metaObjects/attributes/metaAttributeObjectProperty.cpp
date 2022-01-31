////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"
#include "frontend/objinspect/objinspect.h"

void CMetaAttributeObject::OnPropertyCreated()
{
	PropertyContainer *categoryType = m_category->GetCategory(1);

	switch (m_typeDescription.GetTypeObject())
	{
	case eValueTypes::TYPE_NUMBER:
		categoryType->ShowProperty("precision");
		categoryType->ShowProperty("scale");
		categoryType->HideProperty("date_time");
		categoryType->HideProperty("length");
		break;
	case eValueTypes::TYPE_DATE:
		categoryType->HideProperty("precision");
		categoryType->HideProperty("scale");
		categoryType->ShowProperty("date_time");
		categoryType->HideProperty("length");
		break;
	case eValueTypes::TYPE_STRING:
		categoryType->HideProperty("precision");
		categoryType->HideProperty("scale");
		categoryType->HideProperty("date_time");
		categoryType->ShowProperty("length");
		break;
	default:
		categoryType->HideProperty("precision");
		categoryType->HideProperty("scale");
		categoryType->HideProperty("date_time");
		categoryType->HideProperty("length");
	}
}

void CMetaAttributeObject::OnPropertyCreated(Property *property)
{
	IMetaObject::OnPropertyCreated(property);
}

void CMetaAttributeObject::OnPropertySelected(Property *property)
{
	IMetaObject::OnPropertySelected(property);
}

bool CMetaAttributeObject::OnPropertyChanging(Property *property, const wxString &oldValue)
{
	if (m_typeDescription.GetTypeObject() == eValueTypes::TYPE_NUMBER) {

		int precision = m_typeDescription.GetPrecision();
		if (property->GetName() == wxT("precision")) {
			precision = property->GetValueAsInteger();
		}
		int scale = m_typeDescription.GetScale();
		if (property->GetName() == wxT("scale")) {
			scale = property->GetValueAsInteger();
		}
		if (precision > MAX_PRECISION) {
			return false;
		}
		if (precision == 0 || precision < scale) {
			return false;
		}
	}
	else if (m_typeDescription.GetTypeObject() == eValueTypes::TYPE_STRING) {
		int length = m_typeDescription.GetLength();
		if (property->GetName() == wxT("length")) {
			length = property->GetValueAsInteger();
		}
		if (length > MAX_LENGTH_STRING) {
			return false;
		}
	}

	return true;
}

void CMetaAttributeObject::OnPropertyChanged(Property *property)
{
	if (property->GetName() == wxT("type")) {
		IMetaObject *metaObject = GetParent();
		wxASSERT(metaObject);
		if (metaObject->OnReloadMetaObject()) {
			objectInspector->RefreshProperty();
		}
	}

	IMetaObject::OnPropertyChanged(property);
}