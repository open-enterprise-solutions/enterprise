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
#include "backend/metaCollection/partial/commonObject.h"   // ibValueMetaObjectRecordData::GetObjectForm — the form an object opens with
#include "backend/metaCollection/metaFormObject.h"
#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metadataConfiguration.h"
#include "backend/objCtor.h"   // ibCtorMetaValueType — what a reference type points at, by its id
#include "backend/backend_command.h"                            // WalkCommand — the receiver judges a binding
#include "core/description/commandDescription.h"
#include "backend/propertyManager/property/propertyCommandSource.h"
#include "backend/propertyManager/property/propertySource.h"
#include "core/description/sourceDescription.h"
#include "backend/backend_localization.h"   // a caption is an array by language
#include "backend/propertyManager/property/propertyString.h"   // ibPropertyTString
#include "backend/propertyManager/property/propertyComposition.h"       // a composition, wherever it is held
#include "backend/propertyManager/property/propertyDataComposition.h"   // …the report's own
#include "backend/propertyManager/property/propertyDynamicList.h"       // …and the list's, judged by list rules
#include "core/description/compositionDescription.h"
#include "core/stringUtils.h"   // GenerateSynonym — the caption the platform makes from a name

#include "frmserver/visualView/ctrl/form.h"
#include "frmserver/visualView/ctrl/formAttribute.h"   // the main attribute — the head of every binding
#include "frmserver/visualView/ctrl/formCommand.h"     // …and what the form can DO

#include "core/stringTokenizer.h"

namespace {
using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgForm()
{
	static const ibArg s_a(ibT("form"), ibArg::Kind::Whole,
		ibMcpText("The form's NodeId."), /*required*/ true);
	return s_a;
}

// The READING verb's form argument — the same name, one more thing it may be. An edit verb keeps ArgForm: it
// stores what it changed into the form's metaobject, and a generated form has none to store into.
const ibArg& ArgFormOrObject()
{
	static const ibArg s_a(ibT("form"), ibArg::Kind::Whole,
		ibMcpText("The form's NodeId - or an OBJECT's (a document, a catalog...), for the object form it opens "
			  "with: its default one, or the one the platform GENERATES when it declares none."), /*required*/ true);
	return s_a;
}

const ibArg& ArgControl()
{
	static const ibArg s_a(ibT("control"), ibArg::Kind::Whole,
		ibMcpText("The control's id, from form_get. Omit for the form itself."));
	return s_a;
}

const ibArg& ArgClass()
{
	static const ibArg s_a(ibT("class"), ibArg::Kind::Text,
		ibMcpText("The control's class: Textctrl, Tablebox, Button, Staticline, Boxsizer..."), /*required*/ true);
	return s_a;
}

const ibArg& ArgParent()
{
	static const ibArg s_a(ibT("parent"), ibArg::Kind::Whole,
		ibMcpText("The control it goes inside, from form_get. Omit for the form itself."));
	return s_a;
}

// The ADDING verb's class — the same name, one thing fewer it must be: form_add makes an attribute or a form command
// instead when one of those is named, and a class is then not asked for.
const ibArg& ArgClassToAdd()
{
	static const ibArg s_a(ibT("class"), ibArg::Kind::Text,
		ibMcpText("The control's class: Textctrl, Tablebox, TableboxColumn, TableboxColumnGroup, Notebook, "
			  "NotebookPage, Gridbox, Button, Boxsizer... Not needed when `attribute` or `command` is given."));
	return s_a;
}

// ⭐ THE OTHER TWO TREES OF A FORM, addressed by NAME beside a control's id: an ATTRIBUTE — what the form holds —
// and a form COMMAND — what it can do. The designer edits all three the same way, so the verbs are the same verbs.
const ibArg& ArgAttribute()
{
	static const ibArg s_a(ibT("attribute"), ibArg::Kind::Text,
		ibMcpText("A form ATTRIBUTE, by name - form_get lists them. Given instead of `control`, the verb acts on the "
			  "attribute: its Name, Caption, Type, FillCheck, and what its value carries - a dynamic list's or a "
			  "data composition's `Settings` is the whole composition, in the shape report_get answers."));
	return s_a;
}

const ibArg& ArgCommand()
{
	static const ibArg s_a(ibT("command"), ibArg::Kind::Text,
		ibMcpText("A form COMMAND, by name - form_get lists them. Given instead of `control`, the verb acts on the "
			  "command: its Name, Caption, Picture, and its event `Action` - the procedure in the form's module it runs."));
	return s_a;
}

const ibArg& ArgType()
{
	static const ibArg s_a(ibT("type"), ibArg::Kind::Text,
		ibMcpText("The attribute's type in words, as metadata_set_type takes one: DynamicList, DataComposition, "
			  "ValueTable, String, Number, Date, Boolean, CatalogRef.Goods, DocumentObject.Sale. Omitted: String."));
	return s_a;
}

const ibArg& ArgName()
{
	static const ibArg s_a(ibT("name"), ibArg::Kind::Text,
		ibMcpText("What to call it. This is the name a module refers to it by."));
	return s_a;
}

const ibArg& ArgProperty()
{
	static const ibArg s_a(ibT("property"), ibArg::Kind::Text,
		ibMcpText("Which property - or which EVENT, they are named in one space and form_control "
			  "lists them together."), /*required*/ true);
	return s_a;
}

// The BINDING verb's property — the same name, one thing fewer it must be: what a control is bound by is its Source
// unless another binding property is named (a Button's Command).
const ibArg& ArgPropertyToBind()
{
	static const ibArg s_a(ibT("property"), ibArg::Kind::Text,
		ibMcpText("Which binding property. Omitted: Source - what a field or a table shows. A Button's is Command."));
	return s_a;
}

const ibArg& ArgValue()
{
	// Kind::Any, like metadata_set's `value` — ibMcpSetProperty reads the scalar AND the composite, and a
	// composite property (a picture) is sent back in the shape form_control read it in. Declared as text
	// it was refused at the gate before the property could judge it.
	static const ibArg s_a(ibT("value"), ibArg::Kind::Any,
		ibMcpText("The value, IN ITS OWN TYPE. For a closed set, the word. For an event, the NAME of the "
			  "procedure in the form's module that handles it - the answer says which arguments it "
			  "receives. For a composite one - a picture - the same shape form_control shows it in."));
	return s_a;
}

const ibArg& ArgPath()
{
	static const ibArg s_a(ibT("path"), ibArg::Kind::Text,
		ibMcpText("Unfold this far in, as a dotted path of field names: 'Products' for a "
			  "tabular section's columns, 'Warehouse' to step through a reference into "
			  "what it points at, 'Products.Product.Supplier' on through every reference after it - "
			  "as deep as it is written. Omit for the top level. It starts at the form's MAIN attribute; opened "
			  "with another attribute's name ('Prices.Price', 'Report') it starts at that one instead."));
	return s_a;
}

const ibArg& ArgSlot()
{
	static const ibArg s_a(ibT("slot"), ibArg::Kind::Text,
		ibMcpText("Which buffer to put it in. Omit for the usual one."));
	return s_a;
}

// THE FORM AS A TREE, built the way the editor builds it — there is one road in
// (ibValueForm + LoadFormData) and this is it. Returned as a ref-counted value
// the caller owns for the length of one question: nothing here edits a form
// somebody has open, and nothing here leaves one behind.
ibValueForm* OpenForm(const ibDataNode& params, ibString& refusal,
	bool* generated = nullptr, ibValueMetaObjectFormBase** creatorOut = nullptr)
{
	if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
		refusal = ibMcpText("No configuration is open.");
		return nullptr;
	}

	const std::int32_t id = (std::int32_t)ArgForm().Whole(params);
	if (id <= 0) {
		refusal = ibMcpText("Pass the form's NodeId - metadata_get on the owning object lists its forms.");
		return nullptr;
	}

	ibValueMetaObject* object = ibFindMetaObjectById(activeMetaData, (ibMetaID)id);
	if (object == nullptr) {
		refusal = ibString::Format(ibMcpText("Nothing in this configuration has id %i."), (int)id);
		return nullptr;
	}

	ibValueMetaObjectFormBase* creator =
		object->ConvertToType<ibValueMetaObjectFormBase>();
	if (creator == nullptr) {
		refusal = ibString::Format(ibMcpText("'%s' is not a form."), object->GetName());
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
	ibFormPtr<ibBackendValueForm> built;

	if (const ibValueMetaObjectGenericData* owner =
			object->GetParent() != nullptr
				? object->GetParent()->ConvertToType<ibValueMetaObjectGenericData>()
				: nullptr) {

		built = owner->CreateObjectForm(creator);
	}
	else {
		built = ibValueMetaObjectFormBase::CreateAndBuildForm(
			ibFormRequest(), creator, creator->GetTypeForm(), nullptr, nullptr);
	}

	const ibValuePtr<ibValueForm> form(built);
	if (!form) {
		refusal = ibString::Format(
			ibMcpText("'%s' could not be opened. Its module may have refused - messages_read has "
			  "what the platform said."), object->GetName());
		return nullptr;
	}

	form->IncrRef();   // the caller's reference — the holders above give theirs back on the way out
	return form;
}

// …OR THE FORM AN OBJECT OPENS WITH, when the id is the object's own: its default object form, or the one the
// platform GENERATES when it declares none — built by the object itself (GetObjectForm), the very call the running
// application opens it with. A configuration built from command groups and commands needs no form of its own for
// its commands to stand on the bar, so the form worth reading is often one nobody created.
//
// Answers nullptr with NO refusal when the id is not such an object — OpenForm then answers for it.
ibValueForm* OpenObjectForm(const ibDataNode& params, ibString& refusal, bool& generated)
{
	if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen())
		return nullptr;

	const std::int32_t id = (std::int32_t)ArgFormOrObject().Whole(params);
	ibValueMetaObject* object = id > 0 ? ibFindMetaObjectById(activeMetaData, (ibMetaID)id) : nullptr;
	const ibValueMetaObjectRecordData* record =
		object != nullptr ? object->ConvertToType<ibValueMetaObjectRecordData>() : nullptr;
	if (record == nullptr)
		return nullptr;

	const ibValuePtr<ibValueForm> form(record->GetObjectForm());
	if (!form) {
		refusal = ibString::Format(
			ibMcpText("The object form of '%s' could not be opened. Its module may have refused - messages_read "
			  "has what the platform said."), object->GetName());
		return nullptr;
	}

	const ibValueMetaObjectFormBase* creator = form->GetFormMetaObject();
	generated = creator == nullptr || creator->GetFormData().IsEmpty();
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

	node.AddField(ibT("controlId"), ibDataValue::Int((std::int64_t)control->GetControlID()));
	node.SetValue(ibT("class"), control->GetClassName());

	// Only when it says something the class has not. An unnamed control answers with its class,
	// and repeating it reads as a name somebody chose.
	const ibString name = control->GetControlName();
	if (!name.IsEmpty() && name != control->GetClassName())
		node.SetValue(ibT("name"), name);

	// The FAMILY, beside the class: a sizer, a control, a sizer-item. It decides
	// what may go inside, and a caller reading only class names cannot tell a
	// container from a leaf.
	node.SetValue(ibT("kind"), control->GetObjectTypeName());
}

// ⭐ AND THE EVENTS of any part — a control's, a form command's Action. Each carries the ARGUMENTS its handler is
// called with: the half that decides what a handler can be written to do, and the half that cannot be guessed from
// the event's name.
void SayEvents(const ibPropertyObject* part, ibDataNode& node)
{
	std::vector<ibDataValue> events;
	for (unsigned int index = 0; index < part->GetEventCount(); ++index) {

		ibEvent* event = part->GetEvent(index);
		if (event == nullptr)
			continue;

		std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
		entry->SetValue(ibT("name"), event->GetName());

		const ibString handler = event->GetValue().GetString();
		if (!handler.IsEmpty())
			entry->SetValue(ibT("handler"), handler);

		std::vector<ibDataValue> args;
		for (const ibString& arg : event->GetArgs())
			args.push_back(ibDataValue::String(arg));

		if (!args.empty())
			entry->AddField(ibT("arguments"), ibDataValue::Array(args));

		events.push_back(ibDataValue::Child(entry));
	}

	node.AddField(ibT("events"), ibDataValue::Array(events));
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
		node->AddField(ibT("children"), ibDataValue::Array(children));

	return ibDataValue::Child(node);
}

// The attribute a caller NAMED — refused with the names there are, the shape every refusal here takes, so a wrong
// name costs one call rather than a round trip through form_get.
ibFormAttributeValue* AttributeNamed(const ibValueForm* form, const ibString& name, ibString& refusal)
{
	if (ibFormAttributeValue* entry = form->GetAttribute(name))
		return entry;

	ibString known;
	for (unsigned int index = 0; index < form->GetAttributeCount(); ++index) {
		if (const ibFormAttributeValue* one = form->GetAttribute(index))
			known << (known.IsEmpty() ? ibT("") : ibT(", ")) << one->GetName();
	}

	refusal = known.IsEmpty()
		? ibMcpText("This form has no attributes at all.")
		: ibString::Format(ibMcpText("This form has no attribute called '%s'. It has: %s."), name, known);
	return nullptr;
}

// …and the form command, the same way.
ibFormCommandValue* CommandNamed(const ibValueForm* form, const ibString& name, ibString& refusal)
{
	if (ibFormCommandValue* command = form->GetFormCommand(name))
		return command;

	ibString known;
	for (const ibValuePtr<ibFormCommandValue>& entry : form->GetFormCommands()) {
		if (entry)
			known << (known.IsEmpty() ? ibT("") : ibT(", ")) << entry->GetName();
	}

	refusal = ibString::Format(ibMcpText("This form has no command '%s'. It has: %s."),
		name, known.IsEmpty() ? ibString(ibT("none")) : known);
	return nullptr;
}

// The attribute that keeps the form from being stored — one whose value refuses to be written (a dynamic list
// without its main table, ibValueForm::WriteAttributes), asked of each as the store asks it. Null: none does.
const ibFormAttributeValue* IncompleteAttribute(const ibValueForm* form)
{
	for (unsigned int index = 0; index < form->GetAttributeCount(); ++index) {
		const ibFormAttributeValue* const entry = form->GetAttribute(index);
		ibDataNode written;
		if (entry != nullptr && !entry->WriteProperty(written))
			return entry;
	}
	return nullptr;
}

// ⭐ STORED, OR HELD AS IT IS BEING EDITED — and which, said. A form is stored whole or not at all, so one dynamic list
// without its main table keeps the whole of it from the store, as in the designer, where an unfinished list waits in
// the editor until its source is picked. A step taken meanwhile is done, in the form under edit, and the answer says
// what is missing: refused, a list could never be added at all — form_add cannot give it a table in the same call.
// Any other failure to store refuses; `done` says what was done before it.
bool StoreForm(ibValueMetaObjectFormBase* creator, ibValueForm* form, ibDataNode& result, ibString& refusal, const ibString& done)
{
	if (creator != nullptr && creator->SaveFormData(form))
		return true;

	if (const ibFormAttributeValue* const incomplete = creator != nullptr ? IncompleteAttribute(form) : nullptr) {
		result.AddField(ibT("stored"), ibDataValue::Bool(false));
		result.SetValue(ibT("note"), ibString::Format(ibMcpText("Not stored yet: a form is stored whole, and '%s' is not "
			"whole until it has its main table - form_set {\"attribute\": \"%s\", \"property\": \"Settings\", \"value\": "
			"{\"MainTable\": <metadata id>}}. Until then this and every step after it are held only in the form being "
			"edited."), incomplete->GetName(), incomplete->GetName()));
		return true;
	}

	refusal = ibString::Format(ibMcpText("%s but the form could not be stored."), done);
	return false;
}

// ⭐⭐ WHERE A PATH STARTS — at an ATTRIBUTE: the main one, unless the path opens with another's name. A form holds
// more than its object — a list beside it, a composition a page shows, a value table — and the designer's attribute
// tree roots every row at an attribute, a path here the same way. `rest` is the path after the head; `named` says the
// path named it, and then the head alone is a binding: a table to a list, a grid box to a composition.
ibFormAttributeValue* PathHead(const ibValueForm* form, const ibString& path, ibString& rest, bool& named)
{
	ibFormAttributeValue* head = form->GetAttribute(path.BeforeFirst(ibT('.')));
	named = head != nullptr;
	rest = named ? path.AfterFirst(ibT('.')) : path;
	return named ? head : form->GetMainAttribute();
}

// ⭐⭐ WHERE A REFERENCE LEADS — an empty reference to what its type points at, as a SOURCE: its explorer is the
// fields the designer's own tree offers under that reference (docViewFormEditAttribute.cpp opens it the same way),
// references again among them, so a path goes on as deep as it is written. The reference is kept in `held` while
// its explorer is read. Null for a barrier (`AnyRef`, `CatalogRef`), which points at no one object, and for a type
// the configuration does not register.
//
// 🛑 IT ANSWERED THE METAOBJECT, and each tool walked its CHILDREN from there — a road of its own beside the
// engine's: the fields offered took in the object's modules and forms, and a second reference ended the walk
// ("'Goods.Goods.Parent' has no field called 'Description'. It offers: .", 2026-10-28).
ibSourceDataObject* PointedAt(const ibMetaData* metaData, const ibClassID& clsid, std::vector<ibValue>& held)
{
	const ibCtorMetaValueType* type = metaData != nullptr ? metaData->GetTypeCtor(clsid) : nullptr;
	const ibValueMetaObject* object = type != nullptr ? type->GetMetaObject() : nullptr;
	ibValueReferenceDataObject* reference = object != nullptr
		? ibValueReferenceDataObject::Create(metaData, object->GetMetaID()) : nullptr;
	if (reference == nullptr)
		return nullptr;

	held.emplace_back(reference);
	ibSourceDataObject* source = nullptr;
	held.back().ConvertToValue(source);
	return source;
}

} // namespace

//---------------------------------------------------------------------------
// form_get
//---------------------------------------------------------------------------
class ibMcpToolFormGet : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("form_get"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("reading the form '%s'"), ibMcpNameOf(params, ArgForm().Name()));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("A form's three branches: its CONTROLS as a tree, its ATTRIBUTES (one of them "
			"the main one, through which every binding starts), and its COMMANDS with what "
			"each runs. The ids are what every other form verb addresses a part by.\n"
			"And its COMMAND BAR as a person will see it (`commandBar`): the standard actions, the "
			"object's own commands, and each CommandGroup of the category FormCommandBar as ONE entry "
			"with its `submenu` - the object's commands filed under it and the common commands typed "
			"for the object this form shows.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgFormOrObject() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		bool generated = false;
		ibValueForm* form = OpenObjectForm(params, refusal, generated);
		if (form == nullptr && refusal.IsEmpty())
			form = OpenForm(params, refusal, &generated);
		if (form == nullptr)
			return false;

		result.SetValue(ibT("form"), form->GetControlName());

		std::vector<ibDataValue> children;
		for (unsigned int index = 0; index < form->GetChildCount(); ++index) {
			if (ibValueFrame* child = form->GetChild(index))
				children.push_back(ControlEntry(child));
		}

		result.AddField(ibT("controls"), ibDataValue::Array(children));

		// ⭐⭐ A FORM HAS THREE BRANCHES, not one. Controls are what is seen;
		// ATTRIBUTES are what it holds (one of them the MAIN one, through which
		// every binding starts); COMMANDS are what it can do. Answering with the
		// controls alone described a third of the thing and hid the half a binding
		// or a button actually needs.
		// WHAT EACH HOLDS, named the way metadata_get names a field's type — through the one resolver that knows a
		// reference's id and a value type's alike. It decides what a control bound to it can be: a table for a list,
		// a grid box for a composition.
		const auto named = ibMetaTypeResolver(form->GetMetaData());

		std::vector<ibDataValue> attributes;
		for (unsigned int index = 0; index < form->GetAttributeCount(); ++index) {

			ibFormAttributeValue* attribute = form->GetAttribute(index);
			if (attribute == nullptr)
				continue;

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->AddField(ibT("id"), ibDataValue::Int((std::int64_t)attribute->GetId()));
			node->SetValue(ibT("name"), attribute->GetName());

			ibString holds;
			for (const ibClassID& clsid : attribute->GetTypeDesc().GetClsidList()) {
				const ibString word = named(clsid);
				if (!word.IsEmpty())
					holds << (holds.IsEmpty() ? ibT("") : ibT(", ")) << word;
			}
			if (!holds.IsEmpty())
				node->SetValue(ibT("type"), holds);

			// THE MAIN ONE IS NOT ONE OF THE OTHERS. It is the head of every
			// binding on this form, and a caller that cannot tell it apart cannot
			// build a path at all.
			if (attribute == form->GetMainAttribute())
				node->AddField(ibT("main"), ibDataValue::Bool(true));

			attributes.push_back(ibDataValue::Child(node));
		}

		result.AddField(ibT("attributes"), ibDataValue::Array(attributes));

		std::vector<ibDataValue> commands;
		for (const ibValuePtr<ibFormCommandValue>& command : form->GetFormCommands()) {

			if (!command)
				continue;

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->AddField(ibT("id"), ibDataValue::Int((std::int64_t)command->GetId()));
			node->SetValue(ibT("name"), command->GetName());

			// What it RUNS. A command with no procedure is a button that does
			// nothing, and that is worth seeing without opening the module.
			const ibString procedure = command->GetProcedure();
			if (!procedure.IsEmpty())
				node->SetValue(ibT("procedure"), procedure);

			commands.push_back(ibDataValue::Child(node));
		}

		result.AddField(ibT("commands"), ibDataValue::Array(commands));

		// ⭐ THE COMMAND BAR AS IT WILL STAND — built by the bar itself (BuildCommands, the very call the toolbar is
		// filled from), so what is read here is what a person will see: the standard actions, the object's own
		// commands, and a COMMAND GROUP of the form command bar as ONE entry carrying its submenu. Each submenu
		// line is resolved through the bar's door, the same resolve the menu is drawn with.
		if (ibValueCommandBar* bar = form->GetCommandBar()) {
			std::vector<ibDataValue> entries;
			for (const ibCommandEntry& entry : bar->BuildCommands()) {
				if (entry.id == wxNOT_FOUND)
					continue;   // a separator

				std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
				node->SetValue(ibT("caption"), entry.caption);
				node->SetValue(ibT("kind"), ibString(entry.kind == ibCommandEntryKind_Group ? ibT("group")
					: entry.kind == ibCommandEntryKind_QuickFilter ? ibT("quickFilter") : ibT("command")));
				if (!entry.enabled)
					node->AddField(ibT("enabled"), ibDataValue::Bool(false));

				if (entry.kind == ibCommandEntryKind_Group) {
					node->AddField(ibT("picture"), ibDataValue::Bool(!entry.picture.IsEmptyPicture()));
					if (!entry.tooltip.IsEmpty())
						node->SetValue(ibT("tooltip"), entry.tooltip);
					std::vector<ibDataValue> submenu;
					for (const ibValueCommandBarItem* member : entry.members) {
						ibString caption; ibPictureDescription picture;
						if (member != nullptr && bar->ResolveCommand(member->GetBindingDesc(), caption, picture))
							submenu.push_back(ibDataValue::String(caption));
					}
					node->AddField(ibT("submenu"), ibDataValue::Array(submenu));
				}

				entries.push_back(ibDataValue::Child(node));
			}
			result.AddField(ibT("commandBar"), ibDataValue::Array(entries));
		}

		// WHICH OF THE TWO THIS IS, asked of the metaobject rather than guessed
		// from the shape. A generated layout is what the platform makes from the
		// object; a stored one is what somebody arranged. They read alike and they
		// are not the same thing — arranging the first means the form stops
		// following the object it was generated from.
		result.AddField(ibT("generated"), ibDataValue::Bool(generated));

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

	ibString GetName() const override { return ibT("form_control"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("reading a control of the form '%s'"),
			ibMcpNameOf(params, ArgForm().Name()));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("One control in full: its properties with what they hold, and its EVENTS with "
			"the arguments each handler receives. An event is stored as a property of its own, "
			"so both come back together - a control's behaviour is not a separate question "
			"from its appearance. `attribute` or `command` instead of `control` reads that part the same "
			"way - an attribute's `Settings` is its list's or composition's whole schema.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgControl(), ArgAttribute(), ArgCommand() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueForm* form = OpenForm(params, refusal);
		if (form == nullptr)
			return false;

		// The other two trees, read the way a control is: the same properties walk, the same events.
		if (ArgAttribute().Given(params) || ArgCommand().Given(params)) {

			ibPropertyObject* part = nullptr;
			if (ArgAttribute().Given(params)) {
				ibFormAttributeValue* attribute = AttributeNamed(form, ArgAttribute().Text(params), refusal);
				if (attribute != nullptr)
					result.SetValue(ibT("attribute"), attribute->GetName());
				part = attribute;
			}
			else {
				ibFormCommandValue* command = CommandNamed(form, ArgCommand().Text(params), refusal);
				if (command != nullptr)
					result.SetValue(ibT("command"), command->GetName());
				part = command;
			}

			if (part != nullptr) {
				ibMcpSayProperties(part, result);
				SayEvents(part, result);
			}

			form->DecrRef();
			return part != nullptr;
		}

		ibValueFrame* control = form;

		const std::int32_t wanted = (std::int32_t)ArgControl().Whole(params);
		if (wanted > 0) {
			control = form->FindControlByID((ibFormID)wanted);
			if (control == nullptr) {
				refusal = ibString::Format(
					ibMcpText("This form has no control with id %i. form_get lists them."), (int)wanted);
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
		SayEvents(control, result);

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

	ibString GetName() const override { return ibT("form_add"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("adding a %s to the form '%s'"),
			ArgAttribute().Given(params) ? ArgAttribute().Text(params)
				: ArgCommand().Given(params) ? ArgCommand().Text(params) : ArgClassToAdd().Text(params),
			ibMcpNameOf(params, ArgForm().Name()));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Put a control on a form - a text box, a table and its columns or column groups, a notebook "
			"and its pages, a grid box, a button. type_list with kind=control names every class there is. "
			"With `attribute` it adds an ATTRIBUTE instead, of the `type` given - a dynamic list, a data composition, "
			"a value table, an object - for controls to bind to (form_bind); with `command`, a form COMMAND, whose "
			"Action form_set then names a procedure for. The form is SAVED afterwards, so a "
			"generated layout becomes a stored one and stops following the object.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgClassToAdd(), ArgParent(), ArgName(),
			ArgAttribute(), ArgType(), ArgCommand() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectFormBase* creator = nullptr;
		ibValueForm* form = OpenForm(params, refusal, nullptr, &creator);
		if (form == nullptr)
			return false;

		// ⭐ THE OTHER TWO TREES — the designer's "add" on its attribute tree and on its command tree.
		if (ArgAttribute().Given(params) || ArgCommand().Given(params)) {

			bool added = ArgAttribute().Given(params)
				? AddAttribute(form, params, result, refusal)
				: AddCommand(form, params, result, refusal);

			if (added && !StoreForm(creator, form, result, refusal, ibMcpText("It was added")))
				added = false;
			if (added)
				activeMetaData->Modify(true);

			form->DecrRef();
			return added;
		}

		const ibString className = ArgClassToAdd().Text(params);
		if (className.IsEmpty()) {
			refusal = ibMcpText("Which class? type_list with kind=control names them - or name an `attribute` or a "
				"`command` to add one of those.");
			form->DecrRef();
			return false;
		}

		ibValueFrame* parent = form;
		const std::int32_t into = (std::int32_t)ArgParent().Whole(params);
		if (into > 0) {
			parent = form->FindControlByID((ibFormID)into);
			if (parent == nullptr) {
				refusal = ibString::Format(
					ibMcpText("This form has no control with id %i. form_get lists them."), (int)into);
				form->DecrRef();
				return false;
			}
		}

		ibValueFrame* created = form->CreateControl(className, parent);
		if (created == nullptr) {
			// The factory said no. It knows the nesting rules; repeating them here
			// would be a second copy that could disagree with the first.
			refusal = ibString::Format(
				ibMcpText("A %s cannot go there. type_list with kind=control names the classes; "
				  "form_get shows what each control already holds."), className);
			form->DecrRef();
			return false;
		}

		const ibString name = ArgName().Text(params);
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
				dynamic_cast<ibPropertyTString*>(created->GetProperty(ibT("Title")));

			if (title != nullptr && title->IsEmptyProperty()) {

				// …and written INTO the language cell rather than over the whole
				// value: the title HOLDS its translations, so the one in force is
				// filled in and no other language moves.
				title->GetValueAsTranslate().SetTranslate(stringUtils::GenerateSynonym(name));
			}
		}

		// ⚠ SAVED, OR IT NEVER HAPPENED. The value form is ours for the length of
		// this call; the configuration keeps the LAYOUT, and nothing reaches it
		// until the form is written back.
		if (!StoreForm(creator, form, result, refusal, ibMcpText("The control was added"))) {
			form->DecrRef();
			return false;
		}

		activeMetaData->Modify(true);

		result.AddField(ibT("added"), ibDataValue::Bool(true));
		SayControl(created, result);

		form->DecrRef();
		return true;
	}

private:

	// The designer's add on its attribute tree — NAMED and TYPED in the one call, which is what a caller putting a
	// list or a report beside the object means. Made as the designer makes one — String — and the type said is then
	// set as the inspector sets it, through the Type property: the property takes a type by its name itself.
	static bool AddAttribute(ibValueForm* form, const ibDataNode& params, ibDataNode& result, ibString& refusal)
	{
		const ibString name = ArgAttribute().Text(params);
		if (name.IsEmpty() || !form->IsAttributeNameUnique(name)) {
			refusal = name.IsEmpty() ? ibString(ibMcpText("An attribute needs a name."))
				: ibString::Format(ibMcpText("The form already has an attribute '%s'."), name);
			return false;
		}

		ibFormAttributeValue* entry = form->AttachAttribute(form->MakeAttribute(name));

		if (ArgType().Given(params)) {
			ibDataNode typed;
			typed.AddField(ibT("value"), ibDataValue::String(ArgType().Text(params)));
			ibDataNode said;
			if (!ibMcpSetProperty(entry->GetProperty(ibT("Type")), typed, said, refusal)) {
				form->DetachAttribute(entry);   // refused whole: an attribute of another type than asked is not added
				return false;
			}
		}

		result.AddField(ibT("added"), ibDataValue::Bool(true));
		result.SetValue(ibT("attribute"), entry->GetName());
		result.AddField(ibT("id"), ibDataValue::Int((std::int64_t)entry->GetId()));
		return true;
	}

	// …and on its command tree: a command NAMED here, its Action empty until form_set names the procedure it runs.
	static bool AddCommand(ibValueForm* form, const ibDataNode& params, ibDataNode& result, ibString& refusal)
	{
		const ibString name = ArgCommand().Text(params);
		if (name.IsEmpty() || !form->IsFormCommandNameUnique(name)) {
			refusal = name.IsEmpty() ? ibString(ibMcpText("A command needs a name."))
				: ibString::Format(ibMcpText("The form already has a command '%s'."), name);
			return false;
		}

		const ibFormCommandValue* command = form->AddFormCommand(name, ibEmptyString);

		result.AddField(ibT("added"), ibDataValue::Bool(true));
		result.SetValue(ibT("command"), command->GetName());
		result.AddField(ibT("id"), ibDataValue::Int((std::int64_t)command->GetId()));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormAdd);

//---------------------------------------------------------------------------
// form_set
//---------------------------------------------------------------------------
class ibMcpToolFormSet : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("form_set"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("setting %s on a control of '%s'"),
			ArgProperty().Text(params),
			ibMcpNameOf(params, ArgForm().Name()));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Set one property of one control - its caption, its width, what it is BOUND "
			"to - or one EVENT, whose value is the name of a procedure in the form's module. "
			"form_control lists both and what each holds now; a property whose values are a "
			"closed set is set by its word, and one that holds several (`select: multiple`) by an array "
			"of them - the whole set, an empty array clears it. `FunctionalOptions` is such a set: the "
			"functional options a control is available under; while all of them are off it is not available - "
			"neither shown nor offered, and neither is what it holds.\n"
			"A PICTURE goes in `value` in its own shape, the one form_control reads it in: {Type: 1, ClassId} "
			"for an engine picture (picture_list -> engine `id`), {Type: 2, Guid} for one the configuration "
			"declares (picture_list -> configuration `guid`), {Type: 3, Image: {Name, Buffer, Width, Height}} "
			"for an image of its own - Buffer is the PNG file's bytes in base64 written \"base64:<...>\".\n"
			"`attribute` or `command` instead of `control` sets that part the same way: an attribute's Type in words "
			"(DynamicList, DataComposition, CatalogRef.Goods), its `Settings` - a list's or a composition's whole "
			"schema, in the shape report_get answers and form_control reads it in; a command's Caption, Picture, "
			"and its event `Action`.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgControl(), ArgAttribute(), ArgCommand(),
			ArgProperty(), ArgValue() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectFormBase* creator = nullptr;
		ibValueForm* form = OpenForm(params, refusal, nullptr, &creator);
		if (form == nullptr)
			return false;

		// WHICH PART: an attribute or a form command by name, else a control by its id - none, the form itself.
		ibValueFrame* control = nullptr;
		ibFormAttributeValue* attribute = nullptr;
		ibFormCommandValue* command = nullptr;
		ibPropertyObject* part = nullptr;

		if (ArgAttribute().Given(params))
			part = attribute = AttributeNamed(form, ArgAttribute().Text(params), refusal);
		else if (ArgCommand().Given(params))
			part = command = CommandNamed(form, ArgCommand().Text(params), refusal);
		else {
			control = form;
			const std::int32_t wanted = (std::int32_t)ArgControl().Whole(params);
			if (wanted > 0) {
				control = form->FindControlByID((ibFormID)wanted);
				if (control == nullptr)
					refusal = ibString::Format(
						ibMcpText("This form has no control with id %i. form_get lists them."), (int)wanted);
			}
			part = control;
		}

		if (part == nullptr) {
			form->DecrRef();
			return false;
		}

		const ibString name = ArgProperty().Text(params);

		// A control's NAME is what a module calls it by, and on this tree it is not
		// an ordinary property write.
		if (name.IsSameAs(ibT("Name"), false) && control != nullptr) {
			control->SetControlName(ArgValue().Text(params));
			result.SetValue(ibT("property"), name);
			result.SetValue(ibT("value"), control->GetControlName());
		}
		// …and an attribute's is a module VARIABLE, bound again under the new one - the form's rename, as the
		// designer's tree renames it. A command's is a property its own guard keeps unique.
		else if (name.IsSameAs(ibT("Name"), false) && attribute != nullptr) {
			if (!form->RenameAttribute(attribute, ArgValue().Text(params))) {
				refusal = ibString::Format(ibMcpText("'%s' cannot be called '%s' - the name is empty or another "
					"attribute has it."), attribute->GetName(), ArgValue().Text(params));
				form->DecrRef();
				return false;
			}
			result.SetValue(ibT("property"), name);
			result.SetValue(ibT("value"), attribute->GetName());
		}
		else if (ibProperty* property = part->GetProperty(name)) {

			// ONE RULE FOR BOTH TREES — see ibMcpSetProperty. An attribute's Type in words among them: the property
			// takes a type by its name itself (ibPropertyType::ReadNodeValue).
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
		else if (ibEvent* event = part->GetEvent(name)) {

			const ibString handler = ArgValue().Text(params);

			event->SetValue(handler);

			result.SetValue(ibT("event"), name);
			result.SetValue(ibT("handler"), handler);

			// WHAT THE HANDLER RECEIVES, said here because it cannot be guessed
			// from the event's name and decides what the procedure can be written
			// to do.
			std::vector<ibDataValue> args;
			for (const ibString& arg : event->GetArgs())
				args.push_back(ibDataValue::String(arg));

			result.AddField(ibT("arguments"), ibDataValue::Array(args));
		}
		else {
			refusal = ibString::Format(
				ibMcpText("'%s' has no property or event called '%s'. form_control lists both."),
				control != nullptr ? control->GetControlName()
					: attribute != nullptr ? attribute->GetName() : command->GetName(), name);
			form->DecrRef();
			return false;
		}

		if (!StoreForm(creator, form, result, refusal, ibMcpText("The property was set"))) {
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

	ibString GetName() const override { return ibT("form_remove"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("removing a control from '%s'"),
			ibMcpNameOf(params, ArgForm().Name()));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Take a control off a form, with everything inside it - the other half of "
			"form_add, so a wrong step can be undone without a person opening the editor. `attribute` or "
			"`command` instead of `control` takes that off: the controls bound to it are left showing nothing.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgControl(), ArgAttribute(), ArgCommand() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectFormBase* creator = nullptr;
		ibValueForm* form = OpenForm(params, refusal, nullptr, &creator);
		if (form == nullptr)
			return false;

		// Named before it goes, because afterwards there is nothing left to name.
		ibString name;

		if (ArgAttribute().Given(params) || ArgCommand().Given(params)) {

			ibFormAttributeValue* attribute = ArgAttribute().Given(params)
				? AttributeNamed(form, ArgAttribute().Text(params), refusal) : nullptr;
			ibFormCommandValue* command = ArgCommand().Given(params) && attribute == nullptr && refusal.IsEmpty()
				? CommandNamed(form, ArgCommand().Text(params), refusal) : nullptr;

			if (attribute == nullptr && command == nullptr) {
				form->DecrRef();
				return false;
			}

			// The designer's removal on either tree: an attribute leaves the form with its module variable unbound,
			// a command with the buttons that ran it resolving to nothing.
			name = attribute != nullptr ? attribute->GetName() : command->GetName();
			if (attribute != nullptr)
				form->DetachAttribute(attribute);
			else
				form->DeleteFormCommand(name);
		}
		else {
			const std::int32_t wanted = (std::int32_t)ArgControl().Whole(params);
			ibValueFrame* control = form->FindControlByID((ibFormID)wanted);
			if (control == nullptr) {
				refusal = ibString::Format(
					ibMcpText("This form has no control with id %i. form_get lists them."), (int)wanted);
				form->DecrRef();
				return false;
			}

			name = control->GetControlName();
			form->RemoveControl(control);
		}

		if (!StoreForm(creator, form, result, refusal, ibMcpText("It was removed"))) {
			form->DecrRef();
			return false;
		}

		activeMetaData->Modify(true);

		result.AddField(ibT("removed"), ibDataValue::Bool(true));
		result.SetValue(ibT("name"), name);

		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormRemove);

//---------------------------------------------------------------------------
// form_move — the order of a control among its siblings
//---------------------------------------------------------------------------
//
// The same question metadata_move answers for a configuration's objects, asked of a form: which
// control comes first in its group, which column stands left in a table, which page opens first.
// The editor does it with a drag; this is the drag.
//
class ibMcpToolFormMove : public ibMcpTool {

	static const ibArg& ArgPosition()
	{
		static const ibArg s_a(ibT("position"), ibArg::Kind::Whole,
			ibMcpText("Where the control goes among the controls of its group, counted from 0."),
			/*required*/ true);
		return s_a;
	}

public:

	ibString GetName() const override { return ibT("form_move"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("moving a control on '%s'"),
			ibMcpNameOf(params, ArgForm().Name()));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Change the place of a control among the controls of its group - the order of the "
			"fields in a group, of the columns in a table, of the pages. `position` is its place there, "
			"from 0. Answers whether it moved; form_get shows the order.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgControl(), ArgPosition() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectFormBase* creator = nullptr;
		ibValueForm* form = OpenForm(params, refusal, nullptr, &creator);
		if (form == nullptr)
			return false;

		const std::int32_t wanted = (std::int32_t)ArgControl().Whole(params);
		ibValueFrame* control = form->FindControlByID((ibFormID)wanted);
		if (control == nullptr) {
			refusal = ibString::Format(
				ibMcpText("This form has no control with id %i. form_get lists them."), (int)wanted);
			form->DecrRef();
			return false;
		}

		// A control placed in a group sits in its own sizer item, and the item is what stands among the
		// group's children — so a sizer item for a parent means one level up.
		ibValueFrame* item = control;
		if (item->GetParent() != nullptr && item->GetParent()->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
			item = item->GetParent();
		ibValueFrame* const group = item->GetParent();

		const std::int32_t position = (std::int32_t)ArgPosition().Whole(params);
		if (group == nullptr || position < 0 || position >= (std::int32_t)group->GetChildCount()) {
			refusal = ibString::Format(
				ibMcpText("Position %i is past the controls of this group. Nothing was moved."), (int)position);
			form->DecrRef();
			return false;
		}

		const bool moved = group->GetChildPosition(item) != (unsigned int)position;
		group->ChangeChildPosition(item, (unsigned int)position);

		if (moved) {
			if (!StoreForm(creator, form, result, refusal, ibMcpText("The control was moved"))) {
				form->DecrRef();
				return false;
			}
			// The configuration the form BELONGS to — asked of its creator, not of whichever is active.
			if (ibMetaData* const metaData = creator->GetMetaData())
				metaData->Modify(true);
		}

		result.SetValue(ibT("name"), control->GetControlName());
		result.AddField(ibT("moved"), ibDataValue::Bool(moved));

		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormMove);

//---------------------------------------------------------------------------
// form_attribute
//---------------------------------------------------------------------------
//
// ⭐⭐ WHICH ATTRIBUTE THE FORM IS ABOUT — and it was the one thing here a mouse could do and a
// tool could not. `form_get` already SAYS which one is main (`main: true` beside its name), and
// nothing could change it: the designer does it from the attribute panel
// (ibAttributeTree::OnSetMain), and every road from this side was read-only.
//
// 🛑 WHY IT MATTERS MORE THAN IT SOUNDS. The main attribute is the head of every binding on the
// form and the source its controls render against — changing it moves what the whole form is
// about. A form built by a tool therefore got whichever attribute happened to be created first and
// stayed that way forever.
//
// ⚠ AND IT IS A TOGGLE, exactly as the panel's own command is: naming the current main with
// `main: false` clears it, leaving the form with no main - which is a legitimate state (the
// controls then get their own command bar back), not an error.
//
class ibMcpToolFormAttribute : public ibMcpTool {

	static const ibArg& ArgAttribute()
	{
		static const ibArg s_a(ibT("attribute"), ibArg::Kind::Text,
			ibMcpText("The attribute, by NAME - form_get lists them, and marks the one that is main."),
			/*required*/ true);
		return s_a;
	}

	static const ibArg& ArgMain()
	{
		static const ibArg s_a(ibT("main"), ibArg::Kind::Flag,
			ibMcpText("Make this the form's MAIN attribute. On by default; `false` on the one that "
				  "currently is clears it, leaving the form without a main."));
		return s_a;
	}

public:

	ibString GetName() const override { return ibT("form_attribute"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("making '%s' the main attribute of '%s'"),
			ArgAttribute().Text(params), ibMcpNameOf(params, ArgForm().Name()));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Say which attribute the form is ABOUT - its MAIN one, the head of every "
			"binding and the source its controls render against. form_get lists the "
			"attributes and marks the current main; this is what moves it. `main: false` on "
			"the current one clears it.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgAttribute(), ArgMain() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectFormBase* creator = nullptr;
		ibValueForm* form = OpenForm(params, refusal, nullptr, &creator);
		if (form == nullptr)
			return false;

		ibFormAttributeValue* entry = AttributeNamed(form, ArgAttribute().Text(params), refusal);
		if (entry == nullptr) {
			form->DecrRef();
			return false;
		}

		// The panel's own verb: set it, or clear it when this one is already the main.
		const bool wanted = !ArgMain().Given(params) || ArgMain().Flag(params);
		form->SetMainAttribute(wanted ? entry : nullptr);

		if (!StoreForm(creator, form, result, refusal, ibMcpText("The main attribute was set"))) {
			form->DecrRef();
			return false;
		}

		activeMetaData->Modify(true);

		// ⭐ ANSWERED WITH WHAT THE FORM HOLDS NOW, read back rather than echoed: a caller learns
		// what IS, and a form left with no main says so by naming nothing.
		const ibFormAttributeValue* now = form->GetMainAttribute();

		result.SetValue(ibT("form"), form->GetControlName());
		result.SetValue(ibT("main"), now != nullptr ? now->GetName() : ibString());

		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormAttribute);

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

	ibString GetName() const override { return ibT("form_source"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("looking at what the form '%s' can bind to"),
			ibMcpNameOf(params, ArgForm().Name()));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("What the form's source offers to bind a control to: every field with its "
			"name, its title and its type, and every TABULAR SECTION with the columns under "
			"it. A field marked `unfolds` opens one more level - a section's columns, or the fields "
			"of what a reference points at, and their references in turn: the tree goes as deep as "
			"`path` is written. Ask this before form_bind - a name that is not here is a name "
			"the form cannot reach.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgPath() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueForm* form = OpenForm(params, refusal);
		if (form == nullptr)
			return false;

		// FROM AN ATTRIBUTE — the main one, unless the path opens with another's name (PathHead).
		ibString path;
		bool named = false;
		const ibFormAttributeValue* head = PathHead(form, ArgPath().Text(params), path, named);
		ibSourceDataObject* source = head != nullptr ? head->GetSourceValue() : nullptr;
		const ibSourceExplorer* explorer =
			source != nullptr ? source->GetSourceExplorer() : nullptr;

		if (explorer == nullptr) {
			// NOTHING TO UNFOLD is a state rather than a fault: a common form has no main attribute, and an
			// attribute of a primitive type is a leaf a control binds to as it is.
			result.SetValue(ibT("note"), named
				? ibString::Format(ibMcpText("'%s' holds nothing to unfold - a control binds to it as it is."), head->GetName())
				: ibString(ibMcpText("This form has no main attribute - start `path` with one of its attributes "
					"(form_get lists them).")));
			result.AddField(ibT("fields"), ibDataValue::Array(std::vector<ibDataValue>()));
			form->DecrRef();
			return true;
		}

		// ⭐ ONE STEP PER SEGMENT, by NAME and case-insensitively — the same
		// resolution the model layer uses everywhere (FindByName exists because
		// three call sites wrote this loop by hand and one compared exactly, so a
		// path written `Supplier.region` silently stopped matching `Region`).
		ibString walked = named ? head->GetName() : ibString();
		ibString across;             // the object a reference stepped into, while the walk stands at its fields
		std::vector<ibValue> held;   // the references stepped into (PointedAt)

		if (!path.IsEmpty()) {
			ibStringTokenizer segments(path, ibT("."));
			while (segments.HasMoreTokens()) {

				const ibString segment = segments.GetNextToken();
				const ibSourceExplorer* next = explorer->FindByName(segment);

				if (next == nullptr) {
					// A MISS IS A REFUSAL WITH THE ALTERNATIVES, because the caller's
					// next move is to pick one of them — and a value has none to pick.
					ibString available;
					for (unsigned int index = 0; index < explorer->GetHelperCount(); ++index) {
						if (const ibSourceExplorer* field = explorer->GetHelper(index))
							available << (available.IsEmpty() ? ibT("") : ibT(", "))
								<< field->GetSourceName();
					}

					refusal = available.IsEmpty()
						? ibString::Format(ibMcpText("'%s' holds a value - nothing unfolds from it, so '%s' cannot follow."),
							walked, segment)
						: ibString::Format(ibMcpText("'%s' has no field called '%s'. It offers: %s."),
							walked.IsEmpty() ? explorer->GetSourceName() : walked, segment, available);
					form->DecrRef();
					return false;
				}

				walked = walked.IsEmpty() ? segment : walked + ibT(".") + segment;

				// ⭐⭐ A REFERENCE HOP IS NOT A CHILD LOOKUP. A section's columns are nodes of the explorer; the
				// fields of what a REFERENCE points at are not — the target is another object entirely, and
				// materialising it eagerly is what makes the tree infinite. So the walk steps into it only when
				// the path names it: the explorer of what it points at (PointedAt), and on from there, a
				// reference again or not, as deep as the path is written.
				ibSourceDataObject* const pointed = next->GetHelperCount() == 0
					&& IsReference(next->GetTypeDesc().GetFirstClsid())
					? PointedAt(activeMetaData, next->GetTypeDesc().GetFirstClsid(), held) : nullptr;
				const ibSourceExplorer* const into = pointed != nullptr ? pointed->GetSourceExplorer() : nullptr;

				explorer = into != nullptr ? into : next;
				across = into != nullptr && pointed->GetSourceMetaObject() != nullptr
					? pointed->GetSourceMetaObject()->GetName() : ibString();
			}
		}

		result.SetValue(ibT("source"), across.IsEmpty() ? explorer->GetSourceName() : across);
		if (!walked.IsEmpty())
			result.SetValue(ibT("path"), walked);
		if (!across.IsEmpty())
			result.AddField(ibT("throughReference"), ibDataValue::Bool(true));

		const std::vector<ibDataValue> fields = Fields(explorer);
		result.AddField(ibT("fields"), ibDataValue::Array(fields));

		// A LEAF IS AN ANSWER TOO. A field with nothing under it is where a binding
		// ends; saying so saves a caller one more descent to find out.
		if (fields.empty())
			result.SetValue(ibT("note"),
				ibMcpText("Nothing unfolds from here - this is where a binding ends."));

		form->DecrRef();
		return true;
	}

private:

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
			node->SetValue(ibT("name"), field->GetSourceName());

			// The id is what the BINDING is made of — a path is a chain of these,
			// and a name is how a person writes one down.
			node->AddField(ibT("id"), ibDataValue::Int((std::int64_t)field->GetSourceId()));

			const ibString synonym = field->GetSourceSynonym();
			if (!synonym.IsEmpty() && synonym != field->GetSourceName())
				node->SetValue(ibT("title"), synonym);

			if (!field->GetSourceGroup().IsEmpty())
				node->SetValue(ibT("group"), field->GetSourceGroup());

			// A SECTION IS NOT A FIELD. It is a source of its own, and a control
			// bound to it is a table rather than a box.
			if (field->IsTableSection())
				node->AddField(ibT("tabularSection"), ibDataValue::Bool(true));

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
				node->AddField(ibT("unfolds"), ibDataValue::Bool(true));

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

	ibString GetName() const override { return ibT("form_bind"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("binding a control of '%s' to %s"),
			ibMcpNameOf(params, ArgForm().Name()),
			ArgPath().Text(params));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Bind a control to what the form's source offers, by the dotted path "
			"form_source unfolds - 'Warehouse.Code' reaches through the reference into the "
			"catalog it points at, 'Products.Quantity' a column of a tabular section, and the path "
			"goes on through every reference it meets: 'Products.Product.Supplier.TaxNumber'. A field "
			"of what a row points at is a column like any other - 'List.Counterparty.TaxNumber' on a "
			"dynamic list's table needs no change to its query. A "
			"binding is a chain of hops, so it cannot be set as plain text through form_set. A path opened "
			"with another ATTRIBUTE's name starts there ('List.Price' - a column of a table showing a dynamic "
			"list); that name alone binds the control to the attribute itself - a Tablebox to a dynamic list or "
			"a value table, a Gridbox to a data composition.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgControl(), ArgPath(), ArgPropertyToBind() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectFormBase* creator = nullptr;
		ibValueForm* form = OpenForm(params, refusal, nullptr, &creator);
		if (form == nullptr)
			return false;

		// ⚠ "NO CONTROL WITH THAT ID" IS THE WRONG SENTENCE WHEN NO ID CAME. It says the form was
		// searched and answers about a control the caller never named — so the caller goes looking
		// for a control that is missing, when what is missing is their own argument (2026-09-01).
		const std::int32_t wanted = (std::int32_t)ArgControl().Whole(params);

		if (!ArgControl().Given(params)) {
			refusal = ibMcpText("Which control? form_bind needs the control's id - form_get lists them.");
			form->DecrRef();
			return false;
		}

		ibValueFrame* control = form->FindControlByID((ibFormID)wanted);
		if (control == nullptr) {
			refusal = ibString::Format(
				ibMcpText("This form has no control with id %i. form_get lists them."), (int)wanted);
			form->DecrRef();
			return false;
		}

		ibString name = ArgPropertyToBind().Text(params);
		if (name.IsEmpty())
			name = ibT("Source");

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

			const ibString wanted = ArgPath().Text(params);

			ibCommandDescription command;
			ibString available;

			// The FORM'S own commands first — they are the ones a button on this
			// form most often runs.
			for (const ibValuePtr<ibFormCommandValue>& entry : form->GetFormCommands()) {
				if (!entry)
					continue;

				available << (available.IsEmpty() ? ibT("") : ibT(", ")) << entry->GetName();
				if (entry->GetName().IsSameAs(wanted, false))
					command.AppendCommand(entry->GetId());
			}

			// …then the ones the OBJECT declares, which every form of it inherits.
			if (!command.IsOk()) {
				if (ibValueMetaObject* owner = ibFindMetaObjectById(activeMetaData,
						(ibMetaID)(std::int32_t)ArgForm().Whole(params))) {

					if (ibValueMetaObject* holder = owner->GetParent()) {
						for (unsigned int index = 0; index < holder->GetChildCount(); ++index) {

							ibValueMetaObject* child = holder->GetChild(index);
							if (child == nullptr || child->IsDeleted()
								|| child->GetClassType() != g_metaCommandCLSID)
								continue;

							available << (available.IsEmpty() ? ibT("") : ibT(", "))
								<< child->GetName();
							if (child->GetName().IsSameAs(wanted, false))
								command.AppendCommand(child->GetMetaID());
						}
					}
				}
			}

			if (!command.IsOk()) {
				refusal = ibString::Format(
					ibMcpText("There is no command called '%s' here. Available: %s."),
					wanted, available.IsEmpty() ? ibString(ibT("none")) : available);
				form->DecrRef();
				return false;
			}

			// ⭐ THE PLATFORM IS THE JUDGE. A stored path is just hops; walking them
			// is what tells a live binding from a broken one, and the receiver — the
			// button itself — is what walks. Refusing on its answer rather than on
			// my own is the difference between a binding that works and one that
			// merely looks right.
			ibString leaf;
			if (ibBackendCommandReceiver* receiver =
					dynamic_cast<ibBackendCommandReceiver*>(control)) {

				if (!receiver->WalkCommand(command, &leaf)) {
					refusal = ibString::Format(
						ibMcpText("'%s' cannot run '%s' - the path does not resolve from this control."),
						control->GetControlName(), wanted);
					form->DecrRef();
					return false;
				}
			}

			wiring->SetValue(command, leaf);

			if (!StoreForm(creator, form, result, refusal, ibMcpText("The command was bound"))) {
				form->DecrRef();
				return false;
			}

			activeMetaData->Modify(true);

			result.AddField(ibT("bound"), ibDataValue::Bool(true));
			SayControl(control, result);
			result.SetValue(ibT("property"), name);
			result.SetValue(ibT("command"), leaf.IsEmpty() ? wanted : leaf);

			// …and the wiring as it is STORED, said by ibCommandDescriptionMemory — the same
			// reading a form loads, so what a caller reads here is what the file holds.
			ibDataValue described;
			if (!ibCommandDescriptionMemory::WriteNode(described, command)) {
				refusal = ibMcpText("The command was wired, but could not be read back to confirm it.");
				form->DecrRef();
				return false;
			}
			result.AddField(ibT("wiring"), described);

			form->DecrRef();
			return true;
		}

		ibPropertySource* binding = dynamic_cast<ibPropertySource*>(property);

		if (binding == nullptr) {
			refusal = ibString::Format(
				ibMcpText("'%s' has no binding called '%s'. form_control lists its properties."),
				control->GetControlName(), name);
			form->DecrRef();
			return false;
		}

		// THE PATH, RESOLVED HOP BY HOP — the same descent form_source makes, and
		// the same one the form makes at run time.
		ibSourceDescription description;

		// ⭐⭐ THE HEAD OF A BINDING IS AN ATTRIBUTE, not the first field of the source — the
		// form's MAIN one, unless the path opens with another's name (PathHead).
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
		//
		// ⭐ AND A NAMED HEAD ALONE IS A BINDING — a table to a list, a grid box to a composition:
		// the control shows the attribute itself, which is what the designer's drag of an
		// attribute's root row writes.
		ibString path;
		bool named = false;
		const ibFormAttributeValue* head = PathHead(form, ArgPath().Text(params), path, named);
		if (head == nullptr) {
			refusal = ibMcpText("This form has no main attribute to bind through - start the path with one of "
				"its attributes (form_get lists them).");
			form->DecrRef();
			return false;
		}

		description.AppendSource(head->GetId());

		ibString walked = named ? head->GetName() : ibString();
		ibSourceDataObject* source = head->GetSourceValue();
		const ibSourceExplorer* explorer =
			source != nullptr ? source->GetSourceExplorer() : nullptr;

		// ⭐⭐ ONE WALK, THROUGH THE SOURCE'S OWN EXPLORERS, as deep as the path goes. A segment
		// naming a field or a tabular section is a node of the explorer it stands in; a segment
		// naming a REFERENCE steps into what it points at — the explorer of an empty reference to it
		// (PointedAt), the tree the designer offers under that reference — and the next segment is
		// one of ITS fields, a reference again or not. The hop ids are the explorers' on every level,
		// which is what the form resolves at run time (ibSourceDataObject::ResolvePath).
		//
		// 🛑 IT CROSSED INTO METADATA at the first reference and walked the object's children from
		// there — a second road: modules and forms were offered as fields, and a second reference
		// ended the walk, `Goods.Goods.Parent.Description` refused with "It offers: ." (2026-10-28).
		std::vector<ibValue> held;   // the references stepped into (PointedAt)

		ibStringTokenizer segments(path, ibT("."));
		while (segments.HasMoreTokens()) {

			const ibString segment = segments.GetNextToken();
			const ibSourceExplorer* next = explorer != nullptr
				? explorer->FindByName(segment) : nullptr;

			if (next == nullptr) {

				ibString available;
				if (explorer != nullptr) {
					for (unsigned int index = 0; index < explorer->GetHelperCount(); ++index) {
						if (const ibSourceExplorer* field = explorer->GetHelper(index))
							available << (available.IsEmpty() ? ibT("") : ibT(", "))
								<< field->GetSourceName();
					}
				}

				refusal = available.IsEmpty()
					? ibString::Format(ibMcpText("'%s' holds a value - nothing unfolds from it, so '%s' cannot follow."),
						walked.IsEmpty() ? head->GetName() : walked, segment)
					: ibString::Format(ibMcpText("'%s' has no field called '%s'. It offers: %s."),
						walked.IsEmpty() ? head->GetName() : walked, segment, available);
				form->DecrRef();
				return false;
			}

			// The hop RECORDS the type it expects to find there. That is what lets a
			// later walk notice the configuration drifted under it, rather than
			// reading a field that is no longer the one the binding meant.
			description.AppendSource(next->GetSourceId(), next->GetTypeDesc().GetFirstClsid());
			walked = walked.IsEmpty() ? segment : walked + ibT(".") + segment;

			if (next->GetHelperCount() > 0) {
				explorer = next;            // a section — its columns are nodes
			}
			else if (ibSourceDataObject* pointed = IsReference(next->GetTypeDesc().GetFirstClsid())
				? PointedAt(activeMetaData, next->GetTypeDesc().GetFirstClsid(), held) : nullptr) {
				explorer = pointed->GetSourceExplorer();   // across the reference — its own fields
			}
			else {
				explorer = nullptr;         // a leaf; a further segment will refuse
			}
		}

		// The MAIN head alone is not a binding — it is the form holding the object.
		// A path of one hop that did not name its head means nothing was named.
		if (description.GetHopCount() < 2 && !named) {
			refusal = ibMcpText("Nothing to bind to - pass a path such as 'Warehouse.Code', or an attribute's name.");
			form->DecrRef();
			return false;
		}

		binding->SetValue(description);

		if (!StoreForm(creator, form, result, refusal, ibMcpText("The binding was set"))) {
			form->DecrRef();
			return false;
		}

		activeMetaData->Modify(true);

		result.AddField(ibT("bound"), ibDataValue::Bool(true));
		SayControl(control, result);
		result.SetValue(ibT("property"), name);

		// The path as a PERSON reads it, beside the binding as it is STORED. The walked string is
		// this verb's own rendering and belongs to the answer; the binding itself is said by
		// ibSourceDescriptionMemory, which is what a form loads and therefore what stays in step —
		// hop count and all, without this file counting anything.
		result.SetValue(ibT("path"), walked);

		ibDataValue described;
		if (!ibSourceDescriptionMemory::WriteNode(described, description)) {
			refusal = ibMcpText("The binding was placed, but could not be read back to confirm it.");
			form->DecrRef();
			return false;
		}
		result.AddField(ibT("source"), described);

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

	ibString GetName() const override { return ibT("form_copy"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("copying a form control");
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Copy a control - and everything inside it - into the caller's own buffer, to "
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

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueForm* form = OpenForm(params, refusal);
		if (form == nullptr)
			return false;

		const std::int32_t wanted = (std::int32_t)ArgControl().Whole(params);

		ibValueFrame* control = form->FindControlByID((ibFormID)wanted);
		if (control == nullptr) {
			refusal = ibString::Format(
				ibMcpText("This form has no control with id %i. form_get lists them."), (int)wanted);
			form->DecrRef();
			return false;
		}

		// THE FORM IS NOT A CONTROL YOU CAN PASTE. It is the frame everything else
		// lives in, and the designer's own copy refuses it for the same reason.
		if (control->GetComponentType() == COMPONENT_TYPE_FRAME) {
			refusal = ibMcpText("The form itself is not a control. Copy the form metaobject with "
				"metadata_copy instead.");
			form->DecrRef();
			return false;
		}

		ibWriterMemory writer;

		if (!control->CopyObject(writer)) {
			refusal = ibString::Format(ibMcpText("'%s' could not be copied."),
				control->GetControlName());
			form->DecrRef();
			return false;
		}

		const ibString slotName = ArgSlot().Text(params);

		ibMcpClipboardSlot& slot = ibMcpClipboard(slotName);

		slot.m_kind = ibMcpClipboardKind::Control;
		slot.m_name = control->GetControlName();
		slot.m_what = control->GetClassName();
		slot.m_payload = ibDataNode();
		slot.m_payload.SetValue(ibT("bytes"), writer.buffer());

		result.SetValue(ibT("slot"), slotName.IsEmpty() ? ibString(ibT("default")) : slotName);
		result.SetValue(ibT("name"), slot.m_name);
		result.SetValue(ibT("class"), slot.m_what);
		result.AddField(ibT("bytes"), ibDataValue::Int((std::int64_t)writer.size()));

		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormCopy);

class ibMcpToolFormPaste : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("form_paste"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("pasting a form control");
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Paste what form_copy put in the buffer, into a form - the same one or a "
			"different one. The control that arrives is a new one with a name of its own.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgParent(), ArgName(), ArgSlot() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		const ibString slotName = ArgSlot().Text(params);

		ibMcpClipboardSlot& slot = ibMcpClipboard(slotName);

		if (slot.IsEmpty()) {
			refusal = ibMcpText("Nothing has been copied. Call form_copy first.");
			return false;
		}

		if (slot.m_kind != ibMcpClipboardKind::Control) {
			refusal = ibString::Format(
				ibMcpText("The buffer holds %s, not a form control."),
				ibMcpClipboardKindName(slot.m_kind));
			return false;
		}

		ibValueMetaObjectFormBase* creator = nullptr;
		ibValueForm* form = OpenForm(params, refusal, nullptr, &creator);
		if (form == nullptr)
			return false;

		ibValueFrame* parent = form;
		const std::int32_t into = (std::int32_t)ArgParent().Whole(params);
		if (into > 0) {
			parent = form->FindControlByID((ibFormID)into);
			if (parent == nullptr) {
				refusal = ibString::Format(
					ibMcpText("This form has no control with id %i. form_get lists them."), (int)into);
				form->DecrRef();
				return false;
			}
		}

		ibMemoryBuffer bytes = slot.m_payload.GetValue<ibMemoryBuffer>(ibT("bytes"));

		ibReaderMemory reader(bytes);

		// THE PAYLOAD SAYS WHICH CLASS. The factory reads it and makes the
		// control; nothing here has to know what was copied.
		ibValueFrame* created = ibValueFrame::CreatePasteObject(reader, form, parent);

		if (created == nullptr) {
			refusal = ibString::Format(
				ibMcpText("A %s cannot go there. form_get shows what each control already holds."),
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
			refusal = ibString::Format(ibMcpText("'%s' could not be pasted here."), slot.m_name);
			form->DecrRef();
			return false;
		}

		const ibString name = ArgName().Text(params);
		if (!name.IsEmpty())
			created->SetControlName(name);

		// ⚠ SAVED, OR IT NEVER HAPPENED — the configuration keeps the layout, and
		// the value form is ours only for the length of this call.
		if (!StoreForm(creator, form, result, refusal, ibMcpText("The control was pasted"))) {
			form->DecrRef();
			return false;
		}

		activeMetaData->Modify(true);

		result.AddField(ibT("controlId"), ibDataValue::Int((std::int64_t)created->GetControlID()));
		result.SetValue(ibT("class"), created->GetClassName());
		result.SetValue(ibT("name"), created->GetControlName());
		result.SetValue(ibT("copiedFrom"), slot.m_name);

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

	ibString GetName() const override { return ibT("form_accepts"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("asking what a %s holds"), ArgClass().Text(params));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("What a control of this class holds - every property with what it takes, and the "
			"events it can be given handlers for - WITHOUT adding one. An empty one is built where "
			"it would live, asked, and dropped. Name `parent` to ask about it inside a particular "
			"container: a form is a tree, and what a class may hold depends on where it stands.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgForm(), ArgClass(), ArgParent() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueForm* form = OpenForm(params, refusal);
		if (form == nullptr)
			return false;

		ibValueFrame* parent = form;
		const std::int32_t into = (std::int32_t)ArgParent().Whole(params);

		if (into > 0) {
			parent = form->FindControlByID((ibFormID)into);
			if (parent == nullptr) {
				refusal = ibString::Format(
					ibMcpText("This form has no control with id %i. form_get lists them."), (int)into);
				form->DecrRef();
				return false;
			}
		}

		const ibString className = ArgClass().Text(params);
		ibValueFrame* sample = form->CreateControl(className, parent);

		// ⭐ A REFUSAL THAT NAMES THE PLACE. "Cannot be created" on its own reads as a broken class
		// name; the parent is half the answer, and a caller that hears which container turned it
		// down knows to ask about another one.
		if (sample == nullptr) {
			refusal = ibString::Format(
				ibMcpText("A '%s' cannot stand inside '%s'. A form is a tree - name a different `parent`, "
				  "or form_control on an existing control shows what its container takes."),
				className, parent == form ? ibMcpText("the form") : parent->GetControlName());
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
			entry->SetValue(ibT("name"), event->GetName());

			std::vector<ibDataValue> arguments;
			for (const ibString& argument : event->GetArgs())
				arguments.push_back(ibDataValue::String(argument));
			entry->AddField(ibT("arguments"), ibDataValue::Array(arguments));

			events.push_back(ibDataValue::Child(entry));
		}
		result.AddField(ibT("events"), ibDataValue::Array(events));

		// TAKEN BACK OUT, the way form_remove takes one out. The form was never saved, so the
		// configuration is as it was.
		form->RemoveControl(sample);
		form->DecrRef();
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormAccepts);

//---------------------------------------------------------------------------
// what a FORM can be half-built into — config_check asks this too
//---------------------------------------------------------------------------
//
// ⭐⭐ A COMPOSITION DOES NOT ONLY LIVE IN THE METADATA. It lives on FORMS as well — a list, a
// composer dropped onto a gridbox, a table — and anything on a form can hold one (Max, 2026-09-02:
// *"a composer can be on a form too, like a list"*, *"anything can live there — tables, a composer,
// a list"*). The configuration-wide check walks metaobjects, and metaobjects are exactly where
// these are NOT: a form keeps its controls as a blob, which is unreadable to anything that cannot
// build them.
//
// 🛑 SO WITHOUT THIS THE AUDIT WOULD ANSWER "nothing half-built" ABOUT A BASE WITH AN EMPTY LIST ON
// EVERY SCREEN — and a false clean is acted on, while a missing check is not. It registers itself
// through the audit registry (mcpTool.h), so the answer grows by LINKING the DLL that knows about
// forms rather than by the backend learning what a control is.
//
// ⚠ NOTHING IS RUN. The form is loaded from its stored blob — LoadForm builds the control tree and
// nothing else — so no module fires, no source is bound and no window appears. A check with side
// effects is one nobody dares call twice.
class ibMcpAuditForms : public ibMcpAudit {

	// ⭐ ASKED OF THE PROPERTY, NOT OF THE CONTROL. Which controls may carry a composition is a
	// list that goes stale the day somebody adds one; a property that STORES a composition is what
	// it is, whatever holds it. So every property of every control is asked, and the ones that
	// answer are the ones that matter.
	//
	// AND THE TWO KINDS ARE JUDGED DIFFERENTLY. A report composition owes outputs and selected
	// fields; a LIST is a degenerate composer and owes neither — demanding an output of it would
	// report every list in the configuration as broken, which is how an audit teaches people to
	// ignore it.
	// ⭐ THE COMPOSITION A PROPERTY CARRIES, ASKED ONCE — and with it the second fact the caller
	// needs: which rules to judge it by. Three classes carry one, and two of them are report-shaped
	// while a dynamic list is judged as a list.
	//
	// 🛑 IT WAS THREE CASTS IN A ROW AT THE CALLSITE, which is the same question asked three times
	// and answered in three places. Each cast BINDS its name and is used inside its own branch —
	// that part was right — but the chain itself belongs behind one door: the fourth class to carry
	// a composition will be added to this function, and every caller has it at once.
	static const ibCompositionDescription* CompositionIn(const ibProperty* property, bool& asReport)
	{
		if (const ibPropertyComposition* asComposition =
				dynamic_cast<const ibPropertyComposition*>(property)) {
			asReport = true;
			return &asComposition->GetValueAsCompositionDesc();
		}

		if (const ibPropertyDataComposition* asData =
				dynamic_cast<const ibPropertyDataComposition*>(property)) {
			asReport = true;
			return &asData->GetValueAsCompositionDesc();
		}

		// A LIST IS NOT A REPORT, and the difference is not cosmetic: a list needs no output and no
		// variant, so judging it by a report's rules complains about everything it correctly lacks.
		if (const ibPropertyDynamicList* asList =
				dynamic_cast<const ibPropertyDynamicList*>(property)) {
			asReport = false;
			return &asList->GetValueAsCompositionDesc();
		}

		return nullptr;
	}

	static void CheckControl(ibPropertyObject* holder, const ibValueMetaObject* about,
		const ibComplain& complain)
	{
		if (holder == nullptr)
			return;

		for (unsigned int index = 0; index < holder->GetPropertyCount(); index++) {

			ibProperty* property = holder->GetProperty(index);
			if (property == nullptr)
				continue;

			bool asReport = true;
			const ibCompositionDescription* composition = CompositionIn(property, asReport);

			if (composition == nullptr)
				continue;

			if (composition->m_query.IsEmpty()) {
				complain(about, ibString::Format(
					ibMcpText("'%s' reads nothing - its composition has no query, so the control is empty "
					  "whatever the person does"), property->GetName()));
				continue;
			}

			if (!asReport)
				continue;

			std::vector<ibString> missing;
			ibMcpComposerComplaints(*composition, missing);

			for (const ibString& one : missing)
				complain(about, ibString::Format(ibMcpText("'%s': %s"), property->GetName(), one));
		}
	}

public:

	void Check(ibMetaData* metaData, const ibComplain& complain) const override
	{
		if (metaData == nullptr || !metaData->IsConfigOpen())
			return;

		for (ibValueMetaObject* object : metaData->GetAnyArrayObject<ibValueMetaObject>(true)) {

			ibValueMetaObjectFormBase* creator =
				object != nullptr ? object->ConvertToType<ibValueMetaObjectFormBase>() : nullptr;

			if (creator == nullptr)
				continue;

			// AN EMPTY BLOB IS A GENERATED FORM — the platform lays it out when it is opened, and
			// there is nothing stored to find fault with. Not a complaint: it is how most forms in
			// a young configuration legitimately are.
			const ibMemoryBuffer data = creator->GetFormData();
			if (data.GetDataLen() == 0)
				continue;

			ibValueForm form(ibFormRequest(), creator);
			if (!form.LoadForm(data))
				continue;

			for (ibValueControl* control : form.GetControlList())
				CheckControl(control, object, complain);
		}
	}
};

MCP_AUDIT_REGISTER(ibMcpAuditForms);
