////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list data 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "frontend/visualView/controls/form.h"
#include "compiler/methods.h"
#include "appData.h"

wxIMPLEMENT_ABSTRACT_CLASS(IDataObjectList, IValueTable);
wxIMPLEMENT_DYNAMIC_CLASS(CDataObjectList, IDataObjectList);

wxDataViewItem IDataObjectList::GetLineByGuid(const Guid &guid) const
{
	auto foundedIt = m_aObjectValues.find(guid);
	if (foundedIt == m_aObjectValues.end()) {
		return wxDataViewItem(0);
	}

	return GetItem(
		std::distance(m_aObjectValues.begin(), foundedIt)
	);
}

IDataObjectList::IDataObjectList(IMetaObjectRefValue *metaObject, form_identifier_t formType) :
	IDataObjectSource(), m_metaObject(metaObject), m_objGuid(Guid::newGuid())
{
	if (appData->EnterpriseMode()) {
		UpdateModel();
	}
}

#include "metadata/singleMetaTypes.h"

CLASS_ID IDataObjectList::GetTypeID() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeID();
}

wxString IDataObjectList::GetTypeString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString IDataObjectList::GetString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

CSourceExplorer IDataObjectList::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(m_metaObject,
		true, true
	);

	for (auto attribute : m_metaObject->GetObjectAttributes()) {
		srcHelper.AppendSource(attribute);
	}

	srcHelper.AppendSource(m_metaObject);
	return srcHelper;
}

bool IDataObjectList::GetTable(IValueTable *&tableValue, meta_identifier_t id)
{
	if (id == m_metaObject->GetMetaID()) {
		tableValue = this;
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////
//               ÑDataObjectListReturnLine                              //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IDataObjectList::ÑDataObjectListReturnLine, IValueTable::IValueTableReturnLine);

IDataObjectList::ÑDataObjectListReturnLine::ÑDataObjectListReturnLine() : IValueTableReturnLine(), m_methods(NULL), m_ownerTable(NULL), m_lineTable(wxNOT_FOUND) {}
IDataObjectList::ÑDataObjectListReturnLine::ÑDataObjectListReturnLine(IDataObjectList *ownerTable, int line) : IValueTableReturnLine(), m_methods(new CMethods()), m_ownerTable(ownerTable), m_lineTable(line) {}
IDataObjectList::ÑDataObjectListReturnLine::~ÑDataObjectListReturnLine() { if (m_methods) delete m_methods; }

void IDataObjectList::ÑDataObjectListReturnLine::PrepareNames() const
{
	std::vector<SEng> aAttributes;

	for (auto attribute : m_ownerTable->m_metaObject->GetObjectAttributes()) {	
		SEng aAttribute;
		aAttribute.sName = attribute->GetName();
		aAttribute.sSynonym = wxT("default");
		aAttribute.iName = attribute->GetMetaID();
		aAttributes.push_back(aAttribute);
	}

	SEng aAttribute;
	aAttribute.sName = wxT("reference");
	aAttribute.sSynonym = wxT("reference");
	aAttribute.iName = m_ownerTable->m_metaObject->GetMetaID();
	aAttributes.push_back(aAttribute);

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void IDataObjectList::ÑDataObjectListReturnLine::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	int index = m_methods->GetAttributePosition(aParams.GetIndex());

	auto itFoundedByLine = m_ownerTable->m_aObjectValues.begin();
	std::advance(itFoundedByLine, m_lineTable);

	//CValueTypeDescription *m_typeDescription = m_ownerTable->m_aDataColumns->GetColumnType(index);
	//itFoundedByLine->insert_or_assign(index, m_typeDescription ? m_typeDescription->AdjustValue(cVal) : cVal);
}

CValue IDataObjectList::ÑDataObjectListReturnLine::GetAttribute(attributeArg_t &aParams)
{
	if (appData->DesignerMode()) {
		return CValue();
	}

	meta_identifier_t id = m_methods->GetAttributePosition(aParams.GetIndex());

	auto itFoundedByLine = m_ownerTable->m_aObjectValues.begin();
	std::advance(itFoundedByLine, m_lineTable);
	auto &rowValues = itFoundedByLine->second;

	auto itFoundedByIndex = rowValues.find(id);
	if (itFoundedByIndex != rowValues.end()) {
		return itFoundedByIndex->second;
	}

	return CValue();
}

/////////////////////////////////////////////////////////////////////////////////////////////

CDataObjectList::CDataObjectList(IMetaObjectRefValue * metaObject, form_identifier_t formType, bool choiceMode):
	IDataObjectList(metaObject, formType), m_bChoiceMode(choiceMode)
{
	m_methods = new CMethods();
}

CDataObjectList::~CDataObjectList()
{
	wxDELETE(m_methods); 
}

//events 
void CDataObjectList::AddValue(unsigned int before)
{
	IDataObjectValue *dataValue =
		m_metaObject->CreateObjectValue();

	if (dataValue) {
		dataValue->ShowValue();
	}
}

void CDataObjectList::CopyValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	IDataObjectValue *dataValue =
		m_metaObject->CreateObjectRefValue(itFounded->first);
	if (dataValue) {
		CValue reference = dataValue->CopyObjectValue();
		reference.ShowValue();
	}
}

void CDataObjectList::EditValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	CValue reference =
		m_metaObject->CreateObjectRefValue(itFounded->first);

	reference.ShowValue();
}

void CDataObjectList::DeleteValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	IDataObjectValue *objData =
		m_metaObject->CreateObjectRefValue(itFounded->first);

	if (objData) {
		//objData->DeleteObject();
	}

	UpdateModel();
}

void CDataObjectList::ChooseValue(CValueForm *srcForm)
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	CValue reference =
		m_metaObject->FindObjectValue(itFounded->first);

	srcForm->NotifyChoice(reference);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(IDataObjectList::ÑDataObjectListReturnLine, "listValueRow", ÑDataObjectListReturnLine, TEXT2CLSID("VL_LVCR"));