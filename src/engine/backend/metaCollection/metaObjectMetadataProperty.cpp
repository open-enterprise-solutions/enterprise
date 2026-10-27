#include "metaObjectMetadata.h"
#include "backend/compiler/compileCode.h"

void ibValueMetaObjectConfiguration::OnPropertyChanged(ibProperty* property, const ibPropertyValue& oldValue, const ibPropertyValue& newValue)
{
	if (m_propertyDefLanguage == property)
		ibBackendLocalization::SetUserLanguage(GetLangCode());
	
	if (m_propertySyntax == property)
		ibCompileCode::SetCodeStyle(m_propertySyntax->GetValueAsEnum());

	ibValueMetaObject::OnPropertyChanged(property, oldValue, newValue);
}