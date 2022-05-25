////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections - methods
////////////////////////////////////////////////////////////////////////////

#include "baseObject.h"
#include "compiler/methods.h"
#include "utils/stringUtils.h"

enum
{
	enAdd = 0,
	enCount,
	enClear,

	enLoad,
	enUnload,

	enWrite,

	enModified,

	enRead,
	enSelected,

	enGetMetadata,
};

enum
{
	enEmpty,
	enMetadata,
};

enum {
	enSet,
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

CMethods* CRecordKeyObject::GetPMethods() const
{
	PrepareNames();
	return m_methods;
}

//////////////////////////////////////////////////////////////

CMethods* IRecordSetObject::CRecordSetRegisterKeyValue::GetPMethods() const
{
	PrepareNames();
	return m_methods;
}

//////////////////////////////////////////////////////////////

CMethods* IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::GetPMethods() const
{
	PrepareNames();
	return m_methods;
}

//////////////////////////////////////////////////////////////

void CRecordKeyObject::PrepareNames() const
{
	std::vector<SEng> aMethods, aAttributes;

	aMethods = {
	{"isEmpty","isEmpty()"},
	{"metadata","metadata()"},
	};

	m_methods->PrepareMethods(aMethods.data(), aMethods.size());

	for (auto dimension : m_metaObject->GetGenericDimensions()) {
		if (dimension->IsDeleted())
			continue;
		aAttributes.push_back(
			SEng{ dimension->GetName(), wxEmptyString, wxT("dimension"),  dimension->GetMetaID() }
		);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

//////////////////////////////////////////////////////////////

void IRecordSetObject::CRecordSetRegisterKeyValue::PrepareNames() const
{
	std::vector<SEng> aMethods, aAttributes;

	IMetaObjectRegisterData* metaRegister = m_recordSet->GetMetaObject();
	for (auto dimension : metaRegister->GetGenericDimensions()) {
		if (dimension->IsDeleted())
			continue;
		aAttributes.push_back(
			SEng{ dimension->GetName(), wxEmptyString, wxT("dimension"),  dimension->GetMetaID() }
		);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

//////////////////////////////////////////////////////////////

void IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::PrepareNames() const
{
	std::vector<SEng> aMethods, aAttributes;

	aMethods.push_back(
		SEng{ wxT("set"), _("set(value)"), wxT("description") }
	);

	m_methods->PrepareMethods(aMethods.data(), aMethods.size());

	aAttributes.push_back(
		SEng{ wxT("value"), wxEmptyString, wxT("description"), m_metaId }
	);

	aAttributes.push_back(
		SEng{ wxT("use"), wxEmptyString, wxT("description") }
	);

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

//////////////////////////////////////////////////////////////

#include "metadata/metadata.h"

CValue CRecordKeyObject::Method(methodArg_t& aParams)
{
	CValue Ret;

	switch (aParams.GetIndex())
	{
	case enEmpty: return IsEmpty();
	case enMetadata: return m_metaObject;
	}

	return Ret;
}

//////////////////////////////////////////////////////////////

CValue IRecordSetObject::CRecordSetRegisterKeyValue::Method(methodArg_t& aParams)
{
	return CValue();
}

//////////////////////////////////////////////////////////////

CValue IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::Method(methodArg_t& aParams)
{
	CValue Ret;

	switch (aParams.GetIndex())
	{
	case enSet:
		m_recordSet->SetKeyValue(m_metaId, aParams[0]);
		break;
	}

	return Ret;
}

//////////////////////////////////////////////////////////////

unsigned int IRecordSetObject::AppenRow(unsigned int before)
{
	std::map<meta_identifier_t, CValue> valueRow;

	IMetaObjectRegisterData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetadata* metaData = metaObject->GetMetadata();
	for (auto attribute : metaObject->GetGenericAttributes()) {
		valueRow.insert_or_assign(attribute->GetMetaID(), attribute->CreateValue());
	}

	if (before > 0) {
		m_aObjectValues.insert(
			m_aObjectValues.begin() + before + 1, valueRow
		);
	}
	else {
		m_aObjectValues.push_back(valueRow);
	}

	if (!CTranslateError::IsSimpleMode()) {
		if (before > 0) {
			IValueTable::RowInserted(before);
			return before + 1;
		}
		IValueTable::RowPrepended();
		return m_aObjectValues.size() - 1;
	}

	return 0;
}
