#include "form.h"
#include "backend/metaCollection/metaFormObject.h"

#ifdef OES_USE_WEB
#include <iostream>
#include <iomanip>
#endif

bool ibValueForm::LoadForm(const wxMemoryBuffer& formData)
{
	if (formData.GetDataLen() == 0)
		return false;
	ibReaderMemory readerData(formData);
	u64 clsid = 0;
	std::shared_ptr<ibReaderMemory>readerMemory(readerData.open_chunk_iterator(clsid));
	if (!readerMemory)
		return false;
	u64 form_id = 0;
	std::shared_ptr<ibReaderMemory>readerMetaMemory(readerMemory->open_chunk_iterator(form_id));
	if (!readerMetaMemory)
		return false;
	for (unsigned int idx = GetChildCount(); idx > 0; idx--) {
		ibValueFrame* controlChild = GetChild(idx - 1);
		if (controlChild != nullptr) {
			RemoveControl(controlChild);
		}
	}
	std::shared_ptr <ibReaderMemory>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));
	//SetReadOnly(m_metaFormObject->IsEditable());
	wxASSERT_MSG(readerDataMemory, wxT("ibValueForm::LoadForm: eDataBlock missing — form was saved with empty writer (check ibValueFrame::SaveControl signature)"));
	if (!readerDataMemory)
		return false;
	if (!LoadControl(m_metaFormObject, *readerDataMemory)) {
		return false;
	}
	std::shared_ptr <ibReaderMemory> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));
	if (readerChildMemory) {
		if (!LoadChildForm(*readerChildMemory, this)) {
			return false;
		}
	}

	// Controls loaded from memory → the form's attribute surface is stale.
	InvalidateNames();
	return true;
}

bool ibValueForm::LoadChildForm(ibReaderMemory& readerData, ibValueFrame* controlParent)
{
	ibClassID clsid = 0;
	ibReaderMemory* prevReaderMemory = nullptr;
	while (!readerData.eof()) {
		ibReaderMemory* readerMemory = readerData.open_chunk_iterator(clsid, &*prevReaderMemory);
		if (!readerMemory)
			break;
		u64 form_id = 0;
		ibReaderMemory* prevReaderMetaMemory = nullptr;
		while (!readerData.eof()) {
			ibReaderMemory* readerMetaMemory = readerMemory->open_chunk_iterator(form_id, &*prevReaderMetaMemory);
			if (!readerMetaMemory)
				break;
			wxASSERT(clsid != 0);
			ibValueFrame* newControl = ibValueForm::NewObject(clsid, controlParent, false);
#ifdef OES_USE_WEB
			if (newControl == nullptr) {
				std::cerr << "[LoadChildForm] NewObject returned nullptr for clsid=0x"
					<< std::hex << clsid << std::dec
					<< " — unregistered control type; skipping subtree" << std::endl;
				// Skip this control's subtree so we don't crash in LoadControl
				// with a null this, and so the remaining siblings can still load.
				prevReaderMetaMemory = readerMetaMemory;
				continue;
			}
#else
			wxASSERT_MSG(newControl != nullptr, wxT("ibValueForm::LoadChildForm: unregistered clsid"));
			if (newControl == nullptr)
				return false;
#endif
			//newControl->SetReadOnly(m_propEnabled);
			std::shared_ptr <ibReaderMemory>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));
			wxASSERT_MSG(readerDataMemory, wxT("ibValueForm::LoadChildForm: eDataBlock missing for child control"));
			if (!readerDataMemory)
				return false;
			if (!newControl->LoadControl(m_metaFormObject, *readerDataMemory))
				return false;
			std::shared_ptr <ibReaderMemory> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));
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

wxMemoryBuffer ibValueForm::SaveForm()
{
	ibWriterMemory writerData;

	//Save common object
	ibWriterMemory writerMemory;
	ibWriterMemory writerMetaMemory;
	ibWriterMemory writerDataMemory;
	if (!SaveControl(m_metaFormObject, writerDataMemory)) {
		return wxMemoryBuffer();
	}
	wxASSERT_MSG(writerDataMemory.size() > 0, wxT("ibValueForm::SaveForm: SaveControl produced empty block — check that writer is passed by reference"));
	writerMetaMemory.w_chunk(eDataBlock, writerDataMemory.pointer(), writerDataMemory.size());
	ibWriterMemory writerChildMemory;
	if (!SaveChildForm(writerChildMemory, this))
		return wxMemoryBuffer();
	writerMetaMemory.w_chunk(eChildBlock, writerChildMemory.pointer(), writerChildMemory.size());
	writerMemory.w_chunk(GetControlID(), writerMetaMemory.pointer(), writerMetaMemory.size());
	writerData.w_chunk(GetClassType(), writerMemory.pointer(), writerMemory.size());

	return writerData.buffer();
}

bool ibValueForm::SaveChildForm(ibWriterMemory& writerData, ibValueFrame* controlParent)
{
	for (unsigned int idx = 0; idx < controlParent->GetChildCount(); idx++) {
		ibWriterMemory writerMemory;
		ibWriterMemory writerMetaMemory;
		ibWriterMemory writerDataMemory;
		ibValueFrame* child = controlParent->GetChild(idx);
		wxASSERT(child);
		if (!child->SaveControl(m_metaFormObject, writerDataMemory)) {
			return false;
		}
		wxASSERT_MSG(writerDataMemory.size() > 0, wxT("ibValueForm::SaveChildForm: child SaveControl produced empty block"));
		writerMetaMemory.w_chunk(eDataBlock, writerDataMemory.pointer(), writerDataMemory.size());
		ibWriterMemory writerChildMemory;
		if (!SaveChildForm(writerChildMemory, child)) {
			return false;
		}
		writerMetaMemory.w_chunk(eChildBlock, writerChildMemory.pointer(), writerChildMemory.size());
		writerMemory.w_chunk(child->GetControlID(), writerMetaMemory.pointer(), writerMetaMemory.size());
		writerData.w_chunk(child->GetClassType(), writerMemory.pointer(), writerMemory.size());
	}

	return true;
}