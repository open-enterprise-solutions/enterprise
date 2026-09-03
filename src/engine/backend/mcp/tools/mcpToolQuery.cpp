////////////////////////////////////////////////////////////////////////////
//	Description : the query tools — what can be asked, and of what
////////////////////////////////////////////////////////////////////////////
//
// ⭐ THIS IS A DOOR ONTO WORK ALREADY DONE. query/queryConstructorModel.h is the
// constructor's BACKEND half — built so the window above it knows nothing about
// metadata, and therefore built exactly right for a caller that is not a window
// at all. Two rules are written into that file and both are why these tools are
// three lines each instead of a walk of their own:
//
//   the catalogue of sources is a WALK over the factory, never a list — so a
//   metatype registered tomorrow is queryable here the day it registers;
//   the fields are ASKED OF THE SOURCE — a register's balance answers with its
//   own columns, a nested query with its projections.
//
// WHY IT MATTERS FOR SOMETHING WRITING QUERIES. Knowing the grammar is not the
// hard part; knowing what exists is. `Catalog.Goods` versus
// `AccumulationRegister.StockBalance.Balance`, which of them takes parameters,
// which field can be walked through a reference — none of that is guessable,
// all of it is answerable.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/metadataConfiguration.h"
#include "backend/query/queryConstructorModel.h"
#include "backend/query/queryableFactory.h"      // ibQuerySourceParameter — what a virtual table takes
#include "backend/query/queryException.h"
#include "backend/query/queryLowering.h"
#include "backend/query/queryParser.h"
#include "backend/query/queryable.h"

#include <wx/tokenzr.h>

namespace {

ibDataValue FieldEntry(const ibQueryConstructorField& field)
{
	std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();

	// The technical name is what a query writes; the presentation is what a
	// person reads. Both, and named apart — putting a synonym into a query is
	// the mistake this distinction exists to prevent.
	node->SetValue(wxT("name"), field.m_name);
	if (!field.m_presentation.IsEmpty() && field.m_presentation != field.m_name)
		node->SetValue(wxT("title"), field.m_presentation);
	if (!field.m_source.IsEmpty())
		node->SetValue(wxT("source"), field.m_source);

	// A reference can be walked one level further; a composite one cannot, and
	// says so by answering zero.
	if (field.m_reference)
		node->AddField(wxT("reference"), ibDataValue::Bool(true));

	return ibDataValue::Child(node);
}

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgContains()
{
	static const ibArg s_a(wxT("contains"), ibArg::Kind::Text,
		ibMcpText("Narrow to paths containing this text. Omit for all of them."));
	return s_a;
}

const ibArg& ArgPath()
{
	static const ibArg s_a(wxT("path"), ibArg::Kind::Text,
		ibMcpText("The source's dotted path, as query_sources gives it."), /*required*/ true);
	return s_a;
}

const ibArg& ArgText()
{
	static const ibArg s_a(wxT("text"), ibArg::Kind::Text,
		ibMcpText("The query text, exactly as it would be written in the module - the whole package "
			  "if it is one."), /*required*/ true);
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// query_sources
//---------------------------------------------------------------------------
class ibMcpToolQuerySources : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("query_sources"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("looking at what a query can read from");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Everything a query may read from in this configuration, by the dotted path a "
			"query names it with - Catalog.Goods, AccumulationRegister.StockBalance.Balance and "
			"the rest. Ask this before writing a FROM: the names are this configuration's, and "
			"a virtual table is not guessable from the register's name.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {  };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
			refusal = ibMcpText("No configuration is open.");
			return false;
		}

		const wxString contains = ArgContains().Text(params);

		ibQueryConstructorModel model(activeMetaData);

		std::vector<ibDataValue> sources;
		for (const ibQueryConstructorSource& source : model.GetSources()) {

			const wxString path = source.Text();
			if (!contains.IsEmpty() && path.Lower().Find(contains.Lower()) == wxNOT_FOUND)
				continue;

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("path"), path);
			if (!source.m_presentation.IsEmpty() && source.m_presentation != path)
				entry->SetValue(wxT("title"), source.m_presentation);

			sources.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("sources"), ibDataValue::Array(sources));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolQuerySources);

//---------------------------------------------------------------------------
// query_fields
//---------------------------------------------------------------------------
class ibMcpToolQueryFields : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("query_fields"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("looking at the fields of %s"),
			ArgPath().Text(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("The fields of one query source, with the name a query writes and the words a "
			"person reads. A field marked as a reference can be walked further with a dot.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgPath() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
			refusal = ibMcpText("No configuration is open.");
			return false;
		}

		const wxString path = ArgPath().Text(params);
		if (path.IsEmpty()) {
			refusal = ibMcpText("No source named.");
			return false;
		}

		ibQueryConstructorModel model(activeMetaData);

		// The source is named the way a query names it, so it is built the way a
		// query builds it — one AST node, not a lookup of our own. The path is a
		// SEQUENCE of segments, which is why the dots are split rather than kept:
		// "AccumulationRegister.StockBalance.Balance" is three answers to three
		// questions, not one string.
		ibQuerySource source;
		wxStringTokenizer segments(path, wxT("."));
		while (segments.HasMoreTokens())
			source.m_name.push_back(segments.GetNextToken());

		const ibQueryPackage empty;
		const std::vector<ibQueryConstructorField> fields = model.GetFields(source, empty, 0);

		if (fields.empty()) {
			refusal = wxString::Format(
				ibMcpText("'%s' answers with no fields - check the path with query_sources."), path);
			return false;
		}

		std::vector<ibDataValue> out;
		for (const ibQueryConstructorField& field : fields)
			out.push_back(FieldEntry(field));

		std::vector<ibDataValue> parameters;
		for (const ibQuerySourceParameter& parameter : model.GetSourceParameters(source)) {

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), parameter.m_name);
			entry->AddField(wxT("required"), ibDataValue::Bool(parameter.m_required));
			entry->AddField(wxT("condition"), ibDataValue::Bool(parameter.m_condition));

			// ⭐ A CLOSED SET, WHEN THE SOURCE DECLARES ONE — the periodicity a
			// turnover rolls up to, and its like. Declared by the source because
			// only the source knows what it accepts; passed on for the same
			// reason, so a caller writes one of these instead of inventing a word
			// that parses and means nothing.
			if (!parameter.m_choices.empty()) {
				std::vector<ibDataValue> choices;
				for (const wxString& choice : parameter.m_choices)
					choices.push_back(ibDataValue::String(choice));
				entry->AddField(wxT("choices"), ibDataValue::Array(choices));
			}

			// ⭐ …AND WHAT IT DECIDES, where the source says so. A list of words answers "which may
			// I write" and not "what will happen", and for `Periodicity` the two are a whole
			// storey apart from the property of the same name.
			if (!parameter.m_description.IsEmpty())
				entry->SetValue(wxT("description"), parameter.m_description);

			parameters.push_back(ibDataValue::Child(entry));
		}

		result.SetValue(wxT("path"), path);
		result.AddField(wxT("fields"), ibDataValue::Array(out));

		// A VIRTUAL TABLE TAKES ARGUMENTS, and a caller that does not know which
		// writes a balance with no date and gets today's by accident.
		if (!parameters.empty())
			result.AddField(wxT("parameters"), ibDataValue::Array(parameters));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolQueryFields);

//---------------------------------------------------------------------------
// query_check — folded in from mcpToolQueryCheck.cpp on 2026-09-01: one subject,
// one file. What it checks and why it had to exist is the block below.
//
//
// THE HOLE THIS FILLS, and it was a real one. A query written into a module
// lives inside a STRING, and a string is opaque to the compiler: script_check
// answers "ok" for a module whose query names a table that does not exist,
// because as far as the grammar is concerned the module contains a piece of
// text. So everything else could be verified in place and the query - the part
// most likely to be wrong, and the part hardest to get right from memory -
// could only be verified by running it in front of somebody.
//
// THE CHECK ALREADY EXISTED. It is the gate the constructor passes before it
// opens a stored query: parse, then resolve the names against the configuration.
// Two calls. Nothing here is new work; what was missing was a door.
//
// TWO FAILURES, AND THEY ARE DIFFERENT. A query may not PARSE - a comma in the
// wrong place - and the engine says where, to the character. Or it may parse
// perfectly and NAME something that is not there: a field renamed, a table
// deleted, a virtual table asked for a column it does not have. The second is
// the one a writer working from memory actually makes, and the one no grammar
// can catch.
//


//---------------------------------------------------------------------------
// query_check
//---------------------------------------------------------------------------
class ibMcpToolQueryCheck : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("query_check"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("checking a query against the configuration");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Read a query the way the engine will: parse it, then resolve every name in it "
			"against this configuration. ALWAYS ASK THIS BEFORE PUTTING A QUERY IN A MODULE - "
			"a query lives inside a string, so script_check answers 'ok' for a module whose "
			"query names a table that does not exist.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgText() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
			refusal = ibMcpText("No configuration is open.");
			return false;
		}

		wxString text = ArgText().Text(params);
		if (wxString(text).Trim(true).Trim(false).IsEmpty()) {
			refusal = ibMcpText("Nothing to check - pass the query text.");
			return false;
		}

		wxString complaint;
		wxString stage;
		s32      line = 0;
		s32      column = 0;

		try {
			ibQueryParser parser;
			const ibQueryPackage package = parser.ParsePackage(text);

			// AGAINST THIS CONFIGURATION, and said so explicitly. A query in a
			// module sets no scope of its own and still means the tree it lives in;
			// here the tree is named, because a tool is not standing inside one.
			const ibSourceMetaDataScope resolveAgainst(activeMetaData);
			ibQueryLowering::CheckNames(package, std::map<wxString, ibValue>());
		}
		// ⚠ TWO DIFFERENT FAILURES, TWO VARIETIES, TOLD APART BY WHICH ARRIVED. The
		// lowering raises a NAME refusal (the text reads and asks for something that is
		// not there); the lexer and parser raise a SYNTAX one. Both carry the span,
		// which is the whole value of this branch of the family existing.
		//
		// 🛑 IT USED TO GUESS FROM THE POSITION: 0:0 meant a name, anything else a typo.
		// That held only while an unresolved source had nowhere to point, and the moment
		// the parser began recording a source's span (2026-09-02) the guess inverted —
		// telling an author to hunt for a typo in a query that parses perfectly. The
		// repair the old comment here asked for is the one that was made.
		catch (const ibBackendQueryNameException& e) {
			stage = wxT("names");
			complaint = e.GetErrorDescription();
			line = (s32)e.GetLine();
			column = (s32)e.GetColumn();
		}
		catch (const ibBackendQuerySourceException& e) {
			stage = wxT("syntax");
			complaint = e.GetErrorDescription();
			line = (s32)e.GetLine();
			column = (s32)e.GetColumn();
		}
		catch (const ibBackendQueryException& e) {
			// Parsed, but names something that is not there. query_sources and
			// query_fields are where the right names come from.
			stage = wxT("names");
			complaint = e.GetErrorDescription();
		}
		catch (const ibBackendException& e) {
			stage = wxT("engine");
			complaint = e.GetErrorDescription();
		}

		result.AddField(wxT("ok"), ibDataValue::Bool(complaint.IsEmpty()));

		if (complaint.IsEmpty())
			return true;

		result.SetValue(wxT("stage"), stage);
		result.SetValue(wxT("problem"), complaint);

		// Only when there is one: 0:0 means the engine had nowhere to point, and
		// reporting it as a position sends a reader to the first character.
		if (line > 0 || column > 0) {
			result.AddField(wxT("line"), ibDataValue::Int((s64)line));
			result.AddField(wxT("column"), ibDataValue::Int((s64)column));
		}

		// WHICH QUESTION TO ASK NEXT, said rather than left to be guessed. The two
		// failures have different remedies and pointing at the wrong one costs a
		// round trip every time.
		result.SetValue(wxT("nextStep"), stage == wxT("names")
			? ibMcpText("A name in the query does not resolve. query_sources lists what can be read "
			    "from, query_fields what a source has.")
			: ibMcpText("The engine could not read the text at that position."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolQueryCheck);
