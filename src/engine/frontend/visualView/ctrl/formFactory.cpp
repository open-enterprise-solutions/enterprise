#include "form.h"
#include "frontend/visualView/ctrl/sizer.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <unordered_set>

inline wxString GetClassType(const wxString& className)
{
	const ibCtorControlTypeBase* objectSingle =
		dynamic_cast<const ibCtorControlTypeBase*>(ibValue::GetAvailableCtor(className));
	wxASSERT(objectSingle);
	return objectSingle->GetTypeControlName();
}

inline void SetDefaultLayoutProperties(ibValueSizerItem* sizerItem)
{
	ibValueFrame* child = sizerItem->GetChild(0);
	const wxString& obj_type = child->GetObjectTypeName();

	if (obj_type == wxT("Sizer")) {
		sizerItem->SetBorder(0);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (child->GetClassName() == wxT("Splitter") ||
		child->GetClassName() == wxT("Spacer")) {
		sizerItem->SetProportion(1);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (child->GetClassName() == wxT("Staticline")) {
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (child->GetClassName() == wxT("Toolbar")) {
		sizerItem->SetBorder(0);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (child->GetClassName() == wxT("Notebook")) {
		sizerItem->SetProportion(1);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (obj_type == wxT("Widget") ||
		obj_type == wxT("Statusbar")) {
		sizerItem->SetProportion(0);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxSHRINK);
	}
	else if (obj_type == wxT("Container")) {
		sizerItem->SetProportion(1);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
}

ibValueFrame* ibValueForm::NewObject(const ibClassID& clsid, ibValueFrame* controlParent, const ibValue& generateId)
{
	if (ibValue::IsRegisterCtor(clsid)) {
		ibValueFrame* newControl = nullptr;
		ibValue* ppParams[] = { this, controlParent, const_cast<ibValue*>(&generateId) };
		try {
			newControl = ibValue::CreateAndConvertObjectRef< ibValueFrame>(clsid, ppParams, 3);
			// A parented control is owned by the parent's vector (AddChild inside
			// Init). A rootless control has no vector owner, so the caller takes
			// ownership via this reference (e.g. an ibValuePtr member).
			if (controlParent == nullptr) newControl->IncrRef();
		}
		catch (...) {
			return nullptr;
		}
		return newControl;
	}
	return nullptr;
}

#include "frontend/visualView/ctrl/toolBar.h"

void ibValueForm::ResolveNameConflict(ibValueFrame* control)
{
	if (control->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		return;

	// Derive the base name: the original control name with any trailing
	// digits stripped, so "Button3" reuses the base "Button".
	wxString strOriginalName;

	if (control->GetControlNameAsString(strOriginalName)) {

		if (!strOriginalName.IsEmpty()) {
			size_t length = strOriginalName.length();
			while (length > 0 && stringUtils::IsDigit(strOriginalName[length - 1]))
				--length;
			strOriginalName = strOriginalName.Left(length);
		}
		else {
			const ibValueFrame* parentControl = control->GetParent();
			if (parentControl != nullptr &&
				(g_controlToolBarItemCLSID == control->GetClassType() ||
					g_controlToolBarSeparatorCLSID == control->GetClassType())) {
				strOriginalName = parentControl->GetControlName() + control->GetClassName();
			}
			else {
				strOriginalName = control->GetClassName();
			}
		}
	}

	// Normalise to upper case, matching stringUtils::CompareString's
	// case-insensitive semantics, so set lookups are exact comparisons.
	const auto toKey = [](const wxString& name) {
		std::wstring key = name.ToStdWstring();
		std::transform(key.begin(), key.end(), key.begin(), ::towupper);
		return key;
	};

	// Collect the names already taken by the other controls once, so each
	// candidate is probed in O(1). The old do/while rescanned the whole
	// control list for every candidate index — O(controls * candidates)
	// per call and O(N^3) when laying out N like-named controls.
	std::unordered_set<std::wstring> takenNames;
	wxString strControlName;
	for (const auto valueControl : control->GetOwnerForm()->GetControlList()) {

		if (0 == valueControl->GetControlID())
			continue;
		if (control == valueControl)
			continue;
		if (!valueControl->GetControlNameAsString(strControlName))
			continue;

		takenNames.insert(toKey(strControlName));
	}

	// the name cannot be repeated within the same form: bump the
	// numeric suffix until the candidate is free.
	wxString strGenerateName = strOriginalName;
	for (unsigned int index = 0; takenNames.count(toKey(strGenerateName)) > 0; )
		strGenerateName = wxString::Format(wxT("%s%i"), strOriginalName, ++index);

	control->SetControlName(strGenerateName);
}

ibValueFrame* ibValueForm::CreateObject(const wxString& className, ibValueFrame* controlParent)
{
	ibValueFrame* object = nullptr;
	wxString classType = ::GetClassType(className);

	if (controlParent) {
		//FIXME! This is a patch to avoid creating the menubar, statusbar and
		//toolbar types in a form that is not a wxFrame.
		//The set of types needs to be modified to allow having several kinds
		//of forms (like childType of project), but there is a lot of code that is invalid
		//for forms that are not of type "form". In other words, there is
		//code that depends on the type name, which must be avoided.
		if (controlParent->GetObjectTypeName() == wxT("Form") && controlParent->GetClassName() != wxT("ClientForm") &&
			(classType == wxT("Statusbar") ||
				classType == wxT("Menubar") ||
				classType == wxT("Ribbonbar") ||
				classType == wxT("Toolbar")))

			return nullptr; // not a valid type

		// No menu dropdown for wxToolBar until wx 2.9 :(
		if (controlParent->GetObjectTypeName() == wxT("Tool"))
		{
			ibValueFrame* gParent = controlParent->GetParent();

			if (
				(gParent->GetClassName() == wxT("Toolbar")) &&
				(className == wxT("Menu"))
				)
				return nullptr; // not a valid type
		}

		if (controlParent->GetObjectTypeName() == wxT("Toolbar"))
		{
			if (classType == wxT("Tool") /*|| classType == wxT("widget")*/)
			{
				object = NewObject(className, controlParent);
				//set enabled item
				//object->SetReadOnly(controlParent->IsEditable());
			}
		}
		else if (controlParent->GetObjectTypeName() == wxT("Notebook"))
		{
			if (classType == wxT("NotebookPage")) {
				object = NewObject(className, controlParent);
				//set enabled item
				//object->SetReadOnly(controlParent->IsEditable());
			}
		}
		else if (controlParent->GetObjectTypeName() == wxT("Container")
			&& controlParent->GetClassName() == wxT("Tablebox"))
		{
			if (classType == wxT("TableboxColumn"))
			{
				object = NewObject(className, controlParent);
				//set enabled item
				//object->SetReadOnly(controlParent->IsEditable());
			}
		}
		// A column GROUP takes exactly what a table takes — columns, and groups of
		// their own. Its own kind IS "TableboxColumn", which is what lets the branch
		// above accept it into a table without a second rule for it.
		else if (controlParent->GetClassName() == wxT("TableboxColumnGroup"))
		{
			if (classType == wxT("TableboxColumn"))
			{
				object = NewObject(className, controlParent);
			}
		}
		else if (controlParent->GetObjectTypeName() == wxT("NotebookPage"))
		{
			ibValueSizerItem* sizerItem = NewObject<ibValueSizerItem>("SizerItem", controlParent);
			ibValueFrame* obj = NewObject(className, sizerItem);

			if (controlParent) {
				//sizerItem->SetReadOnly(controlParent->IsEditable());
			}

			// the following condition must always hold,
			// since an item must always contain another object
			if (obj) {
				//set enabled item
				//obj->SetReadOnly(sizerItem->IsEditable());

				// sizerItem is a reserved object type; for convenience it gets default
				// values assigned according to the type of object created
				if (sizerItem->IsSubclassOf(wxT("SizerItem"))) {
					SetDefaultLayoutProperties(sizerItem);
				}

				object = sizerItem;
			}
		}
		else if (controlParent->GetComponentType() == COMPONENT_TYPE_FRAME ||
			controlParent->GetComponentType() == COMPONENT_TYPE_SIZER ||
			controlParent->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		{
			ibValueSizerItem* sizerItem = NewObject< ibValueSizerItem>("SizerItem", controlParent);
			ibValueFrame* obj = NewObject(className, sizerItem);

			//if (controlParent) {
			//	sizerItem->SetReadOnly(controlParent->IsEditable());
			//}

			// the following condition must always hold,
			// since an item must always contain another object
			if (obj) {
				//set enabled item
				//obj->SetReadOnly(sizerItem->IsEditable());
				// sizerItem is a reserved object type; for convenience it gets default
				// values assigned according to the type of object created
				if (sizerItem->IsSubclassOf(wxT("sizerItem"))) {
					SetDefaultLayoutProperties(sizerItem);
				}

				object = sizerItem;
			}
		}
	}
	else // controlParent == nullptr;
	{
		object = NewObject(className, nullptr);
	}

	return object;
}

#include <wx/clipbrd.h>

#define	copyBlock 0x022290

bool ibValueForm::CopyObject(ibValueFrame* srcControl, bool copyOnPaste)
{
	if (srcControl->GetComponentType() == COMPONENT_TYPE_FRAME)
		return false;

	// Write some control to clipboard
	if (wxTheClipboard->Open()) {

		ibWriterMemory writerMemory;

		ibWriterMemory writerCopyMemory;
		writerCopyMemory.w_u8(copyOnPaste);

		writerMemory.w_chunk(copyBlock, writerCopyMemory.pointer(), writerCopyMemory.size());

		if (srcControl->CopyObject(writerMemory)) {

			wxDataObjectComposite* composite_object = new wxDataObjectComposite;
			wxCustomDataObject* custom_object = new wxCustomDataObject(oes_clipboard_frame);
			custom_object->SetData(writerMemory.size(), writerMemory.pointer()); // the +1 is used to force copy of the \0 character		

			composite_object->Add(custom_object);
			composite_object->Add(new wxTextDataObject(srcControl->GetControlName()), true);

			// tell clipboard 
			wxTheClipboard->SetData(composite_object);
		}

		wxTheClipboard->Close();
	}

	return false;
}

ibValueFrame* ibValueForm::PasteObject(ibValueForm* dstForm, ibValueFrame* dstParent)
{
	ibValueFrame* clipboard = nullptr;

	if (wxTheClipboard->Open() && wxTheClipboard->IsSupported(oes_clipboard_frame)) {
		wxCustomDataObject data(oes_clipboard_frame);
		if (wxTheClipboard->GetData(data)) {

			ibReaderMemory readerMemory(data.GetData(), data.GetDataSize());

			std::shared_ptr <ibReaderMemory>readerCopyMemory(readerMemory.open_chunk(copyBlock));
			const bool copyOnPaste = readerCopyMemory->r_u8();

			clipboard = ibValueFrame::CreatePasteObject(readerMemory, dstForm, dstParent);

			if (clipboard == nullptr)
				return nullptr;

			if (!clipboard->PasteObject(readerMemory)) 
				wxDELETE(clipboard);

			if (!copyOnPaste) wxTheClipboard->Clear();
		}
		wxTheClipboard->Close();
	}

	return clipboard;
}
