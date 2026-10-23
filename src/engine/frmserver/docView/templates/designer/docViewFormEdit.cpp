#include "docViewFormEdit.h"
#include "docViewComposerEdit.h"   // ibFetchComposerRequest — what a composition's panel asks
#include "backend/propertyManager/property/propertyDynamicSource.h"   // a list's Source, picked (SetProperty)

#include "backend/metaCollection/metaFormObject.h"
#include "backend/metaData.h"   // GetCompileCache — the form the configuration compiled, when it has one
#include "backend/metadataConfiguration.h"   // the session's configuration — the form's object is in it
#include "backend/session/session.h"
#include "backend/backend_picture.h"   // ibBackendPicture::SendPicture — a class's picture, by its id
#include "core/diagnostics/journal.h"
#include "core/serialize/dataBuilder.h"
#include "frmserver/client/clientFrame.h"         // ibClientFrame::ShowProperties — the form's object's rows, in the inspector
#include "frmserver/client/propertyInspector.h"   // a control's rows, as the designer's inspector writes them
#include "frmserver/visualView/controlCtor.h"     // ibCtorControlTypeBase::IsControlSystem — not offered to place
#include "frmserver/visualView/ctrl/formAttribute.h"   // ibFormAttributeValue — an attribute's rows (FetchProperties)
#include "frmserver/visualView/ctrl/formCommand.h"     // ibFormCommandValue — a form command's
#include "frmserver/visualView/layers/commandBar.h"   // ibValueCommandBar — a control's command bar, its rows

// ----------------------------------------------------------------------------
// ibFormDesignView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibFormDesignView, ibModuleTextView);

bool ibFormDesignView::OnCreate(ibDocument* doc, long flags)
{
	if (!ibModuleTextView::OnCreate(doc, flags))
		return false;
	return LoadForm();
}

void ibFormDesignView::OnClosingDocument()
{
	// The document is closing — the controls are cleaned up while the view still holds the form.
	ClearVisualHost();
}

void ibFormDesignView::OnDraw(ibDataNode& frame)
{
	ibModuleTextView::OnDraw(frame);

	// The card's title — the desktop host's SetCaption: the form's caption, else the object's synonym and the form's,
	// else the form's. The object is the form's main attribute's (GetMetaObject — in the designer, by its type).
	if (m_valueForm == nullptr)
		return;
	wxString caption = m_valueForm->GetCaption();
	if (caption.IsEmpty()) {
		const ibValueMetaObjectFormBase* const metaFormObject = m_valueForm->GetFormMetaObject();
		const ibValueMetaObjectGenericData* const genericObject = m_valueForm->GetMetaObject();
		if (genericObject != nullptr && metaFormObject != nullptr)
			caption = genericObject->GetSynonym() + wxT(": ") + metaFormObject->GetSynonym();
		else if (metaFormObject != nullptr)
			caption = metaFormObject->GetSynonym();
	}
	ibDataNode* const state = frame.FindChild(ibProtocol::ibView::State);
	if (state == nullptr)
		return;
	state->SetValue(ibProtocol::ibView::ibFormDesigner::Caption, caption);

	// ⭐ WHAT THE TAB IS OPENED WITH RIDES THE FRAME THAT OPENS IT — once (Opening): the form, the classes and the module's
	// text, as its Text fetch answers them. The client makes the editor without asking.
	if (m_openingDrawn)
		return;
	m_openingDrawn = true;
	ibDataNode request;
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<std::int32_t>(ibProtocol::ibView::ibModule::ibRequest::Text));
	Fetch(request, state->Child(ibProtocol::ibView::ibFormDesigner::Opening));

	// …and the form's object's rows, which its Code page shows in the inspector (the tree's row it was opened from): the
	// configuration's tree names it by tab 0; an external file's tree is a tab of its own, and its page reads them.
	ibValueMetaObject* const metaObject = GetDocument()->GetMetaObject();
	ibClientFrame* const clientFrame = ibClientFrame::GetFrame();
	if (metaObject != nullptr && clientFrame != nullptr
		&& metaObject->GetMetaData() == clientFrame->GetSession()->GetMetaData())
		clientFrame->ShowProperties(metaObject, 0);
}

bool ibFormDesignView::Fetch(const ibDataNode& request, ibDataNode& response)
{
	// ⭐ A FORM CHANGED HERE IS WRITTEN INTO ITS METAOBJECT AT ONCE, as a Change writes it (OnClientEvent) — one a door
	// changed, one the request handed: the client takes the form it is answered as the one held, and hands it no more.
	// A form not editable is not written.
	const auto written = [this](bool changed) {
		ibMetaDocument* const document = GetDocument();
		const ibValueMetaObjectFormBase* const creator =
			document != nullptr ? document->ConvertMetaObjectToType<ibValueMetaObjectFormBase>() : nullptr;
		if (changed && creator != nullptr && creator->IsEditable()) {
			SaveForm();
			document->Modify(true);
		}
		return changed;
	};

	// Asked about a form the client edited and has not handed (a control's state over its edit, a layer's edit) — it is
	// loaded first. One about the form as it stands carries none: the client handed it as it drew it (Change).
	if (const ibDataNode* const form = request.FindChild(ibProtocol::ibView::ibFormDesigner::Form)) {
		ibJournalStopwatch loading;
		loading.Resume();
		if (!written(LoadForm(*form)))
			return false;
		loading.Pause();
		ibJournalInfo(wxT("client"), wxT("      the form handed back loaded %lld ms"), loading.Ms());
	}

	const std::int32_t kind = request.GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::Kind);
	switch (static_cast<ibProtocol::ibView::ibFormDesigner::ibRequest>(kind)) {
	case ibProtocol::ibView::ibFormDesigner::ibRequest::Control:        return written(FetchControl(request, response));
	case ibProtocol::ibView::ibFormDesigner::ibRequest::BoundControl:   return written(FetchBoundControl(request, response));
	case ibProtocol::ibView::ibFormDesigner::ibRequest::CommandControl: return written(FetchCommandControl(request, response));
	case ibProtocol::ibView::ibFormDesigner::ibRequest::Rows:           return FetchRows(request, response);
	case ibProtocol::ibView::ibFormDesigner::ibRequest::Composition:    return FetchComposition(request, response);
	case ibProtocol::ibView::ibFormDesigner::ibRequest::Editor:         return FetchEditor(request, response);
	case ibProtocol::ibView::ibFormDesigner::ibRequest::Attribute:      return written(FetchAttribute(request, response));
	case ibProtocol::ibView::ibFormDesigner::ibRequest::FormCommand:    return written(FetchFormCommand(request, response));
	case ibProtocol::ibView::ibFormDesigner::ibRequest::Properties:     return FetchProperties(request, response);
	case ibProtocol::ibView::ibFormDesigner::ibRequest::Form:           WriteForm(response); return true;
	default:
		break;
	}

	// …AND A COMPOSITION AN ATTRIBUTE HOLDS, as its cell's window asks the configuration (ibPropertyDataComposition): the
	// fields of its query, of a reference's targets, of a dynamic list — the composer tab's own requests, over the form's
	// configuration.
	switch (static_cast<ibProtocol::ibView::ibComposer::ibRequest>(kind)) {
	case ibProtocol::ibView::ibComposer::ibRequest::Fields:
	case ibProtocol::ibView::ibComposer::ibRequest::Expand:
	case ibProtocol::ibView::ibComposer::ibRequest::Source: {
		const ibMetaDocument* const document = GetDocument();
		const ibValueMetaObject* const metaObject = document != nullptr ? document->GetMetaObject() : nullptr;
		return ibFetchComposerRequest(request, response, metaObject != nullptr ? metaObject->GetMetaData() : nullptr);
	}
	default:
		break;
	}

	if (static_cast<ibProtocol::ibView::ibModule::ibRequest>(kind) != ibProtocol::ibView::ibModule::ibRequest::Text)
		return ibModuleTextView::Fetch(request, response);

	if (!ibModuleTextView::Fetch(request, response))
		return false;
	WriteForm(response);
	WriteClasses(response);
	return true;
}

bool ibFormDesignView::FetchProperties(const ibDataNode& request, ibDataNode& response)
{
	// The rows of an attribute, of a form command, as the inspector writes them — the client's holder of it shows them.
	const ibMetaID attributeId = request.GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::Attribute);
	const ibMetaID commandId = request.GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::FormCommand);
	// …and of a value table's column of an attribute (Column): the column is the property object.
	const ibMetaID columnId = request.GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::Column);
	ibPropertyObject* object = nullptr;
	if (attributeId != 0 && columnId != 0)
		object = FindColumnById(m_valueForm->FindAttributeById(attributeId), columnId);
	else if (attributeId != 0)
		object = m_valueForm->FindAttributeById(attributeId);
	else if (commandId != 0)
		object = m_valueForm->FindFormCommandById(commandId);
	if (object == nullptr)
		return false;

	ibPropertyInspector inspector;
	inspector.SelectObject(object, attributeId != 0 ? attributeId : commandId, 0);
	inspector.OnDraw(response);
	if (columnId != 0)
		response.SetValue(ibProtocol::ibView::ibFormDesigner::Column, static_cast<std::int32_t>(columnId));
	return true;
}

bool ibFormDesignView::FetchRows(const ibDataNode& request, ibDataNode& response)
{
	ibValueFrame* const control = m_valueForm->FindControlByID(request.GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::Control));
	if (control == nullptr)
		return false;
	// …and the control as the form so edited draws it — its State is the server's (enabled with its form, shown where
	// available and bound): what an edit of the client's changes there, the client does not decide. A class's shape
	// (WriteClasses): Node, Controls.
	ibDataNode& drawn = response.Child(ibProtocol::ibView::ibFormDesigner::Node);
	drawn = ibDataNode(control->GetClassType(), control->GetControlID());
	RefreshControl(control, drawn);
	// Its rows, and its sizer item's — the inspector shows them with it (the desktop's owner's category).
	ibDataNode& controls = response.Child(ibProtocol::ibView::ibFormDesigner::Controls);
	WriteRows(control, controls);
	ibValueFrame* const holder = control->GetParent();
	if (holder != nullptr && holder->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		WriteRows(holder, controls);
	return true;
}

bool ibFormDesignView::FetchEditor(const ibDataNode& request, ibDataNode& response)
{
	// What a control's property's editor reads beside its row, as the editor opens — the property's to write, its editor
	// the reader (GetProtocolValue); the row carries what the cell shows.
	ibValueFrame* const control = m_valueForm->FindControlByID(request.GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::Control));
	ibProperty* const property = control != nullptr
		? control->GetProperty(request.GetValue<wxString>(ibProtocol::ibView::ibFormDesigner::Name)) : nullptr;
	return property != nullptr && property->GetProtocolValue(response, ibProperty::ibReader::Editor);
}

bool ibFormDesignView::SetProperty(ibPropertyObject* object, const ibDataNode& request)
{
	const wxString name = request.GetValue<wxString>(ibProtocol::ibView::ibFormDesigner::Name);
	const ibDataValue* value = request.FindProperty(ibProtocol::ibView::ibFormDesigner::Value);
	if (value == nullptr)
		value = request.FindField(ibProtocol::ibView::ibFormDesigner::Value);
	if (object == nullptr || value == nullptr)
		return false;

	// An event — the handler a form command runs (Action): its owner asked and told by the event's pair.
	if (ibEvent* const event = object->GetEvent(name)) {
		ibPropertyObject* const owner = event->GetPropertyObject();
		const wxVariant oldValue = event->GetValue();
		if (value->Kind() == ibDataKind::String && !owner->OnEventChanging(event, wxVariant(value->AsString().ToWxString())))
			return false;
		if (!event->SetNodeValue(*value))
			return false;
		owner->OnEventChanged(event, oldValue, event->GetValue());
		return true;
	}

	// …any other property, its own owner's — the attribute's, or its value's (a dynamic list's Source): the change
	// carries up to the holder, which makes its value anew for a new type.
	ibProperty* const property = object->GetProperty(name);
	if (property == nullptr)
		return false;
	ibPropertyObject* const owner = property->GetPropertyObject();
	const wxVariant oldValue = property->GetValue();
	if (value->Kind() == ibDataKind::String && !owner->OnPropertyChanging(property, wxVariant(value->AsString().ToWxString())))
		return false;
	if (!property->SetNodeValue(*value))
		return false;
	owner->OnPropertyChanged(property, oldValue, property->GetValue());
	// A LIST'S SOURCE picked: the list is made anew over it — its main table, its columns (ibValueDynamicList::
	// OnChildChanged, as the desktop's cell's SetSource rebuilds it); the main table lands in its description, which is
	// what the form keeps.
	if (dynamic_cast<const ibPropertyDynamicSource*>(property) != nullptr)
		owner->OnChildChanged();
	return true;
}

bool ibFormDesignView::LoadForm()
{
	ibMetaDocument* const document = GetDocument();
	const ibValueMetaObjectFormBase* const creator =
		document != nullptr ? document->ConvertMetaObjectToType<ibValueMetaObjectFormBase>() : nullptr;

	if (creator == nullptr)
		return false;

	const ibMetaData* metaData = creator->GetMetaData();
	wxASSERT(metaData);

	ibValueForm* compiledForm = nullptr;
	auto* cc = metaData->GetCompileCache();
	if (cc && cc->FindCompileModule(creator, compiledForm)) {
		m_valueForm = compiledForm;
	}
	else {
		m_valueForm = new ibValueForm(ibFormRequest(), creator, nullptr);

		// ⭐⭐ EMPTY IS NOT BROKEN — IT IS NEW, and it is built from its kind (the desktop editor's, where it says why).
		if (creator->GetFormData().IsEmpty()) {
			m_valueForm->BuildForm(creator->GetTypeForm());
		}
		else if (!creator->LoadFormData(m_valueForm)) {
			m_valueForm = nullptr;
			ibJournalWarning(wxT("designer"), _("Form editor: the form \"%s\" could not be loaded (its stored data is unreadable)."),
				creator->GetName().c_str());
			return false;
		}
	}

	if (m_valueForm == nullptr) {
		ibJournalWarning(wxT("designer"), _("Form editor: the form \"%s\" could not be opened."), creator->GetName().c_str());
		return false;
	}

	// first create control
	CreateVisualHost();
	return true;
}

bool ibFormDesignView::LoadForm(const ibDataNode& node)
{
	if (m_valueForm == nullptr)
		return false;

	// The desktop's LoadForm over the node: the controls held go first, the form reads its own (its attributes and its
	// commands with it) and builds its children anew (LoadNode), its names made again.
	ibDataNode handed = node;
	RestoreProperties(handed);
	ClearVisualHost();
	for (unsigned int idx = m_valueForm->GetChildCount(); idx > 0; idx--) {
		if (ibValueFrame* const child = m_valueForm->GetChild(idx - 1))
			m_valueForm->RemoveChild(child);
	}
	const bool loaded = m_valueForm->LoadNode(handed);
	m_valueForm->InvalidateNames();
	CreateVisualHost();
	if (!loaded) {
		const ibValueMetaObjectFormBase* const creator = m_valueForm->GetFormMetaObject();
		const wxString formName = creator != nullptr ? creator->GetName() : wxString();
		ibJournalWarning(wxT("designer"), _("Form editor: the form \"%s\" handed back could not be read whole."),
			formName.c_str());
	}
	return loaded;
}

bool ibFormDesignView::SaveForm()
{
	ibMetaDocument* const document = GetDocument();
	ibValueMetaObjectFormBase* const creator =
		document != nullptr ? document->ConvertMetaObjectToType<ibValueMetaObjectFormBase>() : nullptr;

	if (creator != nullptr && m_valueForm != nullptr)
		creator->SaveFormData(m_valueForm);

	return true;
}

void ibFormDesignView::WriteForm(ibDataNode& response)
{
	if (m_valueForm == nullptr)
		return;

	// The form whole — the client holds it from here on, so nothing is left to draw in part.
	UpdateControl(m_valueForm);
	UpdateVisualHost(response.Child(ibProtocol::ibView::ibFormDesigner::Form));

	// …and the trees beside it — its attributes, its commands.
	ibJournalStopwatch attributes, commands;
	attributes.Resume();
	FillAttributeTree(response.Child(ibProtocol::ibView::ibFormDesigner::AttributeTree));
	attributes.Pause();
	commands.Resume();
	FillCommandTree(response.Child(ibProtocol::ibView::ibFormDesigner::CommandTree));
	commands.Pause();
	ibJournalInfo(wxT("client"), wxT("      the form's attribute tree written %lld ms, its command tree %lld ms"),
		attributes.Ms(), commands.Ms());
}

namespace {

// The classes a control adds in itself from its menu — the desktop's PrepareDefaultMenu (frontend/visualView/ctrl/
// notebookMenu.cpp, tableBoxMenu.cpp, tableBoxColumnGroup.cpp), its items' titles as they were.
struct ibControlMenuItem {
	const char* control;
	const char* added;
	const char* title;
};

const ibControlMenuItem s_controlMenu[] = {
	{ ibProtocol::ibForm::ibType::Notebook,            ibProtocol::ibForm::ibType::NotebookPage,        wxTRANSLATE("Add page") },
	{ ibProtocol::ibForm::ibType::Tablebox,            ibProtocol::ibForm::ibType::TableboxColumn,      wxTRANSLATE("Add column") },
	{ ibProtocol::ibForm::ibType::Tablebox,            ibProtocol::ibForm::ibType::TableboxColumnGroup, wxTRANSLATE("Add column group") },
	{ ibProtocol::ibForm::ibType::TableboxColumnGroup, ibProtocol::ibForm::ibType::TableboxColumn,      wxTRANSLATE("Add column") },
	{ ibProtocol::ibForm::ibType::TableboxColumnGroup, ibProtocol::ibForm::ibType::TableboxColumnGroup, wxTRANSLATE("Add column group") },
};

} // namespace

void ibFormDesignView::WriteClasses(ibDataNode& response)
{
	// The classes a control may be made of, with their pictures — the editor's palette and its trees' icons; one the
	// desktop did not offer to place (IsControlSystem — a sizer's item, a page) is not Visible. Each is made by the
	// server (Control — its palette, its paste), so nothing of one is written but what is shown.
	ibDataNode& classes = response.Child(ibProtocol::ibView::ibFormDesigner::Classes);
	for (const ibCtorAbstractType* ctor : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_control)) {
		const wxString className = ctor->GetClassName();
		ibDataNode& described = classes.AddChild(0, 0);
		described.SetValue(ibProtocol::ibView::ibFormDesigner::Name, className);
		described.SetValue(ibProtocol::ibView::ibFormDesigner::Picture, ibBackendPicture::SendPicture(ctor->GetClassType()));
		const ibCtorControlTypeBase* const controlCtor = dynamic_cast<const ibCtorControlTypeBase*>(ctor);
		if (controlCtor == nullptr)
			continue;
		described.SetValue(ibProtocol::ibView::ibFormDesigner::Type, controlCtor->GetTypeControlName());

		// …its menu made once: Child adds a node each time it is called (a table's "Add column" was lost to its group's).
		ibDataNode* menu = nullptr;
		for (const ibControlMenuItem& item : s_controlMenu) {
			if (className != item.control)
				continue;
			if (menu == nullptr)
				menu = &described.Child(ibProtocol::ibView::ibFormDesigner::Menu);
			ibDataNode& entry = menu->AddChild(0, 0);
			entry.SetValue(ibProtocol::ibView::ibFormDesigner::Name, wxString(item.added));
			entry.SetValue(ibProtocol::ibView::ibFormDesigner::Title, wxGetTranslation(item.title));
		}

		if (controlCtor->IsControlSystem())
			described.SetValue(ibProtocol::ibView::ibFormDesigner::Visible, false);
	}
}

void ibFormDesignView::WriteRows(ibValueFrame* control, ibDataNode& controls)
{
	// What its state hides not sent (OnRefresh); no Tab: an edit of them is the client's.
	ibDataNode& described = controls.AddChild(0, 0);
	described.SetValue(ibProtocol::ibView::ibFormDesigner::Control, static_cast<std::int32_t>(control->GetControlID()));
	{
		ibPropertyInspector inspector;
		inspector.SelectObject(control, control->GetControlID(), 0);
		inspector.OnDraw(described);
	}

	// …and its command bar's — the desktop's "Command interface" node: the bar's rows, each command's of it by its name
	// (Items — what its command is called, its picture: the desktop's inspector read them off the command itself), and a
	// new command's with the node it is (AddCommandItem). The commands are the client's to add, in the control's node; a
	// command made here to be written is taken off again.
	if (control->HasCommandBar()) {
		ibValueCommandBar* const bar = control->GetCommandBar();
		ibDataNode& barRows = described.Child(ibProtocol::ibView::ibFormDesigner::CommandBar);
		{
			ibPropertyInspector inspector;
			inspector.SelectObject(bar, control->GetControlID(), 0);
			inspector.OnDraw(barRows);
		}
		ibDataNode& commandRows = barRows.Child(ibProtocol::ibView::ibFormDesigner::Items);
		for (unsigned int idx = 0; idx < bar->GetCommandItemCount(); idx++) {
			ibValueCommandBarItem* const item = bar->GetCommandItem(idx);
			if (item == nullptr)
				continue;
			ibDataNode& itemRows = commandRows.AddChild(0, 0);
			itemRows.SetValue(ibProtocol::ibView::ibFormDesigner::Name, item->GetName());
			ibPropertyInspector inspector;
			inspector.SelectObject(item, control->GetControlID(), 0);
			inspector.OnDraw(itemRows);
		}
		ibValueCommandBarItem* const item = bar->AddCommandItem();
		{
			ibDataNode& itemRows = described.Child(ibProtocol::ibView::ibFormDesigner::CommandBarItem);
			ibPropertyInspector inspector;
			inspector.SelectObject(item, control->GetControlID(), 0);
			inspector.OnDraw(itemRows);
			item->WriteData(itemRows.Child(ibProtocol::ibView::ibFormDesigner::Node));
		}
		bar->RemoveCommandItem(item);
	}
}

bool ibFormDesignView::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	const ibDataNode* const form = args.FindChild(ibProtocol::ibView::ibFormDesigner::Form);
	if (form == nullptr)
		return ibModuleTextView::OnClientEvent(event, args);

	// The form tested — as it stands on the client, shown as the runtime shows it (the desktop's TestForm).
	if (event == ibProtocol::ibEvent::ibKind::Command &&
		static_cast<ibProtocol::ibView::ibModule::ibCommand>(args.GetValue<std::int32_t>(ibProtocol::ibView::ibModule::Id)) == ibProtocol::ibView::ibModule::ibCommand::TestForm) {
		if (!LoadForm(*form))
			return false;
		// Down to the doc/view side explicitly: ibMetaDocument IS-A ibBackendMetaDocument AND an ibDocument, so both
		// ShowForm overloads would match it equally well.
		return m_valueForm->ShowForm(static_cast<ibDocument*>(GetDocument()), false);
	}
	if (event != ibProtocol::ibEvent::ibKind::Change)
		return ibModuleTextView::OnClientEvent(event, args);

	ibMetaDocument* const document = GetDocument();
	const ibValueMetaObjectFormBase* const creator =
		document != nullptr ? document->ConvertMetaObjectToType<ibValueMetaObjectFormBase>() : nullptr;
	if (creator == nullptr || !creator->IsEditable() || !LoadForm(*form))
		return false;

	// …and written into its metaobject at once, the configuration modified: it is saved from its tree too, past this
	// document.
	SaveForm();
	document->Modify(true);
	return true;
}

void ibFormDesignView::RestoreProperties(ibDataNode& node)
{
	// The wire does not say which area a key came from (ibJsonProvider — a scalar comes back a field): every field is
	// put back as a property too, so a property's reader finds it where WriteData put it; the header's fields stay.
	const std::vector<std::pair<ibString, ibDataValue>> fields = node.Fields();
	for (const std::pair<ibString, ibDataValue>& field : fields) {
		if (node.FindProperty(field.first) == nullptr)
			node.SetProperty(field.first, field.second);
	}
	for (ibDataNode& child : node.Children())
		RestoreProperties(child);
	// …and a node held in a property too: the form's Attributes, its FormCommands — an entry's Name read back a field
	// was no Name to its reader, the attribute renamed to nothing on the next load (2026-10-15).
	for (const std::pair<ibString, ibDataValue>& property : node.Properties()) {
		if (property.second.Kind() == ibDataKind::Child && property.second.AsChild() != nullptr)
			RestoreProperties(*property.second.AsChild());
	}
}

// ----------------------------------------------------------------------------
// ibFormDesignDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibFormDesignDocument, ibModuleTextDocument);

bool ibFormDesignDocument::Save()
{
	// The server's save writes the configuration without asking the document (ibMetaDocument::Save) — so the form goes
	// into its metaobject first, as the desktop's did on save.
	if (ibFormDesignView* const view = GetFirstView())
		view->SaveForm();
	return ibModuleTextDocument::Save();
}

ibFormDesignView* ibFormDesignDocument::GetFirstView() const
{
	return dynamic_cast<ibFormDesignView*>(ibModuleTextDocument::GetFirstView());
}
