////////////////////////////////////////////////////////////////////////////
//	Description : forms — the other tree, on the same skeleton
////////////////////////////////////////////////////////////////////////////
//
// ⭐ A FORM IS NOT A DIFFERENT KIND OF THING. The metadata tree is
// ibValueMetaObject's children; the form tree is ibValueFrame's; both ride the
// same property object. So these verbs are the metadata verbs again — list what
// exists, read one, see what fits inside, add, set, remove — and anything
// learned about one tree is true of the other.
//
// WHY IT MATTERS MORE THAN IT LOOKS. Everything built without it is FACELESS: a
// catalog with no form is a table nobody sees. A developer judges a
// configuration by reading the tree; the person the configuration is FOR judges
// it by looking at a form. Until this exists, the loop ends one step before the
// only place that person lives.
//
// TWO HALVES TO A CONTROL, and the second is easy to forget: properties, and
// EVENTS. An event is stored as a property of its own and knows its own
// arguments, so it is answered here beside the rest rather than through a verb
// that would have to be remembered separately.
//
// ADDRESSED BY (form, controlId). A control's id is unique inside its form,
// which is exactly the scope a caller is working in; the form itself is a
// metaobject and keeps its NodeId like everything else.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"
#include "backend/mcp/mcpClipboard.h"   // the caller's own board — not the one the keyboard uses

#include "backend/metaCollection/genericData.h"   // the owner builds the source a form binds to
#include "backend/metaCollection/metaFormObject.h"
#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metadataConfiguration.h"
#include "backend/backend_command.h"                            // WalkCommand — the receiver judges a binding
#include "backend/commandDescription.h"
#include "backend/propertyManager/property/propertyCommandSource.h"
#include "backend/propertyManager/property/propertySource.h"
#include "backend/sourceDescription.h"
#include "backend/backend_localization.h"   // a caption is an array by language
#include "backend/propertyManager/property/propertyString.h"   // ibPropertyTString
#include "backend/stringUtils.h"   // GenerateSynonym — the caption the platform makes from a name

#include "frontend/visualView/ctrl/form.h"
#include "frontend/visualView/ctrl/formAttribute.h"   // the main attribute — the head of every binding
#include "frontend/visualView/ctrl/formCommand.h"     // …and what the form can DO

#include <wx/tokenzr.h>

namespace {
using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgForm()
{
	static const ibArg s_a(wxT("form"), ibArg::Kind::Whole,
		_("The form's NodeId."), /*required*/ true);
	return s_a;
}

const ibArg& ArgControl()
{
	static const ibArg s_a(wxT("control"), ibArg::Kind::Whole,
		_("The control's id, from form_get. Omit for the form itself."));
	return s_a;
}

const ibArg& ArgClass()
{
	static const ibArg s_a(wxT("class"), ibArg::Kind::Text,
		_("The control's class: Textctrl, Tablebox, Button, Staticline, Boxsizer..."), /*required*/ true);
	return s_a;
}

const ibArg& ArgParent()
{
	static const ibArg s_a(wxT("parent"), ibArg::Kind::Whole,
		_("The control it goes inside, from form_get. Omit for the form itself."));
	return s_a;
}

const ibArg& ArgName()
{
	static const ibArg s_a(wxT("name"), ibArg::Kind::Text,
		_("What to call it. This is the name a module refers to it by."));
	return s_a;
}

const ibArg& ArgProperty()
{
	static const ibArg s_a(wxT("property"), ibArg::Kind::Text,
		_("Which property - or which EVENT, they are named in one space and form_control "
			  "lists them together."), /*required*/ true);
	return s_a;
}

const ibArg& ArgValue()
{
	static const ibArg s_a(wxT("value"), ibArg::Kind::Text,
		_("The value. For a closed set, the word. For an event, the NAME of the procedure "
			  "in the form's module that handles it - the answer says which arguments it "
			  "receives."));
	return s_a;
}

const ibArg& ArgPath()
{
	static const ibArg s_a(wxT("path"), ibArg::Kind::Text,
		_("Unfold this far in, as a dotted path of field names: 'Products' for a "
			  "tabular section's columns, 'Warehouse' to step through a reference into "
			  "what it points at. Omit for the top level."));
	return s_a;
}

const ibArg& ArgSlot()
{
	static const ibArg s_a(wxT("slot"), ibArg::Kind::Text,
		_("Which buffer to put it in. Omit for the usual one."));
	return s_a;
}

// THE FORM AS A TREE, built the way the editor builds it — there is one road in
// (ibValueForm + LoadFormData) and this is it. Returned as a ref-counted value
// the caller owns for the length of one question: nothing here edits a form
// somebody has open, and nothing here leaves one behind.
ibValueForm* OpenForm(const ibDataNode& params, wxString& refusal,
	bool* generated = nullptr, ibValueMetaObjectFormBase** creatorOut = nullptr)
{
	if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
		refusal = _("No configuration is open.");
		return nullptr;
	}

	const s32 id = (s32)ArgForm().Whole(params);
	if (id <= 0) {
		refusal = _("Pass the form's NodeId - metadata_get on the owning object lists its forms.");
		return nullptr;
	}

	ibValueMetaObject* object = ibFindMetaObjectById(activeMetaData, (ibMetaID)id);
	if (object == nullptr) {
		refusal = wxString::Format(_("Nothing in this configuration has id %i."), (int)id);
		return nullptr;
	}

	ibValueMetaObjectFormBase* creator =
		object->ConvertToType<ibValueMetaObjectFormBase>();
	if (creator == nullptr) {
		refusal = wxString::Format(_("'%s' is not a form."), object->GetName());
		return nullptr;
	}

	// Handed back so a caller that CHANGES the form can store it again: the value
	// form is a working copy, the metaobject is where the layout lives.
	if (creatorOut != nullptr)
		*creatorOut = creator;

	if (generated != nullptr)
		*generated = creator->GetFormData().IsEmpty();

	// ⭐⭐ THROUGH THE OWNER, BECAUSE THE OWNER IS WHAT BINDS THE SOURCE.
	//
	// There are two kinds of form and they enter differently: a form declared
	// under an object goes through that object, which builds the SOURCE its kind
	// implies — a list form gets the list, an object form a new object — and a
	// COMMON form, standing under Common, builds standalone. Both land on
	// CreateAndBuildForm underneath; the difference is entirely the source.
	//
	// The first version called CreateAndBuildForm directly and passed nullptr for
	// the source. The form came back with nothing in it, because a generated
	// layout is generated FROM the source: no source, no fields. The mistake was
	// not the missing argument — it was reaching past the owner for a thing only
	// the owner knows how to make.
	ibBackendValueForm* built = nullptr;

	if (const ibValueMetaObjectGenericData* owner =
			object->GetParent() != nullptr
				? object->GetParent()->ConvertToType<ibValueMetaObjectGenericData>()
				: nullptr) {

		built = owner->CreateObjectForm(creator);
	}
	else {
		built = ibValueMetaObjectFormBase::CreateAndBuildForm(
			creator, creator->GetTypeForm(), nullptr, nullptr, wxNullUniqueKey);
	}

	ibValueForm* form = dynamic_cast<ibValueForm*>(built);
	if (form == nullptr) {
		refusal = wxString::Format(
			_("'%s' could not be opened. Its module may have refused - messages_read has "
			  "what the platform said."), object->GetName());
		return nullptr;
	}

	form->IncrRef();
	return form;
}

// ⭐ THE ONE PLACE THAT SAYS A CONTROL — the twin of ibMcpSayObject for the other tree with
// properties on it. The tree walk said it one way and the three verbs that answer about a single
// control each said it their own: `controlId` here and nowhere else, `class` without `kind`, a
// name emitted even when it is only the class name repeated. A caller then holds one vocabulary
// per verb instead of one per server.
//
// Asked of the control's own methods, so nothing here knows how a form is stored.
void SayControl(ibValueFrame* control, ibDataNode& node)
{
	if (control == nullptr)
		return;

	node.AddField(wxT("controlId"), ibDataValue::Int((s64)control->GetControlID()));
	node.SetValue(wxT("class"), control->GetClassName());

	// Only when it says something the class has not. An unnamed control answers with its class,
	// and repeating it reads as a name somebody chose.
	const wxString name = control->GetControlName();
	if (!name.IsEmpty() && name != control->GetClassName())
		node.SetValue(wxT("name"), name);

	// The FAMILY, beside the class: a sizer, a control, a sizer-item. It decides
	// what may go inside, and a caller reading only class names cannot tell a
	// container from a leaf.
	node.SetValue(wxT("kind"), control->GetObjectTypeName());
}

// One control, and everything under it. Recursive because the tree is, and
// because a caller asking about a form wants its shape, not its first row.
ibDataValue ControlEntry(ibValueFrame* control)
{
	std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();

	SayControl(control, *node);

	std::vector<ibDataValue> children;
	for (unsigned int index = 0; index < control->GetChildCount(); ++index) {
		if (ibValueFrame* child = control->GetChild(index))
			children.push_back(ControlEntry(child));
	}

	if (!children.empty())
		node->AddField(wxT("children"), ibDataValue::Array(children));

	return ibDataValue::Child(node);
}

// Depth-first by id. The ids are unique inside a form, so the first match is the
// only match.
ibValueFrame* FindControl(ibValueFrame* from, ibFormID wanted)
{
	if (from == nullptr)
		return nullptr;

	if (from->GetControlID() == wanted)
		return from;

	for (unsigned int index = 0; index < from->GetChildCount(); ++index) {
		if (ibValueFrame* found = FindControl(from->GetChild(index), wanted))
			return found;
	}

	return nullptr;
}


} // namespace

//---------------------------------------------------------------------------
// form_get
//---------------------------------------------------------------------------
class ibMcpToolFormGet : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("form_get"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(_("reading the form '%s'"), ibMcpNameOf(params, ArgForm().Name()));
	}

	wxString GetDescription() const override
	{
		return _("A form's three branches: its CONTROLS as a tree, its ATTRIBUTES (one of them "
			"the main one, through which every binding starts), and its COMMANDS with what "
			"each runs. The ids are what every other form verb addresses a part by.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		bool generated = false;
		ibValueForm* form = OpenForm(params, refusal, &generated);
		if (form == nullptr)
			return false;

		result.SetValue(wxT("form"), form->GetControlName());

		std::vector<ibDataValue> children;
		for (unsigned int index = 0; index < form->GetChildCount(); ++index) {
			if (ibValueFrame* child = form->GetChild(index))
				children.push_back(ControlEntry(child));
		}

		result.AddField(wxT("controls"), ibDataValue::Array(children));

		// ⭐⭐ A FORM HAS THREE BRANCHES, not one. Controls are what is seen;
		// ATTRIBUTES are what it holds (one of them the MAIN one, through which
		// every binding starts); COMMANDS are what it can do. Answering with the
		// controls alone described a third of the thing and hid the half a binding
		// or a button actually needs.
		std::vector<ibDataValue> attributes;
		for (unsigned int index = 0; index < form->GetAttributeCount(); ++index) {

			ibFormAttributeValue* attribute = form->GetAttribute(index);
			if (attribute == nullptr)
				continue;

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->AddField(wxT("id"), ibDataValue::Int((s64)attribute->GetId()));
			node->SetValue(wxT("name"), attribute->GetName());

			// THE MAIN ONE IS NOT ONE OF THE OTHERS. It is the head of every
			// binding on this form, and a caller that cannot tell it apart cannot
			// build a path at all.
			if (attribute == form->GetMainAttribute())
				node->AddField(wxT("main"), ibDataValue::Bool(true));

			attributes.push_back(ibDataValue::Child(node));
		}

		result.AddField(wxT("attributes"), ibDataValue::Array(attributes));

		std::vector<ibDataValue> commands;
		for (const ibValuePtr<ibFormCommandValue>& command : form->GetFormCommands()) {

			if (!command)
				continue;

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->AddField(wxT("id"), ibDataValue::Int((s64)command->GetId()));
			node->SetValue(wxT("name"), command->GetName());

			// What it RUNS. A command with no procedure is a button that does
			// nothing, and that is worth seeing without opening the module.
			const wxString procedure = command->GetProcedure();
			if (!procedure.IsEmpty())
				node->SetValue(wxT("procedure"), procedure);

			commands.push_back(ibDataValue::Child(node));
		}

		result.AddField(wxT("commands"), ibDataValue::Array(commands));

		// WHICH OF THE TWO THIS IS, asked of the metaobject rather than guessed
		// from the shape. A generated layout is what the platform makes from the
		// object; a stored one is what somebody arranged. They read alike and they
		// are not the same thing — arranging the first means the form stops
		// following the object it was generated from.
		result.AddField(wxT("generated"), ibDataValue::Bool(generated));

		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormGet);

//---------------------------------------------------------------------------
// form_control
//---------------------------------------------------------------------------
class ibMcpToolFormControl : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("form_control"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(_("reading a control of the form '%s'"),
			ibMcpNameOf(params, ArgForm().Name()));
	}

	wxString GetDescription() const override
	{
		return _("One control in full: its properties with what they hold, and its EVENTS with "
			"the arguments each handler receives. An event is stored as a property of its own, "
			"so both come back together - a control's behaviour is not a separate question "
			"from its appearance.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgControl() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueForm* form = OpenForm(params, refusal);
		if (form == nullptr)
			return false;

		ibValueFrame* control = form;

		const s32 wanted = (s32)ArgControl().Whole(params);
		if (wanted > 0) {
			control = FindControl(form, (ibFormID)wanted);
			if (control == nullptr) {
				refusal = wxString::Format(
					_("This form has no control with id %i. form_get lists them."), (int)wanted);
				form->DecrRef();
				return false;
			}
		}

		SayControl(control, result);

		// ⭐ THE SAME WALK THE METADATA TREE GETS — ibMcpSayProperties, because a control is an
		// ibPropertyObject exactly as a metaobject is.
		//
		// 🛑 IT WAS A SECOND, POORER COPY: name, title, editable, value, and nothing else. So a
		// control property with a CLOSED SET — an alignment, an anchor, a border style — answered
		// with the raw number it stores and no hint that only certain words are legal or what they
		// are called, while the identical property on a metaobject answered with its whole
		// vocabulary. Two walks over one idea, and the second was the one that went stale.
		ibMcpSayProperties(control, result);

		// ⭐ AND THE EVENTS. Each carries the ARGUMENTS its handler is called with —
		// which is the half that decides what a handler can be written to do, and
		// the half that cannot be guessed from the event's name.
		std::vector<ibDataValue> events;
		for (unsigned int index = 0; index < control->GetEventCount(); ++index) {

			ibEvent* event = control->GetEvent(index);
			if (event == nullptr)
				continue;

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), event->GetName());

			const wxString handler = event->GetValue().GetString();
			if (!handler.IsEmpty())
				entry->SetValue(wxT("handler"), handler);

			std::vector<ibDataValue> args;
			for (const wxString& arg : event->GetArgs())
				args.push_back(ibDataValue::String(arg));

			if (!args.empty())
				entry->AddField(wxT("arguments"), ibDataValue::Array(args));

			events.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("events"), ibDataValue::Array(events));

		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormControl);

//---------------------------------------------------------------------------
// form_add
//---------------------------------------------------------------------------
//
// ⭐ A GENERATED FORM STOPS BEING GENERATED THE MOMENT IT IS TOUCHED, and that is
// not a side effect worth hiding: a generated layout FOLLOWS the object (add an
// attribute, the field appears), an arranged one does not. So the first edit is
// the decision to take the form over, and the answer says so.
//
// WHERE A CONTROL MAY GO IS THE PLATFORM'S ANSWER, not a rule copied here. The
// factory refuses an impossible nesting by returning nothing, and that refusal is
// passed on as it stands.
//
class ibMcpToolFormAdd : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("form_add"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(_("adding a %s to the form '%s'"),
			ArgClass().Text(params),
			ibMcpNameOf(params, ArgForm().Name()));
	}

	wxString GetDescription() const override
	{
		return _("Put a control on a form - a text box, a table, a button. type_list with "
			"kind=control names every class there is. The form is SAVED afterwards, so a "
			"generated layout becomes a stored one and stops following the object.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgClass(), ArgParent(), ArgName() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectFormBase* creator = nullptr;
		ibValueForm* form = OpenForm(params, refusal, nullptr, &creator);
		if (form == nullptr)
			return false;

		ibValueFrame* parent = form;
		const s32 into = (s32)ArgParent().Whole(params);
		if (into > 0) {
			parent = FindControl(form, (ibFormID)into);
			if (parent == nullptr) {
				refusal = wxString::Format(
					_("This form has no control with id %i. form_get lists them."), (int)into);
				form->DecrRef();
				return false;
			}
		}

		const wxString className = ArgClass().Text(params);

		ibValueFrame* created = form->CreateControl(className, parent);
		if (created == nullptr) {
			// The factory said no. It knows the nesting rules; repeating them here
			// would be a second copy that could disagree with the first.
			refusal = wxString::Format(
				_("A %s cannot go there. type_list with kind=control names the classes; "
				  "form_get shows what each control already holds."), className);
			form->DecrRef();
			return false;
		}

		const wxString name = ArgName().Text(params);
		if (!name.IsEmpty()) {

			created->SetControlName(name);

			// ⭐ THE CAPTION IS GENERATED, NOT TYPED. A metaobject does this for
			// itself when it is renamed — the name splits at its capitals and
			// becomes a sentence — and a control should not be the one place where
			// a person (or a tool) invents the same words by hand and gets them
			// slightly different. `WarehouseCode` becomes "Warehouse code" from the
			// platform's own generator, and a caller that wants something else says
			// so with form_set.
			// ⚠ ASKED OF THE PROPERTY, NOT OF ITS STORAGE. A caption is a
			// translatable string, and an EMPTY one is stored as `en = '';` — the
			// loc array with nothing in it. Testing the stored text for emptiness
			// therefore always answered "not empty", and the generated title was
			// never written once. `IsEmptyProperty` exists for exactly this
			// question and answers it in the property's own terms.
			// ⚠ ASKED OF THE PROPERTY, NOT OF ITS STORAGE. A caption is a
			// translatable string, and an EMPTY one is stored as `en = '';` — the
			// loc array with nothing in it. Testing the stored text for emptiness
			// always answered "not empty", so the generated title was never written
			// once. `IsEmptyProperty` asks the question in the property's own terms.
			ibPropertyTString* title =
				dynamic_cast<ibPropertyTString*>(created->GetProperty(wxT("Title")));

			if (title != nullptr && title->IsEmptyProperty()) {

				// …and written INTO the language cell rather than over the array: a
				// bare SetValue would replace the whole thing with one string and
				// take every other language with it.
				ibBackendLocalizationEntryArray array;
				ibBackendLocalization::CreateLocalizationArray(
					title->GetValueAsString(), array);
				ibBackendLocalization::SetArrayTranslate(
					array, stringUtils::GenerateSynonym(name));

				title->SetValue(ibBackendLocalization::GetRawLocText(array));
			}
		}

		// ⚠ SAVED, OR IT NEVER HAPPENED. The value form is ours for the length of
		// this call; the configuration keeps the LAYOUT, and nothing reaches it
		// until the form is written back.
		if (creator == nullptr || !creator->SaveFormData(form)) {
			refusal = _("The control was added but the form could not be stored.");
			form->DecrRef();
			return false;
		}

		activeMetaData->Modify(true);

		result.AddField(wxT("added"), ibDataValue::Bool(true));
		SayControl(created, result);

		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormAdd);

//---------------------------------------------------------------------------
// form_set
//---------------------------------------------------------------------------
class ibMcpToolFormSet : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("form_set"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(_("setting %s on a control of '%s'"),
			ArgProperty().Text(params),
			ibMcpNameOf(params, ArgForm().Name()));
	}

	wxString GetDescription() const override
	{
		return _("Set one property of one control - its caption, its width, what it is BOUND "
			"to - or one EVENT, whose value is the name of a procedure in the form's module. "
			"form_control lists both and what each holds now; a property whose values are a "
			"closed set is set by its word.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgControl(), ArgProperty(), ArgValue() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectFormBase* creator = nullptr;
		ibValueForm* form = OpenForm(params, refusal, nullptr, &creator);
		if (form == nullptr)
			return false;

		ibValueFrame* control = form;
		const s32 wanted = (s32)ArgControl().Whole(params);
		if (wanted > 0) {
			control = FindControl(form, (ibFormID)wanted);
			if (control == nullptr) {
				refusal = wxString::Format(
					_("This form has no control with id %i."), (int)wanted);
				form->DecrRef();
				return false;
			}
		}

		const wxString name = ArgProperty().Text(params);

		// A control's NAME is what a module calls it by, and on this tree it is not
		// an ordinary property write.
		if (name.IsSameAs(wxT("Name"), false)) {
			control->SetControlName(ArgValue().Text(params));
			result.SetValue(wxT("property"), name);
			result.SetValue(wxT("value"), control->GetControlName());
		}
		else if (ibProperty* property = control->GetProperty(name)) {

			// ONE RULE FOR BOTH TREES — see ibMcpSetProperty.
			if (!ibMcpSetProperty(property, params, result, refusal)) {
				form->DecrRef();
				return false;
			}
		}
		// ⭐ AN EVENT IS ADDRESSED THE SAME WAY A PROPERTY IS, so it is answered by
		// the same verb rather than by a second one. ibEvent and ibProperty are
		// SIBLINGS — both ibBackendProperty — and form_control already lists them
		// together; a caller reading that list and then being told the name does
		// not exist would be reading one tool's answer into another tool's blind
		// spot.
		//
		// ⚠ AND WITHOUT IT THERE IS NO MOUSE. A button could be put on a form and
		// never given anything to do: writing a print module and laying a template
		// produced something only a script could reach. The handler is the last
		// link between what is built and what a person can press.
		//
		// The value is a plain string — the NAME of a procedure in the form's
		// module — so this does not go through ibMcpSetProperty: there is no enum,
		// no list and no language dimension to a handler name.
		else if (ibEvent* event = control->GetEvent(name)) {

			const wxString handler = ArgValue().Text(params);

			event->SetValue(handler);

			result.SetValue(wxT("event"), name);
			result.SetValue(wxT("handler"), handler);

			// WHAT THE HANDLER RECEIVES, said here because it cannot be guessed
			// from the event's name and decides what the procedure can be written
			// to do.
			std::vector<ibDataValue> args;
			for (const wxString& arg : event->GetArgs())
				args.push_back(ibDataValue::String(arg));

			result.AddField(wxT("arguments"), ibDataValue::Array(args));
		}
		else {
			refusal = wxString::Format(
				_("'%s' has no property or event called '%s'. form_control lists both."),
				control->GetControlName(), name);
			form->DecrRef();
			return false;
		}

		if (creator == nullptr || !creator->SaveFormData(form)) {
			refusal = _("The property was set but the form could not be stored.");
			form->DecrRef();
			return false;
		}

		activeMetaData->Modify(true);
		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormSet);

//---------------------------------------------------------------------------
// form_remove
//---------------------------------------------------------------------------
class ibMcpToolFormRemove : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("form_remove"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(_("removing a control from '%s'"),
			ibMcpNameOf(params, ArgForm().Name()));
	}

	wxString GetDescription() const override
	{
		return _("Take a control off a form, with everything inside it - the other half of "
			"form_add, so a wrong step can be undone without a person opening the editor.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgControl() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectFormBase* creator = nullptr;
		ibValueForm* form = OpenForm(params, refusal, nullptr, &creator);
		if (form == nullptr)
			return false;

		const s32 wanted = (s32)ArgControl().Whole(params);
		ibValueFrame* control = FindControl(form, (ibFormID)wanted);
		if (control == nullptr) {
			refusal = wxString::Format(_("This form has no control with id %i."), (int)wanted);
			form->DecrRef();
			return false;
		}

		// Named before it goes, because afterwards there is nothing left to name.
		const wxString name = control->GetControlName();

		form->RemoveControl(control);

		if (creator == nullptr || !creator->SaveFormData(form)) {
			refusal = _("The control was removed but the form could not be stored.");
			form->DecrRef();
			return false;
		}

		activeMetaData->Modify(true);

		result.AddField(wxT("removed"), ibDataValue::Bool(true));
		result.SetValue(wxT("name"), name);

		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormRemove);

//---------------------------------------------------------------------------
// form_source
//---------------------------------------------------------------------------
//
// ⭐ WHAT A CONTROL CAN BE BOUND TO — asked of the SOURCE, which is the only
// thing that knows.
//
// A form is bound to a source object, and that source publishes what it offers
// through its explorer: every field with its name, its human title, its type, and
// whether it is a TABULAR SECTION — which is not a field but a source of its own,
// with columns underneath it. That is why the answer is a tree and not a list: a
// tablebox binds to the section, and each of its columns binds to a column of
// that section.
//
// Nothing here decides what is bindable. The explorer is what the form editor's
// own binding drop-down is built from, so a field that becomes available tomorrow
// is offered here the day it appears.
//
// ⚠ READ ONCE, WALKED FROM THE COPY. Re-materialising a source RESETS the helper
// vector these nodes live in, so a walk that outlives the call would dangle. This
// answers into a node and lets go.
//
class ibMcpToolFormSource : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("form_source"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(_("looking at what the form '%s' can bind to"),
			ibMcpNameOf(params, ArgForm().Name()));
	}

	wxString GetDescription() const override
	{
		return _("What the form's source offers to bind a control to: every field with its "
			"name, its title and its type, and every TABULAR SECTION with the columns under "
			"it. Ask this before form_set on a binding - a name that is not here is a name "
			"the form cannot reach.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgPath() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueForm* form = OpenForm(params, refusal);
		if (form == nullptr)
			return false;

		ibSourceDataObject* source = form->GetSourceObject();
		const ibSourceExplorer* explorer =
			source != nullptr ? source->GetSourceExplorer() : nullptr;

		if (explorer == nullptr) {
			// A COMMON form has no source, and that is a state rather than a fault:
			// its controls bind to the form's own attributes instead.
			result.SetValue(wxT("note"),
				_("This form has no source object - a common form binds to its own attributes."));
			result.AddField(wxT("fields"), ibDataValue::Array(std::vector<ibDataValue>()));
			form->DecrRef();
			return true;
		}

		// ⭐ ONE STEP PER SEGMENT, by NAME and case-insensitively — the same
		// resolution the model layer uses everywhere (FindByName exists because
		// three call sites wrote this loop by hand and one compared exactly, so a
		// path written `Supplier.region` silently stopped matching `Region`).
		const wxString path = ArgPath().Text(params);
		wxString walked;

		if (!path.IsEmpty()) {
			wxStringTokenizer segments(path, wxT("."));
			while (segments.HasMoreTokens()) {

				const wxString segment = segments.GetNextToken();
				const ibSourceExplorer* next = explorer->FindByName(segment);

				// ⭐⭐ A REFERENCE HOP IS NOT A CHILD LOOKUP. A section's columns are
				// nodes of the explorer; the fields of what a REFERENCE points at are
				// not — the target is another object entirely, and materialising it
				// eagerly is what makes the tree infinite.
				//
				// So the hop is resolved the way the platform states identity: a
				// reference's class id is CONSTRUCTIVE, its body being the metaID of
				// the object it points at. Read the id, find the object, answer with
				// ITS fields. No lookup table, and true for a metatype added tomorrow.
				if (next != nullptr && next->GetHelperCount() == 0
					&& IsReference(next->GetTypeDesc().GetFirstClsid())) {

					const ibMetaID target =
						(ibMetaID)(next->GetTypeDesc().GetFirstClsid() & kIbClsidBodyMask);

					if (ibValueMetaObject* pointed = ibFindMetaObjectById(activeMetaData, target)) {

						walked = walked.IsEmpty() ? segment : walked + wxT(".") + segment;

						result.SetValue(wxT("source"), pointed->GetName());
						result.SetValue(wxT("path"), walked);
						result.AddField(wxT("throughReference"), ibDataValue::Bool(true));
						result.AddField(wxT("fields"), Referenced(pointed));

						form->DecrRef();
						return true;
					}
				}

				if (next == nullptr) {
					// A MISS IS A REFUSAL WITH THE ALTERNATIVES, because the caller's
					// next move is to pick one of them.
					wxString available;
					for (unsigned int index = 0; index < explorer->GetHelperCount(); ++index) {
						if (const ibSourceExplorer* field = explorer->GetHelper(index))
							available << (available.IsEmpty() ? wxT("") : wxT(", "))
								<< field->GetSourceName();
					}

					refusal = wxString::Format(
						_("'%s' has no field called '%s'. It offers: %s."),
						walked.IsEmpty() ? explorer->GetSourceName() : walked, segment, available);
					form->DecrRef();
					return false;
				}

				explorer = next;
				walked = walked.IsEmpty() ? segment : walked + wxT(".") + segment;
			}
		}

		result.SetValue(wxT("source"), explorer->GetSourceName());
		if (!walked.IsEmpty())
			result.SetValue(wxT("path"), walked);

		const std::vector<ibDataValue> fields = Fields(explorer);
		result.AddField(wxT("fields"), ibDataValue::Array(fields));

		// A LEAF IS AN ANSWER TOO. A field with nothing under it is where a binding
		// ends; saying so saves a caller one more descent to find out.
		if (fields.empty())
			result.SetValue(wxT("note"),
				_("Nothing unfolds from here - this is where a binding ends."));

		form->DecrRef();
		return true;
	}

private:

	// WHAT AN OBJECT OFFERS WHEN A REFERENCE IS STEPPED INTO. Asked of the
	// metaobject, because that is where the answer lives once the hop has left the
	// form's own source behind — and asked one level deep, for the same reason the
	// rest of this is lazy.
	static ibDataValue Referenced(ibValueMetaObject* object)
	{
		std::vector<ibDataValue> out;

		for (unsigned int index = 0; index < object->GetChildCount(); ++index) {

			ibValueMetaObject* child = object->GetChild(index);
			if (child == nullptr || child->IsDeleted())
				continue;

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->SetValue(wxT("name"), child->GetName());
			node->AddField(wxT("id"), ibDataValue::Int((s64)child->GetMetaID()));

			const wxString synonym = child->GetSynonym();
			if (!synonym.IsEmpty() && synonym != child->GetName())
				node->SetValue(wxT("title"), synonym);

			out.push_back(ibDataValue::Child(node));
		}

		return ibDataValue::Array(out);
	}

	// One level, and one only. Recursion here is what makes the tree infinite —
	// see the note at the `unfolds` flag.
	static std::vector<ibDataValue> Fields(const ibSourceExplorer* explorer)
	{
		std::vector<ibDataValue> out;

		for (unsigned int index = 0; index < explorer->GetHelperCount(); ++index) {

			const ibSourceExplorer* field = explorer->GetHelper(index);
			if (field == nullptr)
				continue;

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();

			// The NAME is what a binding writes; the SYNONYM is what a person reads
			// on the form. Both, and named apart — the same distinction a query field
			// keeps, and for the same reason.
			node->SetValue(wxT("name"), field->GetSourceName());

			// The id is what the BINDING is made of — a path is a chain of these,
			// and a name is how a person writes one down.
			node->AddField(wxT("id"), ibDataValue::Int((s64)field->GetSourceId()));

			const wxString synonym = field->GetSourceSynonym();
			if (!synonym.IsEmpty() && synonym != field->GetSourceName())
				node->SetValue(wxT("title"), synonym);

			if (!field->GetSourceGroup().IsEmpty())
				node->SetValue(wxT("group"), field->GetSourceGroup());

			// A SECTION IS NOT A FIELD. It is a source of its own, and a control
			// bound to it is a table rather than a box.
			if (field->IsTableSection())
				node->AddField(wxT("tabularSection"), ibDataValue::Bool(true));

			// ⭐⭐ LAZY, AND NOT AS AN OPTIMISATION. Unfolding eagerly walks into a
			// tabular section and then, at a REFERENCE, into the object it points
			// at — which has references of its own, and so on until the stack ends.
			// A source tree has no natural bottom; it has a bottom only relative to
			// the question being asked.
			//
			// So one level is answered, each node says whether there is more under
			// it, and the caller descends by naming the next segment in `path` —
			// which is exactly what a binding IS: a hop at a time.
			// A reference is told from its CLASS ID, which carries the kind in its
			// high byte — no metadata lookup, and no list of reference types kept
			// by hand.
			const bool unfolds = field->IsTableSection()
				|| field->GetHelperCount() > 0
				|| IsReference(field->GetTypeDesc().GetFirstClsid());

			if (unfolds)
				node->AddField(wxT("unfolds"), ibDataValue::Bool(true));

			out.push_back(ibDataValue::Child(node));
		}

		return out;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormSource);

//---------------------------------------------------------------------------
// form_bind
//---------------------------------------------------------------------------
//
// ⭐ A BINDING IS A PATH, NOT A NAME, which is why it needs a verb of its own.
//
// form_set refused it and said so exactly: "wrong value kind (expected 5, got
// 4)" — the property holds a STRUCTURE, and a dotted string is a string. What it
// holds is a chain of HOPS, one per step: the field on the source, then a column
// of the section it named, or a field of the object a reference pointed at.
// Precisely what form_source unfolds, one level at a time.
//
// So this takes the path in the words a person writes it in and resolves it the
// way the form will read it — segment by segment, through the same explorer, and
// through metadata when a segment steps across a reference. A segment that does
// not resolve is refused WITH what that level offers, because the caller's next
// move is to pick one of them.
//
// SAME REASON metadata_set_type EXISTS BESIDE metadata_set: the commonest edit
// whose value is a structure gets a door where the structure can be said in
// words, instead of a general door where it cannot be said at all.
//
class ibMcpToolFormBind : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("form_bind"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(_("binding a control of '%s' to %s"),
			ibMcpNameOf(params, ArgForm().Name()),
			ArgPath().Text(params));
	}

	wxString GetDescription() const override
	{
		return _("Bind a control to what the form's source offers, by the dotted path "
			"form_source unfolds - 'Warehouse.Code' reaches through the reference into the "
			"catalog it points at, 'Products.Quantity' a column of a tabular section. A "
			"binding is a chain of hops, so it cannot be set as plain text through form_set.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgControl(), ArgPath(), ArgProperty() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectFormBase* creator = nullptr;
		ibValueForm* form = OpenForm(params, refusal, nullptr, &creator);
		if (form == nullptr)
			return false;

		// ⚠ "NO CONTROL WITH THAT ID" IS THE WRONG SENTENCE WHEN NO ID CAME. It says the form was
		// searched and answers about a control the caller never named — so the caller goes looking
		// for a control that is missing, when what is missing is their own argument (2026-09-01).
		const s32 wanted = (s32)ArgControl().Whole(params);

		if (!ArgControl().Given(params)) {
			refusal = _("Which control? form_bind needs the control's id — form_get lists them.");
			form->DecrRef();
			return false;
		}

		ibValueFrame* control = FindControl(form, (ibFormID)wanted);
		if (control == nullptr) {
			refusal = _("This form has no control with that id. form_get lists them.");
			form->DecrRef();
			return false;
		}

		wxString name = ArgProperty().Text(params);
		if (name.IsEmpty())
			name = wxT("Source");

		ibProperty* property = control->GetProperty(name);

		// ⭐⭐ A COMMAND IS BOUND BY A PATH TOO, and by the same kind of path — hops
		// that walk, each step naming the next. What differs is only where the
		// candidates live: a source's are the fields of what the form holds, a
		// command's are the form's own commands and the ones its object declares.
		//
		// So it is the same verb. Asking a caller to remember a second one for the
		// same idea is how two mechanisms drift into three.
		if (ibPropertyCommandSource* wiring =
				dynamic_cast<ibPropertyCommandSource*>(property)) {

			const wxString wanted = ArgPath().Text(params);

			ibCommandDescription command;
			wxString available;

			// The FORM'S own commands first — they are the ones a button on this
			// form most often runs.
			for (const ibValuePtr<ibFormCommandValue>& entry : form->GetFormCommands()) {
				if (!entry)
					continue;

				available << (available.IsEmpty() ? wxT("") : wxT(", ")) << entry->GetName();
				if (entry->GetName().IsSameAs(wanted, false))
					command.AppendCommand(entry->GetId());
			}

			// …then the ones the OBJECT declares, which every form of it inherits.
			if (!command.IsOk()) {
				if (ibValueMetaObject* owner = ibFindMetaObjectById(activeMetaData,
						(ibMetaID)(s32)ArgForm().Whole(params))) {

					if (ibValueMetaObject* holder = owner->GetParent()) {
						for (unsigned int index = 0; index < holder->GetChildCount(); ++index) {

							ibValueMetaObject* child = holder->GetChild(index);
							if (child == nullptr || child->IsDeleted()
								|| child->GetClassType() != g_metaCommandCLSID)
								continue;

							available << (available.IsEmpty() ? wxT("") : wxT(", "))
								<< child->GetName();
							if (child->GetName().IsSameAs(wanted, false))
								command.AppendCommand(child->GetMetaID());
						}
					}
				}
			}

			if (!command.IsOk()) {
				refusal = wxString::Format(
					_("There is no command called '%s' here. Available: %s."),
					wanted, available.IsEmpty() ? _("none") : available);
				form->DecrRef();
				return false;
			}

			// ⭐ THE PLATFORM IS THE JUDGE. A stored path is just hops; walking them
			// is what tells a live binding from a broken one, and the receiver — the
			// button itself — is what walks. Refusing on its answer rather than on
			// my own is the difference between a binding that works and one that
			// merely looks right.
			wxString leaf;
			if (ibBackendCommandReceiver* receiver =
					dynamic_cast<ibBackendCommandReceiver*>(control)) {

				if (!receiver->WalkCommand(command, &leaf)) {
					refusal = wxString::Format(
						_("'%s' cannot run '%s' - the path does not resolve from this control."),
						control->GetControlName(), wanted);
					form->DecrRef();
					return false;
				}
			}

			wiring->SetValue(command, leaf);

			if (creator == nullptr || !creator->SaveFormData(form)) {
				refusal = _("The command was bound but the form could not be stored.");
				form->DecrRef();
				return false;
			}

			activeMetaData->Modify(true);

			result.AddField(wxT("bound"), ibDataValue::Bool(true));
			SayControl(control, result);
			result.SetValue(wxT("property"), name);
			result.SetValue(wxT("command"), leaf.IsEmpty() ? wanted : leaf);

			// …and the wiring as it is STORED, said by ibCommandDescriptionMemory — the same
			// reading a form loads, so what a caller reads here is what the file holds.
			ibDataValue described;
			if (!ibCommandDescriptionMemory::WriteNode(described, command)) {
				refusal = _("The command was wired, but could not be read back to confirm it.");
				form->DecrRef();
				return false;
			}
			result.AddField(wxT("wiring"), described);

			form->DecrRef();
			return true;
		}

		ibPropertySource* binding = dynamic_cast<ibPropertySource*>(property);

		if (binding == nullptr) {
			refusal = wxString::Format(
				_("'%s' has no binding called '%s'. form_control lists its properties."),
				control->GetControlName(), name);
			form->DecrRef();
			return false;
		}

		ibSourceDataObject* source = form->GetSourceObject();
		const ibSourceExplorer* explorer =
			source != nullptr ? source->GetSourceExplorer() : nullptr;

		if (explorer == nullptr) {
			refusal = _("This form has no source object to bind to.");
			form->DecrRef();
			return false;
		}

		// THE PATH, RESOLVED HOP BY HOP — the same descent form_source makes, and
		// the same one the form makes at run time.
		ibSourceDescription description;
		wxString walked;

		// ⭐⭐ THE HEAD OF A BINDING IS THE FORM'S MAIN ATTRIBUTE, not the first
		// field of the source.
		//
		// A path is read from the FORM outward: the form holds the object in its
		// main attribute, and every hop after that walks inside what that attribute
		// holds. The generated layout writes exactly this — SetSource({ mainAttrId,
		// field }) — so a binding that starts at the field instead is one hop short
		// and resolves to nothing. It stored correctly, read back correctly, and
		// showed as "<not selected>" in the designer: right shape, wrong root.
		//
		// The id is form-local, which is why it is not in the source explorer at
		// all — the explorer describes what the attribute POINTS AT.
		ibFormAttributeValue* mainAttr = form->GetMainAttribute();
		if (mainAttr == nullptr) {
			refusal = _("This form has no main attribute to bind through.");
			form->DecrRef();
			return false;
		}

		description.AppendSource(mainAttr->GetId());

		// ⭐⭐ THE PATH CROSSES A BORDER HALFWAY, and the walk has to cross with it.
		//
		// The first segments live in the form's SOURCE — the explorer's own nodes,
		// a field or a tabular section. But a segment that names a REFERENCE steps
		// out of the source entirely: what it points at is another object, which
		// the explorer deliberately does not materialise (that is what would make
		// the tree infinite). From there on the walk continues through METADATA,
		// resolving each segment against the referenced object's own attributes.
		//
		// One path, two halves, and the hop ids are the same kind of thing on both
		// sides — which is why a binding can express `Warehouse.Code` at all.
		ibValueMetaObject* through = nullptr;

		wxStringTokenizer segments(ArgPath().Text(params), wxT("."));
		while (segments.HasMoreTokens()) {

			const wxString segment = segments.GetNextToken();

			// --- the metadata half ---------------------------------------------
			if (through != nullptr) {

				ibValueMetaObject* field = nullptr;
				wxString available;

				for (unsigned int index = 0; index < through->GetChildCount(); ++index) {

					ibValueMetaObject* child = through->GetChild(index);
					if (child == nullptr || child->IsDeleted())
						continue;

					available << (available.IsEmpty() ? wxT("") : wxT(", ")) << child->GetName();
					if (child->GetName().IsSameAs(segment, false))
						field = child;
				}

				if (field == nullptr) {
					refusal = wxString::Format(
						_("'%s' has no field called '%s'. It offers: %s."),
						through->GetName(), segment, available);
					form->DecrRef();
					return false;
				}

				description.AppendSource((ibSourceId)field->GetMetaID());
				walked = walked + wxT(".") + segment;

				// A reference again? Then the next segment steps once more.
				through = nullptr;
				continue;
			}

			// --- the source half -----------------------------------------------
			const ibSourceExplorer* next = explorer != nullptr
				? explorer->FindByName(segment) : nullptr;

			if (next == nullptr) {

				wxString available;
				if (explorer != nullptr) {
					for (unsigned int index = 0; index < explorer->GetHelperCount(); ++index) {
						if (const ibSourceExplorer* field = explorer->GetHelper(index))
							available << (available.IsEmpty() ? wxT("") : wxT(", "))
								<< field->GetSourceName();
					}
				}

				refusal = wxString::Format(
					_("'%s' has no field called '%s'. It offers: %s."),
					walked.IsEmpty() ? source->GetSourceExplorer()->GetSourceName() : walked,
					segment, available);
				form->DecrRef();
				return false;
			}

			// The hop RECORDS the type it expects to find there. That is what lets a
			// later walk notice the configuration drifted under it, rather than
			// reading a field that is no longer the one the binding meant.
			description.AppendSource(next->GetSourceId(), next->GetTypeDesc().GetFirstClsid());
			walked = walked.IsEmpty() ? segment : walked + wxT(".") + segment;

			if (next->GetHelperCount() > 0) {
				explorer = next;            // a section — its columns are nodes
			}
			else if (IsReference(next->GetTypeDesc().GetFirstClsid())) {
				// The border. The body of a reference's class id IS the metaID of
				// what it points at — constructive, so no lookup table and true for
				// a metatype added tomorrow.
				explorer = nullptr;
				through = ibFindMetaObjectById(activeMetaData,
					(ibMetaID)(next->GetTypeDesc().GetFirstClsid() & kIbClsidBodyMask));
			}
			else {
				explorer = nullptr;         // a leaf; a further segment will refuse
			}
		}

		// The head alone is not a binding — it is the form holding the object.
		// A path of one hop means nothing was actually named.
		if (description.GetHopCount() < 2) {
			refusal = _("Nothing to bind to - pass a path such as 'Warehouse.Code'.");
			form->DecrRef();
			return false;
		}

		binding->SetValue(description);

		if (creator == nullptr || !creator->SaveFormData(form)) {
			refusal = _("The binding was set but the form could not be stored.");
			form->DecrRef();
			return false;
		}

		activeMetaData->Modify(true);

		result.AddField(wxT("bound"), ibDataValue::Bool(true));
		SayControl(control, result);
		result.SetValue(wxT("property"), name);

		// The path as a PERSON reads it, beside the binding as it is STORED. The walked string is
		// this verb's own rendering and belongs to the answer; the binding itself is said by
		// ibSourceDescriptionMemory, which is what a form loads and therefore what stays in step —
		// hop count and all, without this file counting anything.
		result.SetValue(wxT("path"), walked);

		ibDataValue described;
		if (!ibSourceDescriptionMemory::WriteNode(described, description)) {
			refusal = _("The binding was placed, but could not be read back to confirm it.");
			form->DecrRef();
			return false;
		}
		result.AddField(wxT("source"), described);

		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormBind);

//---------------------------------------------------------------------------
// form_copy / form_paste
//---------------------------------------------------------------------------
//
// ⭐ THE SAME TWO METHODS THE METADATA TREE USES. ibValueFrame answers
// CopyObject(writer) / PasteObject(reader) with the same signature
// ibValueMetaObject does, so these tools are metadata_copy and metadata_paste
// again with a different finder — one idea twice, not two ideas. The clipboard
// they share knows which family it is holding and refuses the crossing.
//
// ⭐ AND NOT THROUGH THE OS CLIPBOARD, which is where ibValueForm::CopyObject
// (formFactory.cpp) puts it: that board belongs to the person at the keyboard,
// a tool writing to it would overwrite what they had just copied, and reaching
// it needs a lock a window may be holding. The payload is the same bytes; only
// the pocket differs. The `copyBlock` flag the designer's format carries says
// whether to CLEAR the system board after a paste — a question this board does
// not have, so the chunk is not written.
//
// A control copies its whole subtree, so copying a group copies what is in it.
//

class ibMcpToolFormCopy : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("form_copy"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return _("copying a form control");
	}

	wxString GetDescription() const override
	{
		return _("Copy a control - and everything inside it - into the caller's own buffer, to "
			"be pasted into this form or another one. The copy carries every property the "
			"control has, including its binding, which is why copying a laid-out field is a "
			"truer way to make the next one than adding a bare control and setting what you "
			"remember to set.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgControl(), ArgSlot() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueForm* form = OpenForm(params, refusal);
		if (form == nullptr)
			return false;

		const s32 wanted = (s32)ArgControl().Whole(params);

		ibValueFrame* control = FindControl(form, (ibFormID)wanted);
		if (control == nullptr) {
			refusal = wxString::Format(
				_("This form has no control with id %i. form_get lists them."), (int)wanted);
			form->DecrRef();
			return false;
		}

		// THE FORM IS NOT A CONTROL YOU CAN PASTE. It is the frame everything else
		// lives in, and the designer's own copy refuses it for the same reason.
		if (control->GetComponentType() == COMPONENT_TYPE_FRAME) {
			refusal = _("The form itself is not a control. Copy the form metaobject with "
				"metadata_copy instead.");
			form->DecrRef();
			return false;
		}

		ibWriterMemory writer;

		if (!control->CopyObject(writer)) {
			refusal = wxString::Format(_("'%s' could not be copied."),
				control->GetControlName());
			form->DecrRef();
			return false;
		}

		const wxString slotName = ArgSlot().Text(params);

		ibMcpClipboardSlot& slot = ibMcpClipboard(slotName);

		slot.m_kind = ibMcpClipboardKind::Control;
		slot.m_name = control->GetControlName();
		slot.m_what = control->GetClassName();
		slot.m_payload = ibDataNode();
		slot.m_payload.SetValue(wxT("bytes"), writer.buffer());

		result.SetValue(wxT("slot"), slotName.IsEmpty() ? wxString(wxT("default")) : slotName);
		result.SetValue(wxT("name"), slot.m_name);
		result.SetValue(wxT("class"), slot.m_what);
		result.AddField(wxT("bytes"), ibDataValue::Int((s64)writer.size()));

		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormCopy);

class ibMcpToolFormPaste : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("form_paste"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return _("pasting a form control");
	}

	wxString GetDescription() const override
	{
		return _("Paste what form_copy put in the buffer, into a form - the same one or a "
			"different one. The control that arrives is a new one with a name of its own.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgParent(), ArgName(), ArgSlot() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString slotName = ArgSlot().Text(params);

		ibMcpClipboardSlot& slot = ibMcpClipboard(slotName);

		if (slot.IsEmpty()) {
			refusal = _("Nothing has been copied. Call form_copy first.");
			return false;
		}

		if (slot.m_kind != ibMcpClipboardKind::Control) {
			refusal = wxString::Format(
				_("The buffer holds %s, not a form control."),
				ibMcpClipboardKindName(slot.m_kind));
			return false;
		}

		ibValueMetaObjectFormBase* creator = nullptr;
		ibValueForm* form = OpenForm(params, refusal, nullptr, &creator);
		if (form == nullptr)
			return false;

		ibValueFrame* parent = form;
		const s32 into = (s32)ArgParent().Whole(params);
		if (into > 0) {
			parent = FindControl(form, (ibFormID)into);
			if (parent == nullptr) {
				refusal = wxString::Format(
					_("This form has no control with id %i. form_get lists them."), (int)into);
				form->DecrRef();
				return false;
			}
		}

		// ⚠ A NAMED LOCAL, NOT THE EXPRESSION. ibReaderMemory borrows the bytes
		// it is handed and never copies them; the getter returns by value, so
		// reading straight out of the call would leave it pointing at memory
		// freed at the semicolon (fs.h deletes the rvalue overload for exactly
		// this).
		wxMemoryBuffer bytes = slot.m_payload.GetValue<wxMemoryBuffer>(wxT("bytes"));

		ibReaderMemory reader(bytes);

		// THE PAYLOAD SAYS WHICH CLASS. The factory reads it and makes the
		// control; nothing here has to know what was copied.
		ibValueFrame* created = ibValueFrame::CreatePasteObject(reader, form, parent);

		if (created == nullptr) {
			refusal = wxString::Format(
				_("A %s cannot go there. form_get shows what each control already holds."),
				slot.m_what);
			form->DecrRef();
			return false;
		}

		if (!created->PasteObject(reader)) {
			// ⚠ REMOVE, AND NOTHING ELSE. RemoveChild drops the OWNING handle and
			// destroys a sole-owned child on the spot (propertyObject.h says so
			// where it is defined), so a wxDELETE after it frees the same object
			// twice. The metadata paste cleans up the same way, by the same rule:
			// the parent took ownership at creation, so give it back — do not
			// reach past the owner.
			if (ibValueFrame* owner = created->GetParent())
				owner->RemoveChild(created);
			refusal = wxString::Format(_("'%s' could not be pasted here."), slot.m_name);
			form->DecrRef();
			return false;
		}

		const wxString name = ArgName().Text(params);
		if (!name.IsEmpty())
			created->SetControlName(name);

		// ⚠ SAVED, OR IT NEVER HAPPENED — the configuration keeps the layout, and
		// the value form is ours only for the length of this call.
		if (creator == nullptr || !creator->SaveFormData(form)) {
			refusal = _("The control was pasted but the form could not be stored.");
			form->DecrRef();
			return false;
		}

		activeMetaData->Modify(true);

		result.AddField(wxT("controlId"), ibDataValue::Int((s64)created->GetControlID()));
		result.SetValue(wxT("class"), created->GetClassName());
		result.SetValue(wxT("name"), created->GetControlName());
		result.SetValue(wxT("copiedFrom"), slot.m_name);

		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormPaste);

//---------------------------------------------------------------------------
// form_accepts
//---------------------------------------------------------------------------
//
// ⭐⭐ WHAT A CONTROL OF THIS CLASS WOULD HOLD — asked by building one where it would live, and
// taking it straight back out. The twin of metadata_accepts, and it exists for the same reason: a
// caller had to ADD a Textctrl before it could learn what a Textctrl has, so learning cost an edit.
//
// ⭐ AND THE SAME TREE RULE, because a form has one too. Max, 2026-09-01: *"with controls it will be
// the same trouble"* — a control is never born loose; `CreateControl(class, parent)` takes the
// parent it goes under, and the form itself is the parent when none is named. So the question is
// asked WHERE the control would stand: what a Boxsizer accepts inside a page is not what it accepts
// at the top of a form, and a class the parent will not take is an answer, not a failure.
//
// Nothing is left behind: RemoveControl is the door form_remove uses, and the form is not saved.
class ibMcpToolFormAccepts : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("form_accepts"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(_("asking what a %s holds"), ArgClass().Text(params));
	}

	wxString GetDescription() const override
	{
		return _("What a control of this class holds - every property with what it takes, and the "
			"events it can be given handlers for - WITHOUT adding one. An empty one is built where "
			"it would live, asked, and dropped. Name `parent` to ask about it inside a particular "
			"container: a form is a tree, and what a class may hold depends on where it stands.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgClass(), ArgParent() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueForm* form = OpenForm(params, refusal);
		if (form == nullptr)
			return false;

		ibValueFrame* parent = form;
		const s32 into = (s32)ArgParent().Whole(params);

		if (into > 0) {
			parent = FindControl(form, (ibFormID)into);
			if (parent == nullptr) {
				refusal = wxString::Format(
					_("This form has no control with id %i. form_get lists them."), (int)into);
				form->DecrRef();
				return false;
			}
		}

		const wxString className = ArgClass().Text(params);
		ibValueFrame* sample = form->CreateControl(className, parent);

		// ⭐ A REFUSAL THAT NAMES THE PLACE. "Cannot be created" on its own reads as a broken class
		// name; the parent is half the answer, and a caller that hears which container turned it
		// down knows to ask about another one.
		if (sample == nullptr) {
			refusal = wxString::Format(
				_("A '%s' cannot stand inside '%s'. A form is a tree - name a different `parent`, "
				  "or form_control on an existing control shows what its container takes."),
				className, parent == form ? _("the form") : parent->GetControlName());
			form->DecrRef();
			return false;
		}

		SayControl(sample, result);
		ibMcpSayProperties(sample, result);

		// THE EVENTS TOO — a control's handlers are half of what it can be told to do, and they are
		// asked of the control exactly as the properties are.
		std::vector<ibDataValue> events;
		for (unsigned int index = 0; index < sample->GetEventCount(); ++index) {

			ibEvent* event = sample->GetEvent(index);
			if (event == nullptr)
				continue;

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), event->GetName());

			std::vector<ibDataValue> arguments;
			for (const wxString& argument : event->GetArgs())
				arguments.push_back(ibDataValue::String(argument));
			entry->AddField(wxT("arguments"), ibDataValue::Array(arguments));

			events.push_back(ibDataValue::Child(entry));
		}
		result.AddField(wxT("events"), ibDataValue::Array(events));

		// TAKEN BACK OUT, the way form_remove takes one out. The form was never saved, so the
		// configuration is as it was.
		form->RemoveControl(sample);
		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormAccepts);
