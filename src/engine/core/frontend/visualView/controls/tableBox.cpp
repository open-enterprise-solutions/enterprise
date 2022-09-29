#include "tableBox.h"
#include "frontend/visualView/visualEditor.h"
#include "frontend/controls/dataView.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueTableBox, IValueWindow);

//***********************************************************************************
//*                                 Special tablebox func                           *
//***********************************************************************************

CValue CValueTableBox::GetControlValue() const
{
	if (!m_tableModel)
		return CValue();

	return m_tableModel;
}

void CValueTableBox::SetControlValue(CValue& vSelected)
{
	IValueModel* tableModel =
		vSelected.ConvertToType<IValueModel>();

	if (m_tableModel) {
		m_tableModel->DecrRef();
		m_tableModel = NULL;
	}

	if (tableModel != NULL) {
		m_tableModel = tableModel;
		m_tableModel->IncrRef();
	}
}

void CValueTableBox::AddColumn()
{
	wxASSERT(m_formOwner);

	IValueFrame* newTableBoxColumn = m_formOwner->NewObject(wxT("tableBoxColumn"), this);
	g_visualHostContext->InsertObject(newTableBoxColumn, this);
	g_visualHostContext->RefreshEditor();
}

#include "compiler/valueType.h"
#include "compiler/valueTable.h"

#include "metadata/metaObjectsDefines.h"
#include "metadata/metaObjects/objects/object.h"

#include "frontend/visualView/visualHost.h"

#include "appData.h"

void CValueTableBox::CreateColumns(CDataViewCtrl* tableCtrl)
{
	if (appData->DesignerMode())
		return;
	CDataViewCtrl* tc = tableCtrl ?
		tableCtrl : dynamic_cast<CDataViewCtrl*>(GetWxObject());
	wxASSERT(tc);
	CVisualDocument* visualDocument = m_formOwner->GetVisualDocument();
	//clear all controls 
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		IValueFrame* childColumn = GetChild(idx);
		wxASSERT(childColumn);
		if (visualDocument != NULL) {
			CVisualHost* m_visualView = visualDocument->GetVisualView();
			wxASSERT(m_visualView);
			m_visualView->RemoveControl(childColumn, this);
		}
		childColumn->SetParent(NULL);
		childColumn->DecrRef();
	}
	//clear all children
	RemoveAllChildren();
	//clear all old columns
	tc->ClearColumns();
	//create new columns
	IValueModel::IValueTableColumnCollection* tableColumns = m_tableModel->GetColumns();
	wxASSERT(tableColumns);
	for (unsigned int idx = 0; idx < tableColumns->GetColumnCount(); idx++) {
		IValueModel::IValueTableColumnCollection::IValueTableColumnInfo* columnInfo = tableColumns->GetColumnInfo(idx);
		CValueTableBoxColumn* newTableBoxColumn =
			m_formOwner->NewObject<CValueTableBoxColumn>("tableboxColumn", this);
		CValueTypeDescription* typeDescription = columnInfo->GetColumnTypes();
		if (typeDescription != NULL) {
			newTableBoxColumn->SetDefaultMetatype(typeDescription->GetClsids(),
				typeDescription->m_qNumber, typeDescription->m_qDate, typeDescription->m_qString
			);
		}
		else {
			newTableBoxColumn->SetDefaultMetatype(eValueTypes::TYPE_STRING);
		}
		newTableBoxColumn->SetCaption(columnInfo->GetColumnCaption());
		newTableBoxColumn->m_dataSource = GetGuidByID(columnInfo->GetColumnID());
		newTableBoxColumn->SetWidthColumn(columnInfo->GetColumnWidth());
		if (visualDocument != NULL) {
			CVisualHost* m_visualView = visualDocument->GetVisualView();
			wxASSERT(m_visualView);
			m_visualView->CreateControl(newTableBoxColumn, this);
		}
	}

	if (visualDocument != NULL) {
		CVisualHost* visualView = visualDocument->GetVisualView();
		wxASSERT(visualView);
		//fix size in parent window 
		wxWindow* wndParent = visualView->GetParent();
		if (wndParent != NULL) {
			wndParent->Layout();
		}
	}
}

void CValueTableBox::CreateTable()
{
	if (m_tableModel == NULL) {
		IValueModel* tableValue =
			IAttributeControl::CreateAndConvertValueRef<IValueModel>();
		wxASSERT(tableValue);
		for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
			CValueTableBoxColumn* columnTable = wxDynamicCast(
				GetChild(idx), CValueTableBoxColumn
			);
			if (columnTable != NULL) {
				IValueModel::IValueTableColumnCollection* cols =
					tableValue->GetColumns();
				wxASSERT(cols);
				cols->AddColumn(columnTable->GetControlName(),
					columnTable->GetValueTypeDescription(),
					columnTable->GetCaption(),
					columnTable->GetWidthColumn()
				);
			}
		}
		m_tableModel = tableValue;
		m_tableModel->IncrRef();
	}
}

#include "metadata/metaObjects/objects/reference/reference.h"
#include "frontend/visualView/controls/form.h"

void CValueTableBox::CreateModel()
{
	ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
	if (m_dataSource.isValid()) {
		if (srcObject != NULL) {
			IValueModel* tableModel = NULL;
			if (srcObject->GetModel(tableModel, GetIdByGuid(m_dataSource))) {
				if (tableModel != m_tableModel) {
					if (m_tableModel != NULL) {
						m_tableModel->DecrRef();
						m_tableModel = NULL;
					}
					m_tableModel = tableModel;
					m_tableModel->IncrRef();
				}
			}
		}
	}
	
	CreateTable();
	
	if (!m_refreshModel && m_tableModel != NULL && !appData->DesignerMode()) {
		m_tableModel->RefreshModel();
	}
	
	IValueFrame* ownerControl = m_formOwner->GetOwnerControl();
	if (ownerControl != NULL) {
		CReferenceDataObject* refValue = NULL;
		const CValue& selValue = ownerControl->GetControlValue();
		if (selValue.ConvertToValue(refValue)) {
			wxDataViewItem currLine = m_tableModel->GetLineByGuid(refValue->GetGuid());
			if (currLine.IsOk()) {
				if (m_tableCurrentLine != NULL) {
					m_tableCurrentLine->DecrRef();
				}
				m_tableCurrentLine = m_tableModel->GetRowAt(currLine);
				m_tableCurrentLine->IncrRef();
			}
		}
	}
}

void CValueTableBox::RefreshModel()
{
	CValueTableBox::CreateModel();
}

////////////////////////////////////////////////////////////////////////////////////

ISourceDataObject* CValueTableBox::GetSourceObject() const
{
	return m_formOwner ? m_formOwner->GetSourceObject()
		: NULL;
}

bool CValueTableBox::FilterSource(const CSourceExplorer& src, const meta_identifier_t& id)
{
	return src.IsTableSection();
}

//***********************************************************************************
//*                              CValueTableBox                                     *
//***********************************************************************************

CValueTableBox::CValueTableBox() : IValueWindow(), IAttributeControl(g_valueTableCLSID),
m_tableModel(NULL), m_tableCurrentLine(NULL), m_refreshModel(false)
{
	//set default params
	*m_propertyMinSize = wxSize(300, 100);
	*m_propertyBG = wxColour(255, 255, 255);
}

CValueTableBox::~CValueTableBox()
{
	if (m_tableModel != NULL) {
		m_tableModel->DecrRef();
	}

	if (m_tableCurrentLine != NULL) {
		m_tableCurrentLine->DecrRef();
	}
}

/////////////////////////////////////////////////////////////////////////////////////

wxObject* CValueTableBox::Create(wxObject* parent, IVisualHost* visualHost)
{
	CDataViewCtrl* tableCtrl = new CDataViewCtrl((wxWindow*)parent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		wxDV_SINGLE | wxDV_HORIZ_RULES | wxDV_VERT_RULES | wxDV_ROW_LINES | wxDV_VARIABLE_LINE_HEIGHT | wxBORDER_SIMPLE);

	if (!visualHost->IsDemonstration()) {
		tableCtrl->Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, &CValueTableBox::OnColumnClick, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_COLUMN_REORDERED, &CValueTableBox::OnColumnReordered, this);

		//system events:
		tableCtrl->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &CValueTableBox::OnSelectionChanged, this);

		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &CValueTableBox::OnItemActivated, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_COLLAPSED, &CValueTableBox::OnItemCollapsed, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_EXPANDED, &CValueTableBox::OnItemExpanded, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_COLLAPSING, &CValueTableBox::OnItemCollapsing, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_EXPANDING, &CValueTableBox::OnItemExpanding, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_START_EDITING, &CValueTableBox::OnItemStartEditing, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_EDITING_STARTED, &CValueTableBox::OnItemEditingStarted, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_EDITING_DONE, &CValueTableBox::OnItemEditingDone, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &CValueTableBox::OnItemValueChanged, this);

#if wxUSE_DRAG_AND_DROP 
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, &CValueTableBox::OnItemBeginDrag, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, &CValueTableBox::OnItemDropPossible, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_DROP, &CValueTableBox::OnItemDrop, this);
#endif // wxUSE_DRAG_AND_DROP

#if wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
		tableCtrl->EnableDragSource(wxDF_UNICODETEXT);
		tableCtrl->EnableDropTarget(wxDF_UNICODETEXT);
#endif // wxUSE_DRAG_AND_DROP && wxUSE_UNICODE

		tableCtrl->Bind(wxEVT_MENU, &CValueTableBox::OnCommandMenu, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &CValueTableBox::OnContextMenu, this);
	}

	return tableCtrl;
}

void CValueTableBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
	CDataViewCtrl* tableCtrl = dynamic_cast<CDataViewCtrl*>(wxobject);

	if (visualHost->IsDesignerHost() && GetChildCount() == 0
		&& first—reated) {
		CValueTableBox::AddColumn();
	}

	if (tableCtrl != NULL) {

		CValueTableBox::CreateModel();

		if (m_tableModel != tableCtrl->GetModel()) {
			tableCtrl->AssociateModel(m_tableModel);
			if (m_tableModel != NULL && m_tableModel->AutoCreateColumns()) {
				CreateColumns(tableCtrl);
			}
		}

		if (m_tableModel != NULL && m_tableModel->IsEmpty()) {
			if (m_tableCurrentLine != NULL) {
				m_tableCurrentLine->DecrRef();
				m_tableCurrentLine = NULL;
			}
		}

		if (m_tableCurrentLine != NULL) {
			tableCtrl->Select(
				m_tableCurrentLine->GetLineTableItem()
			);
			//tableCtrl->SetFocus();
		}
	}
}

void CValueTableBox::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	CDataViewCtrl* tableCtrl = dynamic_cast<CDataViewCtrl*>(wxobject);

	if (tableCtrl != NULL) {

		bool needRefresh = false;

		if (m_refreshModel && m_tableModel != NULL && !appData->DesignerMode()) {
			needRefresh = true;
		}
		else if (!m_refreshModel && !appData->DesignerMode()) {
			m_refreshModel = true;
		}

		CValueTableBox::RefreshModel();

		if (m_tableModel != tableCtrl->GetModel()) {
			tableCtrl->AssociateModel(m_tableModel);
			if (m_tableModel && m_tableModel->AutoCreateColumns()) {
				CreateColumns(tableCtrl);
			}
		}

		if (needRefresh) {
			m_tableModel->RefreshModel();
			if (!m_tableModel->IsEmpty() ||
				(m_tableCurrentLine != NULL && 
					m_tableCurrentLine->GetLineTable() > m_tableModel->GetCount())) {
				if (m_tableCurrentLine != NULL) {
					m_tableCurrentLine->DecrRef();
					m_tableCurrentLine = NULL;
				}
			}
		}

		if (m_tableCurrentLine != NULL) {
			tableCtrl->Select(
				m_tableCurrentLine->GetLineTableItem()
			);
			//tableCtrl->SetFocus();
		}
	}

	UpdateWindow(tableCtrl);
}

void CValueTableBox::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost)
{
	CDataViewCtrl* tableCtrl = dynamic_cast<CDataViewCtrl*>(wxobject);
}

void CValueTableBox::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
	CDataViewCtrl* tableCtrl = dynamic_cast<CDataViewCtrl*>(obj);

	if (tableCtrl != NULL) {
		tableCtrl->AssociateModel(NULL);
	}
}

//***********************************************************************************
//*                                  Property                                       *
//***********************************************************************************

bool CValueTableBox::LoadData(CMemoryReader& reader)
{
	if (!IAttributeControl::LoadTypeData(reader))
		return false;

	return IValueWindow::LoadData(reader);
}

bool CValueTableBox::SaveData(CMemoryWriter& writer)
{
	if (!IAttributeControl::SaveTypeData(writer))
		return false;

	return IValueWindow::SaveData(writer);
}