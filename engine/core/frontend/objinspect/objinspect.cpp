#include "objinspect.h"
#include "advprops.h"

#include "metadata/metadata.h"

#include "events.h"

#include "frontend/mainFrame.h"
#include "common/formdefs.h"
#include "utils/typeconv.h"

#include "frontend/theme/luna_auitabart.h"

#include <wx/config.h>
#include <wx/propgrid/propgrid.h>

static int wxEVT_OES_PROP_BITMAP_CHANGED = wxNewEventType();

enum
{
	WXOES_PROPERTY_GRID = wxID_HIGHEST + 1000
};

// -----------------------------------------------------------------------
// CObjectInspector
// -----------------------------------------------------------------------

BEGIN_EVENT_TABLE(CObjectInspector, wxPanel)
EVT_PG_CHANGING(WXOES_PROPERTY_GRID, CObjectInspector::OnPropertyGridChanging)
EVT_PG_ITEM_COLLAPSED(WXOES_PROPERTY_GRID, CObjectInspector::OnPropertyGridExpand)
EVT_PG_ITEM_EXPANDED(WXOES_PROPERTY_GRID, CObjectInspector::OnPropertyGridExpand)
EVT_PG_SELECTED(WXOES_PROPERTY_GRID, CObjectInspector::OnPropertyGridItemSelected)
EVT_OBJECT_SELECTED(CObjectInspector::OnObjectSelected)
EVT_PROJECT_REFRESH(CObjectInspector::OnProjectRefresh)
EVT_PROPERTY_MODIFIED(CObjectInspector::OnPropertyModified)
EVT_CHILD_FOCUS(CObjectInspector::OnChildFocus)
END_EVENT_TABLE()

#include "editors.h"

CObjectInspector::CObjectInspector(wxWindow* parent, int id, int style)
	: wxPanel(parent, id), m_style(style)
{
	m_currentSel = NULL;
	m_pg = CreatePropertyGridManager(this, WXOES_PROPERTY_GRID);

	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
	topSizer->Add(m_pg, 1, wxALL | wxEXPAND, 0);
	SetSizer(topSizer);

	Connect(wxID_ANY, wxEVT_OES_PROP_BITMAP_CHANGED, wxCommandEventHandler(CObjectInspector::OnBitmapPropertyChanged));

	//register new editor 
	wxPGRegisterEditorClass(ComboBoxAndButton);
}

CObjectInspector::~CObjectInspector()
{
	Disconnect(wxID_ANY, wxEVT_OES_PROP_BITMAP_CHANGED, wxCommandEventHandler(CObjectInspector::OnBitmapPropertyChanged));
}

void CObjectInspector::SavePosition()
{
	// Save Layout
	wxConfigBase* config = wxConfigBase::Get();
	config->Write(wxT("/mainFrame/objectInspector/DescBoxHeight"), m_pg->GetDescBoxHeight());
}

void CObjectInspector::Create(bool force)
{
	IObjectBase* sel_obj = GetSelectedObject();

	if (sel_obj && (sel_obj != m_currentSel || force))
	{
		m_pg->Freeze();

		m_currentSel = sel_obj;
		m_currentSel->ReadProperty();

		m_currentSel->OnPropertyCreated();

		int pageNumber = m_pg->GetSelectedPage();
		wxString pageName;
		if (pageNumber != wxNOT_FOUND)
		{
			pageName = m_pg->GetPageName(pageNumber);
		}

		// Clear Property Grid Manager
		m_pg->Clear();

		m_propMap.clear();
		m_eventMap.clear();

		std::map<wxString, Property *> propMap, dummyPropMap;
		std::map<wxString, Event *> eventMap, dummyEventMap;

		// We create the categories with the properties of the object organized by "classes"
		CreateCategory(sel_obj->GetClassName(), sel_obj, propMap, false);

		IObjectBase* parent = sel_obj->GetParent();

		if (parent) {
			if (parent->IsItem()) {
				CreateCategory(sel_obj->GetClassName(), parent, dummyPropMap, false);
			}
		}

		CreateCategory(sel_obj->GetClassName(), sel_obj, eventMap, true);

		if (parent) {
			if (parent->IsItem()) {
				CreateCategory(sel_obj->GetClassName(), parent, dummyEventMap, true);
			}
		}

		// Select previously selected page, or first page
		if (m_pg->GetPageCount() > 0)
		{
			int pageIndex = m_pg->GetPageByName(pageName);
			if (wxNOT_FOUND != pageIndex) {
				m_pg->SelectPage(pageIndex);
			}
			else {
				m_pg->SelectPage(0);
			}
		}

		m_pg->Refresh();
		m_pg->Update();

		m_pg->Thaw();
	}

	RestoreLastSelectedPropItem();
}

void CObjectInspector::ClearProperty()
{
	Freeze();

	// Clear Property Grid Manager
	m_pg->Clear();

	m_propMap.clear();
	m_eventMap.clear();

	m_pg->Refresh();
	m_pg->Update();

	Thaw();
}

int CObjectInspector::StringToBits(const wxString& strVal, wxPGChoices& constants)
{
	wxStringTokenizer strTok(strVal, wxT(" |"));
	int val = 0;
	while (strTok.HasMoreTokens())
	{
		wxString token = strTok.GetNextToken();
		unsigned int i = 0;
		bool done = false;
		while (i < constants.GetCount() && !done)
		{
			if (constants.GetLabel(i) == token)
			{
				val |= constants.GetValue(i);
				done = true;
			}
			i++;
		}
	}
	return val;
}

wxPGProperty* CObjectInspector::GetProperty(Property *prop)
{
	wxPGProperty *result = NULL;
	PropertyType type = prop->GetType();
	wxString name = prop->GetName();
	wxVariant vTrue = wxVariant(true, wxT("true"));

	if (type == PT_MACRO)
	{
		result = new wxStringProperty(name, wxPG_LABEL, prop->GetValueAsString());
	}
	else if (type == PT_INT)
	{
		result = new wxIntProperty(name, wxPG_LABEL, prop->GetValueAsInteger());
	}
	else if (type == PT_UINT)
	{
		result = new wxUIntProperty(name, wxPG_LABEL, (unsigned)prop->GetValueAsInteger());
	}
	else if (type == PT_WXNAME)
	{
		result = new wxStringControlProperty(name, wxPG_LABEL, prop->GetValueAsText());
	}
	else if (type == PT_WXSTRING || type == PT_WXSTRING_I18N)
	{
		result = new wxLongStringProperty(name, wxPG_LABEL, prop->GetValueAsText());
	}
	else if (type == PT_TOOL_ACTION)
	{
		OptionList *opt_list = prop->GetOptionList();

		wxString m_option;
		wxString m_help;

		unsigned int i = 0;
		wxPGChoices constants;

		for (auto option : opt_list->GetOptions()) {
			constants.Add(option.m_option, option.m_intVal);
			if (option.m_intVal == prop->GetValueAsInteger()) {
				// Save option and help
				m_option = option.m_option;
				m_help = option.m_description;
			}
		}

		wxDELETE(opt_list);

		result = new wxPGToolActionProperty(name, wxPG_LABEL, constants, prop->GetValueAsString(),
			prop->GetObject());
	}
	else if (type == PT_SOURCE)
	{
		result = new wxPGSourceProperty(name, wxPG_LABEL, prop->GetValueAsInteger(),
			prop->GetObject());
	}
	else if (type == PT_TEXT)
	{
		result = new wxLongStringProperty(name, wxPG_LABEL, prop->GetValueAsString());
	}
	else if (type == PT_BOOL)
	{
		result = new wxBoolProperty(name, wxPG_LABEL, prop->GetValue() == wxT("1"));
	}
	else if (type == PT_BITLIST)
	{
		OptionList *opt_list = prop->GetOptionList();
		assert(opt_list && opt_list->GetOptionCount() > 0);

		unsigned int index = 0;
		wxPGChoices constants;

		for (auto option : opt_list->GetOptions()) {
			constants.Add(option.m_option, option.m_intVal);
		}

		result = new wxFlagsProperty(name, wxPG_LABEL, constants, prop->GetValueAsInteger());

		// Workaround to set the help strings for individual members of a wxFlagsProperty
		wxFlagsProperty* flagsProp = dynamic_cast<wxFlagsProperty*>(result);
		if (NULL != flagsProp)
		{
			for (unsigned int i = 0; i < flagsProp->GetItemCount(); i++)
			{
				wxPGProperty* itemProp = flagsProp->Item(i);

				for (auto option : opt_list->GetOptions())
				{
					if (option.m_option == itemProp->GetLabel())
					{
						m_pg->SetPropertyHelpString(itemProp, option.m_description);
					}
				}
			}
		}

		delete opt_list;
	}
	else if (type == PT_INTLIST || type == PT_UINTLIST)
	{
		result = new wxStringProperty(name, wxPG_LABEL, IntList(prop->GetValueAsString(), type == PT_UINTLIST).ToString());
	}
	else if (type == PT_OPTION || type == PT_EDIT_OPTION)
	{
		OptionList *opt_list = prop->GetOptionList();
		assert(opt_list && opt_list->GetOptionCount() > 0);

		wxString m_option;
		wxString m_help;

		unsigned int i = 0;
		wxPGChoices constants;

		for (auto option : opt_list->GetOptions()) {
			constants.Add(option.m_option, option.m_intVal);
			if (option.m_intVal == prop->GetValueAsInteger()) {
				// Save option and help
				m_option = option.m_option;
				m_help = option.m_description;
			}
		}

		wxDELETE(opt_list);

		if (type == PT_EDIT_OPTION) {
			result = new wxEditEnumProperty(name, wxPG_LABEL, constants);
		}
		else {
			result = new wxEnumProperty(name, wxPG_LABEL, constants);
		}

		result->SetValueFromString(m_option, 0);

		wxString desc = prop->GetDescription();

		if (desc.empty()) {
			desc = m_option + wxT(":\n") + m_help;
		}
		else {
			desc += wxT("\n\n") + m_option + wxT(":\n") + m_help;
		}

		result->SetHelpString(wxGetTranslation(desc));
	}
	else if (type == PT_TYPE_SELECT)
	{
		OptionList *opt_list = prop->GetTypelist();

		wxString m_option;
		wxString m_help;

		unsigned int i = 0;
		wxPGChoices constants;

		if (opt_list) {

			for (auto option : opt_list->GetOptions())
			{
				constants.Add(option.m_option, option.m_intVal);

				if (option.m_intVal == prop->GetValueAsInteger())
				{
					// Save option and help
					m_option = option.m_option;
					m_help = option.m_description;
				}
			}

			wxDELETE(opt_list);
		}

		result = new wxPGTypeSelectorProperty(name, wxPG_LABEL, constants, 0,
			prop->GetObject());

		result->SetValueFromString(m_option, 0);

		wxString desc = prop->GetDescription();

		if (desc.empty())
		{
			desc = m_option + wxT(":\n") + m_help;
		}
		else
		{
			desc += wxT("\n\n") + m_option + wxT(":\n") + m_help;
		}

		result->SetHelpString(wxGetTranslation(desc));
	}
	else if (type == PT_WXPOINT)
	{
		result = new wxPGPointProperty(name, wxPG_LABEL, prop->GetValueAsPoint());
	}
	else if (type == PT_WXSIZE)
	{
		result = new wxPGSizeProperty(name, wxPG_LABEL, prop->GetValueAsSize());
	}
	else if (type == PT_WXFONT)
	{
		result = new wxPGFontProperty(name, wxPG_LABEL, TypeConv::StringToFont(prop->GetValueAsString()));
	}
	else if (type == PT_WXCOLOUR)
	{
		wxString value = prop->GetValueAsString();
		if (value.empty())  // Default Colour
		{
			wxColourPropertyValue colProp;
			colProp.m_type = wxSYS_COLOUR_WINDOW;
			colProp.m_colour = TypeConv::StringToSystemColour(wxT("wxSYS_COLOUR_WINDOW"));
			result = new wxSystemColourProperty(name, wxPG_LABEL, colProp);
		}
		else
		{
			if (value.find_first_of(wxT("wx")) == 0)
			{
				wxColourPropertyValue def; // System Colour
				def.m_type = TypeConv::StringToSystemColour(value);
				result = new wxSystemColourProperty(name, wxPG_LABEL, def);
			}
			else
			{
				result = new wxSystemColourProperty(name, wxPG_LABEL, prop->GetValueAsColour());
			}
		}
	}
	else if (type == PT_PATH)
	{
		result = new wxDirProperty(name, wxPG_LABEL, prop->GetValueAsString());
	}
	else if (type == PT_FILE)
	{
		result = new wxFileProperty(name, wxPG_LABEL, prop->GetValueAsString());
	}
	else if (type == PT_BITMAP)
	{
		wxLogDebug(wxT("OI::GetProperty: prop:%s"), prop->GetValueAsString().c_str());

		result = new wxPGBitmapProperty(name, wxPG_LABEL, prop->GetValueAsString());
	}
	else if (type == PT_STRINGLIST)
	{
		result = new wxArrayStringProperty(name, wxPG_LABEL, prop->GetValueAsArrayString());
#if wxVERSION_NUMBER >= 2901
		wxVariant v("\"");
		result->DoSetAttribute(wxPG_ARRAY_DELIMITER, v);
#endif
	}
	else if (type == PT_FLOAT)
	{
		result = new wxFloatProperty(name, wxPG_LABEL, prop->GetValueAsFloat());
	}
	else if (type == PT_PARENT)
	{
		result = new wxStringProperty(name, wxPG_LABEL);
		result->ChangeFlag(wxPG_PROP_READONLY, true);
	}
	else // Unknown property
	{
		result = new wxStringProperty(name, wxPG_LABEL, prop->GetValueAsString());
		result->SetAttribute(wxPG_BOOL_USE_DOUBLE_CLICK_CYCLING, vTrue);
		wxLogError(_("Property type Unknown"));
	}

	result->Enable(prop->IsEditable());
	return result;
}

#include "utils/stringUtils.h"

void CObjectInspector::GenerateSynonym(const wxString &systemName)
{
	auto foundedIt =
		std::find_if(m_propMap.begin(), m_propMap.end(), [](std::pair<wxPGProperty*, Property*> pair)
	{
		return pair.second->GetName() == wxT("synonym")
			|| pair.second->GetName() == wxT("caption");
	});

	if (foundedIt == m_propMap.end())
		return;

	if (systemName.IsEmpty())
		return;

	wxPGProperty *propGrid = foundedIt->first;
	Property *prop = foundedIt->second;

	wxString newSynonym =
		StringUtils::GenerateSynonym(systemName);

	if (ModifyProperty(prop, newSynonym)) {
		propGrid->SetValueFromString(newSynonym);
	}
}

void CObjectInspector::AddItems(const wxString& name, IObjectBase* obj,
	IObjectBase::PropertyContainer *category, std::map<wxString, Property *> &properties)
{
	unsigned int propCount = category->GetPropertyCount();
	for (unsigned int i = 0; i < propCount; i++)
	{
		wxString propName = category->GetPropertyName(i);

		if (!category->IsVisibleProperty(propName))
			continue;

		Property *prop = obj->GetProperty(propName);

		if (!prop)
			continue;

		// we do not want to duplicate inherited properties
		if (properties.find(propName) == properties.end())
		{
			wxPGProperty* id = m_pg->Append(GetProperty(prop));
			int           propType = prop->GetType();

			if (propType != PT_OPTION && propType != PT_TYPE_SELECT)
			{
				m_pg->SetPropertyHelpString(id, prop->GetDescription());

				if (propType == PT_BITMAP)
				{
					wxPGBitmapProperty *bp = wxDynamicCast(id, wxPGBitmapProperty);
					if (bp)
					{
						bp->CreateChildren();

						// perform delayed child properties update
						wxCommandEvent e(wxEVT_OES_PROP_BITMAP_CHANGED);
						e.SetString(bp->GetName() + wxT(":") + prop->GetValue());
						GetEventHandler()->AddPendingEvent(e);

						//appData->ModifyProperty( prop, bp->GetValueAsString() );
					}
				}
			}

			if (m_style != wxOES_OI_MULTIPAGE_STYLE)
			{
				// Most common classes will be showed with a slightly different colour.
				if (name == wxT("Window"))
					m_pg->SetPropertyBackgroundColour(id, wxColour(255, 255, 205)); // yellow
				else if (name == wxT("Common"))
					m_pg->SetPropertyBackgroundColour(id, wxColour(240, 240, 255)); // light blue
				else if (name == wxT("SizerItem"))
					m_pg->SetPropertyBackgroundColour(id, wxColour(220, 255, 255)); // cyan
			}

			std::map< wxString, bool >::iterator it = m_isExpanded.find(propName);
			if (it != m_isExpanded.end())
			{
				if (it->second)
				{
					m_pg->Expand(id);
				}
				else
				{
					m_pg->Collapse(id);
				}
			}

			if (m_currentSel) {
				m_currentSel->OnPropertyCreated(prop);
			}

			properties.insert(std::map<wxString, Property *>::value_type(propName, prop));
			m_propMap.insert(std::map< wxPGProperty*, Property*>::value_type(id, prop));
		}
	}

	unsigned int catCount = category->GetCategoryCount();
	for (unsigned int i = 0; i < catCount; i++)
	{
		IObjectBase::PropertyContainer *nextCat = category->GetCategory(i);
		if (0 == nextCat->GetCategoryCount() && 0 == nextCat->GetPropertyCount()) {
			continue;
		}
		wxPGProperty* catId = m_pg->AppendIn(category->GetName(), new wxPropertyCategory(nextCat->GetDescription(), nextCat->GetName()));

		AddItems(nextCat->GetName(), obj, nextCat, properties);

		std::map< wxString, bool >::iterator it = m_isExpanded.find(nextCat->GetName());
		if (it != m_isExpanded.end())
		{
			if (it->second)
			{
				m_pg->Expand(catId);
			}
			else
			{
				m_pg->Collapse(catId);
			}
		}
	}
}

void CObjectInspector::AddItems(const wxString& name, IObjectBase* obj,
	IObjectBase::PropertyContainer *category, std::map<wxString, Event *> &events)
{
	unsigned int eventCount = category->GetEventCount();
	for (unsigned int i = 0; i < eventCount; i++)
	{
		wxString eventName = category->GetEventName(i);
		if (!category->IsVisibleEvent(eventName))
			continue;

		Event *event = obj->GetEvent(eventName);

		if (!event)
			continue;

		// We do not want to duplicate inherited events
		if (events.find(eventName) == events.end())
		{
			wxPGProperty* pgProp = new wxEventControlProperty(event->GetName(), wxPG_LABEL, event->GetValue());
			wxPGProperty* id = m_pg->Append(pgProp);

			m_pg->SetPropertyHelpString(id, wxGetTranslation(event->GetDescription()));

			if (m_style != wxOES_OI_MULTIPAGE_STYLE)
			{
				// Most common classes will be showed with a slightly different colour.
				if (name == wxT("Window"))
					m_pg->SetPropertyBackgroundColour(id, wxColour(255, 255, 205)); // Yellow
				else if (name == wxT("Common"))
					m_pg->SetPropertyBackgroundColour(id, wxColour(240, 240, 255)); // light blue
				else if (name == wxT("SizerItem"))
					m_pg->SetPropertyBackgroundColour(id, wxColour(220, 255, 255)); // Cyan
			}

			std::map< wxString, bool >::iterator it = m_isExpanded.find(eventName);
			if (it != m_isExpanded.end())
			{
				if (it->second) {
					m_pg->Expand(id);
				}
				else {
					m_pg->Collapse(id);
				}
			}

			events.insert(std::map<wxString, Event *>::value_type(eventName, event));
			m_eventMap.insert(std::map< wxPGProperty*, Event*>::value_type(id, event));
		}
	}

	unsigned int catCount = category->GetCategoryCount();
	for (unsigned int i = 0; i < catCount; i++)
	{
		IObjectBase::PropertyContainer *nextCat = category->GetCategory(i);
		if (0 == nextCat->GetCategoryCount() && 0 == nextCat->GetEventCount()) {
			continue;
		}

		wxPGProperty* catId = m_pg->AppendIn(category->GetName(), new wxPropertyCategory(nextCat->GetDescription(), nextCat->GetName()));

		AddItems(nextCat->GetName(), obj, nextCat, events);

		std::map< wxString, bool >::iterator it = m_isExpanded.find(nextCat->GetName());
		if (it != m_isExpanded.end())
		{
			if (it->second)
			{
				m_pg->Expand(catId);
			}
			else
			{
				m_pg->Collapse(catId);
			}
		}
	}
}

void CObjectInspector::OnPropertyGridChanging(wxPropertyGridEvent& event)
{
	wxImageFileProperty *imgFileProp = wxDynamicCast(event.GetProperty(), wxImageFileProperty);

	if (imgFileProp)
	{
		// GetValue() returns the pending value, but is only supported by wxEVT_PG_CHANGING.
		wxPGBitmapProperty *bmpProp = wxDynamicCast(imgFileProp->GetParent(), wxPGBitmapProperty);

		if (bmpProp)
		{
			wxString img = bmpProp->SetupImage(event.GetValue().GetString());
			if (img == wxEmptyString) {
				event.Veto();
				return;
			}
		}
	}

	wxPGProperty* propPtr = event.GetProperty();
	std::map< wxPGProperty*, Property*>::iterator itProperty = m_propMap.find(propPtr);

	if (m_propMap.end() == itProperty)
	{
		// Could be a child property
		propPtr = propPtr->GetParent();
		itProperty = m_propMap.find(propPtr);
	}

	wxVariant newValue = event.GetPropertyValue();

	if (itProperty != m_propMap.end())
	{
		Property *prop = itProperty->second;
		switch (prop->GetType())
		{

		case PT_FLOAT:
		{
			// Use typeconv to properly handle locale
			double val = newValue;
			if (!ModifyProperty(prop, TypeConv::FloatToString(val)))
				event.Veto();
			break;
		}
		case PT_TEXT:
		case PT_MACRO:
		case PT_INT:
		case PT_UINT:
		{
			if (!ModifyProperty(prop, newValue))
				event.Veto();
			break;
		}
		case PT_OPTION:
		case PT_EDIT_OPTION:
		{
			long value = newValue.GetInteger();

			// Update displayed description for the new selection
			OptionList *opt_list = prop->GetOptionList();
			assert(opt_list && opt_list->GetOptionCount() > 0);

			wxString helpString = prop->GetDescription();

			if (opt_list && opt_list->GetOptionCount() > 0)
			{
				for (auto option : opt_list->GetOptions())
				{
					if (option.m_intVal != value)
						continue;

					//if (helpString.empty()) {
					//	helpString = value + wxT(":\n") + option.m_description;
					//}
					//else {
					//	helpString += wxT("\n\n") + value + wxT(":\n") + option.m_description;
					//}

					wxString m_valStr;
					m_valStr << option.m_intVal;

					if (!ModifyProperty(prop, m_valStr))
						event.Veto();
				}
			}

			wxString localized = wxGetTranslation(helpString);
			m_pg->SetPropertyHelpString(propPtr, localized);
			m_pg->SetDescription(propPtr->GetLabel(), localized);

			delete opt_list; break;
		}
		case PT_TYPE_SELECT:
		{
			long value = newValue.GetInteger();

			// Update displayed description for the new selection
			OptionList *opt_list = prop->GetTypelist();
			wxString helpString = prop->GetDescription();

			if (opt_list->GetOptionCount() > 0)
			{
				for (auto option : opt_list->GetOptions())
				{
					if (option.m_intVal != value)
						continue;

					/*if (helpString.empty())
					{
						helpString = value + wxT(":\n") + option.m_description;
					}
					else
					{
						helpString += wxT("\n\n") + value + wxT(":\n") + option.m_description;
					}*/

					wxString m_valStr;
					m_valStr << option.m_intVal;

					if (!ModifyProperty(prop, m_valStr))
						event.Veto();
				}
			}

			wxString localized = wxGetTranslation(helpString);
			m_pg->SetPropertyHelpString(propPtr, localized);
			m_pg->SetDescription(propPtr->GetLabel(), localized);
			break;
		}
		case PT_PARENT:
		{
			// GenerateComposedValue() is the only method that does actually return a value,
			// although the documentation claims the other methods just call this one,
			// they return an empty value
			const auto value = propPtr->GenerateComposedValue();
			if (!ModifyProperty(prop, value))
				event.Veto();
			break;
		}
		case PT_TOOL_ACTION:
		{
			wxPGToolActionProperty *toolProperty =
				wxDynamicCast(event.GetProperty(), wxPGToolActionProperty);

			wxASSERT(toolProperty);

			wxString aux = toolProperty->GetString();

			if (!toolProperty->IsCustomEvent()) {
				aux << toolProperty->GetNumber();
			}

			wxString oldValue = prop->GetValue();

			if (!ModifyProperty(prop, aux))
				event.Veto();

			if (!event.WasVetoed()
				&& toolProperty->IsCustomEvent()) {
				wxFrameEventHandlerEvent event(wxEVT_CODE_GENERATION, aux, oldValue);
				m_evthandler->ProcessEvent(event);
			} break;
		}
		case PT_SOURCE:
		{
			wxString aux;
			aux << newValue.GetInteger();
			if (!ModifyProperty(prop, aux))
				event.Veto();
			break;
		}
		case PT_WXNAME:
		case PT_WXSTRING:
		case PT_WXSTRING_I18N:
		{
			// CObjectInspector's text strings are formatted.
			wxString value = TypeConv::TextToString(newValue);

			if (!ModifyProperty(prop, value))
				event.Veto();
			else if (prop->GetType() == PT_WXNAME)
				GenerateSynonym(value);
			break;
		}
		case PT_BOOL:
		{
			if (!ModifyProperty(prop, newValue.GetBool() ? wxT("1") : wxT("0")))
				event.Veto();
			break;
		}
		case PT_BITLIST:
		{
			wxString aux;
			aux << newValue.GetInteger();
			if (!ModifyProperty(prop, aux))
				event.Veto();
			break;
		}
		case PT_WXPOINT:
		{
			wxPoint point = wxPointRefFromVariant(newValue);
			if (!ModifyProperty(prop, wxString::Format(wxT("%i,%i"), point.x, point.y)))
				event.Veto();
			break;
		}
		case PT_WXSIZE:
		{
			wxSize size = wxSizeRefFromVariant(newValue);
			if (!ModifyProperty(prop, wxString::Format(wxT("%i,%i"), size.GetWidth(), size.GetHeight())))
				event.Veto();
			break;
		}
		case PT_WXFONT:
		{
			if (!ModifyProperty(prop, newValue.GetString()))
				event.Veto();
			break;
		}
		case PT_WXCOLOUR:
		{
			wxColourPropertyValue colour;
			colour << newValue;
			switch (colour.m_type) {
			case wxSYS_COLOUR_MAX:
				if (!ModifyProperty(prop, wxT("")))
					event.Veto();
				break;
			case wxPG_COLOUR_CUSTOM:
				if (!ModifyProperty(prop, TypeConv::ColourToString(colour.m_colour)))
					event.Veto();
				break;
			default:
				wxString sCol = TypeConv::SystemColourToString(colour.m_type);
				if (!ModifyProperty(prop, sCol))
					event.Veto();
			}
			break;
		}
		case PT_INTLIST:
		case PT_UINTLIST:
		{
			IntList il(newValue, PT_UINTLIST == prop->GetType());
			if (!ModifyProperty(prop, il.ToString()))
				event.Veto();
			break;
		}
		case PT_BITMAP:
		{
			wxVariant childValue = newValue;

			// Also, handle the case where property value is unspecified
			if (childValue.IsNull())
				return;

			// bp->GetValue() have no updated value...
			wxString bmpVal = propPtr->GetValueAsString(wxPG_FULL_VALUE);

			// Handle changes in values, as needed
			wxVariant thisValue = WXVARIANT(bmpVal);

			wxVariant newVal =
				propPtr->ChildChanged(thisValue, (int)event.GetProperty()->GetIndexInParent(), childValue);

			if (!ModifyProperty(prop, newVal.GetString()))
				event.Veto();

			if (event.GetProperty()->GetIndexInParent() > 0)
			{
				// perform delayed child properties update
				wxCommandEvent e(wxEVT_OES_PROP_BITMAP_CHANGED);
				e.SetString(propPtr->GetName() + wxT(":") + newVal.GetString());
				GetEventHandler()->AddPendingEvent(e);
			}

			break;
		}

		default:
			if (!ModifyProperty(prop, propPtr->GetValueAsString()))
				event.Veto();
		}

		return;
	}

	std::map< wxPGProperty*, Event*>::iterator itEvent = m_eventMap.find(event.GetProperty());

	if (itEvent != m_eventMap.end()) {

		Event *evt = itEvent->second;
		wxString oldValue = evt->GetValue();

		wxString handler = newValue;
		handler.Trim(true).Trim(false);
		evt->SetValue(handler);

		if (m_evthandler) {
			wxFrameEventHandlerEvent event(wxEVT_CODE_GENERATION, handler, oldValue, evt->GetArgs());
			m_evthandler->ProcessEvent(event);
		}
	}
}

void CObjectInspector::OnPropertyGridExpand(wxPropertyGridEvent& event)
{
	m_isExpanded[event.GetPropertyName()] = event.GetProperty()->IsExpanded();

	wxPGProperty* egProp = m_pg->GetProperty(event.GetProperty()->GetName());
	if (egProp)
	{
		if (event.GetProperty()->IsExpanded())
		{
			m_pg->Expand(egProp);
		}
		else
		{
			m_pg->Collapse(egProp);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void CObjectInspector::OnObjectSelected(wxFrameObjectEvent& event)
{
	bool isForced = (event.GetString() == wxT("force"));
	Create(isForced);
}

void CObjectInspector::OnProjectRefresh(wxFrameEvent&)
{
	Create(true);
}

void CObjectInspector::OnPropertyModified(wxFramePropertyEvent& event)
{
	Property *prop = event.GetFrameProperty();

	IObjectBase* propobj = prop->GetObject();
	IObjectBase* appobj = GetSelectedObject();

	bool shouldContinue = (prop->GetObject() == GetSelectedObject());
	if (!shouldContinue)
	{
		// Item objects cannot be selected - their children are selected instead
		if (propobj->IsItem())
		{
			if (propobj->GetChildCount() > 0)
			{
				shouldContinue = (appobj == propobj->GetChild(0));
			}
		}
	}
	if (!shouldContinue)
	{
		return;
	}

	wxPGProperty* pgProp = m_pg->GetPropertyByLabel(prop->GetName());
	if (pgProp == NULL) return; // Maybe now isn't showing this page


	switch (prop->GetType())
	{
	case PT_FLOAT:
	{
		// Use float instead of string -> typeconv handles locale
		pgProp->SetValue(WXVARIANT(prop->GetValueAsFloat()));
		break;
	}
	case PT_INT:
	case PT_UINT:
	{
		pgProp->SetValueFromString(prop->GetValueAsString(), 0);
		break;
	}
	case PT_TEXT:
		pgProp->SetValueFromString(prop->GetValueAsString(), 0);
		break;
	case PT_MACRO:
	case PT_OPTION:
	case PT_TYPE_SELECT:
	case PT_EDIT_OPTION:
	case PT_PARENT:
	case PT_SOURCE:
	case PT_TOOL_ACTION:
	case PT_WXNAME:
	case PT_WXSTRING: pgProp->SetValueFromString(prop->GetValueAsText(), 0); break;
	case PT_WXSTRING_I18N: pgProp->SetValueFromString(prop->GetValueAsText(), 0); break;
	case PT_BOOL:
		pgProp->SetValueFromInt(prop->GetValueAsString() == wxT("0") ? 0 : 1, 0);
		break;
	case PT_BITLIST:
	{
		wxString aux = prop->GetValueAsString();
		aux.Replace(wxT("|"), wxT(", "));
		if (aux == wxT("0")) aux = wxT("");
		pgProp->SetValueFromString(aux, 0);
	}
	break;
	case PT_WXPOINT:
	{
		//m_pg->SetPropertyValue( pgProp, prop->GetValue() );
		wxString aux = prop->GetValueAsString();
		aux.Replace(wxT(","), wxT(";"));
		pgProp->SetValueFromString(aux, 0);
	}
	break;
	case PT_WXSIZE:
	{
		//m_pg->SetPropertyValue( pgProp, prop->GetValue() );
		wxString aux = prop->GetValueAsString();
		aux.Replace(wxT(","), wxT(";"));
		pgProp->SetValueFromString(aux, 0);
	}
	break;
	case PT_WXFONT:
		pgProp->SetValue(WXVARIANT(prop->GetValueAsString()));
		break;
	case PT_WXCOLOUR:
	{
		wxString value = prop->GetValueAsString();
		if (value.empty())  // Default Colour
		{
			wxColourPropertyValue def;
			def.m_type = wxSYS_COLOUR_WINDOW;
			def.m_colour = TypeConv::StringToSystemColour(wxT("wxSYS_COLOUR_WINDOW"));
			m_pg->SetPropertyValue(pgProp, def);
		}
		else
		{
			if (value.find_first_of(wxT("wx")) == 0)
			{
				// System Colour
				wxColourPropertyValue def;
				def.m_type = TypeConv::StringToSystemColour(value);
				def.m_colour = prop->GetValueAsColour();
				m_pg->SetPropertyValue(pgProp, WXVARIANT(def));
			}
			else
			{
				wxColourPropertyValue def(wxPG_COLOUR_CUSTOM, prop->GetValueAsColour());
				m_pg->SetPropertyValue(pgProp, WXVARIANT(def));
			}
		}
	}
	break;
	case PT_BITMAP:
		//      pgProp->SetValue( WXVARIANT( prop->GetValueAsString() ) );
		wxLogDebug(wxT("OI::OnPropertyModified: prop:%s"), prop->GetValueAsString().c_str());
		break;
	default:
		pgProp->SetValueFromString(prop->GetValueAsString(), wxPG_FULL_VALUE);
	}

	m_pg->Refresh();
}

wxPropertyGridManager* CObjectInspector::CreatePropertyGridManager(wxWindow *parent, wxWindowID id)
{
	int pgStyle;
	int defaultDescBoxHeight;

	switch (m_style)
	{
	case wxOES_OI_MULTIPAGE_STYLE:
		pgStyle = wxPG_BOLD_MODIFIED | wxPG_SPLITTER_AUTO_CENTER | wxPG_TOOLBAR | wxPG_DESCRIPTION | wxPGMAN_DEFAULT_STYLE;
		defaultDescBoxHeight = 50;
		break;

	case wxOES_OI_DEFAULT_STYLE:
	case wxOES_OI_SINGLE_PAGE_STYLE:
	default:
		pgStyle = wxPG_BOLD_MODIFIED | wxPG_SPLITTER_AUTO_CENTER | wxPG_DESCRIPTION | wxPGMAN_DEFAULT_STYLE;
		defaultDescBoxHeight = 150;
		break;
	}

	int descBoxHeight;
	wxConfigBase* config = wxConfigBase::Get();
	config->Read(wxT("/mainFrame/objectInspector/DescBoxHeight"), &descBoxHeight, defaultDescBoxHeight);
	if (-1 == descBoxHeight) {
		descBoxHeight = defaultDescBoxHeight;
	}

	wxPropertyGridManager* pg = new wxPropertyGridManager(parent, id, wxDefaultPosition, wxDefaultSize, pgStyle);
	pg->SendSizeEvent();
	pg->SetDescBoxHeight(descBoxHeight);

	return pg;
}

void CObjectInspector::OnBitmapPropertyChanged(wxCommandEvent& event)
{
	wxLogDebug(wxT("OI::BitmapPropertyChanged: %s"), event.GetString().c_str());

	wxString propName = event.GetString().BeforeFirst(':');
	wxString propVal = event.GetString().AfterFirst(':');

	if (!propVal.IsEmpty())
	{
		wxPGBitmapProperty *bp = wxDynamicCast(m_pg->GetPropertyByLabel(propName), wxPGBitmapProperty);
		if (bp) {
			bp->UpdateChildValues(propVal);
		}
	}
}

bool CObjectInspector::ModifyProperty(Property *prop, const wxString& strValue)
{
	wxString oldValue = prop->GetValue();

	if (m_evthandler) {
		prop->SetValue(strValue);
		if (m_currentSel->OnPropertyChanging(prop, oldValue)) {
			wxFramePropertyEvent event(wxEVT_PROPERTY_MODIFIED, prop, oldValue);
			if (m_evthandler->ProcessEvent(event)) {
				m_currentSel->SaveProperty();
				m_currentSel->OnPropertyChanged(prop);
				return true;
			}
		}

		prop->SetValue(oldValue);
		return false;
	}
	else
	{
		prop->SetValue(strValue);
		if (m_currentSel->OnPropertyChanging(prop, oldValue)) {
			m_currentSel->SaveProperty();
			m_currentSel->OnPropertyChanged(prop);
			return true;
		}
		return false;
	}

	return false;
}

void CObjectInspector::OnChildFocus(wxChildFocusEvent&) {
	// do nothing to avoid "scrollbar jump" if wx2.9 is used
}

void CObjectInspector::OnPropertyGridItemSelected(wxPropertyGridEvent& event)
{
	wxPGProperty *propPtr = event.GetProperty();
	if (propPtr)
	{
		m_strSelPropItem = m_pg->GetPropertyName(propPtr);

		std::map< wxPGProperty*, Property*>::iterator it = m_propMap.find(propPtr);

		if (m_propMap.end() == it)
		{
			// Could be a child property
			propPtr = propPtr->GetParent();
			it = m_propMap.find(propPtr);
		}

		if (m_currentSel && it != m_propMap.end()) {
			m_currentSel->OnPropertySelected(it->second);
		}
	}
}

void CObjectInspector::RestoreLastSelectedPropItem()
{
	wxPGProperty* p = m_pg->GetPropertyByName("name");
	if (p) {
		m_pg->SelectProperty(p, false);
	}
}
