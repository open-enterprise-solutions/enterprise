#include "sizer.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "form.h"

ibValueSizerItem::ibValueSizerItem() : ibValueFrame()
{
}

#include "backend/metaData.h"

const ibMetaData* ibValueSizerItem::GetMetaData() const
{
	const ibValueMetaObjectFormBase* metaFormObject = m_formOwner ?
		m_formOwner->GetFormMetaObject() :
		nullptr;

	//for form buider
	if (metaFormObject == nullptr) {
		ibSourceDataObject* srcValue = m_formOwner ?
			m_formOwner->GetSourceObject() :
			nullptr;
		if (srcValue != nullptr) {
			const ibValueMetaObjectGenericData* metaValue = srcValue->GetSourceMetaObject();
			wxASSERT(metaValue);
			return metaValue->GetMetaData();
		}
	}

	return metaFormObject ?
		metaFormObject->GetMetaData() :
		nullptr;
}

#include "backend/metaCollection/metaFormObject.h"

ibFormID ibValueSizerItem::GetTypeForm() const
{
	if (!m_formOwner) {
		wxASSERT(m_formOwner);
		return 0;
	}

	const ibValueMetaObjectFormBase* metaFormObj =
		m_formOwner->GetFormMetaObject();
	wxASSERT(metaFormObj);

	return metaFormObj->GetTypeForm();
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

bool ibValueSizerItem::ReadData(const ibDataNode& node)
{
	//m_propertyProportion->SetNodeValue(node.GetProperty(m_propertyProportion->GetName()));
	//m_propertyFlagBorder->SetNodeValue(node.GetProperty(m_propertyFlagBorder->GetName()));
	//m_propertyFlagState->SetNodeValue(node.GetProperty(m_propertyFlagState->GetName()));
	//m_propertyBorder->SetNodeValue(node.GetProperty(m_propertyBorder->GetName()));

	m_propertyProportion->SetNodeValue(node.GetProperty(m_propertyProportion->GetName()));
	//m_propertyFlagBorder->SetNodeValue(node.GetProperty(m_propertyFlagBorder->GetName()));

	m_propertyFlagBorderLeft->SetNodeValue(node.GetProperty(m_propertyFlagBorderLeft->GetName()));
	m_propertyFlagBorderRight->SetNodeValue(node.GetProperty(m_propertyFlagBorderRight->GetName()));
	m_propertyFlagBorderTop->SetNodeValue(node.GetProperty(m_propertyFlagBorderTop->GetName()));
	m_propertyFlagBorderBottom->SetNodeValue(node.GetProperty(m_propertyFlagBorderBottom->GetName()));

	m_propertyFlagState->SetNodeValue(node.GetProperty(m_propertyFlagState->GetName()));
	m_propertyBorder->SetNodeValue(node.GetProperty(m_propertyBorder->GetName()));

	return ibValueFrame::ReadData(node);
}

bool ibValueSizerItem::WriteData(ibDataNode& node) const
{
	//node.SetProperty(m_propertyProportion->GetName(), m_propertyProportion->GetNodeValue());
	//node.SetProperty(m_propertyFlagBorder->GetName(), m_propertyFlagBorder->GetNodeValue());
	//node.SetProperty(m_propertyFlagState->GetName(), m_propertyFlagState->GetNodeValue());
	//node.SetProperty(m_propertyBorder->GetName(), m_propertyBorder->GetNodeValue());

	node.SetProperty(m_propertyProportion->GetName(), m_propertyProportion->GetNodeValue());
	//node.SetProperty(m_propertyFlagBorder->GetName(), m_propertyFlagBorder->GetNodeValue());

	node.SetProperty(m_propertyFlagBorderLeft->GetName(), m_propertyFlagBorderLeft->GetNodeValue());
	node.SetProperty(m_propertyFlagBorderRight->GetName(), m_propertyFlagBorderRight->GetNodeValue());
	node.SetProperty(m_propertyFlagBorderTop->GetName(), m_propertyFlagBorderTop->GetNodeValue());
	node.SetProperty(m_propertyFlagBorderBottom->GetName(), m_propertyFlagBorderBottom->GetNodeValue());

	node.SetProperty(m_propertyFlagState->GetName(), m_propertyFlagState->GetNodeValue());
	node.SetProperty(m_propertyBorder->GetName(), m_propertyBorder->GetNodeValue());

	return ibValueFrame::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(ibValueSizerItem, "SizerItem", "Sizer", control_to_clsid("CT_SIZR"));
