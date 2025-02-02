#include "objinspect.h"
#include "backend/metaData.h"

#include "frontend/mainFrame/mainFrame.h"




#include <wx/config.h>
#include <wx/propgrid/propgrid.h>

enum {
	WXOES_PROPERTY_GRID = wxID_HIGHEST + 1000
};

// -----------------------------------------------------------------------
// IntList
// -----------------------------------------------------------------------

/**
 * Lista de enteros.
 */
class IntList {
	std::vector<int> m_ints; bool m_abs;
public:

	IntList(bool absolute_value = false) : m_abs(absolute_value) {}
	IntList(wxString value, bool absolute_value = false) : m_abs(absolute_value) {
		SetList(value);
	}

	unsigned int GetSize() { return (unsigned int)m_ints.size(); }
	int GetValue(unsigned int idx) { return m_ints[idx]; }

	void IntList::Add(int value) {
		m_ints.push_back(m_abs ? std::abs(value) : value);
	}

	void IntList::DeleteList() {
		m_ints.erase(m_ints.begin(), m_ints.end());
	}

	void IntList::SetList(const wxString& str) {
		DeleteList();
		wxStringTokenizer tkz(str, wxT(","));
		while (tkz.HasMoreTokens()) {
			long value;
			wxString token;
			token = tkz.GetNextToken();
			token.Trim(true);
			token.Trim(false);
			if (token.ToLong(&value))
				Add((int)value);
		}
	}

	wxString IntList::ToString() {
		wxString result;
		if (m_ints.size() > 0) {
			result = stringUtils::IntToStr(m_ints[0]);
			for (unsigned int i = 1; i < m_ints.size(); i++)
				result = result + wxT(",") + stringUtils::IntToStr(m_ints[i]);
		}

		return result;
	}
};

// -----------------------------------------------------------------------
// CObjectInspector
// -----------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(CObjectInspector, wxPanel)
EVT_PG_CHANGING(WXOES_PROPERTY_GRID, CObjectInspector::OnPropertyGridChanging)
EVT_PG_CHANGED(WXOES_PROPERTY_GRID, CObjectInspector::OnPropertyGridChanged)
EVT_PG_ITEM_COLLAPSED(WXOES_PROPERTY_GRID, CObjectInspector::OnPropertyGridExpand)
EVT_PG_ITEM_EXPANDED(WXOES_PROPERTY_GRID, CObjectInspector::OnPropertyGridExpand)
EVT_PG_SELECTED(WXOES_PROPERTY_GRID, CObjectInspector::OnPropertyGridItemSelected)
EVT_CHILD_FOCUS(CObjectInspector::OnChildFocus)
wxEND_EVENT_TABLE()

///////////////////////////////////////////////////////////////////////////////
// CObjectInspector
///////////////////////////////////////////////////////////////////////////////

CObjectInspector::CObjectInspector(wxWindow* parent, int id, int style)
	: wxPanel(parent, id), m_style(style), m_currentSel(nullptr)
{
	m_pg = CreatePropertyGridManager(this, WXOES_PROPERTY_GRID);

	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
	topSizer->Add(m_pg, 1, wxALL | wxEXPAND, 0);
	SetSizer(topSizer);

	Connect(wxID_ANY, wxEVT_OES_PROP_BITMAP_CHANGED, wxCommandEventHandler(CObjectInspector::OnBitmapPropertyChanged));
}

CObjectInspector::~CObjectInspector()
{
	Disconnect(wxID_ANY, wxEVT_OES_PROP_BITMAP_CHANGED, wxCommandEventHandler(CObjectInspector::OnBitmapPropertyChanged));
}

///////////////////////////////////////////////////////////////////////////////

void CObjectInspector::SavePosition()
{
	// Save Layout
	wxConfigBase* config = wxConfigBase::Get();
	config->Write(wxT("/mainFrame/objectInspector/DescBoxHeight"), m_pg->GetDescBoxHeight());
}

CObjectInspector* CObjectInspector::GetObjectInspector()
{
	return CDocMDIFrame::GetObjectInspector();
}

void CObjectInspector::Create(bool force)
{
	IPropertyObject* sel_obj = GetSelectedObject();

	if (sel_obj && (sel_obj != m_currentSel || force)) {

		m_pg->Freeze();
		m_currentSel = sel_obj;

		int pageNumber = m_pg->GetSelectedPage();

		wxString pageName;
		if (pageNumber != wxNOT_FOUND) {
			pageName = m_pg->GetPageName(pageNumber);
		}

		// Clear Property Grid Manager
		m_pg->Clear();

		m_propMap.clear();
		m_eventMap.clear();

		std::map<wxString, Property*> propMap, dummyPropMap;
		std::map<wxString, Event*> eventMap, dummyEventMap;

		// We create the categories with the properties of the object organized by "classes"
		CreateCategory(sel_obj->GetClassName(), sel_obj, propMap, false);

		IPropertyObject* parent = sel_obj->GetParent();

		if (parent) {
			if (parent->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
				CreateCategory(sel_obj->GetClassName(), parent, dummyPropMap, false);
			}
		}

		CreateCategory(sel_obj->GetClassName(), sel_obj, eventMap, true);

		if (parent) {
			if (parent->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
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

		m_currentSel->OnPropertyCreated();

		m_pg->Refresh();
		m_pg->Update();

		m_pg->Thaw();
	}

	if (m_currentSel != nullptr) {
		m_pg->Freeze();
		for (auto& prop : m_propMap)
			m_currentSel->OnPropertyRefresh(m_pg, prop.first, prop.second);
		for (auto event : m_eventMap)
			m_currentSel->OnEventRefresh(m_pg, event.first, event.second);
		for (auto prop : m_propMap) {
			wxPGProperty* property = prop.first;
			if (property != nullptr) {
				wxPGProperty* parentProperty = property->GetParent();
				if (parentProperty->IsCategory() &&
					parentProperty->IsVisible() != (!property->HasFlag(wxPG_PROP_HIDDEN))) {
					bool visible = false;
					for (unsigned int idx = 0; idx < parentProperty->GetChildCount(); idx++) {
						wxPGProperty* currChild = parentProperty->Item(idx);
						wxASSERT(currChild);
						if (!currChild->HasFlag(wxPG_PROP_HIDDEN))
							visible = true;
					}
					if (parentProperty->IsVisible() != visible)
						parentProperty->Hide(!visible);
				}
			}
		}
		m_pg->Thaw();
	}

	RestoreLastSelectedPropItem();
}

void CObjectInspector::ShowProperty()
{
	mainFrame->ShowProperty();
}

void CObjectInspector::ClearProperty()
{
	Freeze();

	m_currentSel = nullptr;

	// Clear Property Grid Manager
	m_pg->Clear();

	m_propMap.clear();
	m_eventMap.clear();

	m_pg->Refresh();
	m_pg->Update();

	Thaw();
}

wxPropertyGridManager* CObjectInspector::CreatePropertyGridManager(wxWindow* parent, wxWindowID id)
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

wxPGProperty* CObjectInspector::GetProperty(Property* prop)
{
	wxPGProperty* result = nullptr;
	wxString name = prop->GetName();
	wxString label = prop->GetLabel();
	PropertyType type = prop->GetType();
	wxVariant vTrue = wxVariant(true, wxT("true"));

	if (type == PT_INT)
	{
		result = new wxIntProperty(label, name, prop->GetValueAsInteger());
	}
	else if (type == PT_UINT)
	{
		result = new wxUIntProperty(label, name, (unsigned)prop->GetValueAsInteger());
	}
	else if (type == PT_WXNAME)
	{
		result = new wxStringControlProperty(label, name, prop->GetValueAsText());
	}
	else if (type == PT_WXSTRING)
	{
		result = new wxLongStringProperty(label, name, prop->GetValueAsText());
	}
	else if (type == PT_SOURCE)
	{
		result = new wxPGSourceDataProperty(label, name, prop->GetValue(),
			prop->GetObject());
	}
	else if (type == PT_TEXT)
	{
		result = new wxLongStringProperty(label, name, prop->GetValueAsString());
	}
	else if (type == PT_BOOL)
	{
		result = new wxBoolProperty(label, name, prop->GetValueAsString() == wxT("1"));
	}
	else if (type == PT_BITLIST)
	{
		OptionList* opt_list = prop->GetOptionList();
		assert(opt_list && opt_list->GetOptionCount() > 0);

		wxPGChoices constants;
		for (auto option : opt_list->GetOptions()) {
			constants.Add(option.m_name, option.m_intVal);
		}
		result = new wxFlagsProperty(label, name, constants, prop->GetValueAsInteger());
		// Workaround to set the help strings for individual members of a wxFlagsProperty
		wxFlagsProperty* flagsProp = dynamic_cast<wxFlagsProperty*>(result);
		if (nullptr != flagsProp) {
			for (unsigned int i = 0; i < flagsProp->GetItemCount(); i++) {
				wxPGProperty* itemProp = flagsProp->Item(i);
				for (auto option : opt_list->GetOptions()) {
					if (option.m_name == itemProp->GetLabel()) {
						m_pg->SetPropertyHelpString(itemProp, option.m_help);
					}
				}
			}
		}

		delete opt_list;
	}
	else if (type == PT_INTLIST || type == PT_UINTLIST)
	{
		result = new wxStringProperty(label, name, IntList(prop->GetValueAsString(), type == PT_UINTLIST).ToString());
	}
	else if (type == PT_OPTION || type == PT_EDIT_OPTION)
	{
		OptionList* opt_list = prop->GetOptionList();
		assert(opt_list && opt_list->GetOptionCount() > 0);

		wxString m_name;
		wxString m_help;

		unsigned int i = 0;
		wxPGChoices constants;

		for (auto option : opt_list->GetOptions()) {
			constants.Add(option.m_name, option.m_intVal);
			if (option.m_intVal == prop->GetValueAsInteger()) {
				// Save option and help
				m_name = option.m_name;
				m_help = option.m_help;
			}
		}
		wxDELETE(opt_list);
		if (type == PT_EDIT_OPTION) {
			result = new wxEditEnumProperty(label, name, constants);
		}
		else {
			result = new wxEnumProperty(label, name, constants);
		}
		result->SetValueFromString(m_name, 0);
		wxString help = prop->GetHelp();

		if (help.empty()) {
			help = m_name + wxT(":\n") + m_help;
		}
		else {
			help += wxT("\n\n") + m_name + wxT(":\n") + m_help;
		}

		result->SetHelpString(wxGetTranslation(help));
	}
	else if (type == PT_TYPE)
	{
		wxString m_name;
		wxString m_help;

		IPropertyObject* objectBase = prop->GetObject();
		wxASSERT(objectBase);

		result = new wxPGTypeSelectorProperty(label, name, prop->GetValue(),
			objectBase->GetSelectorDataType(),
			objectBase);

		wxString help = prop->GetHelp();

		if (help.empty()) {
			help = m_name + wxT(":\n") + m_help;
		}
		else {
			help += wxT("\n\n") + m_name + wxT(":\n") + m_help;
		}

		result->SetHelpString(wxGetTranslation(help));
	}
	else if (type == PT_OWNER)
	{
		wxString m_name;
		wxString m_help;

		result = new wxPGOwnerSelectorProperty(label, name, prop->GetValue(),
			prop->GetObject());

		wxString help = prop->GetHelp();

		if (help.empty()) {
			help = m_name + wxT(":\n") + m_help;
		}
		else {
			help += wxT("\n\n") + m_name + wxT(":\n") + m_help;
		}

		result->SetHelpString(wxGetTranslation(help));
	}
	else if (type == PT_RECORD)
	{
		wxString m_name;
		wxString m_help;

		result = new wxPGRecordSelectorProperty(label, name, prop->GetValue(),
			prop->GetObject());

		wxString help = prop->GetHelp();

		if (help.empty()) {
			help = m_name + wxT(":\n") + m_help;
		}
		else {
			help += wxT("\n\n") + m_name + wxT(":\n") + m_help;
		}

		result->SetHelpString(wxGetTranslation(help));
	}
	else if (type == PT_GENERATION)
	{
		wxString m_name;
		wxString m_help;

		result = new wxPGGenerationSelectorProperty(label, name, prop->GetValue(),
			prop->GetObject());

		wxString help = prop->GetHelp();

		if (help.empty()) {
			help = m_name + wxT(":\n") + m_help;
		}
		else {
			help += wxT("\n\n") + m_name + wxT(":\n") + m_help;
		}

		result->SetHelpString(wxGetTranslation(help));
	}
	else if (type == PT_WXPOINT)
	{
		result = new wxPGPointProperty(label, name, prop->GetValueAsPoint());
	}
	else if (type == PT_WXSIZE)
	{
		result = new wxPGSizeProperty(label, name, prop->GetValueAsSize());
	}
	else if (type == PT_WXFONT)
	{
		result = new wxPGFontProperty(label, name, typeConv::StringToFont(prop->GetValueAsString()));
	}
	else if (type == PT_WXCOLOUR)
	{
		wxString value = prop->GetValueAsString();
		if (value.empty())  // Default Colour
		{
			wxColourPropertyValue colProp;
			colProp.m_type = wxSYS_COLOUR_WINDOW;
			colProp.m_colour = typeConv::StringToSystemColour(wxT("wxSYS_COLOUR_WINDOW"));
			result = new wxSystemColourProperty(label, name, colProp);
		}
		else
		{
			if (value.find_first_of(wxT("wx")) == 0)
			{
				wxColourPropertyValue def; // System Colour
				def.m_type = typeConv::StringToSystemColour(value);
				result = new wxSystemColourProperty(label, name, def);
			}
			else
			{
				result = new wxSystemColourProperty(label, name, prop->GetValueAsColour());
			}
		}
	}
	else if (type == PT_PATH)
	{
		result = new wxDirProperty(label, name, prop->GetValueAsString());
	}
	else if (type == PT_FILE)
	{
		result = new wxFileProperty(label, name, prop->GetValueAsString());
	}
	else if (type == PT_BITMAP)
	{
		wxLogDebug(wxT("OI::GetProperty: prop:%s"), prop->GetValueAsString().c_str());

		result = new wxPGBitmapProperty(label, name, prop->GetValueAsString());
	}
	else if (type == PT_STRINGLIST)
	{
		result = new wxArrayStringProperty(label, name, prop->GetValueAsArrayString());
#if wxVERSION_NUMBER >= 2901
		wxVariant v("\"");
		result->DoSetAttribute(wxPG_ARRAY_DELIMITER, v);
#endif
	}
	else if (type == PT_NUMBER)
	{
		result = new wxNumberProperty(label, name, prop->GetValueAsNumber());
	}
	else // Unknown property
	{
		result = new wxStringProperty(label, name, prop->GetValueAsString());
		result->SetAttribute(wxPG_BOOL_USE_DOUBLE_CLICK_CYCLING, vTrue);
		wxLogError(_("Property type Unknown"));
	}

	result->Enable(prop->IsEditable());
	return result;
}

wxPGProperty* CObjectInspector::GetEvent(Event* event)
{
	wxPGProperty* result = nullptr;
	wxString name = event->GetName();
	wxString label = event->GetLabel();
	EventType type = event->GetType();

	if (type == ET_EVENT) {
		result = new wxEventControlProperty(
			event->GetName(), event->GetLabel(), event->GetValue()
		);
	}
	else if (type == ET_ACTION) {

		int selValue = wxNOT_FOUND;

		OptionList* opt_list = event->GetOptionList();

		wxString m_name;
		wxString m_label;

		const wxString& value = event->GetValue();
		if (!value.ToInt(&selValue))
			selValue = wxNOT_FOUND;

		wxPGChoices constants;

		for (auto option : opt_list->GetOptions()) {
			if (option.m_intVal == wxNOT_FOUND)
				continue;
			constants.Add(option.m_name, option.m_intVal);
			if (option.m_intVal == selValue) {
				// Save option and help
				m_name = option.m_name;
				m_label = option.m_label;
			}
		}

		wxDELETE(opt_list);
		result = new wxPGToolActionProperty(label, name, constants, event->GetValue(),
			event->GetObject());
	}
	return result;
}

bool CObjectInspector::ModifyProperty(Property* prop, const wxVariant& newValue)
{
	const wxVariant oldValue = prop->GetValue();
	if (m_currentSel->OnPropertyChanging(prop, newValue)) {
		prop->SetValue(newValue);
		m_currentSel->OnPropertyChanged(prop, oldValue, newValue);
		return true;
	}
	return false;
}

bool CObjectInspector::ModifyEvent(Event* event, const wxVariant& newValue)
{
	const wxVariant oldValue = event->GetValue();
	if (m_currentSel->OnEventChanging(event, newValue)) {
		event->SetValue(newValue);
		m_currentSel->OnEventChanged(event, oldValue, newValue);
		return true;
	}
	return false;
}
///////////////////////////////////////////////////////////////////////////////



void CObjectInspector::GenerateSynonym(const wxString& systemName)
{
	auto& it = std::find_if(m_propMap.begin(), m_propMap.end(), [](std::pair<wxPGProperty*, Property*> pair) {
		return pair.second->GetName() == wxT("synonym") || pair.second->GetName() == wxT("caption");
		}
	);

	if (it == m_propMap.end())
		return;

	if (systemName.IsEmpty())
		return;

	wxPGProperty* propGrid = it->first;
	Property* prop = it->second;

	const wxString& newSynonym =
		stringUtils::GenerateSynonym(systemName);

	if (ModifyProperty(prop, newSynonym)) {
		propGrid->SetValueFromString(newSynonym);
	}
}

///////////////////////////////////////////////////////////////////////////////

void CObjectInspector::OnPropertyGridChanging(wxPropertyGridEvent& event)
{
	wxImageFileProperty* imgFileProp = wxDynamicCast(event.GetProperty(), wxImageFileProperty);

	if (imgFileProp != nullptr) {
		// GetValue() returns the pending value, but is only supported by wxEVT_PG_CHANGING.
		wxPGBitmapProperty* bmpProp = wxDynamicCast(imgFileProp->GetParent(), wxPGBitmapProperty);

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

	if (m_propMap.end() == itProperty) {
		// Could be a child property
		propPtr = propPtr->GetParent();
		itProperty = m_propMap.find(propPtr);
	}

	const wxVariant& newValue = event.GetPropertyValue();

	if (itProperty != m_propMap.end()) {
		Property* prop = itProperty->second;
		switch (prop->GetType())
		{
		case PT_NUMBER:
		{
			// Use typeconv to properly handle locale
			double val = newValue;
			if (!ModifyProperty(prop, typeConv::NumberToString(val)))
				event.Veto();
			break;
		}
		case PT_TEXT:
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
			int value = newValue.GetInteger();
			// Update displayed description for the new selection
			OptionList* opt_list = prop->GetOptionList();
			assert(opt_list && opt_list->GetOptionCount() > 0);
			if (opt_list->HasValue(value)) {
				wxString valStr;
				valStr << value;
				if (!ModifyProperty(prop, valStr))
					event.Veto();
			}
			wxString helpString = prop->GetHelp();
			wxString localized = wxGetTranslation(helpString);
			m_pg->SetPropertyHelpString(propPtr, localized);
			m_pg->SetDescription(propPtr->GetLabel(), localized);

			wxDELETE(opt_list); break;
		}
		case PT_TYPE:
		{
			// Update displayed description for the new selection
			wxString helpString = prop->GetHelp();
			if (!ModifyProperty(prop, newValue))
				event.Veto();
			wxString localized = wxGetTranslation(helpString);
			m_pg->SetPropertyHelpString(propPtr, localized);
			m_pg->SetDescription(propPtr->GetLabel(), localized);
			break;
		}
		case PT_RECORD:
		{
			// Update displayed description for the new selection
			wxString helpString = prop->GetHelp();

			if (!ModifyProperty(prop, newValue))
				event.Veto();

			wxString localized = wxGetTranslation(helpString);
			m_pg->SetPropertyHelpString(propPtr, localized);
			m_pg->SetDescription(propPtr->GetLabel(), localized);
			break;
		}
		case PT_OWNER:
		{
			// Update displayed description for the new selection
			wxString helpString = prop->GetHelp();

			if (!ModifyProperty(prop, newValue))
				event.Veto();

			wxString localized = wxGetTranslation(helpString);
			m_pg->SetPropertyHelpString(propPtr, localized);
			m_pg->SetDescription(propPtr->GetLabel(), localized);
			break;
		}
		case PT_GENERATION:
		{
			// Update displayed description for the new selection
			wxString helpString = prop->GetHelp();

			if (!ModifyProperty(prop, newValue))
				event.Veto();

			wxString localized = wxGetTranslation(helpString);
			m_pg->SetPropertyHelpString(propPtr, localized);
			m_pg->SetDescription(propPtr->GetLabel(), localized);
			break;
		}
		case PT_SOURCE:
		{
			if (!ModifyProperty(prop, newValue))
				event.Veto();
			break;
		}
		case PT_WXNAME:
		case PT_WXSTRING:
		{
			// CObjectInspector's text strings are formatted.
			const wxString& value = typeConv::TextToString(newValue);
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
				if (!ModifyProperty(prop, typeConv::ColourToString(colour.m_colour)))
					event.Veto();
				break;
			default:
				if (!ModifyProperty(prop, typeConv::SystemColourToString(colour.m_type)))
					event.Veto();
			}
			break;
		}
		case PT_INTLIST:
		case PT_UINTLIST: {
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
			const wxString& bmpVal = propPtr->GetValueAsString(wxPG_FULL_VALUE);

			// Handle changes in values, as needed
			wxVariant thisValue = WXVARIANT(bmpVal);

			wxVariant newVal =
				propPtr->ChildChanged(thisValue, (int)event.GetProperty()->GetIndexInParent(), childValue);

			if (!ModifyProperty(prop, newVal.GetString()))
				event.Veto();

			if (event.GetProperty()->GetIndexInParent() > 0) {
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
		Event* evt = itEvent->second;
		switch (evt->GetType()) {
		case ET_EVENT:
			if (!ModifyEvent(itEvent->second, newValue))
				event.Veto();
			break;

		case ET_ACTION: {
			wxPGToolActionProperty* toolProperty =
				wxDynamicCast(event.GetProperty(), wxPGToolActionProperty);
			wxASSERT(toolProperty);
			if (toolProperty->IsCustomEvent()) {
				const wxString& oldValue = evt->GetValue();
				if (!ModifyEvent(evt, toolProperty->GetString()))
					event.Veto();
			}
			else {
				wxString aux;
				aux << toolProperty->GetNumber();
				if (!ModifyEvent(evt, aux))
					event.Veto();
			};
			break;
		}
		}
	}
}

void CObjectInspector::OnPropertyGridChanged(wxPropertyGridEvent& event)
{
	if (m_currentSel != nullptr) {
		m_pg->Freeze();
		for (auto prop : m_propMap)
			m_currentSel->OnPropertyRefresh(m_pg, prop.first, prop.second);
		for (auto event : m_eventMap)
			m_currentSel->OnEventRefresh(m_pg, event.first, event.second);
		for (auto prop : m_propMap) {
			wxPGProperty* property = prop.first;
			if (property != nullptr) {
				wxPGProperty* parentProperty = property->GetParent();
				if (parentProperty->IsCategory() &&
					parentProperty->IsVisible() != (!property->HasFlag(wxPG_PROP_HIDDEN))) {
					bool visible = false;
					for (unsigned int idx = 0; idx < parentProperty->GetChildCount(); idx++) {
						wxPGProperty* currChild = parentProperty->Item(idx);
						wxASSERT(currChild);
						if (!currChild->HasFlag(wxPG_PROP_HIDDEN))
							visible = true;
					}
					if (parentProperty->IsVisible() != visible)
						parentProperty->Hide(!visible);
				}
			}
		}
		m_pg->Thaw();
	}
	event.Skip();
}

void CObjectInspector::OnPropertyGridExpand(wxPropertyGridEvent& event)
{
	m_isExpanded[event.GetPropertyName()] = event.GetProperty()->IsExpanded();

	wxPGProperty* egProp = m_pg->GetProperty(event.GetProperty()->GetName());
	if (egProp != nullptr) {
		if (event.GetProperty()->IsExpanded()) {
			m_pg->Expand(egProp);
		}
		else {
			m_pg->Collapse(egProp);
		}
	}
}

void CObjectInspector::OnPropertyGridItemSelected(wxPropertyGridEvent& event)
{
	wxPGProperty* propPtr = event.GetProperty();
	if (propPtr != nullptr) {
		m_strSelPropItem = m_pg->GetPropertyName(propPtr);
		std::map< wxPGProperty*, Property*>::iterator it = m_propMap.find(propPtr);
		if (m_propMap.end() == it) {
			// Could be a child property
			propPtr = propPtr->GetParent();
			it = m_propMap.find(propPtr);
		}
		if (m_currentSel && it != m_propMap.end()) {
			m_currentSel->OnPropertySelected(it->second);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void CObjectInspector::OnBitmapPropertyChanged(wxCommandEvent& event)
{
	wxLogDebug(wxT("OI::BitmapPropertyChanged: %s"), event.GetString().c_str());

	wxString strPropName = event.GetString().BeforeFirst(':');
	wxString propVal = event.GetString().AfterFirst(':');

	if (!propVal.IsEmpty()) {
		wxPGBitmapProperty* bp = wxDynamicCast(m_pg->GetPropertyByLabel(strPropName), wxPGBitmapProperty);
		if (bp != nullptr) {
			bp->UpdateChildValues(propVal);
		}
	}
}

void CObjectInspector::OnChildFocus(wxChildFocusEvent&) {
	// do nothing to avoid "scrollbar jump" if wx2.9 is used
}