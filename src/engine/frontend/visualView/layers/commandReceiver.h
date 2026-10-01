#ifndef __COMMAND_RECEIVER_H__
#define __COMMAND_RECEIVER_H__

#include "frontend/frontend.h"            // FRONTEND_API
#include "backend/backend_command.h"      // ibBackendCommandReceiver — the door EXTENDS the backend receiver (one chain)
#include "backend/commandDescription.h"   // ibCommandDescription / ibCommandHop
#include <wx/string.h>                     // wxString (ResolveValueByPath out-caption)
#include <wx/bitmap.h>                     // wxBitmap (ResolveValueByPath out-icon)
#include <vector>                          // std::vector (ResolveSubCommands out-list)

// The FRONTEND command receiver — the full DOOR. It EXTENDS the backend receiver (ibBackendCommandReceiver), so the
// inheritance reads as one chain: ibBackendCommandReceiver <- ibFrontendCommandReceiver <- the controls (a button /
// the command bar / a bar item). A projection INHERITS this so it IS-A door: it HOPS a command id path from the form
// gate and, at the leaf, RUNS or READS a command; it supplies its own gate form (GetCommandGateForm), so there is no
// temporary door and no form passed twice. It stays on the FRONTEND (it renders wxBitmap); the backend base is the
// slice the command-source variant casts to, and this door IMPLEMENTS its WalkCommand (a walk that reads the leaf).
class FRONTEND_API ibFrontendCommandReceiver : public ibBackendCommandReceiver {
public:

	virtual ~ibFrontendCommandReceiver() {}

	// ibBackendCommandReceiver — validate a binding for the variant. WALK the hops on THIS door (ResolveValueByPath):
	// a gone hop, or a leaf with no name, fails -> "<not found>". Defined once here; every control inherits it.
	virtual bool WalkCommand(const ibCommandDescription& desc, wxString* outText = nullptr) const override;

	// THE one command resolve — EVERY projection (button / bar / the inspector cell) calls this, so "does it exist +
	// what's its look" is decided in ONE place, never guessed per-surface. Walk the path (ResolveValueByPath); where
	// the walk is fragile (a tablebox command resolves the hop but vends an EMPTY caption / no icon in the designer)
	// fall back to the RELIABLE gather — the SAME set the picker offered. Returns whether the command EXISTS (walk
	// resolved OR gather has it); fills the readable caption (button/bar text), icon, the modifies-data flag, and the
	// full PATH name (the inspector cell text). WalkCommand is a thin wrapper over this.
	bool ResolveCommand(const ibCommandDescription& desc, wxString& outCaption, wxBitmap& outIcon,
		bool* outModifies = nullptr, wxString* outPath = nullptr, bool* outPictureAndText = nullptr) const;

	// Walk the WHOLE command id path from the form gate and RUN the leaf command (a source object walks a path to
	// FETCH a value; this walks to RUN a command). Chains ExecuteValueByCommandHop hop by hop.
	bool ExecuteValueByPath(const ibCommandDescription& desc) const;

	// Same walk, but at the leaf it READS the command's own default look (caption + icon) and its modifies-data
	// flag instead of running it — the command OWNS its look AND its behaviour; a projection shows this where it
	// sets nothing of its own, and OBEYS the flag. outModifiesData is optional. FALSE = a hop no longer resolves
	// (the bound command was deleted / re-homed) — this failure is what the command-source check reads as "<not found>".
	bool ResolveValueByPath(const ibCommandDescription& desc, wxString& outCaption, wxBitmap& outIcon,
		bool* outModifiesData = nullptr, bool* outPictureAndText = nullptr) const;

	// HUB — a leaf command that is a GROUP (holds sub-commands) projects as a dropdown, not a run. Resolve the
	// leaf of `desc`; if it is a group, fill `out` with each sub-command as a DIRECT 1-hop path (the sub-command
	// is itself terminal) + caption + icon — the menu a projection pops on click. Returns true + non-empty `out`
	// for a group; false for a leaf (the projection runs `desc` directly).
	struct ibCommandSubItem { ibCommandDescription desc; wxString caption; wxBitmap icon; };
	bool ResolveSubCommands(const ibCommandDescription& desc, std::vector<ibCommandSubItem>& out) const;

	// The form gate the walk starts from (also the ExecuteParameters a command handler receives) — supplied by the
	// projection that IS-A door: a button -> GetOwnerForm(), the command bar -> its owner frame's form. Public so a
	// caller holding a door can reach its form WITHOUT knowing the concrete projection (the command-source picker).
	virtual class ibValueForm* GetCommandGateForm() const = 0;
};

#endif
