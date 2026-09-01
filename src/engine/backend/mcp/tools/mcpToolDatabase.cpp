////////////////////////////////////////////////////////////////////////////
//	Description : save, apply, roll back — the three the buttons take
////////////////////////////////////////////////////////////////////////////
//
// ⭐ THE SAME FUNCTION THE MENU CALLS, not a second one that agrees with it. The three verbs are
// asked OF THE CONFIGURATION and answered by whatever is watching it — the designer's own tree, in
// a process that has one; the Configuration menu redirects into them and so does this file. Nothing
// is re-implemented here — each of these tools is a refusal or two and one call.
//
// ⭐ AND WHETHER ANYTHING CAN DO IT IS THE CONFIGURATION'S ANSWER, not a test made here. A runtime
// host has nothing watching, restructuring is not something it does, and the refusal comes back in
// words — so there is no second "are we in the designer?" in this file to drift from the first.
//
// ⚠ APPLYING IS CONSEQUENTIAL AND SAYS SO. The decision is brought WITH the call rather than asked
// back over the socket, because a transaction is held open while it is being made: an assistant
// that went away to think would hold the database open while it did. `confirm: false` therefore
// means "go all the way and roll it back" — a real pass, not a rehearsal — and what comes back is
// the ledger it declined.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/metadataConfiguration.h"
#include "backend/restructureInfo.h"
#include "backend/metaCollection/metaDiff.h"
#include "backend/metaCollection/metaObject.h"

namespace {

// The configuration these three verbs are asked of, refused in words when there is none.
//
// ⭐ IT USED TO HAND BACK THE TREE. That made the tool one of sixty sites that reached into the
// metadata for a watcher and called a method on it — so the tool could only run where a designer
// existed, and reached only the FIRST of several. The verbs now live on the configuration itself
// (metaData.h), which is what a tool should be talking to; whether anything is watching, and which
// of them can actually do it, is answered in there.
ibMetaDataConfigurationBase* OpenConfiguration(wxString& refusal)
{
	ibMetaDataConfigurationBase* metaData = activeMetaData;

	if (metaData == nullptr || !metaData->IsConfigOpen()) {
		refusal = _("No configuration is open.");
		return nullptr;
	}

	return metaData;
}

using ibArg = ibMcpTool::ibMcpArgument;

// database_diff's arguments — declared once and read through the same objects.
const ibArg& ArgProperties() { static const ibArg a(wxT("properties"), ibArg::Kind::Flag, _("Also list the individual PROPERTIES that differ on a changed object. Off by default: a rename shows as one changed object, and the property rows are the detail behind it.")); return a; }
const ibArg& ArgLimit() { static const ibArg a(wxT("limit"), ibArg::Kind::Whole, _("At most this many entries. A first apply of a fresh configuration differs in everything, and the whole list says less than its first page. Default 200.")); return a; }

// The word a caller acts on. The enum's own spelling is about two configurations being compared
// side by side; here one side IS the database, so left and right have names.
wxString StatusWord(ibMetaDiffStatus status)
{
	switch (status) {
	case ibMetaDiffStatus::OnlyInRight: return wxT("added");     // in the configuration, not in the base
	case ibMetaDiffStatus::OnlyInLeft:  return wxT("removed");   // in the base, gone from the configuration
	case ibMetaDiffStatus::Changed:     return wxT("changed");
	default:                            return wxT("same");
	}
}


// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgConfirm()
{
	static const ibArg s_a(wxT("confirm"), ibArg::Kind::Flag,
		_("true commits it. false runs the whole apply and rolls it back, so the answer "
			  "describes exactly what would happen. REQUIRED - there is no default, because "
			  "neither answer is safe to assume."), /*required*/ true);
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// config_save
//---------------------------------------------------------------------------

class ibMcpToolConfigSave : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("config_save"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return _("saving the configuration");
	}

	wxString GetDescription() const override
	{
		return _("Store the configuration as it stands, so it survives closing the designer and "
			"logging in again. It does NOT become the configuration the application runs - that "
			"is config_apply, and the two are separate on purpose. This is the diskette: cheap, "
			"safe, and the thing to do before stopping work.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {  };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaDataConfigurationBase* config = OpenConfiguration(refusal);
		if (config == nullptr)
			return false;

		// ⭐⭐ THE REASON IS NOT IN THE RETURN VALUE — it is in the LEDGER.
		//
		// SaveConfiguration answers false and fills `refusal` with what IT knows, but the objects
		// that actually refused say so into the restructure ledger (and through the message pane,
		// which is the same text arriving somewhere a tool cannot read). So a real, specific
		// refusal — "the Warehouse attribute has no type", say — reached the person's window while
		// the caller here got a sentence that named nothing.
		//
		// 🛑 AND THAT IS HOW A WRITE GOES MISSING. An assistant that cannot see WHY the save failed
		// reads a vague refusal as a hiccup, carries on, and everything downstream is reasoning
		// about a configuration the database never received — a chart of accounts was lost exactly
		// this way (2026-08-31).
		//
		// ⚠ AND THE LEDGER IS ONLY THE HALF THAT CAN BE READ FROM HERE. The rest is said through
		// the designer's MESSAGE PANE — a chart of accounts with no chart of characteristic types
		// says it there — and no tool exposes that pane, so from this side it does not exist. What
		// is harvested below is therefore everything available, not everything said.
		//
		// Harvested with the same three lines metadata_list already uses for its readiness probe —
		// clear, act, read — so there is one way of asking "what was said about this", not two.
		ibRestructureInfo& ledger = ibMetaDataConfigurationBase::GetRestructureInfo();
		ledger.Clear();

		if (!config->SaveConfiguration(refusal)) {

			wxString said;
			for (const ibRestructureInfo::Entry& entry : ledger) {
				if (entry.type != ibRestructure::error && entry.type != ibRestructure::warning)
					continue;
				if (!said.IsEmpty())
					said += wxT("\n");
				said += entry.descr;
			}

			ledger.Clear();

			// The ledger's account first when there is one: it names the object and the fault,
			// where the returned sentence names the step. Both when both exist.
			if (!said.IsEmpty())
				refusal = refusal.IsEmpty() ? said : said + wxT("\n") + refusal;

			return false;
		}

		ledger.Clear();

		result.AddField(wxT("saved"), ibDataValue::Bool(true));
		result.SetValue(wxT("note"),
			_("Stored. The application still runs the previously applied configuration - "
			  "database_diff says what it does not yet have."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolConfigSave);

//---------------------------------------------------------------------------
// config_apply
//---------------------------------------------------------------------------

class ibMcpToolConfigApply : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("config_apply"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ArgConfirm().Flag(params)
			? _("applying the configuration to the database")
			: _("rehearsing what applying the configuration would do");
	}

	wxString GetDescription() const override
	{
		return _("Make the edited configuration the one the database holds - the designer's "
			"Update database configuration. READ database_diff FIRST: this writes DDL and can "
			"take the base exclusively. With confirm=false it goes all the way and then rolls "
			"back, answering with the ledger of every CREATE, ALTER and DROP it would have made - "
			"which is the only way to see the schema changes in advance, because the engine has "
			"no rehearsal mode by design.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgConfirm() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaDataConfigurationBase* config = OpenConfiguration(refusal);
		if (config == nullptr)
			return false;

		// ⚠ NO DEFAULT. A missing `confirm` is refused rather than read as either answer: assuming
		// true writes to a live database on a forgotten argument, and assuming false makes a
		// caller who meant to apply believe they did.
		const ibDataValue* asked = params.FindField(ArgConfirm().Name());

		if (asked == nullptr || asked->Kind() != ibDataKind::Bool) {
			refusal = _("Say confirm: true to write this to the database, or confirm: false to "
				"see what it would do and roll back. Nothing was done.");
			return false;
		}

		const bool commit = asked->AsBool();

		// WHAT THE LEDGER SAID, captured as the decision is made — afterwards it belongs to the
		// next apply, and a caller that declined would have nothing to show for it.
		std::vector<ibDataValue> ledger;
		bool sawErrors = false;

		// …AND HOW MUCH OF IT WE HAD SEEN BY THEN. The decision is made BEFORE the restructuring
		// runs, so everything the apply itself records — the DDL as it lands, a failure partway —
		// arrives after this snapshot and used to be dropped on the floor: the tool answered with
		// the plan and never with the outcome. See the second pass below.
		size_t capturedUpTo = 0;

		const auto appendEntry = [&ledger](const ibRestructureInfo::Entry& entry) {
			std::shared_ptr<ibDataNode> line = std::make_shared<ibDataNode>();
			line->SetValue(wxT("level"), wxString(
				entry.type == ibRestructure::error ? wxT("error")
				: entry.type == ibRestructure::warning ? wxT("warning") : wxT("info")));
			line->SetValue(wxT("what"), entry.descr);
			ledger.push_back(ibDataValue::Child(line));
		};

		const bool applied = config->ApplyConfiguration(refusal,
			[&appendEntry, &capturedUpTo, &sawErrors, commit](const ibRestructureInfo& info) {

				for (const auto& entry : info)
					appendEntry(entry);

				capturedUpTo = info.Count();
				sawErrors = info.HasErrors();

				// ⭐ AN ERROR DECIDES FOR ITSELF. A ledger carrying errors is rolled back whatever
				// was asked — confirming an apply is confirming the CHANGE, not overriding the
				// engine's own account of it going wrong.
				return commit && !info.HasErrors();
			});

		// ⭐⭐ AND THE SECOND HALF: WHAT THE APPLY ITSELF SAID.
		//
		// The callback above runs at the DECISION, which is before any of it happens. Everything the
		// restructuring records while it runs — each table created or altered, each column added,
		// and any failure partway through — lands in the ledger after that snapshot was taken, and
		// this tool used to return the plan while dropping the outcome. The caller then had a
		// confident-looking answer describing something that had not been done yet.
		//
		// Read from where the snapshot ended, so nothing is reported twice and nothing is lost.
		{
			const ibRestructureInfo& live = ibMetaDataConfigurationBase::GetRestructureInfo();
			for (size_t i = capturedUpTo; i < live.Count(); ++i)
				appendEntry(live.At(i));

			if (live.HasErrors())
				sawErrors = true;
		}

		result.AddField(wxT("changes"), ibDataValue::Int((s64)ledger.size()));
		result.AddField(wxT("ledger"), ibDataValue::Array(ledger));
		result.AddField(wxT("applied"), ibDataValue::Bool(applied));

		if (applied)
			return true;

		// A DECLINE IS NOT A FAILURE, and the ledger above is the whole point of it — so this
		// answers TRUE with the account, rather than refusing and throwing the account away.
		if (!commit || sawErrors) {
			result.SetValue(wxT("note"), sawErrors
				? _("Rolled back: the ledger carries errors, so nothing was written whatever was "
					"confirmed.")
				: _("Rolled back as asked - nothing was written. The ledger above is what "
					"confirm: true would have done."));
			return true;
		}

		return false;   // a real failure — `refusal` already carries the engine's words
	}
};

MCP_TOOL_REGISTER(ibMcpToolConfigApply);

//---------------------------------------------------------------------------
// config_rollback
//---------------------------------------------------------------------------

class ibMcpToolConfigRollback : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("config_rollback"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return _("taking the configuration back from the database");
	}

	wxString GetDescription() const override
	{
		return _("Throw away everything edited since the last apply and take the database's own "
			"copy again. Only meaningful BEFORE applying - afterwards the database's copy is what "
			"was edited. Every open editor is closed by this, and nothing about it can be undone: "
			"database_diff first says exactly what would be lost.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgConfirm() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaDataConfigurationBase* config = OpenConfiguration(refusal);
		if (config == nullptr)
			return false;

		if (!ArgConfirm().Flag(params)) {
			refusal = _("This discards every edit since the last apply and cannot be undone. Pass "
				"confirm: true if that is what you mean; database_diff lists what would go.");
			return false;
		}

		if (!config->RollbackConfiguration(refusal))
			return false;

		result.AddField(wxT("rolled_back"), ibDataValue::Bool(true));
		result.SetValue(wxT("note"),
			_("The configuration is the database's copy again. Anything built and not applied is "
			  "gone."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolConfigRollback);

//---------------------------------------------------------------------------
// database_diff — moved here from mcpToolDiff.cpp on 2026-09-01. It belongs beside
// save / apply / rollback: the four are one subject — what the DATABASE has versus
// what is being built — and the diff is the sentence a person reads before applying.
//
//
// ⭐ THE ONE THING A BUILDER COULD NOT SEE. Everything else here answers about
// the configuration in memory: what objects exist, what a form holds, what a
// query offers. None of it says what would HAPPEN to the database — and since
// applying is deliberately the developer's button and not a tool's, the caller
// was building blind and asking a person to approve something neither of them
// could read.
//
// ⭐ NOTHING IS COMPUTED HERE. Two things already existed and had never been
// pointed at each other:
//
//   • ibMetaDataConfigurationStorage keeps m_configMetadata — the configuration
//     AS THE DATABASE HAS IT. It is what IsConfigSave() compares against, and
//     it is reachable through GetConfiguration().
//   • ibMetaDiffWalker::Walk pairs two metadata trees by guid and returns a
//     flat, pre-ordered list of what differs. It was written for comparing two
//     configurations; the database's own copy IS a configuration.
//
// So this tool is one call and a rendering. That it did not exist is not a gap
// in the engine — it is a question nobody had asked out loud.
//
// ⭐ AND IT ANSWERS IN OBJECTS, NOT IN DDL. What a person approves is "the
// document Goods receipt gained an attribute", not an ALTER TABLE: the diff is
// the source of truth about the schema (docs/schema-authority.md), and the DDL
// is derived from it further down. Naming the derived form here would invite a
// caller to reason about tables, which is exactly the layer this platform keeps
// nobody's business.
//
//---------------------------------------------------------------------------
// database_diff
//---------------------------------------------------------------------------

class ibMcpToolDatabaseDiff : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("database_diff"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return _("reading what the database does not yet have");
	}

	wxString GetDescription() const override
	{
		return _("What would change in the database if the configuration were applied - object by "
			"object, in the words the metadata tree uses, plus whether the change touches the "
			"database STRUCTURE at all. Ask it after building or editing anything, and ask it "
			"BEFORE config_apply: a configuration is only a proposal until it is applied, and "
			"this list is the only description of what applying would do. Answers nothing "
			"changed when the base is already up to date.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgProperties(), ArgLimit() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		// The BASE type, which is what the global actually is — a configuration open from a file
		// is one too, and narrowing here would have refused it for the wrong reason.
		ibMetaDataConfigurationBase* metaData = activeMetaData;

		if (metaData == nullptr || !metaData->IsConfigOpen()) {
			refusal = _("No configuration is open.");
			return false;
		}

		// ⭐ THE BASELINE IS A CONFIGURATION OF ITS OWN — the one the database holds. Only a
		// STORAGE configuration has one: an external report or a data processor opened from a
		// file answers to no database, and saying so is better than comparing it against itself
		// and reporting "nothing changed".
		// ⭐ ASKED, NOT RECOGNISED. GetConfiguration is virtual on ibMetaDataConfigurationBase and
		// answers null for exactly the containers that have no baseline — so the cast to the
		// storage class was reaching past a question the base already answers.
		ibMetaDataConfigurationBase* baseline = metaData->GetConfiguration();

		// ⚠ ONE NULL, TWO MEANINGS, AND THEY USED TO BE TWO CHECKS: a container that HAS no baseline
		// (an external report answers to no database) and a baseline not loaded. The cast made the
		// first, the null the second. With the cast gone they collapse into one answer, and the
		// sentence says the thing a caller can act on.
		if (baseline == nullptr) {
			refusal = _("This configuration is not held in a database, so there is nothing to "
				"compare it against.");
			return false;
		}

		// LEFT IS THE DATABASE, RIGHT IS WHAT IS BEING BUILT. Stated once, here, because every
		// word below reads off it: what exists only on the right was ADDED.
		const std::vector<ibMetaDiffRecord> records = ibMetaDiffWalker::Walk(
			baseline->GetCommonMetaObject(), metaData->GetCommonMetaObject());

		const bool withProperties = ArgProperties().Flag(params);

		s32 limit = (s32)ArgLimit().Whole(params);
		if (limit <= 0)
			limit = 200;

		std::vector<ibDataValue> entries;
		int differing = 0;

		for (const ibMetaDiffRecord& record : records) {

			// Group rows are the tree's own scaffolding (Catalogs, Documents, …) and say nothing
			// a caller can act on; an unchanged object says nothing either. Both are skipped so
			// what remains is the answer rather than the walk.
			if (record.IsGroup() || record.m_status == ibMetaDiffStatus::Same)
				continue;

			if (record.IsProperty() && !withProperties)
				continue;

			differing++;

			if ((int)entries.size() >= limit)
				continue;   // still counted — a truncated list must not lie about the total

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();

			entry->SetValue(wxT("status"), StatusWord(record.m_status));

			if (record.IsProperty()) {
				entry->SetValue(wxT("property"), record.m_propertyName);
				entry->SetValue(wxT("inDatabase"), record.m_leftValue);
				entry->SetValue(wxT("inConfiguration"), record.m_rightValue);
			}
			else if (const ibValueMetaObject* object = record.GetAnyObject()) {
				ibMcpSayObject(object, *entry);
			}

			entry->AddField(wxT("depth"), ibDataValue::Int((s64)record.m_depth));

			entries.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("differences"), ibDataValue::Int((s64)differing));

		// ⭐ WOULD IT TOUCH THE DATABASE AT ALL — the question that decides everything else, and
		// the platform already answers it: IsDynamicUpdateAvailable is what the apply flow asks
		// before it starts, resting on SameStructure, which "holds no connection and calls
		// nothing" (schemaSnapshot.h). A change living in modules, forms and properties moves no
		// table under anybody.
		//
		// ⚠ AND NO FURTHER. A table-by-table forecast is deliberately NOT available: it would
		// need a rehearsal mode inside the differ, and the header says plainly why there is none
		// — "one missed branch there writes to a live database during what the caller believed
		// was a question". So this answers WHICH OBJECTS differ and WHETHER the schema is
		// involved; what DDL falls out is the apply's own truth, shown by the apply's own dialog.
		// Virtual on ibMetaDataConfigurationBase too — asked of the container it is, not of a class
		// it was first converted into.
		result.AddField(wxT("structural"),
			ibDataValue::Bool(differing != 0 && !metaData->IsDynamicUpdateAvailable()));

		result.AddField(wxT("entries"), ibDataValue::Array(entries));

		if ((int)entries.size() < differing) {
			result.AddField(wxT("shown"), ibDataValue::Int((s64)entries.size()));
			result.SetValue(wxT("note"),
				_("More than the limit. Raise `limit` to see the rest."));
		}

		// ⭐ THE ANSWER A CALLER WANTS MOST, said plainly rather than left to be inferred from an
		// empty list — which is also what an error looks like.
		if (differing == 0) {
			result.SetValue(wxT("note"),
				_("The database already holds this configuration - applying it would change "
				  "nothing."));
		}
		else {
			// Where the verb is, said here because a caller reading a list of pending changes
			// will look for it next.
			result.SetValue(wxT("apply"),
				_("config_apply writes these to the database - the same road the designer's "
				  "Update database configuration button takes. Read this list first: it is the "
				  "only description of what that would do."));
		}

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolDatabaseDiff);
