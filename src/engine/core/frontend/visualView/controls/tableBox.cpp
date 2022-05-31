#include "tableBox.h"
#include "frontend/visualView/visualEditor.h"

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
	IValueTable* tableModel =
		vSelected.ConvertToType<IValueTable>();

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

	IValueControl* newTableBoxColumn =
		m_formOwner->NewObject(wxT("tableBoxColumn"), this);

	newTableBoxColumn->ReadProperty();

	m_visualHostContext->InsertObject(newTableBoxColumn, this);
	newTableBoxColumn->SaveProperty();

	m_visualHostContext->RefreshEditor();
}

#include "compiler/valueType.h"
#include "compiler/valueTable.h"

#include "metadata/metaObjectsDefines.h"
#include "metadata/metaObjects/objects/baseObject.h"

#include "frontend/visualView/visualHost.h"

#include "appData.h"

void CValueTableBox::CreateColumns(wxDataViewCtrl* tableCtrl)
{
	if (appData->DesignerMode()) {
		return;
	}

	wxDataViewCtrl* tc = tableCtrl ?
		tableCtrl : dynamic_cast<wxDataViewCtrl*>(GetWxObject());
	wxASSERT(tc);
	CVisualDocument* visualDocument = m_formOwner->GetVisualDocument();

	//clear all controls 
	for (unsigned int idx = 0; idx < GetChildCount(); idx++)
	{
		IValueFrame* childColumn = GetChild(idx);

		wxASSERT(childColumn);

		if (visualDocument) {
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
	IValueTable::IValueTableColumnCollection* tableColumns = m_tableModel->GetColumns();
	wxASSERT(tableColumns);
	for (unsigned int idx = 0; idx < tableColumns->GetColumnCount(); idx++)
	{
		IValueTable::IValueTableColumnCollection::IValueTableColumnInfo* columnInfo = tableColumns->GetColumnInfo(idx);

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

		newTableBoxColumn->m_caption = columnInfo->GetColumnCaption();
		newTableBoxColumn->m_dataSource = columnInfo->GetColumnID();
		newTableBoxColumn->m_width = columnInfo->GetColumnWidth();

		newTableBoxColumn->ReadProperty();

		if (visualDocument) {
			CVisualHost* m_visualView = visualDocument->GetVisualView();
			wxASSERT(m_visualView);
			m_visualView->CreateControl(newTableBoxColumn, this);
		}
	}

	if (visualDocument) {
		CVisualHost* visualView = visualDocument->GetVisualView();
		wxASSERT(visualView);
		//fix size in parent window 
		wxWindow* wndParent = visualView->GetParent();
		if (wndParent) {
			wndParent->Layout();
		}
	}
}

void CValueTableBox::CreateTable()
{
	if (m_tableModel == NULL) {
		IValueTable* tableValue =
			IAttributeControl::CreateAndConvertValueRef<IValueTable>();
		wxASSERT(tableValue);
		for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
			CValueTableBoxColumn* columnTable = wxDynamicCast(
				GetChild(idx), CValueTableBoxColumn
			);
			if (columnTable != NULL) {
				IValueTable::IValueTableColumnCollection* cols =
					tableValue->GetColumns();
				wxASSERT(cols);
				cols->AddColumn(columnTable->m_controlName,
					columnTable->GetValueTypeDescription(),
					columnTable->m_caption,
					columnTable->m_width
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
	if (m_dataSource != wxNOT_FOUND) {
		if (srcObject != NULL) {
			IValueTable* tableModel = NULL;
			if (srcObject->GetTable(tableModel, m_dataSource)) {
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
		CValue& selValue = ownerControl->GetControlValue();
		if (selValue.ConvertToValue(refValue)) {
			wxDataViewItem currLine = m_tableModel->GetLineByGuid(refValue->GetGuid());
			if (currLine.IsOk()) {
				if (m_tableCurrentLine) {
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

bool CValueTableBox::LoadFromVariant(const wxVariant& variant)
{
	wxVariantSourceData* srcData =
		dynamic_cast<wxVariantSourceData*>(variant.GetData());

	if (srcData && *srcData->GetAttributeData() != IAttributeControl::GetTypeDescription()) {
		for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
			CValueTableBoxColumn* column = dynamic_cast<CValueTableBoxColumn*>(GetChild(idx));
			if (column != NULL) {
				column->ResetSource();
			}
		}
	}

	return IAttributeControl::LoadFromVariant(variant);
}

void CValueTableBox::SaveToVariant(wxVariant& variant, IMetadata* metaData) const
{
	IAttributeControl::SaveToVariant(variant, metaData);
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
	PropertyContainer* categoryTable = IObjectBase::CreatePropertyContainer("TableBox");
	categoryTable->AddProperty("name", PropertyType::PT_WXNAME);
	m_category->AddCategory(categoryTable);

	PropertyContainer* categoryData = IObjectBase::CreatePropertyContainer("Data");
	categoryData->AddProperty("source", PropertyType::PT_SOURCE_DATA);
	m_category->AddCategory(categoryData);

	//event
	PropertyContainer* categoryEvent = IObjectBase::CreatePropertyContainer("Events");
	categoryEvent->AddEvent("selection", { {"control"}, {"rowSelected"}, {"standardProcessing"} }, _("On double mouse click or pressing of Enter."));
	categoryEvent->AddEvent("onActivateRow", { {"control"} }, _("When row is activated"));
	categoryEvent->AddEvent("beforeAddRow", { {"control"}, {"cancel"}, {"clone"} }, _("When row addition mode is called"));
	categoryEvent->AddEvent("beforeDeleteRow", { {"control"}, {"cancel"} }, _("When row deletion is called"));

	m_category->AddCategory(categoryEvent);

	//set default params
	m_minimum_size = wxSize(300, 100);
	m_bg = wxColour(255, 255, 255);
}

CValueTableBox::~CValueTableBox()
{
	if (m_tableModel) {
		m_tableModel->DecrRef();
	}

	if (m_tableCurrentLine) {
		m_tableCurrentLine->DecrRef();
	}
}

/////////////////////////////////////////////////////////////////////////////////////

wxObject* CValueTableBox::Create(wxObject* parent, IVisualHost* visualHost)
{
	CDataViewCtrl* tableCtrl = new CDataViewCtrl((wxWindow*)parent, wxID_ANY,
		m_pos,
		m_size,
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

		if (m_tableModel != NULL && m_tableModel->GetCount() == 0) {
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
			if (m_tableModel->GetCount() == 0) {
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

	if (tableCtrl) {
		tableCtrl->AssociateModel(NULL);
	}
}

//***********************************************************************************
//*                                  Property                                       *
//***********************************************************************************

bool CValueTableBox::LoadData(CMemoryReader& reader)
{
	if (!IAttributeControl::LoadData(reader))
		return false;

	return IValueWindow::LoadData(reader);
}

bool CValueTableBox::SaveData(CMemoryWriter& writer)
{
	if (!IAttributeControl::SaveData(writer))
		return false;

	return IValueWindow::SaveData(writer);
}

void CValueTableBox::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);

	SaveToVariant(
		GetPropertyAsVariant("source"), GetMetaData()
	);
}

void CValueTableBox::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);

	LoadFromVariant(
		GetPropertyAsVariant("source")
	);
}