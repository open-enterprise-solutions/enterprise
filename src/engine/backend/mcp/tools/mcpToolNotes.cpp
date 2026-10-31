////////////////////////////////////////////////////////////////////////////
//	Description : the two texts a metaobject carries — help, and notes
////////////////////////////////////////////////////////////////////////////
//
// HELP is for the PERSON USING THE APPLICATION: what this thing is and what to put in it, read
// on F1. NOTES are the ENGINEERING INTENT in markdown — why the object exists, what it was
// decided to be, what was tried and rejected — read by whoever picks the work up next.
//
// ⭐ THE READ ANSWERS ABOUT THE WHOLE CONFIGURATION AT ONCE, and that is the point of it. A
// per-object getter would mean sixty calls to learn what a configuration is about, so nobody
// would make them and the notes would be written and never read. One call at the start of a
// session is a thing that actually happens.
//
// ⭐ AND IT EXISTS BECAUSE THE REASONS DO NOT SURVIVE OTHERWISE. A configuration records what was
// built and never why: which of two shapes was chosen, what a register is for, which rejected
// idea must not be proposed again. That knowledge lived only in whoever was in the room, and it
// is exactly what is lost between one session and the next.
//
// ⚠ THE FIELD WAS ALREADY HALF THERE. `Help` has existed on every metaobject since long before
// this file — stored, serialised, with a getter and a setter — and NOTHING read or wrote it:
// no editor, no F1, no tool. A field with no door at either end. These verbs are one of the two
// doors; the designer's own is the other.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metadataConfiguration.h"

#include <algorithm> // std::max - the best score there was
#include <set>       // the link check keeps one entry per id
#include <vector>

namespace {

ibMetaData* OpenConfiguration(ibString& refusal)
{
	ibMetaData* metaData = activeMetaData;

	if (metaData == nullptr || !metaData->IsConfigOpen()) {
		refusal = ibMcpText("No configuration is open.");
		return nullptr;
	}

	return metaData;
}

// Walk the whole tree, collecting whatever carries text. Pre-order, so a reader meets an object
// before the attributes under it — the same order the navigator shows and the order the reasons
// were written in.
void Collect(ibValueMetaObject* object, bool wantNotes, bool wantHelp,
	std::vector<ibDataValue>& into)
{
	if (object == nullptr || object->IsDeleted())
		return;

	const bool carries = (wantNotes && !object->GetNoteContent().IsEmpty())
		|| (wantHelp && !object->GetHelpContent().IsEmpty());

	if (carries) {
		std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
		ibMcpSayObject(object, *entry, /*withText*/ true);
		into.push_back(ibDataValue::Child(entry));
	}

	for (unsigned int index = 0; index < object->GetChildCount(); ++index)
		Collect(object->GetChild(index), wantNotes, wantHelp, into);
}

// One object that carries a text, scored against the words — see ibMcpToolNoteRead::FindInside.
struct ibNotedPlace {
	const ibValueMetaObject* m_object;
	size_t                   m_score;
};

void Score(ibValueMetaObject* object, const ibString& query, size_t& asked, size_t& best,
	std::vector<ibNotedPlace>& into)
{
	if (object == nullptr || object->IsDeleted())
		return;

	const ibString help = object->GetHelpContent();
	const ibString notes = object->GetNoteContent();

	// Only what carries a text: a bare name is what metadata_list answers, and every attribute called
	// Stock would stand in front of the one report that says what stock is.
	if (!help.IsEmpty() || !notes.IsEmpty()) {
		const ibString haystack = object->GetName() + ibT("\n") + object->GetSynonym() + ibT("\n")
			+ help + ibT("\n") + notes;
		const size_t score = ibMcpWordsFound(haystack, query, &asked);
		if (score > 0) {
			best = std::max(best, score);
			into.push_back({ object, score });
		}
	}

	for (unsigned int index = 0; index < object->GetChildCount(); ++index)
		Score(object->GetChild(index), query, asked, best, into);
}

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgHelp()
{
	static const ibArg s_a(ibT("help"), ibArg::Kind::Flag,
		ibMcpText("Include the user-facing help text as well as the notes. Off by default: the two "
		  "are written for different readers, and mixing them is how one ends up in the "
		  "other."));
	return s_a;
}

const ibArg& ArgId()
{
	static const ibArg s_a(ibT("id"), ibArg::Kind::Whole,
		ibMcpText("One object's NodeId, and everything under it. Omit for the whole configuration - "
			  "which is the usual way to ask."));
	return s_a;
}

const ibArg& ArgText()
{
	static const ibArg s_a(ibT("text"), ibArg::Kind::Text,
		ibMcpText("The markdown. Empty clears what is there."), /*required*/ true);
	return s_a;
}

const ibArg& ArgTarget()
{
	static const ibArg s_a(ibT("target"), ibArg::Kind::Text,
		ibMcpText("Which text: `notes` - why this exists, for whoever builds the configuration; or "
			  "`help` - what this is and what to put in it, shown to the person USING the "
			  "application when they press F1. Different readers, different words."),
			/*required*/ true, { ibT("notes"), ibT("help") });
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// note_read
//---------------------------------------------------------------------------

class ibMcpToolNoteRead : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("note_read"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("reading what is known about this configuration");
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Everything recorded ABOUT this configuration, in one answer: the markdown NOTES - "
			"how each thing actually works inside, what was decided, what was tried and rejected - "
			"and optionally the user-facing help beside them. READ THIS FIRST when picking work "
			"up: it is where the FINDINGS from earlier work are kept, and a configuration records "
			"what was built and never what was learned building it. Answers only the objects that "
			"carry something.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgHelp() };
		return s_arguments;
	}

	// ⭐⭐ THE CONFIGURATION'S OWN ANSWER, ASKED BESIDE THE VERBS. A question in the words of the trade - "how
	// much is left in stock" - is answered best by what THIS configuration built for it, and the object that
	// answers it says so in the texts it carries: its help, in the words of the person using it, and its notes.
	// So mcp_search asks here too, and an object whose texts meet the words comes back as a place: which
	// object, which of its texts, and the line that met them (Max, 2026-09-11: *"you write it every time you
	// add an object - your answer is simply in the technical documentation"*). The patterns answer how such a
	// thing is built anywhere; this answers where it is built HERE, and the envelope puts it first.
	//
	// Ranked the corpus's way: everything that met every word if anything did, otherwise the best there was,
	// marked "k of your n words".
	void FindInside(const ibString& query, std::vector<ibDataValue>& places) const override
	{
		if (query.IsEmpty())
			return;

		// Through the notes' one door to the configuration, as the notes' own commands go; with nothing open
		// there is simply nothing of this configuration to offer, and the refusal is nobody's to hear.
		ibString refusal;
		ibMetaData* const metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return;

		std::vector<ibNotedPlace> scored;
		size_t asked = 0, best = 0;
		Score(metaData->GetCommonMetaObject(), query, asked, best, scored);

		const bool partial = best > 0 && best < asked;
		size_t given = 0;

		for (const ibNotedPlace& place : scored) {

			if (place.m_score < best)
				continue;
			if (++given > 8)
				break;

			std::shared_ptr<ibDataNode> hit = std::make_shared<ibDataNode>();
			ibMcpSayObject(place.m_object, *hit, /*withText*/ false);

			if (partial)
				hit->SetValue(ibT("matched"), ibString::Format(
					ibMcpText("%i of your %i words"), (int)place.m_score, (int)asked));

			// The line that met the words, and which text it stands in - the help first, because it is
			// written in the words of the people who ask.
			ibString line = ibMcpMatchingLine(place.m_object->GetHelpContent(), query);
			ibString text = ibT("help");
			if (line.IsEmpty()) {
				line = ibMcpMatchingLine(place.m_object->GetNoteContent(), query);
				text = ibT("notes");
			}
			if (!line.IsEmpty()) {
				hit->SetValue(ibT("text"), text);
				hit->SetValue(ibT("line"), line);
			}

			places.push_back(ibDataValue::Child(hit));
		}
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		ibValueMetaObject* from = nullptr;

		if (auto asked = params.FindField(ArgId().Name())) {
			if (asked->Kind() == ibDataKind::Number) {
				from = ibFindMetaObjectById(metaData, (ibMetaID)asked->AsInt());
				if (from == nullptr) {
					refusal = ibString::Format(ibMcpText("Nothing in this configuration has id %s."),
						asked->AsNumber().ToString());
					return false;
				}
			}
		}

		if (from == nullptr)
			from = metaData->GetCommonMetaObject();

		if (from == nullptr) {
			refusal = ibMcpText("This configuration has no root to read from.");
			return false;
		}

		const bool wantHelp = ArgHelp().Flag(params);

		std::vector<ibDataValue> entries;
		Collect(from, /*wantNotes*/ true, wantHelp, entries);

		result.AddField(ibT("recorded"), ibDataValue::Int((std::int64_t)entries.size()));
		result.AddField(ibT("objects"), ibDataValue::Array(entries));

		// ⭐ SAID PLAINLY, because an empty list is also what a broken read looks like — and
		// because "nothing is written down" is itself the most actionable answer this tool has.
		if (entries.empty()) {
			result.SetValue(ibT("note"),
				ibMcpText("Nothing is recorded yet. note_write puts it there - and what is worth writing "
				  "is the reasoning a later reader cannot recover from the objects themselves."));
		}

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolNoteRead);

//---------------------------------------------------------------------------
// note_write
//---------------------------------------------------------------------------

class ibMcpToolNoteWrite : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("note_write"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("writing notes on '%s'"), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Write one of the two texts an object carries, saying WHICH every time. `notes` "
			"records what a later reader could not work out from the object itself - why it "
			"exists, how it works inside, which shape was chosen, what was rejected. `help` is what "
			"the person USING the application reads when they press F1: what this is and what to put "
			"in it, in their words and not in engineering ones. Markdown; empty clears it.\n\n"
			"WRITE THEM AS YOU BUILD, in the words somebody will ASK with: mcp_search reads every "
			"object's help and notes and answers a question with the object that speaks to it. A "
			"stock report whose help says 'how much is left in each warehouse' is what 'how much is "
			"left in stock' lands on - straight to the report, and the data is read from there. An "
			"object nobody described is found by nobody who does not already know its name.");
	}

	// WHAT IS ABOUT TO BE WRITTEN, shown to the person it is being written about. Notes and help
	// are prose meant to be read, so there is no reason to make them go and open a dialog to find
	// out what an assistant put there in their name.
	ibString GetDetail(const ibDataNode& params) const override
	{
		return ibMcpFencedExcerpt(ArgText().Text(params), ibT("markdown"));
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgText(), ArgTarget() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		auto asked = params.FindField(ArgId().Name());
		if (!asked || asked->Kind() != ibDataKind::Number) {
			refusal = ibMcpText("No id given.");
			return false;
		}

		ibValueMetaObject* object = ibFindMetaObjectById(metaData, (ibMetaID)asked->AsInt());
		if (object == nullptr) {
			refusal = ibString::Format(ibMcpText("Nothing in this configuration has id %s."),
				asked->AsNumber().ToString());
			return false;
		}

		// ⚠ THE KIND IS CHECKED. GetValue<wxString> answers empty for anything that is not a
		// String, so an object sent here would CLEAR the notes and report success — the same trap
		// that cost an evening on module_write.
		auto incoming = params.FindField(ArgText().Name());

		if (!incoming || incoming->Kind() != ibDataKind::String) {
			refusal = ibMcpText("'text' must be a string. Nothing was written.");
			return false;
		}

		const ibString text = incoming->AsString();

		const ibString target = ArgTarget().Text(params);

		if (!target.IsSameAs(ibT("notes"), false) && !target.IsSameAs(ibT("help"), false)) {
			refusal = ibMcpText("Say which text: 'notes' for the engineering intent, 'help' for what the "
				"person using the application reads. Nothing was written.");
			return false;
		}

		// ⭐⭐ A LINK IS CHECKED WHEN IT IS WRITTEN, not counted forever afterwards on every read.
		//
		// The root note of this very configuration carried FOURTEEN `oes:` links of which THIRTEEN
		// named something else — `Warehouses` opening UnitsOfMeasure, `GoodsInWarehouses` opening a
		// resource, `Stock` opening a predefined attribute (measured 2026-09-09). Not one of them
		// was broken; every one resolved, to the wrong thing, and read as perfectly normal.
		//
		// 🛑 AND THE IDS HAD NOT MOVED. `SetMetaID` has no callers anywhere in the tree: an id is
		// assigned once at creation and restored from storage on load, so a live object's number is
		// stable. The drift was structural — 0, then +12, then +24, exactly one catalog's span each
		// time — which is not ids sliding but a note written against a DIFFERENT BUILD of the
		// configuration and carried across when it was rebuilt. Nothing connected the two, so the
		// text outlived the tree it described.
		//
		// That is what makes the write the right place to ask: at the read there is nobody left who
		// knows what was meant, while here the author is still holding it.
		std::vector<ibDataValue> links;
		std::set<long long> seen;

		if (ibMcpSayObjectLinks(text, metaData, links, seen) > 0) {

			// Only the OFFENDING links are named. A note may carry a dozen good ones, and a
			// refusal that lists them all makes the caller find the fault itself.
			ibString says;
			for (const ibDataValue& one : links) {

				const auto link = one.AsChild();
				if (!link)
					continue;

				const ibString label = link->GetValue<ibString>(ibT("label"));

				// The id is stored as a number, so it is read as one — GetValue<wxString> on an
				// Int answers empty, and the refusal would name every bad link as `oes:`.
				auto number = link->FindField(ibT("id"));
				const ibString said = ibString::Format(ibT("[%s](oes:%d)"), label,
					number ? (int)number->AsInt() : 0);

				if (link->Has(ibT("broken"))) {
					says << ibT("\n  ") << said
						 << ibT(" - nothing in this configuration carries that id");
				}
				else if (link->Has(ibT("agrees"))) {
					says << ibT("\n  ") << said << ibT(" - that id is ")
						 << link->GetValue<ibString>(ibT("resolves")) << ibT(", a ")
						 << link->GetValue<ibString>(ibT("kind"));
				}
			}

			refusal = ibMcpText("NOTHING WAS WRITTEN. Some `oes:<id>` links in this text do not name what "
				"they say, and a link like that is worse than a missing one: it resolves, so whoever "
				"follows it reads a different object and nothing looks wrong. Correct the ids - "
				"`metadata_tree` depth 1 gives the current ones - and write it again. A link whose "
				"visible text is a phrase rather than a bare name is not checked, so describing an "
				"object instead of naming it is always allowed.") + says;

			return false;
		}

		const bool toHelp = target.IsSameAs(ibT("help"), false);

		if (toHelp)
			object->SetHelpContent(text);
		else
			object->SetNoteContent(text);

		metaData->Modify(true);

		// The object as it now stands, texts included — so the answer IS the read-back rather
		// than a claim about it. "Written" is true of a write that stored nothing; only the
		// object can say what it holds, and this is the one place that asks it.
		ibMcpSayObject(object, result, /*withText*/ true);
		result.SetValue(ibT("wrote"), ibString(toHelp ? ibT("help") : ibT("note")));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolNoteWrite);
