#include "value.h"
#include "valueSerialization.h"

#include "backend/serialize/dataBuilder.h"
#include "backend/backend_exception.h"

////////////////////////////////////////////////////////////////////////////
// Value serialization — ONE mechanism, over a node
////////////////////////////////////////////////////////////////////////////
//
// There used to be a string form here, and it was the only one: every value had
// to reduce itself to text, and a composite could not — which is why arrays and
// structures had no packed form at all.
//
// Now there is one mechanism and it writes a NODE — the same tree the platform
// writes metadata through. What comes out the other end is the provider's
// business: binary for storage and transport, JSON for exchange and for a human
// reading a dump. A string is therefore still available everywhere it was, but
// as a RENDERING of the node rather than as a second implementation every type
// had to maintain.
//
// The split inside is deliberate:
//
//   Serialize / Deserialize     — the BASE's job: the header (the type),
//                                 written once so no child can spell it
//                                 differently or forget it.
//   DoSerialize / DoDeserialize — the CHILD's job: its own contents. Its
//                                 elements are asked the same question, so the
//                                 walk continues by itself, one class at a time.
//
// The default below knows the PRIMITIVES and nothing else, which is all the base
// can honestly claim. A mutable value — a form, an open object, a lambda —
// overrides nothing and is refused by IsTransferable before any of this runs.
////////////////////////////////////////////////////////////////////////////

namespace {
// Header / payload field names. Short because they repeat per element in a
// binary stream; stable because they are what a JSON dump shows.
const wxChar* const kFieldClsid = wxT("t");   // the type — written and read FIRST
const wxChar* const kFieldData  = wxT("v");   // the primitive payload
} // namespace

bool ibValue::Serialize(ibDataNode& node) const
{
	// THE GATE, not a second opinion: IsTransferable already answers "may this
	// value leave its session", and packing asks precisely that.
	if (!IsTransferable())
		return false;

	// THE TYPE AS TEXT. The node has codecs for wxString / bool / s32 / ibNumber /
	// wxDateTime / guid / blob — and none for a 64-bit id. Text costs a few bytes
	// in the binary provider and makes the JSON dump readable, which is half the
	// reason the JSON provider exists.
	node.SetValue(kFieldClsid, wxString::Format(wxT("%llu"), (unsigned long long)GetClassType()));
	return DoSerialize(node);
}

bool ibValue::Deserialize(const ibDataNode& node)
{
	// The type was resolved by whoever CREATED this value, so by
	// now it already is of the right type; what is left is its own contents.
	return DoDeserialize(node);
}

bool ibValue::DoSerialize(ibDataNode& node) const
{
	// A reference wrapper is an ALIAS — it hops to what it wraps, exactly as
	// IsTransferable does, so the wrapper never appears in the packed form.
	if (IsReference() && m_pRef != nullptr)
		return m_pRef->DoSerialize(node);

	switch (m_typeClass) {
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:
		// Nothing to write: the type in the header is the whole value.
		return true;
	case ibValueTypes::TYPE_BOOLEAN:
		node.SetValue(kFieldData, m_bData);
		return true;
	case ibValueTypes::TYPE_NUMBER:
		// Through the node's ibNumber codec — exact decimal, no double anywhere
		// on the way. The internal tagged-word/bignum layout stays internal: it
		// is built to be fast in memory, not durable on disk.
		node.SetValue(kFieldData, m_fData);
		return true;
	case ibValueTypes::TYPE_STRING:
		node.SetValue(kFieldData, GetString());
		return true;
	case ibValueTypes::TYPE_DATE:
		node.SetValue(kFieldData, GetDateTime());
		return true;
	default:
		break;
	}

	// A type with contents of its own that did not override this. NOT an
	// assertion and not a silent empty: the caller is told, and decides whether
	// that is a refusal or something to skip.
	return false;
}

bool ibValue::DoDeserialize(const ibDataNode& node)
{
	if (m_typeClass == ibValueTypes::TYPE_REFFER && m_pRef != nullptr)
		return m_pRef->DoDeserialize(node);

	switch (m_typeClass) {
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:
		return true;
	case ibValueTypes::TYPE_BOOLEAN:
		m_bData = node.GetValue<bool>(kFieldData);
		return true;
	case ibValueTypes::TYPE_NUMBER:
		m_fData = node.GetValue<ibNumber>(kFieldData);
		return true;
	case ibValueTypes::TYPE_STRING:
		SetString(node.GetValue<wxString>(kFieldData));
		return true;
	case ibValueTypes::TYPE_DATE:
		// Straight into the scalar: SetDate takes a STRING (the script-facing
		// conversion), and going through text here would parse what the codec
		// just formatted.
		m_dData = node.GetValue<wxDateTime>(kFieldData).GetValue().GetValue();
		return true;
	default:
		break;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////
// Creating a value out of a node — THE mechanism
////////////////////////////////////////////////////////////////////////////
//
// ONE mechanism, reached from two doors. A caller with no configuration in play
// — a test, a tool, a headless run — calls this directly. A caller holding a
// metadata calls that, and it creates the types only IT has (a catalog
// reference, an enum member) and redirects everything else straight back here.
//
// The value is still blind to metadata: it is the metadata that knows about
// this, not the other way round.
////////////////////////////////////////////////////////////////////////////

ibClassID ibReadNodeType(const ibDataNode& node)
{
	// Parsed into wx's own type first: ToULongLong writes through an
	// `unsigned long long*` and ibClassID is uint64_t — same width, different
	// type on LP64, so its address does not fit the parameter.
	// NO TYPE IN THE NODE READS AS UNDEFINED, not as zero. There is no such thing
	// as a value with class id 0: a value whose type is nothing IS Undefined, and
	// saying so here means every caller downstream asks one question ("is this a
	// type I have?") instead of two.
	unsigned long long parsed = 0;
	if (!node.GetValue<wxString>(kFieldClsid).ToULongLong(&parsed) || parsed == 0)
		return g_valueUndefinedCLSID;
	return static_cast<ibClassID>(parsed);
}

ibValue ibValue::FromNode(const ibDataNode& node)
{
	const ibClassID classType = ibReadNodeType(node);

	// THE END OF THE LINE. Either nobody asked a configuration at all, or one
	// asked its own registry first and sent the type down here. If the value
	// registry does not have it either, then nobody does. Returning an empty
	// would look like a legitimately empty value and surface far away as a blank
	// field nobody can explain.
	if (!ibValue::IsRegisterCtor(classType))
		ibBackendCoreException::Error(_("Unknown value type '%llu' in the data"),
			(unsigned long long)classType);

	ibValue created;
	try {
		created = ibValue::CreateObjectRef(classType);
	}
	catch (const ibBackendException&) {
		throw;
	}
	catch (...) {
		ibBackendCoreException::Error(_("Failed to create a value of type '%s'"),
			ibValue::GetNameObjectFromID(classType));
	}

	// CREATED, THEN HANDED THE WHOLE NODE — how it reads itself is its own
	// business. This layer never learns what any of them contain, only whether
	// they managed.
	if (!created.Deserialize(node))
		ibBackendCoreException::Error(_("Failed to read the contents of a value of type '%s'"),
			ibValue::GetNameObjectFromID(classType));

	return created;
}