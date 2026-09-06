////////////////////////////////////////////////////////////////////////////
//	Description : PACKING AND UNPACKING A VALUE - value_pack / value_unpack.
//	              The two directions of the platform's own serialiser, made
//	              reachable, so a caller can MAKE a typed value and not only
//	              copy one.
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ WHY THESE EXIST. A packed value is how every typed thing travels in this platform: a schema
// stores its parameters that way, a saved setting stores a filter's right-hand side that way,
// `SerializeValue` writes that shape and `DeserializeValue` reads it. So a caller could always COPY
// one — take the `Value` out of a report_get answer and hand it back somewhere else — and could not
// MAKE one. The gap showed up on the smallest possible case: a report wanted a period, JSON has no
// date, and a date could therefore not be stated at all (Max, 2026-09-06, asking the question that
// settled it: *"can you pack?"* — the honest answer was no).
//
// 🛑 AND THE ANSWER WAS NOT A CONVERSION TABLE. The first attempt parsed a date out of the text where
// the parameter was applied, which works for dates and then wants a branch for the next type, and
// every pair nobody wrote yet fails by producing an EMPTY value — silently, looking exactly like an
// answer (measured: a report said 0 at every date). What was missing was not a conversion; it was
// this door.
//
// ⭐ SO CONVERTING IS THE PLATFORM'S JOB, ASKED FOR DELIBERATELY. `value_pack` is handed a TYPE by
// name — the caller states it — and the platform makes a value of that type out of what was given.
// That is not guessing: the caller said which type they meant. Where nothing was given, the answer is
// the EMPTY packed form of that type, which is the shape to fill in and send back.
//
// ⭐⭐ AND COVERAGE GROWS WITHOUT THIS FILE CHANGING. It is the configuration's own serialiser
// underneath (`ibMetaData::Serialize` / `Deserialize`), of which the platform already says:
// "whatever learns to pack itself is in here the day it does, with nothing to add". A new type is
// CONNECTED there; nothing here is written again for it.

#include "backend/mcp/mcpTool.h"

#include "backend/metadataConfiguration.h"
#include "backend/typeDescription.h"
#include "backend/system/value/valueType.h"
#include "backend/stringUtils.h"                // CompareString - how this tree compares text

#include <memory>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

const ibArg& ArgType()
{
	static const ibArg s_a(wxT("type"), ibArg::Kind::Text,
		ibMcpText("What type to make it - the name as type_list gives it. The caller states this, which "
			  "is what makes the conversion below honest rather than a guess: 'make a Date of "
			  "this text' is an instruction, 'work out what this text might be' is not."),
		/*required*/ true);
	return s_a;
}

const ibArg& ArgValue()
{
	static const ibArg s_a(wxT("value"), ibArg::Kind::Any,
		ibMcpText("What it should hold, as a plain scalar - a string, a number, a flag. Omit it and the "
			  "answer is the EMPTY packed form of that type, which is the shape to look at, fill in "
			  "and send back."));
	return s_a;
}

const ibArg& ArgPacked()
{
	static const ibArg s_a(wxT("value"), ibArg::Kind::Node,
		ibMcpText("The packed value to read - the shape report_get shows under `Value`, or anything "
			  "value_pack answered with."), /*required*/ true);
	return s_a;
}

// The type the caller named, as a description of one. Empty when the name is not a type here, which
// is a refusal rather than a default: making 'whatever this looks like' is the thing being avoided.
bool TypeNamed(const ibDataNode& params, ibTypeDescription& into, wxString& refusal)
{
	if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
		refusal = ibMcpText("No configuration is open, so there is no vocabulary of types to name one in.");
		return false;
	}

	const wxString name = ArgType().Text(params);
	const ibClassID clsid = activeMetaData->GetIDObjectFromString(name);
	if (clsid == ibClassID(0)) {
		refusal = wxString::Format(
			ibMcpText("'%s' is not a type in this configuration. type_list says what there is - and the "
				  "vocabulary is this configuration's, not a general one."), name);
		return false;
	}

	into = ibTypeDescription();
	into.SetDefaultMetaType(clsid);
	return true;
}

//---------------------------------------------------------------------------
// value_pack
//---------------------------------------------------------------------------
class ibMcpToolValuePack : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("value_pack"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("packing a value of type '%s'"), ArgType().Text(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("MAKE A TYPED VALUE, packed - the form every typed thing travels in here. A "
			"schema stores its parameters this way, a saved setting stores a filter's right-hand "
			"side this way, and the language's own SerializeValue writes exactly this.\n"
			  "\n"
			"WHAT IT IS FOR: a value you could not otherwise state. JSON has a string, a number and "
			"a flag, and that is all - so a DATE, a REFERENCE, an enumeration member cannot be "
			"written as an argument at all. Pack one here and hand the answer to whatever takes a "
			"value: compose_run's `parameters`, a schema's own `Value`, a saved setting.\n"
			  "\n"
			"WITH NO `value` it answers the EMPTY packed form of the type. That is the shape to look "
			"at when you do not know one - fill it in and send it back. It is also how to see what a "
			"reference is made of, which is its type and its identifier and nothing else.\n"
			  "\n"
			"A DATE IS WRITTEN WITH ITS TIME - '2026-12-31 00:00:00', '31.12.2026 00:00:00' or "
			"'20261231000000'. The date on its own is NOT read, and that is worth stating because "
			"the failure used to be silent: what came back was the beginning of time, which a "
			"report then answers against without complaining. It is a refusal now.\n"
			  "\n"
			"THE CONVERSION IS THE PLATFORM'S and you asked for it: you name the type, and a value of "
			"that type is made from what you gave. Nothing anywhere else guesses - a value handed to "
			"a parameter whose type it is not is refused rather than converted, because there the "
			"caller said nothing about what they meant.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgType(), ArgValue() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibTypeDescription type;
		if (!TypeNamed(params, type, refusal))
			return false;

		// ⭐ THE PLATFORM MAKES IT. With something given, of that thing; with nothing, the type's own
		// default - which is the empty form worth looking at.
		ibValue made;
		if (const ibDataValue* given = params.FindField(ArgValue().Name())) {
			ibValue stated;
			switch (given->Kind()) {
			case ibDataKind::String: stated = ibValue(given->AsString()); break;
			case ibDataKind::Number: stated = ibValue(given->AsNumber()); break;
			case ibDataKind::Bool:   stated = ibValue(given->AsBool());   break;
			default:
				refusal = ibMcpText("`value` has to be a plain scalar - a string, a number or a flag. "
					"Anything richer is what the PACKED form is for, and you are making one.");
				return false;
			}
			made = ibValueTypeDescription::AdjustValue(type, stated, activeMetaData);

			// 🛑⭐⭐ A CONVERSION THAT DID NOT TAKE IS AN EMPTY VALUE, AND EMPTY IS NOT AN ERROR
			// ANYWHERE ELSE - which is what makes this the quietest failure on the whole road.
			// MEASURED 2026-09-06: `value_pack {type: Date, value: "2026-12-31"}` answered the
			// beginning of time with `empty: true` beside it, and that packed nothing travelled
			// into a report, which then answered 0 movements with a straight face. The sibling
			// conversion refuses outright - `{type: Number, value: "not a number"}` throws "Cannot
			// convert string to number!" - so a date is the odd one out among its own neighbours,
			// not a type with a different contract.
			//
			// ⚠ WHY THE GUARD IS HERE AND NOT IN `ibValue::GetDate`, which is where the silence
			// actually is. That function is the general "read this value as a date" and is used to
			// COMPARE arbitrary values (value.cpp: `m_dData == cParam.GetDate()`); making it throw
			// would turn a comparison into an exception across the engine. Here the caller has
			// SAID "make a Date of this text", so failing to is a refusal and nothing else.
			//
			// ⚠ THE TEST IS EMPTINESS THAT WAS NOT ASKED FOR, not emptiness. A number 0, a False
			// and an empty string are legitimate values and are themselves empty, so they arrive
			// empty and leave empty and nothing fires. Only a non-empty statement that came out
			// empty is a conversion that lost it.
			//
			// ⚠ AND ITS PRICE, named rather than hidden: the strings whose type renders back the
			// same way are let through by the second test - "0" as a Number stays "0" - but "0.00"
			// renders as "0" and would be refused, wrongly. The trade is deliberate: a caller
			// refused a padded zero re-sends it as a JSON number and loses a call, where a caller
			// handed an empty period loses a figure and does not know it.
			if (made.IsEmpty() && !stated.IsEmpty()
				&& !stringUtils::CompareString(made.GetString(), stated.GetString())) {
				refusal = wxString::Format(
					ibMcpText("'%s' could not be made into a %s - what came back is that type's "
						"EMPTY value, which would have travelled on as if it were an answer. "
						"A DATE is written with its time: '2026-12-31 00:00:00', '31.12.2026 "
						"00:00:00' or '20261231000000'. The date alone is not read."),
					stated.GetString(), ArgType().Text(params));
				return false;
			}
		}
		else {
			made = ibValueTypeDescription::AdjustValue(type, activeMetaData);
		}

		std::shared_ptr<ibDataNode> packed = std::make_shared<ibDataNode>();
		activeMetaData->Serialize(made, *packed);

		result.AddField(wxT("packed"), ibDataValue::Child(packed));
		// …and what it came out as, in words, so a caller can see at once whether the type they named
		// is the type they got.
		result.SetValue(wxT("text"), made.GetString());
		result.SetValue(wxT("empty"), made.IsEmpty());
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolValuePack);

//---------------------------------------------------------------------------
// value_unpack
//---------------------------------------------------------------------------
class ibMcpToolValueUnpack : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("value_unpack"); }

	wxString GetActivity(const ibDataNode& /*params*/) const override
	{
		return ibMcpText("reading a packed value");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("READ A PACKED VALUE - what it IS, rather than the bytes it is stored as. Hand it "
			"anything in that form: a schema's parameter `Value`, a filter's right-hand side, "
			"whatever value_pack answered with.\n"
			  "\n"
			"It answers with the type, the presentation a person would see, and - where the value "
			"has one - its identifier. That last is what a REFERENCE actually is, and the reason a "
			"reference cannot be written as a scalar anywhere.\n"
			  "\n"
			"⚠ A designer holds the CONFIGURATION, not the data: a PREDEFINED item can be named "
			"here, an ordinary one is a row in somebody's application and this can only say what "
			"the packed value claims about it.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgPacked() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
			refusal = ibMcpText("No configuration is open, and a packed value means nothing without one: "
				"its type lives in the configuration's registry.");
			return false;
		}

		const ibDataNode* packed = params.FindChild(ArgPacked().Name());
		if (packed == nullptr) {
			refusal = ibMcpText("No `value` came with the call. It is the packed form - a sub-node, not a "
				"scalar; a scalar needs no unpacking.");
			return false;
		}

		const ibValue read = activeMetaData->Deserialize(*packed);

		result.SetValue(wxT("text"), read.GetString());
		result.SetValue(wxT("empty"), read.IsEmpty());

		// The type it turned out to BE - which is the question, and which a caller cannot answer by
		// looking at the packed node without knowing this file's business.
		const wxString named = activeMetaData->GetNameObjectFromID(read.GetClassType());
		if (!named.IsEmpty())
			result.SetValue(wxT("type"), named);

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolValueUnpack);

} // namespace
