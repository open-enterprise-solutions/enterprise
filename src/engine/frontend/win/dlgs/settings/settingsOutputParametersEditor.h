#ifndef __SETTINGS_OUTPUT_PARAMETERS_EDITOR_H__
#define __SETTINGS_OUTPUT_PARAMETERS_EDITOR_H__

// ---------------------------------------------------------------------------
// THE OTHER SETTINGS PAGE — how a report behaves as a whole (ibOutputParameter): its theme, its heading. The
// page after Sort, on the report and on every node, pointed at the storey selected the way the filter and sort
// editors are (Max, 2026-09-29).
//
// The same table the appearance window is — one line per parameter of the platform's list, ticked or not, with
// its value — because it is the same description (ibParameterValuesDescription). Only which parameters a
// storey lists differs (ibOutputParameterScope): a grouping has no heading to title.
// ---------------------------------------------------------------------------

#include <functional>

#include <wx/panel.h>

#include "backend/compositionDescription.h"   // ibOutputParametersDescription — what is edited here

class ibMetaData;
class ibDataViewCtrl;
class ibOutputParametersModel;

class ibOutputParametersEditor : public wxPanel {
public:
	// `metaOf` names the configuration whose languages a title is written in — ASKED when a title is edited,
	// the way the appearance window is handed it at the click: the window that holds this page may not know it
	// yet while it is being built.
	using MetaOf = std::function<const ibMetaData*()>;
	ibOutputParametersEditor(wxWindow* parent, ibOutputParametersDescription* parameters,
		ibOutputParameterScope scope, MetaOf metaOf);

	// POINTED AT ANOTHER STOREY — the node selected, or the report. What it lists follows the storey.
	void SetParameters(ibOutputParametersDescription* parameters, ibOutputParameterScope scope);
	void SetReadOnly(bool readOnly);
	// …and told when a person changed something, as it happens — the window marks its settings touched.
	void SetOnChanged(std::function<void()> changed) { m_changed = std::move(changed); }

private:
	// THE "..." OF A LINE — a list to pick from for a theme and for whether something is shown, the translate
	// constructor for a title.
	bool EditValue(wxString& text);
	void Changed();

	ibOutputParametersDescription* m_parameters = nullptr;
	ibOutputParameterScope         m_scope = ibOutputParameterScope::Report;
	MetaOf                         m_metaOf;
	bool                           m_readOnly = false;
	ibDataViewCtrl*                m_view = nullptr;
	ibOutputParametersModel*       m_model = nullptr;
	std::function<void()>          m_changed;

	friend class ibOutputParametersModel;
};

#endif
