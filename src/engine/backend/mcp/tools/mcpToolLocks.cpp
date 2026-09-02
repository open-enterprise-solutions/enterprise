////////////////////////////////////////////////////////////////////////////
//	Description : who is holding what — the long-held pessimistic locks
////////////////////////////////////////////////////////////////////////////
//
// ⭐ ASKED OF ibLockManager, WHICH ALREADY ANSWERS IT. GetSnapshot() has been
// there since the lock table was written — declared, implemented, and waiting
// for the admin surface its own comment names ("admin UI / /admin/locks,
// Phase B.5"). This tool IS that surface. The lock's own header even lists the
// use case: "Admin force-release / who holds what snapshot".
//
// ⚠ THE FIRST VERSION OF THIS FILE READ sys_lock DIRECTLY, and argued that the
// manager "holds no list" so there was nothing to go through. That was the
// wrong conclusion from a true observation: when a door is missing something,
// the fix goes DOWN into the door — and here it was not even missing. Reaching
// around the manager would have meant a second reader of the same table, free
// to disagree with the first about isolation, about the zombie sweep, and about
// which connection the read happens on (Max, 2026-08-31).
//
// The snapshot is CLUSTER-WIDE by construction: the rows live in the base, so
// several processes on one file see each other's. That is the whole value — the
// holder is usually not the session asking.
//
// ⚠ AND IT ONLY READS. The manager has a release path, and it is deliberately
// not offered here: breaking somebody else's lock is not a thing to do quietly
// from a tool. A lock goes when its owner drops it, when their session ends, or
// through the sweep.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/appData.h"
#include "backend/lock/lockManager.h"

//---------------------------------------------------------------------------
// lock_list
//---------------------------------------------------------------------------
namespace {
using ibArg = ibMcpTool::ibMcpArgument;
const ibArg& ArgObject() { static const ibArg a(wxT("object"), ibArg::Kind::Text, ibMcpText("Only locks on this object - its full name as a lock names it, e.g. 'Catalog.Products'. Omit for everything held.")); return a; }
} // namespace


class ibMcpToolLockList : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("lock_list"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("looking at what is locked");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Every long-held lock on this base right now: what is locked, who holds it, "
			"from which machine and since when. This is the answer to 'why can this person not "
			"write that record' and to 'why will the schema not apply' - a lock is held ACROSS "
			"PROCESSES, so the holder is usually not the session asking, and session_list is "
			"what says whether they are still there. Read-only: a lock goes when its owner "
			"drops it or their session ends.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgObject() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (appData == nullptr) {
			refusal = ibMcpText("The application is not up.");
			return false;
		}

		ibLockManager* locks = appData->GetLockManager();
		if (locks == nullptr) {
			refusal = ibMcpText("There is no lock manager in this process.");
			return false;
		}

		const wxString wanted = ArgObject().Text(params);

		// The snapshot is a COPY — the manager says so at the declaration, and the filtering
		// below is therefore this tool's own business rather than a second query.
		const std::vector<ibLockSnapshotRow> held = locks->GetSnapshot();

		std::vector<ibDataValue> entries;

		for (const ibLockSnapshotRow& row : held) {

			if (!wanted.IsEmpty() && !row.namespaceName.IsSameAs(wanted, false))
				continue;

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();

			// WHAT IS LOCKED, in words a person recognises. keyData is kept in the row precisely
			// so a conflict can be explained rather than shown as a hash.
			entry->SetValue(wxT("object"), row.namespaceName);
			entry->SetValue(wxT("key"), row.keyData);

			// The mode is answered as a WORD. A caller acts on the difference between a shared
			// and an exclusive hold, and a bare number makes them go and look it up.
			entry->SetValue(wxT("mode"),
				wxString(row.lockMode == ibLockMode::Exclusive ? wxT("exclusive") : wxT("shared")));

			// WHO, snapshotted when the lock was taken — so the holder is still nameable after
			// their session row has gone.
			entry->SetValue(wxT("user"), row.userName);
			entry->SetValue(wxT("computer"), row.computer);
			entry->SetValue(wxT("since"), row.acquiredAt);

			// The tie to session_list: the two lists are read together when the question is
			// "who is this, and are they still connected".
			entry->SetValue(wxT("session"), row.sessionGuid);

			entries.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("held"), ibDataValue::Int((s64)entries.size()));
		result.AddField(wxT("locks"), ibDataValue::Array(entries));

		if (entries.empty()) {
			result.SetValue(wxT("note"), wanted.IsEmpty()
				? ibMcpText("Nothing is locked.")
				: ibMcpText("Nothing is locked on that object."));
		}

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolLockList);
