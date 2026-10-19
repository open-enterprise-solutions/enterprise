#ifndef __COMMAND_SOURCE_DATA_VARIANT_H__
#define __COMMAND_SOURCE_DATA_VARIANT_H__

#include <wx/variant.h>

#include "core/description/commandDescription.h"

// The variant behind a command-source property — the engine's ibVariantDataCommandSource (backend/propertyManager/
// property/variant/variantCommandSource.h): an ibCommandDescription (the address of a COMMAND: a hop path own / main /
// table / general), and the name it reads as.
//
// ⚠ NO OWNER TO WALK. The engine's walks the binding through its owner (ibBackendCommandReceiver::WalkCommand) to
// say whether it still stands; the commands are the server's, so the client reads it as the server wrote it — the
// row's Presentation, the display the engine's falls back to when no owner is wired.
class ibVariantDataCommandSource : public wxVariantData {

	wxString MakeString() const {
		if (!m_commandDesc.IsOk())
			return wxT("<not selected>");                        // nothing bound
		return m_display.IsEmpty() ? wxT("<not selected>") : m_display;
	}

public:

	bool IsEmptyCommand() const { return !m_commandDesc.IsOk(); }

	// The binding address — a command-hop path (own object / main attribute / table / general command). The
	// variant's only stored state; the property serialises THIS.
	ibCommandDescription& GetCommandDesc() { return m_commandDesc; }
	const ibCommandDescription& GetCommandDesc() const { return m_commandDesc; }
	void SetCommandDesc(const ibCommandDescription& desc) { m_commandDesc = desc; }

	// The display name — the words the server wrote for it.
	const wxString& GetDisplay() const { return m_display; }
	void SetDisplay(const wxString& display) { m_display = display; }

	ibVariantDataCommandSource() : wxVariantData() {}
	ibVariantDataCommandSource(const ibCommandDescription& desc, const wxString& display = wxEmptyString)
		: wxVariantData(), m_commandDesc(desc), m_display(display) {}
	ibVariantDataCommandSource(const ibVariantDataCommandSource& src)
		: wxVariantData(), m_commandDesc(src.m_commandDesc), m_display(src.m_display) {}

	virtual ibVariantDataCommandSource* Clone() const override { return new ibVariantDataCommandSource(*this); }

	virtual bool Eq(wxVariantData& data) const override {
		ibVariantDataCommandSource* cmdData = dynamic_cast<ibVariantDataCommandSource*>(&data);
		return cmdData != nullptr && m_commandDesc == cmdData->m_commandDesc;   // full identity (path AND form name)
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const override { str << MakeString(); return true; }
#endif
	virtual bool Write(wxString& str) const override { str = MakeString(); return true; }

	virtual wxString GetType() const override { return wxT("ibVariantDataCommandSource"); }

protected:

	ibCommandDescription m_commandDesc;   // binding address: the command-hop path
	wxString             m_display;       // the name it reads as — the server's words
};

#endif // !__COMMAND_SOURCE_DATA_VARIANT_H__
