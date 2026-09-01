////////////////////////////////////////////////////////////////////////////
//	Description : the bindings that wire metaobjects to each other
////////////////////////////////////////////////////////////////////////////
//
// ⭐ A BINDING IS NOT A VALUE. `ListOwner` says which catalog a catalog is subordinate to;
// `ListRegisterRecord` says which registers a document posts to; `ListGeneration` says what may be
// generated from what. None of them holds a number or a word — each holds a set of METAOBJECTS
// (ibMetaDescription, a list of metaIDs), and that is why metadata_set cannot express them: it
// refused with "wrong value kind (expected 7, got 4)", numbers of kinds where words should be.
//
// ⭐⭐ AND THE CONNECTION HAS TWO ENDS. Max, 2026-08-31: "when you add a property it changes two
// properties at once — its own and the other one's". A document knowing its register is the same
// fact as the register knowing its recorder, and writing one side by hand leaves the other stale —
// silently, because the configuration still builds. So nothing here writes a node: the value is
// handed to the property's own typed setter and the object is then told, exactly as the object
// inspector tells it, and the object updates whatever else that means.
//
// ⚠ NO CANDIDATE LIST OF ITS OWN. The designer's editors hold one clsid list each
// (advpropOwner: catalogs; advpropRecord: the three register kinds) — that knowledge is already
// written down twice, in the frontend, and a third copy here would be the one that goes stale.
// This resolves what it was given, hands it over, and reports what the binding holds afterwards;
// a target the platform will not take shows up as a binding that did not change.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/stringUtils.h"
#include "backend/typeDescription.h"              // ibMetaDescription — what these properties hold
#include "backend/propertyManager/property/variant/variantMetaDesc.h"   // the one shape that holds it

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments, declared once and read through the same objects — see ibMcpTool::Arguments().
const ibArg& ArgId() { static const ibArg a(wxT("id"), ibArg::Kind::Whole, _("The object being wired, by NodeId."), true); return a; }
const ibArg& ArgProperty() { static const ibArg a(wxT("property"), ibArg::Kind::Text, _("Which binding. Naming one the object does not have is refused WITH the list of the ones it does, so a wrong guess costs one call."), true); return a; }
const ibArg& ArgTarget() { static const ibArg a(wxT("target"), ibArg::Kind::Text, _("The metaobject to bind to, by name. Omit to read the binding instead of changing it.")); return a; }
const ibArg& ArgRemove() { static const ibArg a(wxT("remove"), ibArg::Kind::Flag, _("Take the target OUT of the binding instead of putting it in.")); return a; }
const ibArg& ArgOnly() { static const ibArg a(wxT("only"), ibArg::Kind::Flag, _("Make the target the ONLY thing bound, clearing whatever else was there. Off by default, because most bindings legitimately hold several.")); return a; }

// ONE BINDING, WHICHEVER OF THE THREE CLASSES IT IS.
//
// ⭐ THREE CASTS, AND THEY ARE THE RIGHT QUESTION. ibPropertyOwner, ibPropertyRecord and
// ibPropertyGeneration each derive straight from ibProperty and each holds an ibMetaDescription,
// with no base between them — so "which of these is in front of me" is a genuine question about a
// type, asked once, at the boundary where a caller's word becomes a property. That is what a cast
// is for; the ones worth removing are the ones that appear because something was asked of a
// subclass that the base could have answered.
//
// ⚠ What WOULD be an improvement, and is a separate piece of work: a shared base for "a property
// holding a metadescription". It would collapse these three arms — and, more usefully, it is where
// the candidate-list functor belongs. ibPropertyList already has one (GetValueList fires it, and
// the owning metaobject supplies it); these do not, which is exactly why the designer's three
// editors each carry their own hardcoded clsid list.
struct Binding {
	ibProperty*        property = nullptr;
	ibMetaDescription* held     = nullptr;
	std::function<void(const ibMetaDescription&)> set;

	bool IsOk() const { return property != nullptr && held != nullptr; }
};

// ⭐ ONE QUESTION, ASKED OF THE VALUE. It used to be three `dynamic_cast`s over PROPERTY classes —
// Owner, Record, Generation — written out here, and the family is five: a chart of accounts' binding
// and a chart of characteristic types' binding were simply not in the list, so `metadata_bind`
// answered "no such binding" for two relationships that exist. Building an accounting configuration
// hit both in one sitting, because those two are exactly what a chart of accounts and an accounting
// register are wired with.
//
// Now the question goes to the VARIANT, which is what actually holds the relationship, and every one
// of them is an ibVariantDataMetaDesc. A property joins the family by holding such a value — there is
// no list here, and nothing to add when a sixth appears.
Binding AsBinding(ibProperty* property)
{
	Binding found;

	// `find_`, not `get_`: BindingNamed walks every property of the object asking "are you a
	// binding", and most answer no. The raising form is for a shape that is already known.
	if (ibVariantDataMetaDesc* held = property->find_cell_variant<ibVariantDataMetaDesc>()) {
		found.property = property;
		found.held = &held->GetMetaDesc();
		found.set = [property, held](const ibMetaDescription& value) {
			held->GetMetaDesc() = value;
			property->SetValue(wxVariant(held->Clone()));
		};
	}

	return found;
}

Binding BindingNamed(ibValueMetaObject* object, const wxString& name, wxString& refusal)
{
	std::vector<wxString> bindings;

	for (unsigned int index = 0; index < object->GetPropertyCount(); ++index) {

		ibProperty* property = object->GetProperty(index);
		if (property == nullptr)
			continue;

		Binding binding = AsBinding(property);
		if (!binding.IsOk())
			continue;

		if (property->GetName().IsSameAs(name, false))
			return binding;

		bindings.push_back(property->GetName());
	}

	// ⭐ REFUSED WITH WHAT THERE IS. Which bindings an object even has depends on its metatype — a
	// catalog has an owner, a document has register records — and a caller cannot know that in
	// advance. Listing them turns one refusal into the answer to the next question.
	wxString known;

	for (const wxString& one : bindings)
		known << (known.IsEmpty() ? wxT("") : wxT(", ")) << one;

	refusal = known.IsEmpty()
		? wxString::Format(_("'%s' has no bindings at all."), object->GetName())
		: wxString::Format(_("'%s' has no binding called '%s'. It has: %s."),
			object->GetName(), name, known);

	return Binding();
}

// What the binding holds, as names — the reading side, so a caller can see what a write did
// instead of being told it succeeded.
std::vector<ibDataValue> BoundNames(ibMetaData* metaData, const ibMetaDescription& description)
{
	std::vector<ibDataValue> names;

	for (unsigned int index = 0; index < description.GetTypeCount(); ++index) {

		const ibMetaID id = description.GetByIdx(index);
		ibValueMetaObject* bound = ibFindMetaObjectById(metaData, id);

		// AN ID THAT RESOLVES TO NOTHING IS STILL REPORTED. A binding pointing at a deleted object
		// is exactly the thing somebody would want to see, and hiding it would make the list look
		// healthy while the configuration is not.
		names.push_back(ibDataValue::String(bound != nullptr
			? bound->GetName()
			: wxString::Format(wxT("#%i (missing)"), (int)id)));
	}

	return names;
}

} // namespace

//---------------------------------------------------------------------------
// metadata_bind
//---------------------------------------------------------------------------

class ibMcpToolMetadataBind : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_bind"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString target = ArgTarget().Text(params);

		if (target.IsEmpty())
			return wxString::Format(_("reading the binding '%s' of '%s'"),
				ArgProperty().Text(params), ibMcpNameOf(params));

		return wxString::Format(ArgRemove().Flag(params)
				? _("unbinding '%s' from '%s'")
				: _("binding '%s' to '%s'"),
			target, ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return _("Wire one metaobject to another - the bindings that carry no value but a "
			"relationship: `ListOwner` (which catalog this one is subordinate to), "
			"`ListRegisterRecord` (which registers a document posts to), `ListGeneration`. "
			"metadata_set cannot express these: they hold metaobjects, not words. Without "
			"`target` it reads the binding back, which is also how you check what a write did - "
			"and the binding has two ends, so the other object learns about it too.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {
			ArgId(), ArgProperty(), ArgTarget(), ArgRemove(), ArgOnly() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		const wxString name = ArgProperty().Text(params);

		const Binding binding = BindingNamed(object, name, refusal);
		if (!binding.IsOk())
			return false;

		const wxString target = ArgTarget().Text(params);

		result.SetValue(wxT("object"), object->GetName());
		result.SetValue(wxT("binding"), binding.property->GetName());

		// READING IS A WHOLE ANSWER. Asked without a target, this is how a caller finds out what
		// is wired to what - and it is the same reading a write is verified by.
		if (target.IsEmpty()) {
			result.AddField(wxT("bound"),
				ibDataValue::Array(BoundNames(activeMetaData, *binding.held)));
			return true;
		}

		// BY NAME, ACROSS EVERY KIND. A binding names an OBJECT, and a caller writing "Goods" does
		// not also want to say which metatype it is — that is exactly the thing the configuration
		// already knows.
		//
		// ⭐ ASKED OF THE METADATA. This was a pull-everything-and-compare loop, which is
		// FindAnyObjectByFilter written out by hand — including the deleted check, which that walk
		// makes for itself.
		ibValueMetaObject* other = activeMetaData->FindAnyObjectByFilter<ibValueMetaObject>(target);

		if (other == nullptr) {
			refusal = wxString::Format(
				_("Nothing in this configuration is called '%s'."), target);
			return false;
		}

		// ⚠ WORKED ON A COPY, then handed over whole. The description is held BY REFERENCE inside
		// the property's variant, so editing it in place would change the value without the
		// property ever being told — which is the exact failure this tool exists to avoid.
		ibMetaDescription description = *binding.held;
		const ibMetaID id = other->GetMetaID();
		const bool remove = ArgRemove().Flag(params);

		if (ArgOnly().Flag(params))
			description.ClearMetaType();

		if (remove) {

			ibMetaDescription kept;

			for (unsigned int index = 0; index < description.GetTypeCount(); ++index)
				if (description.GetByIdx(index) != id)
					kept.AppendMetaType(description.GetByIdx(index));

			description = kept;
		}
		else if (!description.ContainMetaType(id)) {
			description.AppendMetaType(id);
		}

		// ⭐ THE PROPERTY'S OWN SETTER, THEN THE OBJECT IS TOLD — the object inspector's sequence,
		// which is what makes the OTHER end of the binding update. Writing the node instead would
		// store the same ids and leave the relationship half-made.
		//
		// ⚠ The value has to go in through SetValue(ibMetaDescription) because that is the only
		// thing that builds this property's variant; so the veto half cannot be offered, and the
		// telling half is called explicitly. Same shape as the composite road in ibMcpSetProperty,
		// and for the same reason.
		const wxVariant before = binding.property->GetValue();

		binding.set(description);
		ibMcpNotifyChanged(binding.property, before);

		activeMetaData->Modify(true);

		result.SetValue(wxT("target"), other->GetName());
		result.AddField(wxT("bound"),
			ibDataValue::Array(BoundNames(activeMetaData, *binding.held)));

		// ⭐ THE ANSWER IS THE READING, not a claim of success. If the platform declined the
		// target, `bound` says so by not containing it — which is the only report that cannot be
		// wrong.
		result.SetValue(wxT("note"),
			_("`bound` is read back from the binding after the change - if the target is not in "
			  "it, the platform did not take it."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataBind);
