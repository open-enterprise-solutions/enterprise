#ifndef __ADVPROP_DATA_COMPOSITION_H__
#define __ADVPROP_DATA_COMPOSITION_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

#include "backend/backend_core.h"

#include <functional>   // the settings window a registration hands in — see ibSettingsDoor

class BACKEND_API ibPropertyObject;
class BACKEND_API ibMetaData;
struct ibCompositionDescription;

// -----------------------------------------------------------------------
// ibPGDataCompositionProperty — the "Settings…" cell over an ibVariantDataComposition, whichever
// window it opens. Read-only, no in-place editor; a click arrives as a NAME on the value, and the
// cell clones the description, shows the window over the clone and hands the result back through
// SetValueInEvent.
//
// ⭐ ONE CLASS, TWO REGISTRATIONS (2026-08-24). There were two classes — this and
// `ibPGDynamicListProperty` — defended as "they open two different windows onto two different
// things, and the registry already dispatches by type". The second half of that is exactly right and
// it argues for two REGISTRATIONS: the window is an argument (`ibSettingsDoor`), and everything else
// was identical down to the comments. 158 lines went with the merge.
// -----------------------------------------------------------------------

class ibPGDataCompositionProperty : public wxPGProperty {
public:

	// ⭐⭐ THE WINDOW IS AN ARGUMENT, and it is the ONLY thing that differed between this cell and the
	// dynamic list's (2026-08-24). Both were "Settings…" over an ibVariantDataComposition: the same
	// ctor, the same ValueToString, the same StringToValue, the same RefreshChildren, and the same
	// OnSetValue down to the clone, the holder variant and the SetValueInEvent — with one line apart,
	// which window to open.
	//
	// Two REGISTRATIONS, one class: the registry already dispatches by the backend property's type,
	// which is the argument the old split was defended with — and it is an argument for two Register
	// calls, not for two classes.
	using ibSettingsDoor = std::function<bool(ibCompositionDescription& desc, const ibMetaData* metaData)>;

	ibPGDataCompositionProperty(const ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant,
		ibSettingsDoor door = ibSettingsDoor());
	virtual ~ibPGDataCompositionProperty();

	const ibPropertyObject* GetPropertyObject() const { return m_property; }

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual void OnSetValue() override;

	// Refresh values of child properties. Automatically called after value is set.
	virtual void RefreshChildren();

protected:

	// ⭐⭐ THE PROPERTY THIS CELL WAS MADE FOR — the BACKEND one, not just its object.
	//
	// It is where the value LIVES. A cell is a view of it: the inspector rebuilds cells whenever the
	// editor refreshes, and each new cell is born from `m_property->GetValue()`. So a result written
	// only into the cell (`wxPGProperty::SetValue`) survives exactly until the next rebuild — which
	// the accept itself triggers, so the settings a person had just made were overwritten by the
	// value they replaced, about a second later (measured end to end in the journal, 2026-08-24).
	//
	// Through it the cell also reaches what it must ASK (the configuration, via GetPropertyObject) and
	// what it must TELL (the same object raises modified-ness and starts the cascade — Max).
	const ibPropertyObject* m_property = nullptr;

	// WHICH WINDOW THIS CELL OPENS — handed in at registration. Empty is not a state anybody reaches:
	// every registration supplies one, and a cell with none simply does nothing on a click.
	ibSettingsDoor m_door;

private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGDataCompositionProperty);
};

#endif // __ADVPROP_DATA_COMPOSITION_H__
