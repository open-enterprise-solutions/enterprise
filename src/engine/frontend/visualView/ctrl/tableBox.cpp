#include "tableBox.h"
#include "tableBoxColumnRenderer.h"

#include "form.h"

#include "frontend/visualView/visualHostClient.h"
#include "backend/system/value/valueTable.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/appData.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(ibValueModelTableBox, ibValueWindow);

wxIMPLEMENT_DYNAMIC_CLASS(ibValueEnumTableBoxSelectionMode, ibValue);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueEnumTableBoxViewMode, ibValue);

//***********************************************************************************
//*                                 Special tablebox func                           *
//***********************************************************************************

bool ibValueModelTableBox::GetControlValue(ibValue& pvarControlVal) const
{
	if (m_tableModel == nullptr) {
		if (appData->DesignerMode()) {
			if (!m_propertySource->IsEmptyProperty()) {
				ibSourceDataObject* srcObject = m_formOwner->GetSourceObject();
				if (srcObject != nullptr) {
					ibValueModel* tableModel = nullptr;
					if (srcObject->GetModel(tableModel, m_propertySource->GetValueAsSource())) {
						if (tableModel != m_tableModel) {
							pvarControlVal = tableModel;
							return true;
						}
					}
				}
			}
		}
		return false;
	}
	pvarControlVal = m_tableModel;
	return true;
}

bool ibValueModelTableBox::SetControlValue(const ibValue& varControlVal)
{
	m_tableModel = varControlVal.ConvertToType<ibValueModel>();
	return true;
}

void ibValueModelTableBox::AddColumn()
{
	wxASSERT(m_formOwner);

	ibValueModelTableBoxColumn* columnTable = wxDynamicCast(m_formOwner->NewObject(g_controlTableBoxColumnCLSID, this), ibValueModelTableBoxColumn);
	g_visualHostContext->InsertControl(columnTable, this);
	if (m_tableModel != nullptr) {
		ibValueModel::ibValueModelColumnCollection* columnData = m_tableModel->GetColumnCollection();
		wxASSERT(columnData);
		ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* column_info = columnData->AddColumn(
			columnTable->GetControlName(),
			columnTable->GetTypeDesc(),
			columnTable->GetCaption(),
			columnTable->GetWidthColumn()
		);
		if (column_info != nullptr) column_info->SetColumnID(columnTable->GetControlID());
	}

	g_visualHostContext->RefreshEditor();
}

void ibValueModelTableBox::CreateColumnCollection(ibDataViewCtrl* dataViewCtrl)
{
	if (appData->DesignerMode())
		return;

	ibDataViewCtrl* tc = dataViewCtrl ?
		dataViewCtrl : dynamic_cast<ibDataViewCtrl*>(GetWxObject());
	wxASSERT(tc);

	ibFormVisualDocument* visualDocument = m_formOwner->GetVisualDocument();
	//clear all controls 
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibValueFrame* childColumn = GetChild(idx);
		wxASSERT(childColumn);
		if (visualDocument != nullptr) {
			ibVisualHostClient* visualView = visualDocument->GetFirstView() ?
				visualDocument->GetFirstView()->GetVisualHost() : nullptr;
			wxASSERT(visualView);
			visualView->RemoveControl(childColumn, this);
		}

		RemoveChild(childColumn);

		childColumn->SetParent(nullptr);
		childColumn->DecrRef();
	}

	//clear all children
	RemoveAllChildren();

	//clear all old columns
	tc->ClearColumns();

	//create new columns
	ibValueModel::ibValueModelColumnCollection* tableColumns = m_tableModel->GetColumnCollection();
	wxASSERT(tableColumns);
	for (unsigned int idx = 0; idx < tableColumns->GetColumnCount(); idx++) {

		ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* columnInfo = tableColumns->GetColumnInfo(idx);
		ibValueModelTableBoxColumn* newTableBoxColumn =
			m_formOwner->NewObject<ibValueModelTableBoxColumn>(g_controlTableBoxColumnCLSID, this);

		const ibTypeDescription& typeDescription = columnInfo->GetColumnType();
		if (typeDescription.IsOk())
			newTableBoxColumn->SetDefaultMetaType(typeDescription);
		else
			newTableBoxColumn->SetDefaultMetaType(ibValueTypes::TYPE_STRING);

		newTableBoxColumn->SetCaption(columnInfo->GetColumnCaption());
		newTableBoxColumn->SetWidthColumn(columnInfo->GetColumnWidth());
		newTableBoxColumn->SetModelColumn(columnInfo->GetColumnID());

		if (visualDocument != nullptr) {

			ibVisualHostClient* visualView = visualDocument->GetFirstView() ?
				visualDocument->GetFirstView()->GetVisualHost() : nullptr;

			wxASSERT(visualView);
			visualView->CreateControl(newTableBoxColumn, this);
		}
	}

	if (visualDocument != nullptr) {

		ibVisualHostClient* visualView = visualDocument->GetFirstView() ?
			visualDocument->GetFirstView()->GetVisualHost() : nullptr;

		wxASSERT(visualView);
		//fix size in parent window 
		wxWindow* wndParent = visualView->GetParent();
		if (wndParent != nullptr) {
			wndParent->Layout();
		}
	}
}

void ibValueModelTableBox::CreateTable(bool recreateModel) {

	if (recreateModel && m_tableModel != nullptr) m_tableModel = nullptr;

	if (m_tableModel == nullptr) {

		m_tableModel = ibTypeControlFactory::CreateAndConvertValueRef<ibValueModel>();

		if (m_tableModel != nullptr) {
			for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
				ibValueModelTableBoxColumn* columnTable = wxDynamicCast(GetChild(idx), ibValueModelTableBoxColumn);
				if (columnTable != nullptr) {
					ibValueModel::ibValueModelColumnCollection* columnData = m_tableModel->GetColumnCollection();
					wxASSERT(columnData);
					ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* column_info = columnData->AddColumn(
						columnTable->GetControlName(),
						columnTable->GetTypeDesc(),
						columnTable->GetCaption(),
						columnTable->GetWidthColumn()
					);

					if (column_info != nullptr) column_info->SetColumnID(columnTable->GetControlID());
				}
			}
		}
	}
}

void ibValueModelTableBox::CreateModel(bool recreateModel)
{
	if (!m_propertySource->IsEmptyProperty()) {
		ibSourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject != nullptr) {
			ibValueModel* tableModel = nullptr;
			if (srcObject->GetModel(tableModel, m_propertySource->GetValueAsSource())) {
				if (tableModel != m_tableModel) m_tableModel = tableModel;
			}
		}
		CreateTable(false);
	}
	else if (m_tableModel != nullptr && m_propertySource->GetValueAsTypeDesc() != m_tableModel->GetSourceClassType()) {
		CreateTable(true);
	}
	else {
		CreateTable(recreateModel);
	}
}

void ibValueModelTableBox::RefreshModel(bool recreateModel)
{
	ibValueModelTableBox::CreateModel(recreateModel);
}

////////////////////////////////////////////////////////////////////////////////////

ibSourceObject* ibValueModelTableBox::GetSourceObject() const
{
	return m_formOwner ? m_formOwner->GetSourceObject() : nullptr;
}

ibValueMetaObjectCompositeData* ibValueModelTableBox::GetSourceMetaObject() const
{
	wxASSERT(m_tableModel);
	if (m_tableModel == nullptr) return nullptr;
	return m_tableModel->GetSourceMetaObject();
}

ibClassID ibValueModelTableBox::GetSourceClassType() const
{
	wxASSERT(m_tableModel);
	if (m_tableModel == nullptr) return 0;
	return m_tableModel->GetSourceClassType();
}

bool ibValueModelTableBox::FilterSource(const ibSourceExplorer& src, const ibMetaID& id) const
{
	return src.IsTableSection();
}

//***********************************************************************************
//*                              ibValueModelTableBox                                     *
//***********************************************************************************

ibValueModelTableBox::ibValueModelTableBox() : ibValueWindow(), ibTypeControlFactory(),
m_tableModel(nullptr), m_tableCurrentLine(nullptr),
m_dataViewCreated(false), m_dataViewSelected(false), m_dataViewUpdated(false), m_dataViewSizeChanged(false),
m_need_calculate_pos(false)
{
	m_propertySource->SetValue(ibTypeDescription(g_valueTableCLSID));

	//set default params
	m_propertyMinSize->SetValue(wxSize(150, 75));
	m_propertyBG->SetValue(wxColour(255, 255, 255));
}

/////////////////////////////////////////////////////////////////////////////////////

void ibValueModelTableBox::CalculateColumnPos()
{
	ibTableViewCtrl* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(GetWxObject());
	if (dataViewCtrl != nullptr) {

		dataViewCtrl->SetExpanderColumn(nullptr);

		ibHeaderGenericCtrl* headerCtrl = dataViewCtrl->GenericGetHeader();
		if (headerCtrl != nullptr) {

			bool need_reset_columns_order = false;

			for (unsigned int idx = 0; idx < GetChildCount(); idx++) {

				const ibValueFrame* valueFrame = GetChild(idx);
				wxASSERT(valueFrame);

				ibDataViewColumn* column = dynamic_cast<ibDataViewColumn*>(valueFrame->GetWxObject());

				const unsigned int column_model_index = dataViewCtrl->GetColumnIndex(column);
				const unsigned int column_index = headerCtrl->GetColumnPos(column_model_index);

				const unsigned int real_column_index = valueFrame->GetParentPosition();

				if (column_index != real_column_index) {
					dataViewCtrl->DeleteColumn(column);
					dataViewCtrl->InsertColumn(real_column_index, column);
					need_reset_columns_order = true;
				}

				if (column->IsShown() && dataViewCtrl->GetExpanderColumn() == nullptr) {
					dataViewCtrl->SetExpanderColumn(column);
				}
			}

			if (need_reset_columns_order) headerCtrl->ResetColumnsOrder();
		}
		else
		{
			for (unsigned int idx = 0; idx < GetChildCount(); idx++) {

				const ibValueFrame* valueFrame = GetChild(idx);
				wxASSERT(valueFrame);

				ibDataViewColumn* column = dynamic_cast<ibDataViewColumn*>(valueFrame->GetWxObject());

				const unsigned int column_model_index = dataViewCtrl->GetColumnIndex(column);
				const unsigned int real_column_index = valueFrame->GetParentPosition();

				if (column_model_index != real_column_index) {
					dataViewCtrl->DeleteColumn(column);
					dataViewCtrl->InsertColumn(real_column_index, column);
				}

				if (column->IsShown() && dataViewCtrl->GetExpanderColumn() == nullptr) {
					dataViewCtrl->SetExpanderColumn(column);
				}
			}
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////

wxObject* ibValueModelTableBox::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibTableViewCtrl* dataViewCtrl = new ibTableViewCtrl(wxparent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		wxDV_SINGLE | wxDV_HORIZ_RULES | wxDV_VERT_RULES | wxDV_ROW_LINES | wxDV_VARIABLE_LINE_HEIGHT | wxBORDER_SIMPLE);

	const ibFormVisualDocument* visualDoc = ibValueModelTableBox::GetVisualDocument();

	if (visualDoc == nullptr || (visualDoc != nullptr && !visualDoc->IsVisualDemonstrationDoc())) {

		dataViewCtrl->Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, &ibValueModelTableBox::OnColumnClick, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_COLUMN_REORDERED, &ibValueModelTableBox::OnColumnReordered, this);

		//system events:
		dataViewCtrl->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &ibValueModelTableBox::OnSelectionChanged, this);

		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &ibValueModelTableBox::OnItemActivated, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_COLLAPSED, &ibValueModelTableBox::OnItemCollapsed, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_EXPANDED, &ibValueModelTableBox::OnItemExpanded, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_COLLAPSING, &ibValueModelTableBox::OnItemCollapsing, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_EXPANDING, &ibValueModelTableBox::OnItemExpanding, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_START_EDITING, &ibValueModelTableBox::OnItemStartEditing, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_EDITING_STARTED, &ibValueModelTableBox::OnItemEditingStarted, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_EDITING_DONE, &ibValueModelTableBox::OnItemEditingDone, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &ibValueModelTableBox::OnItemValueChanged, this);

		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_START_INSERTING, &ibValueModelTableBox::OnItemStartInserting, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_START_DELETING, &ibValueModelTableBox::OnItemStartDeleting, this);

#if wxUSE_DRAG_AND_DROP 
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, &ibValueModelTableBox::OnItemBeginDrag, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, &ibValueModelTableBox::OnItemDropPossible, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_DROP, &ibValueModelTableBox::OnItemDrop, this);
#endif // wxUSE_DRAG_AND_DROP

		dataViewCtrl->Bind(wxEVT_DATAVIEW_VIEW_SET, &ibValueModelTableBox::OnViewSet, this);

		dataViewCtrl->GenericGetHeader()->Bind(wxEVT_HEADER_RESIZING, &ibValueModelTableBox::OnHeaderResizing, this);

		dataViewCtrl->Bind(wxEVT_SCROLLWIN_TOP, &ibValueModelTableBox::HandleOnScroll, this);
		dataViewCtrl->Bind(wxEVT_SCROLLWIN_BOTTOM, &ibValueModelTableBox::HandleOnScroll, this);
		dataViewCtrl->Bind(wxEVT_SCROLLWIN_LINEUP, &ibValueModelTableBox::HandleOnScroll, this);
		dataViewCtrl->Bind(wxEVT_SCROLLWIN_LINEDOWN, &ibValueModelTableBox::HandleOnScroll, this);
		dataViewCtrl->Bind(wxEVT_SCROLLWIN_PAGEUP, &ibValueModelTableBox::HandleOnScroll, this);
		dataViewCtrl->Bind(wxEVT_SCROLLWIN_PAGEDOWN, &ibValueModelTableBox::HandleOnScroll, this);

		dataViewCtrl->Bind(wxEVT_SCROLLWIN_THUMBTRACK, &ibValueModelTableBox::HandleOnScroll, this);
		dataViewCtrl->Bind(wxEVT_SCROLLWIN_THUMBRELEASE, &ibValueModelTableBox::HandleOnScroll, this);

		dataViewCtrl->GetMainWindow()->Bind(wxEVT_LEFT_DOWN, &ibValueModelTableBox::OnMainWindowClick, this);

#if wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
		dataViewCtrl->EnableDragSource(wxDF_UNICODETEXT);
		dataViewCtrl->EnableDropTarget(wxDF_UNICODETEXT);
#endif // wxUSE_DRAG_AND_DROP && wxUSE_UNICODE

		dataViewCtrl->Bind(wxEVT_SIZE, &ibValueModelTableBox::OnSize, this);
		dataViewCtrl->Bind(wxEVT_IDLE, &ibValueModelTableBox::OnIdle, this);

		dataViewCtrl->Bind(wxEVT_MENU, &ibValueModelTableBox::OnCommandMenu, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibValueModelTableBox::OnContextMenu, this);
	}

	return dataViewCtrl;
}

void ibValueModelTableBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
	ibTableViewCtrl* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(wxobject);

	if (dataViewCtrl != nullptr) ibValueModelTableBox::CreateModel();

	if (visualHost->IsDesignerHost() && GetChildCount() == 0
		&& firstСreated) {
		ibValueModelTableBox::AddColumn();
	}
}

#include <wx/itemattr.h>

void ibValueModelTableBox::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	ibTableViewCtrl* dataViewCtrl =
		dynamic_cast<ibTableViewCtrl*>(wxobject);

	if (dataViewCtrl != nullptr) {
		UpdateWindow(dataViewCtrl);
	}
}

void ibValueModelTableBox::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibTableViewCtrl* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(wxobject);

	if (dataViewCtrl != nullptr) {

		ibDataViewModel* dataViewOldModel = dataViewCtrl->GetModel();
		ibDataViewModel* dataViewNewModel = m_tableModel != nullptr ?
			m_tableModel->GetDataViewModel() : nullptr;

		if (dataViewNewModel != dataViewOldModel) {
			if (dataViewOldModel == nullptr) dataViewCtrl->SetFocus();
			dataViewCtrl->AssociateModel(dataViewNewModel);
			m_tableCurrentLine.Reset();
		}

		if (appData->DesignerMode()) {
			if (!visualHost->IsDesignerHost() || m_propertyHeader->GetValueAsBoolean()) {
				dataViewCtrl->ShowHeaderWindow(m_propertyHeader->GetValueAsBoolean());
				dataViewCtrl->SetHeaderHeight(m_propertyHeaderHeight->GetValueAsUInteger());
			}
			else {
				dataViewCtrl->ShowHeaderWindow(true);
				dataViewCtrl->SetHeaderHeight(1);
				dataViewCtrl->SetForegroundColour(*wxLIGHT_GREY);
			}
		}
		else {
			dataViewCtrl->ShowHeaderWindow(m_propertyHeader->GetValueAsBoolean());
			dataViewCtrl->SetHeaderHeight(m_propertyHeaderHeight->GetValueAsUInteger());
		}

		dataViewCtrl->ShowFooterWindow(m_propertyFooter->GetValueAsBoolean());
		dataViewCtrl->SetFooterHeight(m_propertyFooterHeight->GetValueAsUInteger());

		dataViewCtrl->FreezeTo(
			m_propertyFreezeRow->GetValueAsUInteger(), m_propertyFreezeCol->GetValueAsUInteger());

		dataViewCtrl->SetSelectionMode(m_propertyRowSelectionMode->GetValueAsEnum());
		dataViewCtrl->SetViewMode(m_propertyViewMode->GetValueAsEnum());

		wxItemAttr attr(
			dataViewCtrl->GetForegroundColour(),
			dataViewCtrl->GetBackgroundColour(),
			dataViewCtrl->GetFont()
		);

		dataViewCtrl->SetHeaderAttr(attr);

		if (!appData->DesignerMode())
			m_dataViewCreated = m_dataViewUpdated = true;
	}
}

void ibValueModelTableBox::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
	ibTableViewCtrl* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(obj);
	m_dataViewCreated = m_dataViewUpdated = false;
	if (dataViewCtrl != nullptr) dataViewCtrl->AssociateModel(nullptr);
	m_tableCurrentLine.Reset();
}

//***********************************************************************************
//*                                  Property                                       *
//***********************************************************************************

bool ibValueModelTableBox::LoadData(ibReaderMemory& reader)
{
	if (!m_propertySource->LoadData(reader))
		return false;

	m_propertyHeader->LoadData(reader);
	m_propertyHeaderHeight->LoadData(reader);
	m_propertyFooter->LoadData(reader);
	m_propertyFooterHeight->LoadData(reader);

	m_propertyFreezeRow->LoadData(reader);
	m_propertyFreezeCol->LoadData(reader);

	m_propertyRowSelectionMode->LoadData(reader);

	//events
	m_eventSelection->LoadData(reader);
	m_eventBeforeAddRow->LoadData(reader);
	m_eventBeforeDeleteRow->LoadData(reader);
	m_eventOnActivateRow->LoadData(reader);

	return ibValueWindow::LoadData(reader);
}

bool ibValueModelTableBox::SaveData(ibWriterMemory writer)
{
	if (!m_propertySource->SaveData(writer))
		return false;

	m_propertyHeader->SaveData(writer);
	m_propertyHeaderHeight->SaveData(writer);
	m_propertyFooter->SaveData(writer);
	m_propertyFooterHeight->SaveData(writer);

	m_propertyFreezeRow->SaveData(writer);
	m_propertyFreezeCol->SaveData(writer);

	m_propertyRowSelectionMode->SaveData(writer);

	//events
	m_eventSelection->SaveData(writer);
	m_eventBeforeAddRow->SaveData(writer);
	m_eventBeforeDeleteRow->SaveData(writer);
	m_eventOnActivateRow->SaveData(writer);

	return ibValueWindow::SaveData(writer);
}

//***********************************************************************************

enum prop {
	eTableValue,
	eCurrentRow,
};

ibMetaData* ibValueModelTableBox::GetMetaData() const
{
	return m_formOwner != nullptr ?
		m_formOwner->GetMetaData() : nullptr;
}

void ibValueModelTableBox::PrepareNames() const
{
	ibValueFrame::PrepareNames();

	m_methodHelper->AppendProp(wxT("Value"), eTableValue, eControl);
	m_methodHelper->AppendProp(wxT("CurrentRow"), eCurrentRow, eControl);
}

bool ibValueModelTableBox::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum); bool refreshColumn = false;
	if (lPropAlias == eControl) {
		const long lPropData = m_methodHelper->GetPropData(lPropNum);
		if (lPropData == eTableValue) {
			m_tableModel = varPropVal.ConvertToType<ibValueModelTableBase>();
			m_tableCurrentLine.Reset();
			refreshColumn = true;
		}
		else if (lPropData == eCurrentRow) {
			ibValueModelTableBase::ibValueModelReturnLine* tableReturnLine = nullptr;
			if (varPropVal.ConvertToValue(tableReturnLine)) {
				if (m_tableModel == tableReturnLine->GetOwnerModel())
					m_tableCurrentLine = tableReturnLine;
				else m_tableCurrentLine.Reset();
			}
			else {
				m_tableCurrentLine.Reset();
			}
		}
	}

	bool result = ibValueFrame::SetPropVal(lPropNum, varPropVal);

	if (refreshColumn && m_tableModel != nullptr && m_tableModel->AutoCreateColumn()) {
		ibValueModelTableBox::CreateColumnCollection();
	}

	return result;
}

bool ibValueModelTableBox::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
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
	return ibValueFrame::GetPropVal(lPropNum, pvarPropVal);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

ENUM_TYPE_REGISTER(ibValueEnumTableBoxSelectionMode, "TableboxRowSelectionMode", string_to_clsid("EN_TBXSL"));
ENUM_TYPE_REGISTER(ibValueEnumTableBoxViewMode, "TableboxViewMode", string_to_clsid("EN_TBXVM"));
CONTROL_TYPE_REGISTER(ibValueModelTableBox, "Tablebox", "Container", g_controlTableBoxCLSID);