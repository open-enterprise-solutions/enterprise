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
#include "core/stringUtils.h"
#include "backend/typeDescription.h"              // ibMetaDescription — what these properties hold
#include "backend/propertyManager/property/variant/variantMetaDesc.h"   // the one shape that holds it

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments, declared once and read through the same objects — see ibMcpTool::Arguments().
const ibArg& ArgId() { static const ibArg a(wxT("id"), ibArg::Kind::Whole, ibMcpText("The object being wired, by NodeId."), true); return a; }
const ibArg& ArgProperty() { static const ibArg a(wxT("property"), ibArg::Kind::Text, ibMcpText("Which binding. Naming one the object does not have is refused WITH the list of the ones it does, so a wrong guess costs one call."), true); return a; }
const ibArg& ArgTarget() { static const ibArg a(wxT("target"), ibArg::Kind::Any, ibMcpText("The metaobject to bind, by its name, by Kind.Name (AccumulationRegister.Cars), or by its id as a number or a string of digits. A bare name is resolved among this binding's own candidates. When more than one candidate shares it, the call is refused and names them. Omit to read the binding instead of changing it.")); return a; }
const ibArg& ArgRemove() { static const ibArg a(wxT("remove"), ibArg::Kind::Flag, ibMcpText("Take the target OUT of the binding instead of putting it in.")); return a; }
const ibArg& ArgOnly() { static const ibArg a(wxT("only"), ibArg::Kind::Flag, ibMcpText("Make the target the ONLY thing bound, clearing whatever else was there. Off by default, because most bindings legitimately hold several; a binding that holds one (a register's chart) is replaced either way.")); return a; }

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
//
// ⚠ AN ARRAY HERE, A STRING IN metadata_get, AND BOTH ARE RIGHT. This tool is ABOUT the binding, so
// it answers with the members one by one and names an id that resolves to nothing — a binding
// pointing at a deleted object is exactly what somebody asking this verb wants to see. A property
// walk is about the object, and there the value says itself the way the variant renders it.
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

// Kind.Name when the object is still there, the choice's own name when it is not.
wxString CandidateLabel(ibMetaData* metaData, long id, const wxString& name)
{
	ibValueMetaObject* object = ibFindMetaObjectById(metaData, (ibMetaID)id);
	if (object == nullptr)
		return name;
	return object->GetClassName() + wxT(".") + object->GetName();
}

// A name, Kind.Name, or an id. Absent is the read. Anything else is refused: a boolean is not a target.
wxString ReadTarget(const ibDataNode& params, bool& rejected)
{
	rejected = false;
	const ibDataValue* given = params.FindField(ArgTarget().Name());
	if (given == nullptr)
		return wxString();
	if (given->Kind() == ibDataKind::String)
		return given->AsString();
	if (given->Kind() == ibDataKind::Number)
		return wxString::Format(wxT("%lld"), (long long)given->AsInt());
	rejected = true;
	return wxString();
}

// The index of the one candidate `target` names, -1 when it names none, -2 when it names several
// (refusal already written). Resolved HERE, among the property's own candidates: a catalog and a
// register may share a name, and the catalog is not a candidate for a document's registers.
int ChooseCandidate(ibMetaData* metaData, const ibPropertyChoiceList& choices,
	const wxString& target, const wxString& binding, wxString& refusal)
{
	std::vector<unsigned int> hits;

	for (unsigned int index = 0; index < choices.GetCount(); ++index) {
		const long id = choices.GetId(index);
		const wxString name = choices.GetName(index);
		const wxString qualified = CandidateLabel(metaData, id, name);
		const wxString idText = wxString::Format(wxT("%ld"), id);
		if (target.IsSameAs(idText, false) || target.IsSameAs(name, false) || target.IsSameAs(qualified, false))
			hits.push_back(index);
	}

	if (hits.size() == 1)
		return (int)hits.front();
	if (hits.empty())
		return -1;

	wxString listed;
	for (unsigned int index : hits)
		listed << (listed.IsEmpty() ? wxT("") : wxT(", "))
			<< CandidateLabel(metaData, choices.GetId(index), choices.GetName(index))
			<< wxString::Format(wxT(" (#%ld)"), choices.GetId(index));

	refusal = wxString::Format(
		ibMcpText("'%s' names more than one candidate of '%s': %s. Name the kind as well, or pass the id."),
		target, binding, listed);
	return -2;
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
		bool rejected = false;
		const wxString target = ReadTarget(params, rejected);

		if (target.IsEmpty() && !rejected)
			return wxString::Format(ibMcpText("reading the binding '%s' of '%s'"),
				ArgProperty().Text(params), ibMcpNameOf(params));

		return wxString::Format(ibMcpText("binding '%s' to '%s'"), target, ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Wire one metaobject to another - the bindings that carry no value but a "
			"relationship: `ListOwner` (which catalog this one is subordinate to), "
			"`ListRegisterRecord` (which registers a document posts MOVEMENTS to, which is the "
			"same fact as the register accepting that document as a RECORDER), "
			"`ListSequenceRecord` (which SEQUENCES it registers in - its place in an order, said "
			"beside the movements and never mixed into them), `ListGeneration` "
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

		bool rejected = false;
		const wxString target = ReadTarget(params, rejected);
		if (rejected) {
			refusal = ibMcpText("'target' takes a name, Kind.Name, or an id.");
			return false;
		}

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
		const ibPropertyChoiceMode mode = binding.property->GetValueList(choices);

		if (mode == ibPropertyChoiceMode::None) {
			refusal = wxString::Format(
				ibMcpText("'%s' offers nothing to choose from."), binding.property->GetName());
			return false;
		}

		// AMONG THIS BINDING'S CANDIDATES. A bare name used to be resolved across every kind first,
		// so Catalog.Cars was found and AccumulationRegister.Cars — the one this list offers — was
		// never asked. Kind.Name and the id name one candidate when the bare name names several.
		const int chosen = ChooseCandidate(activeMetaData, choices, target, binding.property->GetName(), refusal);
		if (chosen == -2)
			return false;

		if (chosen >= 0) {
			const unsigned int index = (unsigned int)chosen;
			ibValueMetaObject* other = ibFindMetaObjectById(activeMetaData, (ibMetaID)choices.GetId(index));
			if (other == nullptr) {
				refusal = wxString::Format(
					ibMcpText("Nothing in this configuration has id %ld."), choices.GetId(index));
				return false;
			}

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

			// ⭐ A SINGLE BINDING IS REPLACED, NOT ADDED TO — the property says which it is (the mode its
			// list came back with). Adding to it would give a calculation register two charts, a binding
			// its every reader takes as one.
			const bool replaces = ArgOnly().Flag(params) ||
				(mode == ibPropertyChoiceMode::Single && !ArgRemove().Flag(params));
			ibMetaDescription set = replaces ? ibMetaDescription() : *binding.held;
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

			// ⭐ SAID FROM THE SET THAT WAS PLACED, and there is nothing left to go and read. This
			// used to reach back into the property's cell for a fresh variant — for a good reason
			// that had stopped applying: `binding.held` points into the value the write let go, so
			// reporting THAT would report the old relationship. But the new one is not somewhere
			// else to be found; it is `set`, composed three lines up out of what GetValueList
			// offered, and placed through the gate, which returned false if it did not land.
			// Asking the property to hand it back again is one more cast for a value already held.
			result.AddField(wxT("bound"), ibDataValue::Array(BoundNames(activeMetaData, set)));
		}
		else {
			wxString offered;
			for (unsigned int index = 0; index < choices.GetCount(); ++index)
				offered << (offered.IsEmpty() ? wxT("") : wxT(", "))
					<< CandidateLabel(activeMetaData, choices.GetId(index), choices.GetName(index));

			refusal = offered.IsEmpty()
				? wxString::Format(ibMcpText("'%s' has nothing of that kind to be bound to yet."),
					binding.property->GetName())
				: wxString::Format(ibMcpText("'%s' takes one of: %s."),
					binding.property->GetName(), offered);
			return false;
		}

		// ⭐ THE ANSWER IS THE SET THAT WAS PLACED, and the sentence says that rather than promising
		// a re-reading it no longer does. A write that the platform declined does not reach here at
		// all — ibMcpApplyByHand returns false and the refusal is the answer — so what `bound` is
		// worth is exactly "this is what went in". A caller who wants the other kind of assurance
		// asks this verb again without a target, which reads the binding and nothing else.
		result.SetValue(wxT("note"),
			ibMcpText("`bound` is the set as it was placed. A change the platform declined would have "
			  "come back as a refusal instead of an answer; to read the binding on its own, ask "
			  "again without a target."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataBind);
