#ifndef __SOURCE_DATA_VARIANT_H__
#define __SOURCE_DATA_VARIANT_H__

#include <wx/variant.h>

#include "frmclient/backend/backend_core.h"
#include "core/description/sourceDescription.h"

// The variant behind a source property — the engine's ibVariantDataSource (backend/propertyManager/property/variant/
// variantSource.h): an ibSourceDescription (the address of a VALUE inside a data source, a hop path), and the name it
// reads as.
//
// ⚠ NO SOURCE TO WALK. The engine's walks the path through its owner's source factory — whether it still stands, what
// it reads as, the type its leaf has (the attribute beside the path); the sources are the server's, so the client reads
// the binding as the server wrote it (the row's Presentation), as the command source's variant reads its command
// (ibVariantDataCommandSource), and holds no type.
class ibVariantDataSource : public wxVariantData {

	wxString MakeString() const {
		return m_sourceDesc.IsOk() && !m_display.IsEmpty() ? m_display : wxString(_("<not selected>"));
	}

public:

	// Nothing bound — the engine's asks the walk; the client knows only whether there is a path.
	bool IsEmptySource() const { return !m_sourceDesc.IsOk(); }

	ibSourceDescription& GetSourceDesc() { return m_sourceDesc; }
	const ibSourceDescription& GetSourceDesc() const { return m_sourceDesc; }

	// Selecting a direct source resets the path to a single leaf; wxNOT_FOUND — none.
	void SetSource(const ibMetaID& id) {
		if (id != wxNOT_FOUND)
			m_sourceDesc.SetDefaultSource(id);
		else
			m_sourceDesc.ClearSource();
	}

	ibVariantDataSource(const ibSourceDescription& desc = ibSourceDescription(), const wxString& display = wxEmptyString)
		: wxVariantData(), m_sourceDesc(desc), m_display(display) {}
	ibVariantDataSource(const ibVariantDataSource& src)
		: wxVariantData(), m_sourceDesc(src.m_sourceDesc), m_display(src.m_display) {}

	virtual ibVariantDataSource* Clone() const override { return new ibVariantDataSource(*this); }

	virtual bool Eq(wxVariantData& data) const override {
		ibVariantDataSource* srcData = dynamic_cast<ibVariantDataSource*>(&data);
		return srcData != nullptr && m_sourceDesc.GetPath() == srcData->m_sourceDesc.GetPath();
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const override { str << MakeString(); return true; }
#endif
	virtual bool Write(wxString& str) const override { str = MakeString(); return true; }

	virtual wxString GetType() const override { return wxT("ibVariantDataSource"); }

private:

	ibSourceDescription m_sourceDesc;   // binding address: the hop path
	wxString            m_display;      // the name it reads as — the server's words
};

#endif // !__SOURCE_DATA_VARIANT_H__
