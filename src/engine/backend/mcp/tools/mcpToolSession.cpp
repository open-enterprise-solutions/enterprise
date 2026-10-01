////////////////////////////////////////////////////////////////////////////
//	Description : who uses this installation, and who is using it now
////////////////////////////////////////////////////////////////////////////
//
// TWO DIFFERENT QUESTIONS THAT SOUND ALIKE.
//
//   WHO EXISTS is a table: the accounts of this installation, each with the
//   roles it was given and the language it reads in. It answers "why can this
//   person not open that document" and "which language is this configuration
//   actually used in" - and the second one decides how synonyms are written.
//
//   WHO IS HERE is a moment: the sessions connected right now, from which
//   machine, under which application. It answers "why is the schema locked",
//   "who else is in the designer", and after a failure, "was anybody else even
//   running".
//
// They are separate tools because the answers come from different places and
// age differently - one changes when an administrator changes it, the other
// changes by itself.
//
// PASSWORDS ARE NOT HERE and are not a field anybody may ask for. The record
// holds one; this door does not open it, in either shape.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/appData.h"
#include "backend/session/session.h"          // Current() — the one session we must not kick
#include "backend/session/sessionRegistry.h"
#include "backend/session/sessionSnapshot.h"
#include "backend/userInfo.h"

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgFull()
{
	static const ibArg s_a(wxT("full"), ibArg::Kind::Flag,
		ibMcpText("Include roles and language for EVERY account. Costs one read per account, so it "
		  "is off by default - but it is one call instead of one per person."));
	return s_a;
}


const ibArg& ArgName()
{
	static const ibArg s_a(wxT("name"), ibArg::Kind::Text,
		ibMcpText("One account, in full. Omit for the list."));
	return s_a;
}

const ibArg& ArgSession()
{
	static const ibArg s_a(wxT("session"), ibArg::Kind::Text,
		ibMcpText("Which one - the `session` handle from session_list or lock_list."), /*required*/ true);
	return s_a;
}


} // namespace

//---------------------------------------------------------------------------
// user_list
//---------------------------------------------------------------------------
class ibMcpToolUserList : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("user_list"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString name = ArgName().Text(params);
		return name.IsEmpty()
			? wxString(ibMcpText("looking at the accounts"))
			: wxString::Format(ibMcpText("looking at the account '%s'"), name);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("The accounts of this installation - who exists, what roles they hold and which "
			"language they read in, which together decide what each may see and how they are "
			"spoken to. A name gives one account; `full` gives every account with its roles and "
			"language in a single answer, instead of asking again once per person.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgName(), ArgFull() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString name = ArgName().Text(params);

		if (name.IsEmpty()) {

			const bool full = ArgFull().Flag(params);

			std::vector<ibDataValue> users;
			for (const ibUserInfo::Brief& brief : ibUserInfo::ListAll()) {
				std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
				entry->SetValue(wxT("name"), brief.m_strUserName);
				if (!brief.m_strUserFullName.IsEmpty()
					&& brief.m_strUserFullName != brief.m_strUserName)
					entry->SetValue(wxT("fullName"), brief.m_strUserFullName);

				// THE SAME FACTS THE SINGLE-ACCOUNT BRANCH GIVES, read the same way. Written as
				// one helper below rather than twice, because two renderings of one account are
				// two chances to report it differently.
				if (full)
					DescribeAccount(ibUserInfo::Read(brief.m_strUserName), *entry);

				users.push_back(ibDataValue::Child(entry));
			}

			result.AddField(wxT("count"), ibDataValue::Int((s64)users.size()));
			result.AddField(wxT("users"), ibDataValue::Array(users));

			// AN EMPTY TABLE IS A MODE, NOT AN ABSENCE. With no accounts at all the
			// installation is open to anyone, and reading "no users" as "the list
			// failed" would be exactly backwards.
			if (users.empty())
				result.SetValue(wxT("note"),
					ibMcpText("No accounts at all - this installation opens without asking who you are."));

			return true;
		}

		const ibUserInfo info = ibUserInfo::Read(name);
		if (!info.IsOk()) {
			refusal = wxString::Format(ibMcpText("No account named '%s'."), name);
			return false;
		}

		result.SetValue(wxT("name"), info.m_strUserName);
		if (!info.m_strUserFullName.IsEmpty())
			result.SetValue(wxT("fullName"), info.m_strUserFullName);

		DescribeAccount(info, result);

		return true;
	}

private:

	// ⭐ ONE ACCOUNT, DESCRIBED ONCE. The list and the single-account answer say the same things
	// about a person, so they say them from the same place — the alternative is two renderings
	// that agree today.
	static void DescribeAccount(const ibUserInfo& info, ibDataNode& into)
	{
		if (!info.IsOk())
			return;

		std::vector<ibDataValue> roles;

		for (const ibUserInfo::ibUserRole& role : info.m_roleArray) {

			// ⭐ THE MODE TRAVELS WITH THE ROLE. A RESTRICTING role grants nothing of its own and
			// only subtracts, so a name on its own reads as "may do this" when it may mean the
			// exact opposite — see role_rights, which reports the same distinction.
			roles.push_back(ibDataValue::String(
				role.m_mode == ibRoleCompositionMode_Intersection
					? role.m_strRoleName + ibMcpText(" (restricting)")
					: role.m_strRoleName));
		}

		into.AddField(wxT("roles"), ibDataValue::Array(roles));

		// NO ROLES IS THE MOST PERMISSIVE STATE, not the least, and it is worth a
		// sentence because it reads the other way round at first glance.
		if (roles.empty())
			into.SetValue(wxT("rolesNote"),
				ibMcpText("No roles are assigned - nothing narrows what this account may do."));

		if (info.IsSetLanguage()) {
			into.SetValue(wxT("language"), info.m_strLanguageName);
			into.SetValue(wxT("languageCode"), info.m_strLanguageCode);
		}
	}
};

MCP_TOOL_REGISTER(ibMcpToolUserList);

//---------------------------------------------------------------------------
// session_list
//---------------------------------------------------------------------------
class ibMcpToolSessionList : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("session_list"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("looking at who is connected");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Who is connected right now: the user, the machine, the application they are "
			"in, and when they started. Ask before anything that needs the installation to "
			"itself - applying a schema, taking the designer exclusively - and after a "
			"failure, to know whether anyone else was running at all.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {  };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (appData == nullptr) {
			refusal = ibMcpText("The application is not up.");
			return false;
		}

		ibSessionRegistry* registry = appData->GetSessionRegistry();
		if (registry == nullptr) {
			refusal = ibMcpText("There is no session registry in this process.");
			return false;
		}

		const ibSessionSnapshot snapshot = registry->GetClusterSnapshot();

		std::vector<ibDataValue> sessions;
		for (unsigned int index = 0; index < snapshot.GetSessionCount(); ++index) {

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("user"), snapshot.GetUserName(index));
			entry->SetValue(wxT("computer"), snapshot.GetComputerName(index));
			entry->SetValue(wxT("application"), snapshot.GetApplication(index));

			// Server or client — the same application name means different things
			// on the two sides, and the label is already worked out for the
			// designer's own Active Users list.
			entry->SetValue(wxT("side"), snapshot.GetSessionKindDescr(index));
			entry->SetValue(wxT("started"), snapshot.GetStartedDate(index));

			// ⭐ AND THE HANDLE THAT ADDRESSES IT. Without this the list could be read and
			// nothing in it could be acted on — session_kick needs a name for one row, and
			// lock_list reports the same value, so "who is holding this" and "are they still
			// here" join up on one field rather than on a guess about the user's name.
			entry->SetValue(wxT("session"), snapshot.GetSession(index));

			sessions.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("count"), ibDataValue::Int((s64)sessions.size()));
		result.AddField(wxT("sessions"), ibDataValue::Array(sessions));

		// The snapshot is refreshed on a timer rather than read live, so a session
		// that started a moment ago may not be in it yet. Saying so is cheaper than
		// letting an empty list be read as an empty installation.
		if (sessions.empty())
			result.SetValue(wxT("note"),
				ibMcpText("Nobody listed. The snapshot refreshes every few seconds, so a session "
				  "started just now may not have reached it."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSessionList);

//---------------------------------------------------------------------------
// session_kick
//---------------------------------------------------------------------------
//
// ⭐ THE REGISTRY ALREADY DOES IT. ibSessionRegistry::Kick writes into
// sys_session.signal, and the OWNING PROCESS picks it up on its next check
// (~3 seconds) and tears that session down. That is why it works across
// machines and why it is not immediate — and both facts belong in the answer,
// because a caller that reads "kicked" and immediately re-reads the session
// list will still see the row and conclude that nothing happened.
//
// ⚠ AND IT IS A REQUEST, NOT A KILL. The signal is honoured by the owner; a
// process that is wedged hard enough not to poll will not notice it. Saying
// "asked" rather than "done" is the difference between a report and a promise.
//
class ibMcpToolSessionKick : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("session_kick"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("disconnecting a session");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Ask a session to disconnect - the handle comes from session_list. Used "
			"before something that needs the installation to itself, and to free a lock whose "
			"holder has walked away (lock_list names the session). It is a SIGNAL: the process "
			"that owns the session acts on it within a few seconds, so the row does not vanish "
			"the instant this returns. Refuses to disconnect the session this server is running "
			"in.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgSession() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (appData == nullptr) {
			refusal = ibMcpText("The application is not up.");
			return false;
		}

		ibSessionRegistry* registry = appData->GetSessionRegistry();
		if (registry == nullptr) {
			refusal = ibMcpText("There is no session registry in this process.");
			return false;
		}

		const wxString wanted = ArgSession().Text(params);
		if (wanted.IsEmpty()) {
			refusal = ibMcpText("No session named. session_list lists them with their handles.");
			return false;
		}

		// ⚠ NOT THE ONE WE ARE STANDING IN. This server runs inside the developer's own session;
		// kicking it would tear down the thing answering the call, and the caller would see a
		// dropped connection rather than a result. A refusal that says which session it was is
		// more useful than a silent guard.
		if (ibSession* self = ibSession::Current()) {
			if (self->Identity().m_guid.str().IsSameAs(wanted, false)) {
				refusal = ibMcpText("That is the session this assistant is running in - disconnecting it "
					"would end this conversation. Kick another one, or close the designer.");
				return false;
			}
		}

		// WHO IT IS, read BEFORE the signal — afterwards the row may be gone, and a report that
		// can only say a guid is a report nobody can check.
		wxString user, computer;
		bool found = false;

		const ibSessionSnapshot snapshot = registry->GetClusterSnapshot();
		for (unsigned int index = 0; index < snapshot.GetSessionCount(); ++index) {
			if (snapshot.GetSession(index).IsSameAs(wanted, false)) {
				user = snapshot.GetUserName(index);
				computer = snapshot.GetComputerName(index);
				found = true;
				break;
			}
		}

		// A handle nobody answers to is refused rather than signalled into nowhere: the UPDATE
		// would touch no row and report success, which reads as "done".
		if (!found) {
			refusal = ibMcpText("No session with that handle is connected. The list refreshes every few "
				"seconds, so it may have left already.");
			return false;
		}

		if (!registry->Kick(wanted)) {
			refusal = ibMcpText("The disconnect could not be written to the session table.");
			return false;
		}

		result.SetValue(wxT("session"), wanted);
		result.SetValue(wxT("user"), user);
		result.SetValue(wxT("computer"), computer);

		// ⭐ "ASKED", NOT "DONE" — see the note above the class. The word is the whole report.
		result.SetValue(wxT("asked"), wxString(wxT("disconnect")));
		result.SetValue(wxT("note"),
			ibMcpText("The owning process acts on this within a few seconds. Read session_list again "
			  "to see it gone."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSessionKick);

//---------------------------------------------------------------------------
// session_reload
//---------------------------------------------------------------------------
//
// ⭐ THE SIBLING OF KICK, AND NOT THE SAME VERB. The registry declares them
// together and means different things by them: Kick is a SOLE-ROW kill, Reload
// is a PROCESS-WIDE eviction — every web-client session that process owns is
// torn down so the clients log in again. The guid argument names WHICH PROCESS
// to poke, not which session dies, and any row belonging to it will do.
//
// Two doors rather than one verb with a flag, because a caller choosing between
// them is choosing between "this person" and "everyone on that server", and a
// flag would let that be a typo.
//
class ibMcpToolSessionReload : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("session_reload"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("asking a server to re-login its clients");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Ask the process owning a session to evict ALL its web clients, so they log in "
			"again and pick up the current configuration. Use after applying a change that web "
			"clients must see; use session_kick when the target is one person. The handle names "
			"the PROCESS through any of its sessions - it is not the session that ends. Like a "
			"kick this is a signal, acted on within a few seconds.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgSession() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (appData == nullptr) {
			refusal = ibMcpText("The application is not up.");
			return false;
		}

		ibSessionRegistry* registry = appData->GetSessionRegistry();
		if (registry == nullptr) {
			refusal = ibMcpText("There is no session registry in this process.");
			return false;
		}

		const wxString wanted = ArgSession().Text(params);
		if (wanted.IsEmpty()) {
			refusal = ibMcpText("No session named. session_list lists them with their handles.");
			return false;
		}

		if (!registry->Reload(wanted)) {
			refusal = ibMcpText("The reload could not be written to the session table.");
			return false;
		}

		result.SetValue(wxT("session"), wanted);
		result.SetValue(wxT("asked"), wxString(wxT("reload")));
		result.SetValue(wxT("note"),
			ibMcpText("Every web client of that process will be asked to log in again, within a few "
			  "seconds."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSessionReload);
