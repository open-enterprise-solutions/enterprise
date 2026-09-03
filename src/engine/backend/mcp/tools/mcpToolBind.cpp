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
// ⚠ NO CANDIDATE LIST OF ITS OWN, AND NO VALUE OF ITS OWN EITHER. The property answers both:
// GetValueList gives every candidate with the VARIANT that is what to place if it is chosen. This
// tool picks one of them, puts the resulting SET into that value (a Mult binding holds several, so
// one choice is one member and not a replacement) and hands it to the gate. It names no variant
// class and never touches the value the property is holding — which is exactly why the second end
// of the binding gets made.
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
const ibArg& ArgId() { static const ibArg a(wxT("id"), ibArg::Kind::Whole, ibMcpText("The object being wired, by NodeId."), true); return a; }
const ibArg& ArgProperty() { static const ibArg a(wxT("property"), ibArg::Kind::Text, ibMcpText("Which binding. Naming one the object does not have is refused WITH the list of the ones it does, so a wrong guess costs one call."), true); return a; }
const ibArg& ArgTarget() { static const ibArg a(wxT("target"), ibArg::Kind::Text, ibMcpText("The metaobject to bind to, by name. Omit to read the binding instead of changing it.")); return a; }
const ibArg& ArgRemove() { static const ibArg a(wxT("remove"), ibArg::Kind::Flag, ibMcpText("Take the target OUT of the binding instead of putting it in.")); return a; }
const ibArg& ArgOnly() { static const ibArg a(wxT("only"), ibArg::Kind::Flag, ibMcpText("Make the target the ONLY thing bound, clearing whatever else was there. Off by default, because most bindings legitimately hold several.")); return a; }

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
		? wxString::Format(ibMcpText("'%s' has no bindings at all."), object->GetName())
		: wxString::Format(ibMcpText("'%s' has no binding called '%s'. It has: %s."),
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
			: wxString::Format(ibMcpText("#%i (missing)"), (int)id)));
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
			return wxString::Format(ibMcpText("reading the binding '%s' of '%s'"),
				ArgProperty().Text(params), ibMcpNameOf(params));

		return wxString::Format(ibMcpText("binding '%s' to '%s'"), target, ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Wire one metaobject to another - the bindings that carry no value but a "
			"relationship: `ListOwner` (which catalog this one is subordinate to), "
			"`ListRegisterRecord` (which registers a document posts MOVEMENTS to, which is the "
			"same fact as the register accepting that document as a RECORDER), `ListGeneration` "
			"(what may be entered on the basis of what). This is the verb for 'this document "
			"writes that register' and for 'this catalog belongs to that one'. Without `target` "
			"it reads the binding back, which is also how you check what a write did - and the "
			"binding has two ends, so the other object learns about it too.");
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

		// ⭐⭐ THE PROPERTY ALREADY SAYS WHAT IT MAY BECOME — GetValueList hands back, per candidate,
		// the number that says WHICH and the VARIANT that is what to place. So nothing here builds a
		// value or names a variant class: walk the list, find the one the caller asked for, and put
		// that value in through the same door a click uses.
		//
		// 🛑 WHAT THIS REPLACED, because the shape of the mistake is worth keeping: the tool reached
		// past the list, edited the metadescription held INSIDE the property's own variant, and then
		// announced the change. `wxVariant` is reference-counted, so the "old value" every
		// notification path reads pointed at the very object just edited — old and new were one
		// description, the difference was empty, and ibValueMetaObjectDocument::OnPropertyChanged,
		// which puts the document's reference into the register's Recorder, walked an empty list. The
		// binding read back correctly and the OTHER END was never made: the configuration stood, and
		// refused to save with "Doesn't have any recorder".
		ibPropertyChoiceList choices;

		if (binding.property->GetValueList(choices) == ibPropertyChoiceMode::None) {
			refusal = wxString::Format(
				ibMcpText("'%s' offers nothing to choose from."), binding.property->GetName());
			return false;
		}

		// BY NAME, ACROSS EVERY KIND — a caller writing "GoodsInWarehouses" should not also have to
		// say which metatype it is. The name is resolved ONCE, here, and what is compared against the
		// list afterwards is the metaID: the number is what a choice IS, and the two vocabularies a
		// name has are not.
		ibValueMetaObject* other = activeMetaData->FindAnyObjectByFilter<ibValueMetaObject>(target);

		if (other == nullptr) {
			refusal = wxString::Format(
				ibMcpText("Nothing in this configuration is called '%s'."), target);
			return false;
		}

		for (unsigned int index = 0; index < choices.GetCount(); ++index) {

			if (choices.GetId(index) != (long)other->GetMetaID())
				continue;

			// ⭐⭐ A MULT BINDING IS A SET, AND ONE CHOICE IS ONE MEMBER OF IT. The list offers each
			// candidate on its own, so placing that value AS IT COMES would make every bind a
			// replacement — a document that writes eight registers would end up writing the last one
			// named. So the set the property holds now is read, the choice is added to it (or taken
			// out of it), and the whole set goes back.
			//
			// ⚠ THE VARIANT EDITED HERE IS THE LIST'S OWN — built while listing, owned by nobody,
			// alive until this call ends. The one thing that must never be touched is the variant the
			// PROPERTY holds: editing that is editing the old value and the new one at once, which is
			// how the second end of the binding stopped being made.
			wxVariant placing = choices.GetValue(index);

			ibVariantDataMetaDesc* carried = binding.property->find_cell_variant<ibVariantDataMetaDesc>(placing);
			if (carried == nullptr) {
				refusal = wxString::Format(
					ibMcpText("'%s' offered a value this tool cannot read."), binding.property->GetName());
				return false;
			}

			ibMetaDescription set = ArgOnly().Flag(params) ? ibMetaDescription() : *binding.held;
			const ibMetaID id = other->GetMetaID();

			if (ArgRemove().Flag(params)) {

				ibMetaDescription kept;

				for (unsigned int held = 0; held < set.GetTypeCount(); ++held)
					if (set.GetByIdx(held) != id)
						kept.AppendMetaType(set.GetByIdx(held));

				set = kept;
			}
			else if (!set.ContainMetaType(id)) {
				set.AppendMetaType(id);
			}

			carried->GetMetaDesc() = set;

			// PLACED THROUGH ibPropertyGate — veto, set, tell, and the owner's OnChildChanged. The
			// telling is not decoration: it IS the second end of the binding.
			if (!ibMcpApplyByHand(binding.property, placing, refusal))
				return false;

			activeMetaData->Modify(true);

			result.SetValue(wxT("target"), choices.GetName(index));

			// ⚠ ASKED AGAIN, NOT REUSED. The write replaced the variant `binding.held` points into,
			// so reading it here would report the value that has just been let go.
			if (const ibVariantDataMetaDesc* now =
					binding.property->find_cell_variant<ibVariantDataMetaDesc>())
				result.AddField(wxT("bound"),
					ibDataValue::Array(BoundNames(activeMetaData, now->GetMetaDesc())));
			break;
		}

		if (!result.FindField(wxT("target"))) {

			wxString offered;
			for (unsigned int index = 0; index < choices.GetCount(); ++index)
				offered << (offered.IsEmpty() ? wxT("") : wxT(", ")) << choices.GetName(index);

			refusal = offered.IsEmpty()
				? wxString::Format(ibMcpText("'%s' has nothing of that kind to be bound to yet."),
					binding.property->GetName())
				: wxString::Format(ibMcpText("'%s' takes one of: %s."),
					binding.property->GetName(), offered);
			return false;
		}

		// ⭐ THE ANSWER IS THE READING, not a claim of success. If the platform declined the
		// target, `bound` says so by not containing it — which is the only report that cannot be
		// wrong.
		result.SetValue(wxT("note"),
			ibMcpText("`bound` is read back from the binding after the change - if the target is not in "
			  "it, the platform did not take it."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataBind);
