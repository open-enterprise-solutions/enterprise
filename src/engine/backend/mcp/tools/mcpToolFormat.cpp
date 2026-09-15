////////////////////////////////////////////////////////////////////////////
//	Description : THE FORMAT STRING - format_string. What Format(value, format)
//	              reads, built and checked by the code that runs it.
////////////////////////////////////////////////////////////////////////////
//
// ⭐ WHY IT EXISTS. A format string is written by hand in a module (`Format(x, "NFD=2; NGS= ")`), and
// every way of getting one wrong is silent: a code the function does not read changes nothing, `mm` is
// the month where the minutes were meant, a separator trimmed to nothing leaves a number ungrouped.
// The designer has a constructor for it; a caller writing code over MCP had nothing but the help text
// and a guess (Max, 2026-09-15: "connect the format tool for yourself").
//
// ⭐ AND IT IS THE SAME CODE, not a description of it. The string is read, edited and written by
// ibFormatString (backend/formatString.h), and every sample in the answer is ibFormatString::
// Apply - which IS Format. What this answers is what a module prints.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/formatString.h"
#include "backend/compiler/value.h"

#include <memory>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

const ibArg& ArgFormat()
{
	static const ibArg s_a(wxT("format"), ibArg::Kind::Text,
		ibMcpText("The format string to start from, exactly as Format takes it - 'NFD=2; NGS= '. Omit it "
			  "to start from nothing, which formats every value as its plain string."));
	return s_a;
}

const ibArg& ArgSet()
{
	static const ibArg s_a(wxT("set"), ibArg::Kind::Node,
		ibMcpText("Codes to write over it, as an object of code to value: {\"NFD\": 2, \"NGS\": \" \", "
			  "\"DF\": \"dd.mm.yyyy\"}. A number or a string each. An EMPTY string is a value, not a "
			  "removal - {\"NZ\": \"\"} prints a zero as nothing; take a code out with `clear`. Only the "
			  "codes Format reads are taken."));
	return s_a;
}

const ibArg& ArgClear()
{
	static const ibArg s_a(wxT("clear"), ibArg::Kind::Many,
		ibMcpText("Codes to take out, by name: [\"NZ\", \"DE\"]. Applied before `set`."));
	return s_a;
}

const ibArg& ArgSample()
{
	static const ibArg s_a(wxT("sample"), ibArg::Kind::Any,
		ibMcpText("One more value to format besides the standard samples: a number or a flag as itself, "
			  "or a DATE written as text with its time - '2026-12-31 00:00:00'. A string is read as a "
			  "date because a string is never formatted: Format prints it as it is."));
	return s_a;
}

const wxChar* KindWord(ibFormatKind kind)
{
	switch (kind) {
	case ibFormatKind::Number:  return wxT("number");
	case ibFormatKind::Date:    return wxT("date");
	case ibFormatKind::Boolean: return wxT("boolean");
	}
	return wxT("number");
}

// One printed sample: what went in, in words, and what Format made of it.
ibDataValue Sample(const ibFormatString& format, const wxString& shown, const ibValue& value)
{
	std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
	entry->SetValue(wxT("value"), shown);
	entry->SetValue(wxT("prints"), format.Apply(value));
	return ibDataValue::Child(entry);
}

// The object a caller sent under `set` - as a child, or as a value carrying one.
const ibDataNode* NodeArgument(const ibDataNode& params, const wxString& name)
{
	if (const ibDataNode* child = params.FindChild(name))
		return child;
	for (const ibDataValue* value : { params.FindField(name), params.FindProperty(name) }) {
		if (value != nullptr && value->Kind() == ibDataKind::Child)
			return value->AsChild().get();
	}
	return nullptr;
}

//---------------------------------------------------------------------------
// format_string
//---------------------------------------------------------------------------
class ibMcpToolFormatString : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("format_string"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString format = ArgFormat().Text(params);
		return format.IsEmpty() ? ibMcpText("building a format string")
			: wxString::Format(ibMcpText("reading the format string '%s'"), format);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("BUILD OR CHECK A FORMAT STRING - the second argument of Format(value, format) - with "
			"the code that runs it. Hand it a string, codes to set or clear, or both; it answers the "
			"string as it will be written, each code it holds with the kind of value it applies to, the "
			"codes it keeps without reading, and SAMPLES: a number, a date and a flag each printed "
			"through it - by the same function Format is, so they are what a module prints.\n"
			  "\n"
			"THE CODES, `CODE=value` pairs separated by `;`:\n"
			"  NUMBER  ND significant digits in all, trailing zeros dropped; NFD digits after the point, "
			"always that many (1250.5 at NFD=2 prints 1250.50); NDS the decimal separator; NGS the group "
			"separator - groups of three unless NG says otherwise; NG digits per group; NZ what a zero "
			"prints as.\n"
			"  DATE    DF the pattern; DE what an empty date prints as.\n"
			"  BOOLEAN BT and BF what True and False print as.\n"
			  "\n"
			"STOP: IN A DATE PATTERN `mm` IS THE MONTH AND `MM` THE MINUTES. yyyy or yy the year, mm or m "
			"the month, dd or d the day, HH or H the hours, MM or M the minutes, SS or S the seconds - "
			"'dd.mm.yyyy HH:MM'. The other way round reads fine and prints the minutes as the month.\n"
			  "\n"
			"KEY: AN ABSENT CODE AND AN EMPTY ONE ARE DIFFERENT. No NZ prints a zero as a number; NZ= "
			"prints it as nothing. A value that is all space IS a space (NGS= groups with one); any other "
			"is trimmed. `;` and `=` cannot stand in a value at all, and are refused here.\n"
			  "\n"
			"NOTE: A CODE FORMAT DOES NOT READ changes nothing - it is kept in the string as written and "
			"listed under `kept`, but not taken from `set`. A value of any type other than a number, a "
			"date and a flag is printed as its plain string whatever the codes say.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgFormat(), ArgSet(), ArgClear(), ArgSample() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibFormatString format = ibFormatString::Parse(ArgFormat().Text(params));

		// CLEAR first, then SET - so a call that does both says "instead of", not "and then nothing".
		if (const ibDataValue* clear = params.FindField(ArgClear().Name())) {
			if (clear->Kind() == ibDataKind::Array) {
				for (const ibDataValue& item : clear->AsArray()) {
					ibFormatCode code = ibFormatCode::NumberDigits;
					if (item.Kind() != ibDataKind::String || !ibFormatString::FindCode(item.AsString(), code)) {
						refusal = ibMcpText("`clear` names a code Format does not read. The codes are ND, NFD, "
							"NDS, NGS, NG, NZ, DF, DE, BT and BF, spelled in capitals.");
						return false;
					}
					format.Clear(code);
				}
			}
		}

		if (const ibDataNode* set = NodeArgument(params, ArgSet().Name())) {
			for (const std::pair<wxString, ibDataValue>& field : set->Fields()) {
				ibFormatCode code = ibFormatCode::NumberDigits;
				if (!ibFormatString::FindCode(field.first, code)) {
					refusal = wxString::Format(ibMcpText("'%s' is not a code Format reads, so setting it would "
						"change nothing a module prints. The codes are ND, NFD, NDS, NGS, NG, NZ, DF, DE, "
						"BT and BF, spelled in capitals."), field.first);
					return false;
				}
				wxString value;
				switch (field.second.Kind()) {
				case ibDataKind::String: value = field.second.AsString(); break;
				case ibDataKind::Number: value = field.second.AsNumber().ToString(); break;
				default:
					refusal = wxString::Format(ibMcpText("'%s' takes a number or a string. To take it out, "
						"name it in `clear`."), field.first);
					return false;
				}
				if (!ibFormatString::IsWritable(value)) {
					refusal = wxString::Format(ibMcpText("'%s' cannot hold ';' or '=': a format string has no "
						"way to write them - ';' ends a pair and '=' is not kept in one."), field.first);
					return false;
				}
				format.Set(code, value);
			}
		}

		// ⭐ WHAT IS ANSWERED IS WHAT THE ENGINE READ BACK - written, and read again by the one reader, so
		// a space trimmed or a separator dropped shows up here and not in a printed report.
		format = ibFormatString::Parse(format.Render());
		result.SetValue(wxT("format"), format.Render());

		std::vector<ibDataValue> codes;
		for (const ibFormatCode code : ibFormatString::Codes()) {
			const std::optional<wxString> value = format.Get(code);
			if (!value)
				continue;
			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("code"), wxString(ibFormatString::CodeName(code)));
			entry->SetValue(wxT("kind"), wxString(KindWord(ibFormatString::KindOf(code))));
			entry->SetValue(wxT("value"), *value);
			codes.push_back(ibDataValue::Child(entry));
		}
		result.AddField(wxT("codes"), ibDataValue::Array(codes));

		if (!format.m_other.empty()) {
			std::vector<ibDataValue> kept;
			for (const auto& pair : format.m_other) {
				std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
				entry->SetValue(wxT("code"), pair.first);
				entry->SetValue(wxT("value"), pair.second);
				kept.push_back(ibDataValue::Child(entry));
			}
			result.AddField(wxT("kept"), ibDataValue::Array(kept));
		}

		// THE SAMPLES - one of each kind, so a caller sees every tab's effect in one answer.
		std::shared_ptr<ibDataNode> samples = std::make_shared<ibDataNode>();
		{
			ibValue big, negative;
			big.SetNumber(wxT("1234567.891"));
			negative.SetNumber(wxT("-42.5"));
			samples->AddField(wxT("number"), ibDataValue::Array({
				Sample(format, wxT("1234567.891"), big),
				Sample(format, wxT("-42.5"), negative),
				Sample(format, wxT("0"), ibValue(0)) }));
			samples->AddField(wxT("date"), ibDataValue::Array({
				Sample(format, wxT("2026-09-15 14:05:07"), ibValue(2026, 9, 15, 14, 5, 7)),
				Sample(format, wxT("empty date"), ibValue(ibValueTypes::TYPE_DATE)) }));
			samples->AddField(wxT("boolean"), ibDataValue::Array({
				Sample(format, wxT("True"), ibValue(true)),
				Sample(format, wxT("False"), ibValue(false)) }));
		}
		result.AddField(wxT("samples"), ibDataValue::Child(samples));

		// …and the caller's own, when one came.
		if (const ibDataValue* given = params.FindField(ArgSample().Name())) {
			ibValue value;
			switch (given->Kind()) {
			case ibDataKind::Number: value = ibValue(given->AsNumber()); break;
			case ibDataKind::Bool:   value = ibValue(given->AsBool());   break;
			case ibDataKind::String:
				if (!value.SetDate(given->AsString()) || value.IsEmpty()) {
					refusal = wxString::Format(ibMcpText("`sample` '%s' is not a date this platform reads. A "
						"string here is a DATE, written with its time: '2026-12-31 00:00:00', '31.12.2026 "
						"00:00:00' or '20261231000000'."), given->AsString());
					return false;
				}
				break;
			default:
				refusal = ibMcpText("`sample` is a number, a flag, or a date written as text with its time.");
				return false;
			}
			result.AddField(wxT("sample"), Sample(format, value.GetString(), value));
		}
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolFormatString);

} // namespace
