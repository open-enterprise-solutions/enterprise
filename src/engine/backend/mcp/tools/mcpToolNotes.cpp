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

namespace {

ibMetaData* OpenConfiguration(wxString& refusal)
{
	ibMetaData* metaData = activeMetaData;

	if (metaData == nullptr || !metaData->IsConfigOpen()) {
		refusal = _("No configuration is open.");
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

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgHelp()
{
	static const ibArg s_a(wxT("help"), ibArg::Kind::Flag,
		_("Include the user-facing help text as well as the notes. Off by default: the two "
		  "are written for different readers, and mixing them is how one ends up in the "
		  "other."));
	return s_a;
}

const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		_("One object's NodeId, and everything under it. Omit for the whole configuration - "
			  "which is the usual way to ask."));
	return s_a;
}

const ibArg& ArgText()
{
	static const ibArg s_a(wxT("text"), ibArg::Kind::Text,
		_("The markdown. Empty clears what is there."), /*required*/ true);
	return s_a;
}

const ibArg& ArgTarget()
{
	static const ibArg s_a(wxT("target"), ibArg::Kind::Text,
		_("Which text: `notes` - why this exists, for whoever builds the configuration; or "
			  "`help` - what this is and what to put in it, shown to the person USING the "
			  "application when they press F1. Different readers, different words."),
			/*required*/ true, { wxT("notes"), wxT("help") });
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// note_read
//---------------------------------------------------------------------------

class ibMcpToolNoteRead : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("note_read"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return _("reading what is known about this configuration");
	}

	wxString GetDescription() const override
	{
		return _("Everything recorded ABOUT this configuration, in one answer: the markdown NOTES - "
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

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		ibValueMetaObject* from = nullptr;

		if (const ibDataValue* asked = params.FindField(ArgId().Name())) {
			if (asked->Kind() == ibDataKind::Number) {
				from = ibFindMetaObjectById(metaData, (ibMetaID)asked->AsInt());
				if (from == nullptr) {
					refusal = wxString::Format(_("Nothing in this configuration has id %s."),
						asked->AsNumber().ToString());
					return false;
				}
			}
		}

		if (from == nullptr)
			from = metaData->GetCommonMetaObject();

		if (from == nullptr) {
			refusal = _("This configuration has no root to read from.");
			return false;
		}

		const bool wantHelp = ArgHelp().Flag(params);

		std::vector<ibDataValue> entries;
		Collect(from, /*wantNotes*/ true, wantHelp, entries);

		result.AddField(wxT("recorded"), ibDataValue::Int((s64)entries.size()));
		result.AddField(wxT("objects"), ibDataValue::Array(entries));

		// ⭐ SAID PLAINLY, because an empty list is also what a broken read looks like — and
		// because "nothing is written down" is itself the most actionable answer this tool has.
		if (entries.empty()) {
			result.SetValue(wxT("note"),
				_("Nothing is recorded yet. note_write puts it there - and what is worth writing "
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

	wxString GetName() const override { return wxT("note_write"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(_("writing notes on '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return _("Write one of the two texts an object carries, saying WHICH every time. `notes` "
			"records what a later reader could not work out from the object itself - why it "
			"exists, which shape was chosen, what was rejected. `help` is what the person USING "
			"the application reads when they press F1: what this is and what to put in it, in "
			"their words and not in engineering ones. Markdown; empty clears it.");
	}

	// WHAT IS ABOUT TO BE WRITTEN, shown to the person it is being written about. Notes and help
	// are prose meant to be read, so there is no reason to make them go and open a dialog to find
	// out what an assistant put there in their name.
	wxString GetDetail(const ibDataNode& params) const override
	{
		return ibMcpFencedExcerpt(ArgText().Text(params), wxT("markdown"));
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgText(), ArgTarget() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		const ibDataValue* asked = params.FindField(ArgId().Name());
		if (asked == nullptr || asked->Kind() != ibDataKind::Number) {
			refusal = _("No id given.");
			return false;
		}

		ibValueMetaObject* object = ibFindMetaObjectById(metaData, (ibMetaID)asked->AsInt());
		if (object == nullptr) {
			refusal = wxString::Format(_("Nothing in this configuration has id %s."),
				asked->AsNumber().ToString());
			return false;
		}

		// ⚠ THE KIND IS CHECKED. GetValue<wxString> answers empty for anything that is not a
		// String, so an object sent here would CLEAR the notes and report success — the same trap
		// that cost an evening on module_write.
		const ibDataValue* incoming = params.FindField(ArgText().Name());

		if (incoming == nullptr || incoming->Kind() != ibDataKind::String) {
			refusal = _("'text' must be a string. Nothing was written.");
			return false;
		}

		const wxString text = incoming->AsString();

		const wxString target = ArgTarget().Text(params);

		if (!target.IsSameAs(wxT("notes"), false) && !target.IsSameAs(wxT("help"), false)) {
			refusal = _("Say which text: 'notes' for the engineering intent, 'help' for what the "
				"person using the application reads. Nothing was written.");
			return false;
		}

		const bool toHelp = target.IsSameAs(wxT("help"), false);

		if (toHelp)
			object->SetHelpContent(text);
		else
			object->SetNoteContent(text);

		metaData->Modify(true);

		// The object as it now stands, texts included — so the answer IS the read-back rather
		// than a claim about it. "Written" is true of a write that stored nothing; only the
		// object can say what it holds, and this is the one place that asks it.
		ibMcpSayObject(object, result, /*withText*/ true);
		result.SetValue(wxT("wrote"), wxString(toHelp ? wxT("help") : wxT("note")));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolNoteWrite);
