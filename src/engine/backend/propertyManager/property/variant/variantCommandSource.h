#ifndef __COMMAND_SOURCE_DATA_VARIANT_H__
#define __COMMAND_SOURCE_DATA_VARIANT_H__

#include "backend/propertyManager/property/variant/variantType.h"
#include "backend/commandDescription.h"
#include "backend/backend_command.h"   // ibBackendCommandReceiver — the factory the variant WALKS to validate the binding

// The variant behind a command-source property — the command-door twin of ibVariantDataSource. Where the
// source variant holds an ibSourceDescription (the address of a VALUE inside a data source), this holds an
// ibCommandDescription (the address of a COMMAND: a hop path own / main / table / general). It is the ONLY
// stored state — serialised by the property and fed to the button, exactly like the source variant's path.
class BACKEND_API ibVariantDataCommandSource : public wxVariantData {

	// The string the cell shows — SERVER-SIDE and live, exactly like ibVariantDataSource::MakeString asks its source
	// factory. The owner (a button / a bar item) is cast to ibBackendCommandReceiver; WalkCommand WALKS the bound hops
	// and returns valid / invalid. Can't walk to the end (a hop gone) -> "<not found>", the guarantee the path broke;
	// this runs at RUNTIME too, so a deleted command reads "<not found>" everywhere, not just in the designer cell.
	// Only with no owner wired (a detached temp) does it fall back to the picker-set display.
	wxString MakeString() const {
		if (!m_commandDesc.IsOk())
			return wxT("<not selected>");                        // nothing bound
		wxString text;
		if (m_owner != nullptr)
			return m_owner->WalkCommand(m_commandDesc, &text) ? text : wxT("<not found>");
		return m_display.IsEmpty() ? wxT("<not selected>") : m_display;   // no owner to walk -> cached picker name
	}

public:

	bool IsEmptyCommand() const { return !m_commandDesc.IsOk(); }

	// The binding address — a command-hop path (own object / main attribute / table / general command). The
	// variant's only stored state; the property serialises THIS (mirrors ibVariantDataSource::GetSourceDesc).
	ibCommandDescription& GetCommandDesc() { return m_commandDesc; }
	const ibCommandDescription& GetCommandDesc() const { return m_commandDesc; }
	void SetCommandDesc(const ibCommandDescription& desc) { m_commandDesc = desc; }

	// The display name (set by the picker when a command is chosen) — kept so the inspector cell reads it back.
	const wxString& GetDisplay() const { return m_display; }
	void SetDisplay(const wxString& display) { m_display = display; }

	ibVariantDataCommandSource() : wxVariantData() {}
	ibVariantDataCommandSource(const ibBackendCommandReceiver* owner, const ibCommandDescription& desc, const wxString& display = wxEmptyString)
		: wxVariantData(), m_owner(owner), m_commandDesc(desc), m_display(display) {}
	ibVariantDataCommandSource(const ibVariantDataCommandSource& src)
		: wxVariantData(), m_owner(src.m_owner), m_commandDesc(src.m_commandDesc), m_display(src.m_display) {}

	virtual ibVariantDataCommandSource* Clone() const { return new ibVariantDataCommandSource(*this); }

	virtual bool Eq(wxVariantData& data) const {
		ibVariantDataCommandSource* cmdData = dynamic_cast<ibVariantDataCommandSource*>(&data);
		return cmdData != nullptr && m_commandDesc == cmdData->m_commandDesc;   // full identity (path AND form name)
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const { str << MakeString(); return true; }
#endif
	virtual bool Write(wxString& str) const { str = MakeString(); return true; }

	virtual wxString GetType() const { return wxT("ibVariantDataCommandSource"); }

protected:

	const ibBackendCommandReceiver* m_owner = nullptr;   // the button / bar item — WALKED to validate the binding (existence + name)
	ibCommandDescription m_commandDesc;   // binding address: the command-hop path
	wxString             m_display;       // picker-set display name (fallback only when no owner is wired)
};

#endif // !__COMMAND_SOURCE_DATA_VARIANT_H__
