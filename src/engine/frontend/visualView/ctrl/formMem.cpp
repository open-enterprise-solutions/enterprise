#include "form.h"
#include "backend/metaCollection/metaFormObject.h"

bool CValueForm::LoadForm(const wxMemoryBuffer& formData)
{
	CMemoryReader readerData(formData);
	u64 clsid = 0;
	std::shared_ptr<CMemoryReader>readerMemory(readerData.open_chunk_iterator(clsid));
	if (!readerMemory)
		return false;
	u64 form_id = 0;
	std::shared_ptr<CMemoryReader>readerMetaMemory(readerMemory->open_chunk_iterator(form_id));
	if (!readerMetaMemory)
		return false;
	for (unsigned int idx = GetChildCount(); idx > 0; idx--) {
		IValueFrame* controlChild =
			dynamic_cast<IValueFrame*>(GetChild(idx - 1));
		if (controlChild != nullptr) {
			RemoveControl(controlChild);
		}
	}
	std::shared_ptr <CMemoryReader>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));
	SetReadOnly(m_metaFormObject->IsEditable());
	if (!LoadControl(m_metaFormObject, *readerDataMemory)) {
		return false;
	}
	std::shared_ptr <CMemoryReader> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));
	if (readerChildMemory) {
		if (!LoadChildForm(*readerChildMemory, this)) {
			return false;
		}
	}

	CValueForm::PrepareNames();
	return true;
}

bool CValueForm::LoadChildForm(CMemoryReader& readerData, IValueFrame* controlParent)
{
	class_identifier_t clsid = 0;
	CMemoryReader* prevReaderMemory = nullptr;
	while (!readerData.eof()) {
		CMemoryReader* readerMemory = readerData.open_chunk_iterator(clsid, &*prevReaderMemory);
		if (!readerMemory)
			break;
		u64 form_id = 0;
		CMemoryReader* prevReaderMetaMemory = nullptr;
		while (!readerData.eof()) {
			CMemoryReader* readerMetaMemory = readerMemory->open_chunk_iterator(form_id, &*prevReaderMetaMemory);
			if (!readerMetaMemory)
				break;
			wxASSERT(clsid != 0);
			wxString classType = CValue::GetNameObjectFromID(clsid);
			wxASSERT(classType.Length() > 0);
			IValueFrame* newControl = CValueForm::NewObject(classType, controlParent, false);
			newControl->SetReadOnly(m_propEnabled);
			std::shared_ptr <CMemoryReader>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));
			if (!newControl->LoadControl(m_metaFormObject, *readerDataMemory))
				return false;
			std::shared_ptr <CMemoryReader> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));
			if (readerChildMemory) {
				if (!LoadChildForm(*readerChildMemory, newControl))
					return false;
			}
			prevReaderMetaMemory = readerMetaMemory;
		}
		prevReaderMemory = readerMemory;
	}
	return true;
}

wxMemoryBuffer CValueForm::SaveForm()
{
	CMemoryWriter writterData;

	//Save common object
	CMemoryWriter writterMemory;
	CMemoryWriter writterMetaMemory;
	CMemoryWriter writterDataMemory;
	if (!SaveControl(m_metaFormObject, writterDataMemory)) {
		return wxMemoryBuffer();
	}
	writterMetaMemory.w_chunk(eDataBlock, writterDataMemory.pointer(), writterDataMemory.size());
	CMemoryWriter writterChildMemory;
	if (!SaveChildForm(writterChildMemory, this))
		return wxMemoryBuffer();
	writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
	writterMemory.w_chunk(GetControlID(), writterMetaMemory.pointer(), writterMetaMemory.size());
	writterData.w_chunk(GetClassType(), writterMemory.pointer(), writterMemory.size());
	
	CValueForm::PrepareNames();
	return writterData.buffer();
}

bool CValueForm::SaveChildForm(CMemoryWriter& writterData, IValueFrame* controlParent)
{
	for (unsigned int idx = 0; idx < controlParent->GetChildCount(); idx++) {
		CMemoryWriter writterMemory;
		CMemoryWriter writterMetaMemory;
		CMemoryWriter writterDataMemory;
		IValueFrame* child = controlParent->GetChild(idx);
		wxASSERT(child);
		if (!child->SaveControl(m_metaFormObject, writterDataMemory)) {
			return false;
		}
		writterMetaMemory.w_chunk(eDataBlock, writterDataMemory.pointer(), writterDataMemory.size());
		CMemoryWriter writterChildMemory;
		if (!SaveChildForm(writterChildMemory, child)) {
			return false;
		}
		writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
		writterMemory.w_chunk(child->GetControlID(), writterMetaMemory.pointer(), writterMetaMemory.size());
		writterData.w_chunk(child->GetClassType(), writterMemory.pointer(), writterMemory.size());
	}

	return true;
}