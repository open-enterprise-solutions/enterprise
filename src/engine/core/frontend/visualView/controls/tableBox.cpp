#include "tableBox.h"
#include "frontend/visualView/visualEditor.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueTableBox, IValueWindow);

//***********************************************************************************
//*                                 Special tablebox func                           *
//***********************************************************************************

bool CValueTableBox::GetControlValue(CValue& pvarControlVal) const
{
	if (!m_tableModel)
		return false;
	pvarControlVal = m_tableModel;
	return true;
}

bool CValueTableBox::SetControlValue(const CValue& varControlVal)
{
	IValueModel* tableModel =
		varControlVal.ConvertToType<IValueModel>();

	if (m_tableModel) {
		m_tableModel->DecrRef();
		m_tableModel = NULL;
	}

	if (tableModel != NULL) {
		m_tableModel = tableModel;
		m_tableModel->IncrRef();
	}

	return true;
}

void CValueTableBox::AddColumn()
{
	wxASSERT(m_formOwner);

	IValueFrame* newTableBoxColumn = m_formOwner->NewObject(g_controlTableBoxColumnCLSID, this);
	g_visualHostContext->InsertControl(newTableBoxColumn, this);
	g_visualHostContext->RefreshEditor();
}

#include "core/compiler/valueType.h"
#include "core/compiler/valueTable.h"

#include "core/metadata/metaObjectsDefines.h"
#include "core/metadata/metaObjects/objects/object.h"

#include "frontend/visualView/visualHost.h"

#include "appData.h"

void CValueTableBox::CreateColumnCollection(wxDataViewCtrl* tableCtrl)
{
	if (appData->DesignerMode())
		return;
	wxDataViewCtrl* tc = tableCtrl ?
		tableCtrl : dynamic_cast<wxDataViewCtrl*>(GetWxObject());
	wxASSERT(tc);
	CVisualDocument* visualDocument = m_formOwner->GetVisualDocument();
	//clear all controls 
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		IValueFrame* childColumn = GetChild(idx);
		wxASSERT(childColumn);
		if (visualDocument != NULL) {
			CVisualHost* visualView = visualDocument->GetVisualView();
			wxASSERT(visualView);
			visualView->RemoveControl(childColumn, this);
		}
		childColumn->SetParent(NULL);
		childColumn->DecrRef();
	}
	//clear all children
	RemoveAllChildren();
	//clear all old columns
	tc->ClearColumns();
	//create new columns
	IValueModel::IValueModelColumnCollection* tableColumns = m_tableModel->GetColumnCollection();
	wxASSERT(tableColumns);
	for (unsigned int idx = 0; idx < tableColumns->GetColumnCount(); idx++) {

		IValueModel::IValueModelColumnCollection::IValueModelColumnInfo* columnInfo = tableColumns->GetColumnInfo(idx);
		CValueTableBoxColumn* newTableBoxColumn =
			m_formOwner->NewObject<CValueTableBoxColumn>(g_controlTableBoxColumnCLSID, this);

		const typeDescription_t& typeDescription = columnInfo->GetColumnType();
		if (typeDescription.IsOk())
			newTableBoxColumn->SetDefaultMetatype(typeDescription);
		else
			newTableBoxColumn->SetDefaultMetatype(eValueTypes::TYPE_STRING);

		newTableBoxColumn->SetCaption(columnInfo->GetColumnCaption());
		newTableBoxColumn->SetWidthColumn(columnInfo->GetColumnWidth());

		newTableBoxColumn->m_dataSource = GetGuidByID(columnInfo->GetColumnID());

		if (visualDocument != NULL) {
			CVisualHost* visualView = visualDocument->GetVisualView();
			wxASSERT(visualView);
			visualView->CreateControl(newTableBoxColumn, this);
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
		IValueModel* modelValue =
			ITypeControl::CreateAndConvertValueRef<IValueModel>();
		wxASSERT(modelValue);
		for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
			CValueTableBoxColumn* columnTable = wxDynamicCast(
				GetChild(idx), CValueTableBoxColumn
			);
			if (columnTable != NULL) {
				IValueModel::IValueModelColumnCollection* cols =
					modelValue->GetColumnCollection();
				wxASSERT(cols);
				IValueModel::IValueModelColumnCollection::IValueModelColumnInfo* colInfo = cols->AddColumn(
					columnTable->GetControlName(),
					columnTable->GetTypeDescription(),
					columnTable->GetCaption(),
					columnTable->GetWidthColumn()
				);
				if (colInfo != NULL) {
					colInfo->SetColumnID(columnTable->GetControlID());
				}
			}
		}

		m_tableModel = modelValue;
		m_tableModel->IncrRef();
	}
}

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

	if (!m_refreshModel && m_tableModel != NULL
		&& !appData->DesignerMode()) {
		m_tableModel->RefreshModel();
	}

	if (m_tableModel != NULL) {
		IValueFrame* ownerControl = m_formOwner->GetOwnerControl();
		if (ownerControl != NULL) {
			CValue retValue; ownerControl->GetControlValue(retValue);
			const wxDataViewItem& currLine = m_tableModel->FindRowValue(retValue);
			if (currLine.IsOk()) {
				if (m_tableCurrentLine != NULL)
					m_tableCurrentLine->DecrRef();
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

ISourceObject* CValueTableBox::GetSourceObject() const
{
	return m_formOwner ? m_formOwner->GetSourceObject()
		: NULL;
}

#include "core/metadata/metadata.h"
#include "core/metadata/singleClass.h"

IMetaObjectWrapperData* CValueTableBox::GetMetaObject() const
{
	if (!m_dataSource.isValid()) {
		IMetadata* metaData = GetMetaData();
		wxASSERT(metaData);
		IMetaTypeObjectValueSingle* singleObject = metaData->GetTypeObject(GetFirstClsid());
		wxASSERT(singleObject);
		if (singleObject != NULL)
			return dynamic_cast<IMetaObjectWrapperData*>(singleObject->GetMetaObject());
		return NULL;
	}
	ISourceObject* srcObject = GetSourceObject();
	wxASSERT(srcObject);
	return srcObject ? srcObject->GetMetaObject()
		: NULL;
}

CLASS_ID CValueTableBox::GetTypeClass() const
{
	if (!m_dataSource.isValid()) {
		IMetadata* metaData = GetMetaData();
		wxASSERT(metaData);
		IMetaTypeObjectValueSingle* singleObject = metaData->GetTypeObject(GetFirstClsid());
		wxASSERT(singleObject);
		if (singleObject != NULL)
			singleObject->GetTypeClass();
		return 0;
	}
	ISourceObject* srcObject = GetSourceObject();
	wxASSERT(srcObject);
	return srcObject ? srcObject->GetTypeClass()
		: 0;
}

bool CValueTableBox::FilterSource(const CSourceExplorer& src, const meta_identifier_t& id)
{
	return src.IsTableSection();
}

//***********************************************************************************
//*                              CValueTableBox                                     *
//***********************************************************************************

CValueTableBox::CValueTableBox() : IValueWindow(), ITypeControl(g_valueTableCLSID),
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

wxObject* CValueTableBox::Create(wxWindow* wxparent, IVisualHost* visualHost)
{
	wxDataModelViewCtrl* tableCtrl = new wxDataModelViewCtrl(wxparent, wxID_ANY,
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

		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_START_DELETING, &CValueTableBox::OnItemStartDeleting, this);

#if wxUSE_DRAG_AND_DROP 
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, &CValueTableBox::OnItemBeginDrag, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, &CValueTableBox::OnItemDropPossible, this);
		tableCtrl->Bind(wxEVT_DATAVIEW_ITEM_DROP, &CValueTableBox::OnItemDrop, this);
#endif // wxUSE_DRAG_AND_DROP

		tableCtrl->GenericGetHeader()->Bind(wxEVT_HEADER_RESIZING, &CValueTableBox::OnHeaderResizing, this);

		tableCtrl->Bind(wxEVT_SCROLLWIN_TOP, &CValueTableBox::HandleOnScroll, this);
		tableCtrl->Bind(wxEVT_SCROLLWIN_BOTTOM, &CValueTableBox::HandleOnScroll, this);
		tableCtrl->Bind(wxEVT_SCROLLWIN_LINEUP, &CValueTableBox::HandleOnScroll, this);
		tableCtrl->Bind(wxEVT_SCROLLWIN_LINEDOWN, &CValueTableBox::HandleOnScroll, this);
		tableCtrl->Bind(wxEVT_SCROLLWIN_PAGEUP, &CValueTableBox::HandleOnScroll, this);
		tableCtrl->Bind(wxEVT_SCROLLWIN_PAGEDOWN, &CValueTableBox::HandleOnScroll, this);

		tableCtrl->GetMainWindow()->Bind(wxEVT_LEFT_DOWN, &CValueTableBox::OnMainWindowClick, this);

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
	wxDataModelViewCtrl* tableCtrl = dynamic_cast<wxDataModelViewCtrl*>(wxobject);

	if (visualHost->IsDesignerHost() && GetChildCount() == 0
		&& first—reated) {
		CValueTableBox::AddColumn();
	}

	if (tableCtrl != NULL) {

		CValueTableBox::CreateModel();

		if (m_tableModel != tableCtrl->GetModel()) {
			tableCtrl->AssociateModel(m_tableModel);
		}

		if (m_tableCurrentLine != NULL) {
			tableCtrl->Select(
				m_tableCurrentLine->GetLineItem()
			);
		}
	}
}

#include <wx/itemattr.h>

void CValueTableBox::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	wxDataModelViewCtrl* tableCtrl = dynamic_cast<wxDataModelViewCtrl*>(wxobject);

	if (tableCtrl != NULL) {

		bool needRefresh = false;
		if (m_refreshModel && m_tableModel != NULL
			&& !appData->DesignerMode()) {
			needRefresh = true;
		}
		else if (!m_refreshModel
			&& !appData->DesignerMode()) {
			m_refreshModel = true;
		}

		CValueTableBox::RefreshModel();

		if (m_tableModel != tableCtrl->GetModel()) {
			tableCtrl->AssociateModel(m_tableModel);
		}

		if (needRefresh) {

			m_tableModel->RefreshModel(
				tableCtrl->GetTopItem(),
				tableCtrl->GetCountPerPage()
			);

			if (m_tableCurrentLine != NULL && !m_tableModel->ValidateReturnLine(m_tableCurrentLine)) {
				const wxDataViewItem& currLine = m_tableModel->FindRowValue(m_tableCurrentLine);
				m_tableCurrentLine->DecrRef();
				m_tableCurrentLine = NULL;
				if (currLine.IsOk()) {
					m_tableCurrentLine = m_tableModel->GetRowAt(currLine);
					m_tableCurrentLine->IncrRef();
				}
			}
		}

		if (m_tableCurrentLine != NULL) {
			tableCtrl->Select(
				m_tableCurrentLine->GetLineItem()
			);
		}
	}

	UpdateWindow(tableCtrl);

	if (tableCtrl != NULL) {
		wxItemAttr attr(
			tableCtrl->GetForegroundColour(),
			tableCtrl->GetBackgroundColour(),
			tableCtrl->GetFont()
		);
		tableCtrl->SetHeaderAttr(attr);
	}
}

void CValueTableBox::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost)
{
	wxDataModelViewCtrl* tableCtrl = dynamic_cast<wxDataModelViewCtrl*>(wxobject);

	//if (m_tableModel != NULL)
	//	m_tableModel->Resort();
}

void CValueTableBox::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
	wxDataModelViewCtrl* tableCtrl = dynamic_cast<wxDataModelViewCtrl*>(obj);
	if (tableCtrl != NULL)
		tableCtrl->AssociateModel(NULL);
}

//***********************************************************************************
//*                                  Property                                       *
//***********************************************************************************

bool CValueTableBox::LoadData(CMemoryReader& reader)
{
	if (!ITypeControl::LoadTypeData(reader))
		return false;

	return IValueWindow::LoadData(reader);
}

bool CValueTableBox::SaveData(CMemoryWriter& writer)
{
	if (!ITypeControl::SaveTypeData(writer))
		return false;

	return IValueWindow::SaveData(writer);
}

//***********************************************************************************

enum prop {
	eTableValue,
	eCurrentRow,
};

void CValueTableBox::PrepareNames() const
{
	IValueFrame::PrepareNames();

	m_methodHelper->AppendProp("tableValue", eTableValue, eControl);
	m_methodHelper->AppendProp("currentRow", eCurrentRow, eControl);
}

bool CValueTableBox::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum); bool refreshColumn = false;
	if (lPropAlias == eControl) {
		const long lPropData = m_methodHelper->GetPropData(lPropNum);
		if (lPropData == eTableValue) {
			refreshColumn = true;
			if (m_tableCurrentLine != NULL)
				m_tableCurrentLine->DecrRef();
			m_tableCurrentLine = NULL;
			if (m_tableModel != NULL)
				m_tableModel->DecrRef();
			m_tableModel = varPropVal.ConvertToType<IValueTable>();
			m_tableModel->IncrRef();
		}
		else if (lPropData == eCurrentRow) {
			if (m_tableCurrentLine != NULL)
				m_tableCurrentLine->DecrRef();
			m_tableCurrentLine = NULL;
			IValueTable::IValueModelReturnLine* tableReturnLine = NULL;
			if (varPropVal.ConvertToValue(tableReturnLine)) {
				if (m_tableModel == tableReturnLine->GetOwnerModel()) {
					m_tableCurrentLine = tableReturnLine;
					m_tableCurrentLine->IncrRef();
				}
			}
		}
	}

	bool result = IValueFrame::SetPropVal(lPropNum, varPropVal);

	if (refreshColumn && m_tableModel != NULL && m_tableModel->AutoCreateColumn()) {
		CValueTableBox::CreateColumnCollection();
	}

	return result;
}

bool CValueTableBox::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_methodHelper->GetPropData(lPropNum);
		if (lPropData == eTableValue) {
			pvarPropVal = m_tableModel;
			return true;
		}
		else if (lPropData == eCurrentRow) {
			pvarPropVal = m_tableCurrentLine;
			return true;
		}
	}
	return IValueFrame::GetPropVal(lPropNum, pvarPropVal);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_VALUE_REGISTER(CValueTableBox, "tablebox", "tablebox", g_controlTableBoxCLSID);