#ifndef __BACKEND_COMMAND_H__
#define __BACKEND_COMMAND_H__

#include "backend/backend_core.h"          // BACKEND_API
#include "backend/commandDescription.h"    // ibCommandHop / ibCommandDescription — a command-hop path
#include "backend/compiler/value.h"        // ibValue (+ ConvertToValue — a value self-describes as command-capable)
#include <wx/string.h>
#include <vector>

// ====================================================================================================
//  The command interface — TWO ends, one path. A command SENDS from a source and is RECEIVED by a
//  projection. Both live here (the backend command header) so the whole contract reads in one place.
// ====================================================================================================

// ---------------------------------------------------------------------------------------------------
//  SENDER — everything that GIVES commands: the form, a table (tablebox), a section, a command
//  metaobject. The exact twin of ibSourceDataObject on the command side. A source path hops through
//  GetValueBySourceHop and each step's VALUE is again a source (self-describing) → the walk is uniform.
//  A command path hops through GetCommandByHop and each step's value is again an ibBackendCommandSender
//  → the walk climbs the SAME way: form → its command / a control's command / up to the common commands
//  (a "melting pot"), a section id that hops WITHIN itself. No front-end ladder of special cases: each
//  object resolves its OWN hop, exactly as a source object does.
// ---------------------------------------------------------------------------------------------------
class BACKEND_API ibBackendCommandSender {
public:
	virtual ~ibBackendCommandSender() {}

	// THE hop gate — resolve ONE command id to the next command-capable VALUE (self-describing: it is again an
	// ibBackendCommandSender, so the walk continues on it). Gateway bool: false = the id is not a command here.
	// Default: this object vends no command (the twin of GetValueBySourceHop's default).
	// NOT const (unlike GetValueBySourceHop): a command source can be a LIVE frontend form, and telling whether a hop
	// is one of its standard actions goes through GetStandardCommands, which owns a mutable back-pointer to the form.
	// The walk still MUTATES NOTHING here — command EXECUTION-const lives on ibBackendCommandItem::Execute (const).
	virtual bool GetCommandByHop(const ibCommandHop& hop, ibValue& out) { return false; }

	// THE shared command walk — the mirror of ibSourceDataObject::ResolvePath. Each step's value IS command-capable,
	// so it self-describes the next id: value -> ConvertToValue<ibBackendCommandSender> -> GetCommandByHop ->
	// next value. Static because the start need not be THIS object (the form gate feeds its own first hop). Gateway
	// bool: false = a hop hit a non-command value (a primitive mid-path) or a missing id; `out` is the leaf only on true.
	static bool ResolveCommandPath(const ibValue& start, const std::vector<ibCommandHop>& path, size_t from, ibValue& out)
	{
		ibValue current = start;
		for (size_t i = from; i < path.size(); ++i) {
			ibBackendCommandSender* node = nullptr;
			current.ConvertToValue<ibBackendCommandSender>(node);
			if (node == nullptr)
				return false;   // a non-command value ends the walk — you cannot hop into it
			ibValue next;
			if (!node->GetCommandByHop(path[i], next))
				return false;   // the id is not a command on this node
			current = next;
		}
		out = current;
		return true;
	}
};

// ---------------------------------------------------------------------------------------------------
//  RECEIVER — everything that RECEIVES a command: a button, a toolbar / command bar (and its items).
//  The command-side twin of ibBackendTypeSourceFactory. This is the BACKEND slice, declared here so the
//  variant (ibVariantDataCommandSource, backend) can CAST its owner to it and validate the binding in
//  MakeString — which runs at RUNTIME too, not only in the designer cell: a bound command that was
//  deleted must read "<not found>" everywhere, so the check cannot live in a frontend-only adapter.
//
//  A stored path is just HOPS; returning them blind can't tell a live binding from a broken one.
//  WalkCommand WALKS the hops — it cannot reach the end when a hop no longer resolves, and THAT failure
//  is the guarantee the path is invalid. The FRONTEND door (ibFrontendCommandReceiver) extends this and
//  implements the walk; the controls inherit the door. Sender and Receiver are DISJOINT object sets —
//  coherence along two parallel interfaces, so a control that gives commands never confuses one for the
//  other.
// ---------------------------------------------------------------------------------------------------
class BACKEND_API ibBackendCommandReceiver {
public:
	// Out-of-line (defined in propertyCommandSource.cpp) so this is the vtable KEY FUNCTION — one exported vtable +
	// typeinfo in backend.dll, so the backend variant's dynamic_cast<ibBackendCommandReceiver*> resolves the
	// frontend-built button / bar item across the DLL boundary. Mirrors ibBackendTypeSourceFactory's out-of-line members.
	virtual ~ibBackendCommandReceiver();

	// Validate the binding by WALKING its hops. Gateway bool: false = a hop no longer resolves (the command is gone,
	// "<not found>"). `outText` (optional) receives the resolved leaf's name when the whole path is valid.
	virtual bool WalkCommand(const ibCommandDescription& desc, wxString* outText = nullptr) const = 0;
};

#endif // !__BACKEND_COMMAND_H__
