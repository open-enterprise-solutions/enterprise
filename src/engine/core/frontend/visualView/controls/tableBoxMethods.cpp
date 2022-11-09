#include "tableBox.h"
#include "form.h"
#include "metadata/metaObjects/objects/object.h"
#include "compiler/methods.h"
#include "utils/stringUtils.h"

void CValueTableBox::PrepareNames() const
{
	IValueFrame::PrepareNames();

	m_methods->AppendAttribute(wxT("tableValue"));
	m_methods->AppendAttribute(wxT("currentRow"));
}

void CValueTableBox::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	if (StringUtils::CompareString(aParams.GetName(), wxT("tableValue"))) {
		if (m_tableModel != NULL)
			m_tableModel->DecrRef();

		m_tableModel = cVal.ConvertToType<IValueTable>();
		m_tableModel->IncrRef();
	}
	else if (StringUtils::CompareString(aParams.GetName(), wxT("currentRow"))) {
		if (m_tableCurrentLine != NULL)
			m_tableCurrentLine->DecrRef();
		m_tableCurrentLine = NULL;
		IValueTable::IValueModelReturnLine *tableReturnLine = NULL;
		if (cVal.ConvertToValue(tableReturnLine)) {
			if (m_tableModel == tableReturnLine->GetOwnerModel()) {
				m_tableCurrentLine = tableReturnLine;
				m_tableCurrentLine->IncrRef();
			}
		}
	}

	IValueFrame::SetAttribute(aParams, cVal);
}

CValue CValueTableBox::GetAttribute(attributeArg_t &aParams)
{
	if (StringUtils::CompareString(aParams.GetName(), wxT("tableValue"))) {
		return m_tableModel;
	}
	else if (StringUtils::CompareString(aParams.GetName(), wxT("currentRow"))) {
		return m_tableCurrentLine;
	}

	return IValueFrame::GetAttribute(aParams);
}
