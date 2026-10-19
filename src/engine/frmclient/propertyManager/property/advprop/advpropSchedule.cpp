#include "advpropSchedule.h"

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"                   // wxPGEditor_HyperLink — the click-only editor
#include "frmclient/win/dlgs/jobSchedule/jobScheduleSettings.h"         // the window it opens

#include "core/description/jobScheduleDescription.h"
#include "core/serialize/dataBuilder.h"

namespace {

// THE VALUE — the row's node as the server wrote it; the cell does not read it, the window does.
class ibVariantDataScheduleNode : public wxVariantData {
public:
	explicit ibVariantDataScheduleNode(const ibDataValue& node) : m_node(node) {}
	virtual bool Eq(wxVariantData& data) const override { return &data == this; }
	virtual wxString GetType() const override { return wxT("ibVariantDataScheduleNode"); }
	virtual wxVariantData* Clone() const override { return new ibVariantDataScheduleNode(m_node); }
	const ibDataValue& GetNode() const { return m_node; }
private:
	ibDataValue m_node;
};

const ibVariantDataScheduleNode* NodeOf(const wxVariant& value)
{
	return dynamic_cast<const ibVariantDataScheduleNode*>(value.GetData());
}

} // namespace

// -----------------------------------------------------------------------
// ibPGScheduleProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGScheduleProperty, wxPGProperty, HyperLink)

// register frontend property — a scheduled job's Schedule
class ibPropertyScheduleLoader
{
public:
	ibPropertyScheduleLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertySchedule"),
			[](ibBackendProperty* property) -> wxPGProperty* {
				return new ibPGScheduleProperty(property->GetLabel(), property->GetName(),
					wxVariant(new ibVariantDataScheduleNode(property->GetNodeValue())));
			},
			[](const wxVariant& value) {
				const ibVariantDataScheduleNode* const node = NodeOf(value);
				return node != nullptr ? node->GetNode() : ibDataValue();
			});
	}
} g_scheduleLoader;

ibPGScheduleProperty::ibPGScheduleProperty(const wxString& label, const wxString& name, const wxVariant& value)
	: wxPGProperty(label, name)
{
	wxPGProperty::SetFlagRecursively(wxPGFlags::ReadOnly, true);
	wxPGProperty::SetFlagRecursively(wxPGFlags::NoEditor, true);
	wxPGProperty::SetValue(value);
}

ibPGScheduleProperty::~ibPGScheduleProperty()
{
}

// A fixed label, as the desktop's: the sentence lives in the window, where it is always current.
wxString ibPGScheduleProperty::ValueToString(wxVariant& WXUNUSED(value), wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	return _("Open");
}

bool ibPGScheduleProperty::StringToValue(wxVariant& WXUNUSED(variant), const wxString& WXUNUSED(text),
	wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	return false;
}

void ibPGScheduleProperty::OnSetValue()
{
	// THE NAME IS THE CLICK (propertyEditor.cpp) — taken, so ended.
	if (wxT("hyperLink_clicked") != m_value.GetName())
		return;
	m_value.SetName(wxEmptyString);

	const ibVariantDataScheduleNode* const node = NodeOf(m_value);
	if (node == nullptr)
		return;

	ibJobScheduleDescription schedule;
	ibJobScheduleDescriptionMemory::ReadNode(node->GetNode(), schedule);

	// HERE, NOT DEFERRED — inside the editor's event is where a pending value is collected (see the composition's cell).
	// ⚠ A TOP-LEVEL PARENT, NOT THE GRID: a dialog parented to the grid is the grid's child and dies with it, in the
	// middle of the grid's own teardown (an assert on exit, 2026-10-19).
	wxWindow* const grid = GetGrid();
	ibDialogJobSchedule dialog(grid != nullptr ? wxGetTopLevelParent(grid) : nullptr, schedule);
	if (dialog.ShowModal() != wxID_OK)
		return;

	ibDataValue edited;
	ibJobScheduleDescriptionMemory::WriteNode(edited, dialog.GetSchedule());
	SetValueInEvent(wxVariant(new ibVariantDataScheduleNode(edited)));
}

void ibPGScheduleProperty::RefreshChildren()
{
	wxPGProperty::Enable(true);
}
