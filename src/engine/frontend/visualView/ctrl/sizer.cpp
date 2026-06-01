#include "sizer.h"
#ifdef OES_USE_WEB
// Needed so ibWebSizer's SetMinSize / Layout no-ops are visible when
// UpdateSizer calls through an ibFrontendSizer* that resolves to
// ibWebSizer on this build.
#include "frontend/web/webSizer.h"
#endif


//*******************************************************************
//*                            Control                              *
//*******************************************************************

void ibValueSizer::UpdateSizer(ibFrontendSizer* sizer)
{
	if (sizer == nullptr)
		return;

	// Web body: SetMinSize / Layout are declared as no-ops on
	// ibWebSizer (webSizer.h) so this compiles and silently drops on
	// the web build — CSS handles the actual layout. Desktop body:
	// push MinSize onto the wxSizer and relayout. Historically only
	// wrapped in #ifndef; unified via ibFrontendSizer typedef.
	if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize)
		sizer->SetMinSize(m_propertyMinSize->GetValueAsSize());

	if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize)
		sizer->Layout();
}

void ibSizerOps::SetChildParams(ibFrontendSizer* sizer, wxObject* child,
	int proportion, int flag, int border, int idx)
{
	if (sizer == nullptr || child == nullptr)
		return;

#ifdef OES_USE_WEB
	// Web: ibWebSizer::Item carries AddParams per-child; update in place.
	// idx is ignored — the vector order is already maintained by the
	// walker when children are added.
	(void)idx;
	ibWebSizer::AddParams params;
	params.proportion = proportion;
	params.flag       = flag;
	params.border     = border;
	sizer->UpdateItemParams(child, params);
#else
	// Desktop: wxSizer doesn't expose in-place SetItemProportion /
	// SetItemBorder, so the only way to change an existing item's
	// params is Detach + Add (or Insert at idx to preserve order).
	// This was previously inline in ibValueSizerItem::OnCreated /
	// OnUpdated and referred to as "the hack".
	if (wxWindow* windowChild = wxDynamicCast(child, wxWindow)) {
		sizer->Detach(windowChild);
		if (idx >= 0)
			sizer->Insert(idx, windowChild, proportion, flag, border);
		else
			sizer->Add(windowChild, proportion, flag, border);
		windowChild->Layout();
	}
	else if (wxSizer* sizerChild = wxDynamicCast(child, wxSizer)) {
		sizer->Detach(sizerChild);
		if (idx >= 0)
			sizer->Insert(idx, sizerChild, proportion, flag, border);
		else
			sizer->Add(sizerChild, proportion, flag, border);
		sizerChild->Layout();
	}
#endif
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

bool ibValueSizer::LoadData(ibReaderMemory& reader)
{
	wxString propValue = wxEmptyString;
	reader.r_stringZ(propValue);
	m_propertyMinSize->SetValue(typeConv::StringToSize(propValue));
	reader.r_stringZ(propValue);

	return ibValueFrame::LoadData(reader);
}

bool ibValueSizer::SaveData(ibWriterMemory& writer)
{
	writer.w_stringZ(
		m_propertyMinSize->GetValueAsString()
	);

	return ibValueFrame::SaveData(writer);
}