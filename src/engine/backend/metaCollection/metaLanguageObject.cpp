#include "metaLanguageObject.h"
#include "core/serialize/dataBuilder.h"

//***********************************************************************
//*                            MetaObjectLanguage                       *
//***********************************************************************


//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

#include "backend/metaData.h"

bool ibValueMetaObjectLanguage::IsValidCode(const ibString& strLangCode)
{
	if (m_metaData == nullptr)
		return false;

	for (const auto language : m_metaData->GetAnyArrayObject<ibValueMetaObjectLanguage>(g_metaLanguageCLSID)) {
		if (language != this && stringUtils::IsSameAs(strLangCode, language->GetLangCode()))
			return false;
	}

	return true;
}

ibValueMetaObjectLanguage::ibValueMetaObjectLanguage(const ibString& name, const ibString& synonym, const ibString& comment) :
	ibValueMetaObject(name, synonym, comment)
{
}

bool ibValueMetaObjectLanguage::OnDeleteMetaObject()
{
	return true;
}

bool ibValueMetaObjectLanguage::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectLanguage::OnAfterRunMetaObject(int flags)
{
	if ((flags & newObjectFlag) != 0 || (flags & pasteObjectFlag) != 0) {

		const ibString& strLangCode = m_propertyCode->GetValueAsString(); bool foundedName = false;
		const auto languageArray = m_metaData->GetAnyArrayObject<ibValueMetaObjectLanguage>(g_metaLanguageCLSID);

		for (const auto object : languageArray) {
			if (object != this &&
				stringUtils::IsSameAs(strLangCode, object->GetLangCode())) {
				foundedName = true;
				break;
			}
		}

		if (foundedName) {
			const ibString& metaPrevName = m_propertyCode->GetValueAsString();
			size_t length = metaPrevName.length();
			while (length >= 0 && stringUtils::IsDigit(metaPrevName[--length]));
			for (unsigned int num_rec = 1;; num_rec++) {
				const ibString& strNewLangCode = ibString::Format(ibT("%s%i"),
					metaPrevName.Left(length + 1), num_rec); bool foundedCodeName = false;
				for (const auto object : languageArray) {
					if (object != this &&
						stringUtils::IsSameAs(strNewLangCode, object->GetLangCode())) {
						foundedCodeName = true;
						break;
					}
				}

				if (!foundedCodeName) {
					m_propertyCode->SetValue(strNewLangCode);
					break;
				}
			}
		}
	}

	return ibValueMetaObject::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectLanguage::ReadData(const ibDataNode& node)
{
	m_propertyCode->SetNodeValue(node.GetProperty(m_propertyCode->GetName()));
	return true;
}

bool ibValueMetaObjectLanguage::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyCode->GetName(), m_propertyCode->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectLanguage, "Language", g_metaLanguageCLSID);