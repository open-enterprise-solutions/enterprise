////////////////////////////////////////////////////////////////////////////
//	Description : form-local command (an event; registry slot on a form)
////////////////////////////////////////////////////////////////////////////

#include "formCommand.h"
#include "form.h"

#include "backend/system/value/valueType.h"   // SYSTEM_TYPE_REGISTER
#include "backend/serialize/dataBuilder.h"     // ibDataNode + ibBinaryProvider (property serialize + clipboard)
#include "backend/fileSystem/fs.h"             // ibWriterMemory / ibReaderMemory (clipboard serialize)
#include "backend/backend_core.h"              // oes_clipboard_command
#ifndef OES_USE_WEB
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#endif

ibFormCommandValue::ibFormCommandValue(ibValueForm* ownerForm)
	: m_ownerForm(ownerForm),
	  m_commandId(ownerForm != nullptr ? ownerForm->NextFormCommandId() : 0)
{
	// The base ibValueLayerObject is a non-member value (no script members of its own) — a form command is RUN
	// by name (its Action event), not read as a value.
}

ibValueFrame* ibFormCommandValue::GetOwnerFrame() const
{
	return m_ownerForm;   // the owner form IS a frame — its metadata + FindVisualEditor drive the base routing
}

bool ibFormCommandValue::IsEditable() const
{
	return m_ownerForm != nullptr ? m_ownerForm->IsEditable() : true;
}

bool ibFormCommandValue::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	// Name must stay UNIQUE among the form's commands — the inspector edits m_propertyName directly (bypassing
	// RenameFormCommand), so reject a collision here, exactly as ibValueCommandBarItem guards its own Name.
	if (property == m_propertyName && m_ownerForm != nullptr && !m_ownerForm->IsFormCommandNameUnique(newValue.GetString(), this))
		return false;
	return true;
}

bool ibFormCommandValue::WriteProperty(ibDataNode& node) const
{
	node.SetValue(wxT("CommandId"), (int)m_commandId);   // form-unique id (not a property)
	// Each own property + the Action event by hand — the base only serializes ATTACHED objects.
	node.SetProperty(m_propertyName->GetName(),    m_propertyName->GetNodeValue());
	node.SetProperty(m_propertyCaption->GetName(), m_propertyCaption->GetNodeValue());
	node.SetProperty(m_propertyPicture->GetName(), m_propertyPicture->GetNodeValue());
	node.SetProperty(m_eventCommand->GetName(),    m_eventCommand->GetNodeValue());
	return ibPropertyObject::WriteProperty(node);
}

bool ibFormCommandValue::ReadProperty(const ibDataNode& node)
{
	m_commandId = (ibMetaID)node.GetValue<int>(wxT("CommandId"));
	m_propertyName->ReadNodeValue(node.GetProperty(m_propertyName->GetName()));
	m_propertyCaption->ReadNodeValue(node.GetProperty(m_propertyCaption->GetName()));
	m_propertyPicture->ReadNodeValue(node.GetProperty(m_propertyPicture->GetName()));
	m_eventCommand->ReadNodeValue(node.GetProperty(m_eventCommand->GetName()));
	return ibPropertyObject::ReadProperty(node);
}

//****************************************************************************
//*                          Clipboard (designer-only)                        *
//****************************************************************************

bool ibFormCommandValue::CopyToClipboard(const ibFormCommandValue* entry)
{
#ifndef OES_USE_WEB
	if (entry == nullptr)
		return false;
	ibDataNode node;
	entry->WriteProperty(node);
	ibWriterMemory writer;
	if (!ibBinaryProvider().Write(node, writer))
		return false;
	if (wxTheClipboard->Open()) {
		wxCustomDataObject* custom = new wxCustomDataObject(oes_clipboard_command);
		custom->SetData(writer.size(), writer.pointer());
		wxTheClipboard->SetData(custom);
		wxTheClipboard->Close();
		return true;
	}
#else
	(void)entry;
#endif
	return false;
}

ibFormCommandValue* ibFormCommandValue::PasteFromClipboard(ibValueForm* form)
{
#ifndef OES_USE_WEB
	if (form == nullptr)
		return nullptr;
	ibFormCommandValue* entry = nullptr;
	if (wxTheClipboard->Open()) {
		if (wxTheClipboard->IsSupported(oes_clipboard_command)) {
			wxCustomDataObject data(oes_clipboard_command);
			if (wxTheClipboard->GetData(data)) {
				ibReaderMemory reader(data.GetData(), data.GetDataSize());
				ibDataNode node;
				if (ibBinaryProvider().Read(reader, node))
					entry = form->PasteFormCommand(node);   // fresh entry, unique name + id
			}
		}
		wxTheClipboard->Close();
	}
	return entry;
#else
	(void)form;
	return nullptr;
#endif
}

bool ibFormCommandValue::HasClipboardData()
{
#ifndef OES_USE_WEB
	bool has = false;
	if (wxTheClipboard->Open()) {
		has = wxTheClipboard->IsSupported(oes_clipboard_command);
		wxTheClipboard->Close();
	}
	return has;
#else
	return false;
#endif
}

// Registered so GetClassName / GetClassType resolve (a runtime property object the inspector shows must be
// registered). SYSTEM type: no ctor, never created by the factory — the form builds it programmatically.
SYSTEM_TYPE_REGISTER(ibFormCommandValue, "FormCommandValue");
