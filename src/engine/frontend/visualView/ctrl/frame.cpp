////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control
////////////////////////////////////////////////////////////////////////////

#include "control.h"
#include "form.h"
#include "backend/compiler/procUnit.h"

wxIMPLEMENT_ABSTRACT_CLASS(ibValueFrame, ibValue);

//*************************************************************************
//*                          ValueControl		                          *
//*************************************************************************

ibValueFrame::ibValueFrame() : ibValue(ibValueTypes::TYPE_VALUE),
m_methodHelper(new ibValueMethodHelper()),
m_valEventContainer(ibValue::CreateAndPrepareValueRef<ibValueEventContainer>(this)),
m_controlId(0), m_controlGuid(ibGuid::newGuid())
{
}

ibValueFrame::~ibValueFrame()
{
	wxDELETE(m_methodHelper);
}

wxString ibValueFrame::GetClassName() const
{
	const ibClassID& clsid = GetClassType();
	if (clsid == 0)
		return _("Class not registered");

	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
	if (typeCtor != nullptr)
		return typeCtor->GetClassName();
	return _("Class not registered");
}

wxString ibValueFrame::GetObjectTypeName() const
{
	const ibCtorControlTypeBase* typeCtor =
		static_cast<const ibCtorControlTypeBase*>(ibValue::GetAvailableCtor(GetClassInfo()));

	if (typeCtor != nullptr)
		return typeCtor->GetTypeControlName();
	return _("Class not registered");
}

#define	headerBlock 0x012230
#define	dataBlock 0x012250
#define	childBlock 0x012270
#define	frameBlock 0x012290

bool ibValueFrame::IsEditable() const
{
	const ibValueForm* handler = GetOwnerForm();
	if (handler != nullptr)
		return handler->IsEditable();
	return false;
}

bool ibValueFrame::LoadControl(const ibValueMetaObjectFormBase* metaForm, ibReaderMemory& dataReader)
{
	//save meta version 
	const ibVersionID& version = dataReader.r_u32(); //reserved 

	//Load meta id
	m_controlId = dataReader.r_u32();

	//Load standart fields
	SetControlName(dataReader.r_stringZ());

	//default value 
	m_expanded = dataReader.r_u8();

	//load frame 
	wxMemoryBuffer buffer_chunk;
	if (!dataReader.r_chunk(frameBlock, buffer_chunk))
		return false;

	ibReaderMemory dataObjectReader(buffer_chunk);
	dataObjectReader.r_u32(); //reserved flags
	if (!LoadData(dataObjectReader))
		return false;
	
	return true;
}

bool ibValueFrame::SaveControl(const ibValueMetaObjectFormBase* metaForm, ibWriterMemory dataWritter, bool copy_form)
{
	//save meta version 
	dataWritter.w_u32(version_oes_last); //reserved 

	//save meta id 
	dataWritter.w_u32(m_controlId);

	//save standart fields
	dataWritter.w_stringZ(GetControlName());

	//default value 
	dataWritter.w_u8(m_expanded);

	//save frame 
	ibWriterMemory dataObjectWritter;
	dataObjectWritter.w_u32(0); //reserved flags
	if (!SaveData(dataObjectWritter))
		return false;

	dataWritter.w_chunk(frameBlock, dataObjectWritter.buffer());
	return true;
}

//*******************************************************************

bool ibValueFrame::Init()
{
	// always false
	return false;
}

bool ibValueFrame::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 2)
		return false;
	ibValueForm* ownerForm = nullptr;
	ibValueFrame* controlParent = nullptr;
	if (paParams[0]->ConvertToValue(ownerForm) &&
		paParams[1]->ConvertToValue(controlParent)) {
		if (controlParent != nullptr) {
			controlParent->AddChild(this);
			SetParent(controlParent);
		}
		SetOwnerForm(ownerForm);
		ownerForm->ResolveNameConflict(this);
		if (paParams[2]->GetBoolean()) {
			GenerateNewID();
		}
		return true;
	}
	return false;
}

//*******************************************************************

bool ibValueFrame::ChangeChildPosition(ibValueFrame* obj, unsigned int pos)
{
	OnChangeChildPosition(obj, pos);
	return ibPropertyObjectHelper::ChangeChildPosition(obj, pos);
}

//*******************************************************************

ibValueFrame* ibValueFrame::CreatePasteObject(const ibReaderMemory& reader,
	ibValueForm* dstForm, ibValueFrame* dstParent)
{
	std::shared_ptr <ibReaderMemory>readerHeaderMemory(reader.open_chunk(headerBlock));
	if (readerHeaderMemory == nullptr)
		return nullptr;

	const ibVersionID& version = readerHeaderMemory->r_s32();
	const ibClassID& clsid = readerHeaderMemory->r_u64();

	return dstForm->NewObject(clsid, dstParent);
}

//*******************************************************************

bool ibValueFrame::CopyObject(ibWriterMemory& writer) const
{
	ibWriterMemory writerHeaderMemory;
	writerHeaderMemory.w_s32(0); //reserved
	writerHeaderMemory.w_u64(GetClassType()); //get class type 
	writer.w_chunk(headerBlock, writerHeaderMemory.pointer(), writerHeaderMemory.size());
	ibWriterMemory writerDataMemory;
	if (!CopyProperty(writerDataMemory))
		return false;
	writer.w_chunk(dataBlock, writerDataMemory.pointer(), writerDataMemory.size());
	ibWriterMemory writerChildMemory;
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibWriterMemory writerMemory;
		ibValueFrame* obj = GetChild(idx);
		if (!obj->CopyObject(writerMemory))
			return false;
		writerChildMemory.w_chunk(obj->GetClassType(), writerMemory.pointer(), writerMemory.size());
	}
	writer.w_chunk(childBlock, writerChildMemory.pointer(), writerChildMemory.size());
	return true;
}

bool ibValueFrame::PasteObject(ibReaderMemory& reader)
{
	ibValueForm* valueForm = GetOwnerForm();
	if (valueForm == nullptr) return false;

	std::shared_ptr <ibReaderMemory>readerHeaderMemory(reader.open_chunk(headerBlock));

	const ibVersionID& version = readerHeaderMemory->r_s32(); //reserved
	const ibClassID& clsid = readerHeaderMemory->r_u64();

	std::shared_ptr <ibReaderMemory> readerChildMemory(reader.open_chunk(childBlock));
	if (readerChildMemory != nullptr) {
		ibReaderMemory* prevReaderMemory = nullptr;
		do {
			ibClassID founded_clsid = 0;
			ibReaderMemory* readerMemory = readerChildMemory->open_chunk_iterator(founded_clsid, &*prevReaderMemory);
			if (readerMemory == nullptr)
				break;
			if (founded_clsid > 0) {
				ibValueFrame* valueFrame = valueForm->NewObject(founded_clsid, this, false);
				if (valueFrame != nullptr && !valueFrame->PasteObject(*readerMemory))
					return false;
			}
			prevReaderMemory = readerMemory;
		} while (true);
	}

	std::shared_ptr <ibReaderMemory>readerDataMemory(reader.open_chunk(dataBlock));
	if (!PasteProperty(*readerDataMemory))
		return false;

	valueForm->ResolveNameConflict(this);
	return true;
}

//*******************************************************************

#include "backend/metaData.h"
#include "backend/objCtor.h"

bool ibValueFrame::HasQuickChoice() const {
	ibMetaData* metaData = GetMetaData();
	if (metaData == nullptr)
		return false;
	ibValue selValue; GetControlValue(selValue);
	const ibCtorAbstractType* so = metaData->GetAvailableCtor(selValue.GetClassType());
	if (so != nullptr && so->GetObjectTypeCtor() == ibCtorObjectType_object_primitive) {
		return true;
	}
	else if (so != nullptr && so->GetObjectTypeCtor() == ibCtorObjectType_object_meta_value) {
		const ibCtorMetaValueType* meta_so = dynamic_cast<const ibCtorMetaValueType*>(so);
		if (meta_so != nullptr) {
			ibValueMetaObjectRecordDataRef* metaObject = dynamic_cast<ibValueMetaObjectRecordDataRef*>(meta_so->GetMetaObject());
			if (metaObject != nullptr)
				return metaObject->HasQuickChoice();
		}
	}
	return false;
}

//*******************************************************************

ibBackendValueForm* ibValueFrame::GetBackendForm() const
{
	return GetOwnerForm();
}

//*******************************************************************

ibFormVisualDocument* ibValueFrame::GetVisualDocument() const
{
	ibValueForm* const valueForm = GetOwnerForm();
	if (valueForm == nullptr)
		return nullptr;
	return valueForm->GetVisualDocument();
}

ibFrontendVisualEditorNotebook* ibValueFrame::FindVisualEditor() const
{
	return ibFrontendVisualEditorNotebook::FindEditorByForm(GetOwnerForm());
}

//*******************************************************************
//*                          Runtime                                *
//*******************************************************************

#include "frontend/visualView/visualHostClient.h"

wxObject* ibValueFrame::GetWxObject() const
{
	ibValueForm* const valueForm = GetOwnerForm();
	if (valueForm != nullptr) {

		ibFormVisualDocument* const visualDoc = valueForm->GetVisualDocument();
		if (visualDoc != nullptr) {

			const ibFormVisualEditView* visualView = visualDoc->GetFirstView();
			const ibVisualHost* visualHost = visualView ?
				visualView->GetVisualHost() : nullptr;

			if (visualHost == nullptr)
				return nullptr;

			return visualHost->GetWxObject((ibValueFrame*)this);

		}
		else if (g_visualHostContext != nullptr) {

			//if run designer form search in own visualHost 
			const ibVisualHost* visualHost =
				g_visualHostContext->GetVisualHost();

			return visualHost->GetWxObject((ibValueFrame*)this);
		}
	}

	return nullptr;
}

#include "backend/metaCollection/partial/commonObject.h"

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueFrame::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	{
		wxString propertyName;
		for (unsigned int idx = 0; idx < ibPropertyObject::GetPropertyCount(); idx++) {
			ibProperty* property = ibPropertyObject::GetProperty(idx);
			if (property == nullptr)
				continue;
			property->GetName(propertyName);
			m_methodHelper->AppendProp(propertyName, idx, eProperty);
		}
		//if we have sizerItem then call him  
		ibValueFrame* sizeritem = GetParent();
		if (sizeritem != nullptr && sizeritem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			for (unsigned int idx = 0; idx < sizeritem->GetPropertyCount(); idx++) {
				ibProperty* property = sizeritem->GetProperty(idx);
				if (property == nullptr)
					continue;
				property->GetName(propertyName);
				m_methodHelper->AppendProp(propertyName, idx, eSizerItem);
			}
		}
	}

	m_methodHelper->AppendProp(wxT("Events"), true, false, 0, eEvent);

	if (m_valEventContainer != nullptr) m_valEventContainer->PrepareNames();
}

bool ibValueFrame::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProperty) {
		unsigned int idx = m_methodHelper->GetPropData(lPropNum);
		ibProperty* property = GetPropertyByIndex(idx);
		if (property != nullptr)
			property->SetDataValue(varPropVal);
	}
	else if (lPropAlias == eSizerItem) {
		//if we have sizerItem then call him savepropery 
		ibValueFrame* sizerItem = GetParent();
		if (sizerItem != nullptr &&
			sizerItem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			ibProperty* property = sizerItem->GetPropertyByIndex(lPropNum);
			if (property != nullptr)
				property->SetDataValue(varPropVal);
		}
	}

	ibValueForm* ownerForm = GetOwnerForm();
	if (ownerForm == nullptr)
		return false;

	ibFormVisualDocument* visualDoc = ownerForm->GetVisualDocument();
	if (visualDoc != nullptr) {

		ibVisualHostClient* visualView = visualDoc->GetFirstView() ?
			visualDoc->GetFirstView()->GetVisualHost() : nullptr;

		if (visualView == nullptr)
			return false;

		wxObject* wxobject = visualView->GetWxObject(this);

		if (wxobject != nullptr) {
			wxWindow* wxparent = nullptr;
			Update(wxobject, visualView);
			ibValueFrame* nextParent = GetParent();
			while (wxparent == nullptr && nextParent != nullptr) {
				if (nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW) {
					wxObject* wxobject = visualView->GetWxObject(nextParent);
					wxparent = dynamic_cast<wxWindow*>(wxobject);
					break;
				}
				nextParent = nextParent->GetParent();
			}
			if (wxparent == nullptr) wxparent = visualView->GetBackgroundWindow();
			OnUpdated(wxobject, wxparent, visualView);
			if (wxparent != nullptr) wxparent->Layout();
		}
	}

	return true;
}

bool ibValueFrame::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eSizerItem) {
		//if we have sizerItem then call him savepropery 
		ibValueFrame* sizerItem = GetParent();
		if (sizerItem != nullptr &&
			sizerItem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			unsigned int idx = m_methodHelper->GetPropData(lPropNum);
			ibProperty* property = sizerItem->GetPropertyByIndex(idx);
			if (property != nullptr)
				return property->GetDataValue(pvarPropVal);
			return false;
		}
	}
	else if (lPropAlias == eEvent) {
		pvarPropVal = m_valEventContainer;
		return true;
	}
	else {
		unsigned int idx = m_methodHelper->GetPropData(lPropNum);
		ibProperty* property = GetPropertyByIndex(idx);
		if (property != nullptr)
			return property->GetDataValue(pvarPropVal);
		return false;
	}

	return false;
}