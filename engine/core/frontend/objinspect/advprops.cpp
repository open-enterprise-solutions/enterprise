#include "advprops.h"

#include "appData.h"
#include "utils/typeconv.h"
#include "metadata/objects/baseObject.h"

#include <wx/regex.h>

// -----------------------------------------------------------------------
// wxPGSizeProperty
// -----------------------------------------------------------------------
#if wxCHECK_VERSION(3, 1, 0)
wxPG_IMPLEMENT_PROPERTY_CLASS(wxPGSizeProperty, wxPGProperty, TextCtrl)
#else
WX_PG_IMPLEMENT_PROPERTY_CLASS(wxPGSizeProperty, wxPGProperty, wxSize, const wxSize&, TextCtrl)
#endif

wxPGSizeProperty::wxPGSizeProperty(const wxString& label,
	const wxString& name,
	const wxSize&   value) : wxPGProperty(label, name)
{
	DoSetValue(value);
	AddPrivateChild(new wxIntProperty(wxT("Width"), wxPG_LABEL, value.x));
	AddPrivateChild(new wxIntProperty(wxT("Height"), wxPG_LABEL, value.y));
}

void wxPGSizeProperty::RefreshChildren()
{
	if (GetChildCount() < 2) return;

	const wxSize& size = wxSizeRefFromVariant(m_value);

	Item(0)->SetValue((long)size.x);
	Item(1)->SetValue((long)size.y);
}

wxVariant wxPGSizeProperty::ChildChanged(wxVariant& thisValue, const int childIndex,
	wxVariant& childValue) const {
	wxSize& size = wxSizeRefFromVariant(thisValue);

	wxVariant();

	int val = childValue.GetLong();
	switch (childIndex)
	{
	case 0:
		size.x = val;
		break;
	case 1:
		size.y = val;
		break;
	}

	wxVariant newVariant;
	newVariant << size;
	return newVariant;
}

// -----------------------------------------------------------------------
// wxPGPointProperty
// -----------------------------------------------------------------------

#if wxCHECK_VERSION(3, 1, 0)
wxPG_IMPLEMENT_PROPERTY_CLASS(wxPGPointProperty, wxPGProperty, TextCtrl)
#else
WX_PG_IMPLEMENT_PROPERTY_CLASS(wxPGPointProperty, wxPGProperty, wxPoint, const wxPoint&, TextCtrl)
#endif

wxPGPointProperty::wxPGPointProperty(const wxString& label,
	const wxString& name,
	const wxPoint&  value) : wxPGProperty(label, name)
{
	DoSetValue(value);
	AddPrivateChild(new wxIntProperty(wxT("X"), wxPG_LABEL, value.x));
	AddPrivateChild(new wxIntProperty(wxT("Y"), wxPG_LABEL, value.y));
}

wxPGPointProperty::~wxPGPointProperty() { }

void wxPGPointProperty::RefreshChildren()
{
	if (GetChildCount() < 2) return;

	const wxPoint& point = wxPointRefFromVariant(m_value);

	Item(0)->SetValue((long)point.x);
	Item(1)->SetValue((long)point.y);
}

wxVariant wxPGPointProperty::ChildChanged(wxVariant& thisValue, const int childIndex,
	wxVariant& childValue) const {
	wxPoint& point = wxPointRefFromVariant(thisValue);

	wxVariant();

	int val = childValue.GetLong();
	switch (childIndex)
	{
	case 0:
		point.x = val;
		break;
	case 1:
		point.y = val;
		break;
	}

	wxVariant newVariant;
	newVariant << point;
	return newVariant;
}

// -----------------------------------------------------------------------
// wxPGBitmapProperty
// -----------------------------------------------------------------------

// static long gs_imageFilterIndex = -1; TODO: new wxPropertyGrid misses the
//                                             wxPG_FILE_FILTER_INDEX attribute ID
static wxString gs_imageInitialPath = wxEmptyString;

#if wxCHECK_VERSION(3, 1, 0)
wxPG_IMPLEMENT_PROPERTY_CLASS(wxPGBitmapProperty, wxPGProperty, TextCtrl)
#else
WX_PG_IMPLEMENT_PROPERTY_CLASS(wxPGBitmapProperty, wxPGProperty,
	wxString, const wxString&, TextCtrl)
#endif

	void wxPGBitmapProperty::GetChildValues(const wxString& parentValue, wxArrayString& childValues) const
{
	// some properties can contain value like "[-1;-1]" which must be modified due to use of ";" as a
	// string separator
	wxString values = parentValue;

	wxRegEx regex(wxT("\\[.+;.+\\]"));
	if (regex.IsValid())
	{
		if (regex.Matches(values))
		{
			wxString sizeVal = regex.GetMatch(values);
			sizeVal.Replace(wxT(";"), wxT("<semicolon>"));
			sizeVal.Replace(wxT("["), wxT(""));
			sizeVal.Replace(wxT("]"), wxT(""));
			regex.Replace(&values, sizeVal);
		}
	}

	childValues = wxStringTokenize(values, wxT(';'), wxTOKEN_RET_EMPTY_ALL);
	for (wxArrayString::iterator value = childValues.begin(); value != childValues.end(); ++value)
	{
		value->Trim(false);
		value->Replace(wxT("<semicolon>"), wxT(";"));
	}
}

wxPGBitmapProperty::wxPGBitmapProperty(const wxString& label,
	const wxString& name,
	const wxString& value) : wxPGProperty(label, name)
{
	SetValue(WXVARIANT(value));
}

void wxPGBitmapProperty::CreateChildren()
{
	wxString  propValue = m_value.GetString();
	wxVariant thisValue = WXVARIANT(propValue);
	wxVariant childValue;
	int       childIndex = 0;
	wxArrayString childVals;
	GetChildValues(propValue, childVals);
	wxString  source;
	if (childVals.Count() > 0)
	{
		source = childVals.Item(0);
	}
	else
	{
		source = _("Load From File");
	}
	prevSrc = -1;
	if (source == wxString(_("Load From Art Provider")))
	{
		childIndex = 1;
	}

	childValue = WXVARIANT(childIndex);

	CreatePropertySource(childIndex);

	ChildChanged(thisValue, 0, childValue);
}

wxPGProperty *wxPGBitmapProperty::CreatePropertySource(int sourceIndex)
{
	wxPGChoices sourceChoices;

	// Add 'source' property (common for all other children)
	sourceChoices.Add(_("Load From Art Provider"));

	wxPGProperty *srcProp = new wxEnumProperty(wxT("source"), wxPG_LABEL, sourceChoices, sourceIndex);
	srcProp->SetHelpString(
		wxString(_("Load From Art Provider:\n")) +
		wxString(_("Query registered providers for bitmap with given ID.\n\n")));
	AppendChild(srcProp);

	return srcProp;
}

wxPGProperty *wxPGBitmapProperty::CreatePropertyArtId()
{
	wxPGChoices artIdChoices;

	// Create 'id' property ('Load From Art Provider' only)
	artIdChoices.Add(wxT("wxART_ADD_BOOKMARK"));
	artIdChoices.Add(wxT("wxART_DEL_BOOKMARK"));
	artIdChoices.Add(wxT("wxART_HELP_SIDE_PANEL"));
	artIdChoices.Add(wxT("wxART_HELP_SETTINGS"));
	artIdChoices.Add(wxT("wxART_HELP_BOOK"));
	artIdChoices.Add(wxT("wxART_HELP_FOLDER"));
	artIdChoices.Add(wxT("wxART_HELP_PAGE"));
	artIdChoices.Add(wxT("wxART_GO_BACK"));
	artIdChoices.Add(wxT("wxART_GO_FORWARD"));
	artIdChoices.Add(wxT("wxART_GO_UP"));
	artIdChoices.Add(wxT("wxART_GO_DOWN"));
	artIdChoices.Add(wxT("wxART_GO_TO_PARENT"));
	artIdChoices.Add(wxT("wxART_GO_HOME"));
	artIdChoices.Add(wxT("wxART_FILE_OPEN"));
	artIdChoices.Add(wxT("wxART_FILE_SAVE"));
	artIdChoices.Add(wxT("wxART_FILE_SAVE_AS"));
	artIdChoices.Add(wxT("wxART_GOTO_FIRST"));
	artIdChoices.Add(wxT("wxART_GOTO_LAST"));
	artIdChoices.Add(wxT("wxART_PRINT"));
	artIdChoices.Add(wxT("wxART_HELP"));
	artIdChoices.Add(wxT("wxART_TIP"));
	artIdChoices.Add(wxT("wxART_REPORT_VIEW"));
	artIdChoices.Add(wxT("wxART_LIST_VIEW"));
	artIdChoices.Add(wxT("wxART_NEW_DIR"));
	artIdChoices.Add(wxT("wxART_HARDDISK"));
	artIdChoices.Add(wxT("wxART_FLOPPY"));
	artIdChoices.Add(wxT("wxART_CDROM"));
	artIdChoices.Add(wxT("wxART_REMOVABLE"));
	artIdChoices.Add(wxT("wxART_FOLDER"));
	artIdChoices.Add(wxT("wxART_FOLDER_OPEN"));
	artIdChoices.Add(wxT("wxART_GO_DIR_UP"));
	artIdChoices.Add(wxT("wxART_EXECUTABLE_FILE"));
	artIdChoices.Add(wxT("wxART_NORMAL_FILE"));
	artIdChoices.Add(wxT("wxART_TICK_MARK"));
	artIdChoices.Add(wxT("wxART_CROSS_MARK"));
	artIdChoices.Add(wxT("wxART_ERROR"));
	artIdChoices.Add(wxT("wxART_QUESTION"));
	artIdChoices.Add(wxT("wxART_WARNING"));
	artIdChoices.Add(wxT("wxART_INFORMATION"));
	artIdChoices.Add(wxT("wxART_MISSING_IMAGE"));
	artIdChoices.Add(wxT("wxART_COPY"));
	artIdChoices.Add(wxT("wxART_CUT"));
	artIdChoices.Add(wxT("wxART_PASTE"));
	artIdChoices.Add(wxT("wxART_DELETE"));
	artIdChoices.Add(wxT("wxART_NEW"));
	artIdChoices.Add(wxT("wxART_UNDO"));
	artIdChoices.Add(wxT("wxART_REDO"));
	artIdChoices.Add(wxT("wxART_PLUS"));
	artIdChoices.Add(wxT("wxART_MINUS"));
	artIdChoices.Add(wxT("wxART_CLOSE"));
	artIdChoices.Add(wxT("wxART_QUIT"));
	artIdChoices.Add(wxT("wxART_FIND"));
	artIdChoices.Add(wxT("wxART_FIND_AND_REPLACE"));
	artIdChoices.Add(wxT("wxART_FULL_SCREEN"));
	artIdChoices.Add(wxT("wxART_EDIT"));

	wxPGProperty *propArtId = new wxEditEnumProperty(wxT("id"), wxPG_LABEL, artIdChoices);
	propArtId->SetHelpString(_("Choose a wxArtID unique identifier of the bitmap or enter a wxArtID for your custom wxArtProvider. IDs with prefix 'gtk-' are available under wxGTK only."));

	return propArtId;
}

wxPGProperty *wxPGBitmapProperty::CreatePropertyArtClient()
{
	wxPGChoices artClientChoices;

	// Create 'client' property ('Load From Art Provider' only)
	artClientChoices.Add(wxT("wxART_TOOLBAR"));
	artClientChoices.Add(wxT("wxART_MENU"));
	artClientChoices.Add(wxT("wxART_BUTTON"));
	artClientChoices.Add(wxT("wxART_FRAME_ICON"));
	artClientChoices.Add(wxT("wxART_CMN_DIALOG"));
	artClientChoices.Add(wxT("wxART_HELP_BROWSER"));
	artClientChoices.Add(wxT("wxART_MESSAGE_BOX"));
	artClientChoices.Add(wxT("wxART_OTHER"));

	wxPGProperty *propArtClient = new wxEditEnumProperty(wxT("client"), wxPG_LABEL, artClientChoices);
	propArtClient->SetHelpString(_("Choose a wxArtClient identifier of the client (i.e. who is asking for the bitmap) or enter a wxArtClient for your custom wxArtProvider."));

	return propArtClient;
}

wxPGBitmapProperty::~wxPGBitmapProperty()
{
}

wxVariant wxPGBitmapProperty::ChildChanged(wxVariant& thisValue, const int childIndex,
	wxVariant& childValue) const {
	wxPGBitmapProperty* bp = (wxPGBitmapProperty*)this;

	wxString val = thisValue.GetString();
	wxArrayString childVals;
	GetChildValues(val, childVals);
	wxString newVal = val;

	// Find the appropriate new state
	switch (childIndex)
	{
		// source
	case 0:
	{
		unsigned int count = GetChildCount();

		if (prevSrc != 4)
		{
			for (unsigned int i = 1; i < count; i++)
			{
				wxPGProperty *p = Item(i);
				if (p)
				{
					wxLogDebug(wxT("wxOESBP::ChildChanged: Removing:%s"), p->GetLabel().c_str());
					GetGrid()->DeleteProperty(p);
				}
			}
			bp->AppendChild(bp->CreatePropertyArtId());
			bp->AppendChild(bp->CreatePropertyArtClient());
		}

		if (childVals.GetCount() == 3)
			newVal = childVals.Item(0) + wxT("; ") + childVals.Item(1) + wxT("; ") + childVals.Item(2);
		else if (childVals.GetCount() > 1)
			newVal = childVals.Item(0) + wxT("; ; ");
		break;
	}

	// file_path || id || resource_name
	case 1:
	{
		if (Item(0)->GetValueAsString() == _("Load From Art Provider")) {

			Item(1)->SetValue(childValue);

			if (childVals.GetCount() == 3)
				newVal = childVals.Item(0) + wxT("; ") + Item(1)->GetValueAsString() + wxT("; ") + Item(2)->GetValueAsString();
			else if (childVals.GetCount() > 1)
				newVal = childVals.Item(0) + wxT("; ; ");
		}
		break;
	}
	case 2:

		Item(2)->SetValue(childValue);

		if (childVals.GetCount() == 3)
			newVal = childVals.Item(0) + wxT("; ") + Item(1)->GetValueAsString() + wxT("; ") + Item(2)->GetValueAsString();
		else if (childVals.GetCount() > 1)
			newVal = childVals.Item(0) + wxT("; ; ");

		break;
	}

	bp->SetPrevSource(childValue.GetInteger());

	if (newVal != val)
	{
		wxVariant ret = WXVARIANT(newVal);
		bp->SetValue(ret);

		return ret;
	}
	return thisValue;
}

void wxPGBitmapProperty::UpdateChildValues(const wxString& value)
{
	wxArrayString childVals;
	GetChildValues(value, childVals);

	if (childVals[0].Contains(_("Load From Art Provider")))
	{
		Item(0)->SetValue(childVals[0]);

		if (childVals.Count() > 1)
		{
			wxString img = childVals[1];
			Item(1)->SetValue(childVals[1]);
		}

		if (childVals.Count() > 2)
		{
			wxString img = childVals[2];
			Item(2)->SetValue(childVals[2]);
		}
	}
}

void wxPGBitmapProperty::OnSetValue()
{
}

wxString wxPGBitmapProperty::SetupImage(const wxString &imgPath)
{
	if (!imgPath.IsEmpty())
	{
		wxFileName imgName = wxFileName(imgPath);

		// Allow user to specify any file path he needs (even if it seemingly doesn't exist)
		if (!imgName.FileExists()) return imgPath;

		wxString   res = wxT("");
		wxImage    img = wxImage(imgPath);

		if (!img.IsOk()) return res;

		// Setup for correct file_path
		if (imgName.IsAbsolute())
		{
			return TypeConv::MakeRelativeURL(imgPath, appData->GetProjectPath());
		}
		else
		{
			imgName.MakeAbsolute(appData->GetProjectPath());

			if (!imgName.FileExists()) return res;
		}
	}
	return imgPath;
}

wxString wxPGBitmapProperty::SetupResource(const wxString &resName)
{
	wxString res = wxEmptyString;
	// Keep old value from an icon resource only
	if (resName.Contains(wxT(";")) && resName.Contains(wxT("[")))
	{
		return resName.BeforeFirst(wxT(';'));
	}
	else if (resName.Contains(wxT(";")))
	{
		return res;
	}
	return resName;
}

// -----------------------------------------------------------------------
// wxPGFontProperty
// -----------------------------------------------------------------------

#include <wx/fontdlg.h>
#include <wx/fontenum.h>

static const wxChar* gs_fp_es_family_labels[] = {
	wxT("Default"), wxT("Decorative"),
	wxT("Roman"), wxT("Script"),
	wxT("Swiss"), wxT("Modern"),
	wxT("Teletype"), wxT("Unknown"),
	(const wxChar*)NULL
};

static long gs_fp_es_family_values[] = {
	wxFONTFAMILY_DEFAULT, wxFONTFAMILY_DECORATIVE,
	wxFONTFAMILY_ROMAN, wxFONTFAMILY_SCRIPT,
	wxFONTFAMILY_SWISS, wxFONTFAMILY_MODERN,
	wxFONTFAMILY_TELETYPE, wxFONTFAMILY_UNKNOWN
};

static const wxChar* gs_fp_es_style_labels[] = {
	wxT("Normal"),
	wxT("Slant"),
	wxT("Italic"),
	(const wxChar*)NULL
};

static long gs_fp_es_style_values[] = {
	wxFONTSTYLE_NORMAL,
	wxFONTSTYLE_SLANT,
	wxFONTSTYLE_ITALIC
};

static const wxChar* gs_fp_es_weight_labels[] = {
	wxT("Normal"),
	wxT("Light"),
	wxT("Bold"),
	(const wxChar*)NULL
};

static long gs_fp_es_weight_values[] = {
	wxFONTWEIGHT_NORMAL,
	wxFONTWEIGHT_LIGHT,
	wxFONTWEIGHT_BOLD
};

#if wxCHECK_VERSION(3, 1, 0)
wxPG_IMPLEMENT_PROPERTY_CLASS(wxPGFontProperty, wxPGProperty, TextCtrlAndButton)
#else
WX_PG_IMPLEMENT_PROPERTY_CLASS(wxPGFontProperty, wxPGProperty,
	wxFont, const wxFont&, TextCtrlAndButton)
#endif


	wxPGFontProperty::wxPGFontProperty(const wxString& label, const wxString& name,
		const wxFontContainer& value)
	: wxPGProperty(label, name)
{
	SetValue(WXVARIANT(TypeConv::FontToString(value)));

	// Initialize font family choices list
	if (!wxPGGlobalVars->m_fontFamilyChoices)
	{
		wxFontEnumerator enumerator;
		enumerator.EnumerateFacenames();

		wxArrayString faceNames = enumerator.GetFacenames();

		faceNames.Sort();
		faceNames.Insert(wxEmptyString, 0);

		wxPGGlobalVars->m_fontFamilyChoices = new wxPGChoices(faceNames);
	}

	//wxString emptyString(wxEmptyString);

	AddPrivateChild(new wxIntProperty(_("Point Size"), wxT("Point Size"),
		value.m_pointSize));

	AddPrivateChild(new wxEnumProperty(_("Family"), wxT("Family"),
		gs_fp_es_family_labels, gs_fp_es_family_values,
		value.m_family));

	wxString faceName = value.m_faceName;
	// If font was not in there, add it now
	if (faceName.length() &&
		wxPGGlobalVars->m_fontFamilyChoices->Index(faceName) == wxNOT_FOUND)
		wxPGGlobalVars->m_fontFamilyChoices->AddAsSorted(faceName);

	wxPGProperty* p = new wxEnumProperty(_("Face Name"), wxT("Face Name"),
		*wxPGGlobalVars->m_fontFamilyChoices);

	p->SetValueFromString(faceName, wxPG_FULL_VALUE);

	AddPrivateChild(p);

	AddPrivateChild(new wxEnumProperty(_("Style"), wxT("Style"),
		gs_fp_es_style_labels, gs_fp_es_style_values, value.m_style));

	AddPrivateChild(new wxEnumProperty(_("Weight"), wxT("Weight"),
		gs_fp_es_weight_labels, gs_fp_es_weight_values, value.m_weight));

	AddPrivateChild(new wxBoolProperty(_("Underlined"), wxT("Underlined"),
		value.m_underlined));
}

wxPGFontProperty::~wxPGFontProperty() { }

void wxPGFontProperty::OnSetValue()
{
	// do nothing
}

wxString wxPGFontProperty::GetValueAsString(int argFlags) const
{
	return wxPGProperty::GetValueAsString(argFlags);
}

bool wxPGFontProperty::OnEvent(wxPropertyGrid* propgrid, wxWindow* WXUNUSED(primary),
	wxEvent& event)
{
	if (propgrid->IsMainButtonEvent(event))
	{
		// Update value from last minute changes

		wxFontData data;
		wxFont font = TypeConv::StringToFont(m_value.GetString());

		data.SetInitialFont(font);
		data.SetColour(*wxBLACK);

		wxFontDialog dlg(propgrid, data);
		if (dlg.ShowModal() == wxID_OK)
		{
			propgrid->EditorsValueWasModified();

			wxFontContainer fcont(dlg.GetFontData().GetChosenFont());

			wxVariant variant = WXVARIANT(TypeConv::FontToString(fcont));
			SetValueInEvent(variant);

			return true;
		}
	}
	return false;
}

void wxPGFontProperty::RefreshChildren()
{
	wxString fstr = m_value.GetString();
	wxFontContainer font = TypeConv::StringToFont(fstr);

	Item(0)->SetValue(font.m_pointSize);
	Item(1)->SetValue(font.m_family);
	Item(2)->SetValueFromString(font.m_faceName, wxPG_FULL_VALUE);
	Item(3)->SetValue(font.m_style);
	Item(4)->SetValue(font.m_weight);
	Item(5)->SetValue(font.m_underlined);
}

wxVariant
wxPGFontProperty::ChildChanged(wxVariant& thisValue, int ind, wxVariant& childValue) const
{
	wxFontContainer font = TypeConv::StringToFont(thisValue.GetString());

	if (ind == 0)
	{
		font.m_pointSize = childValue.GetLong();
	}
	else if (ind == 1)
	{
		int fam = childValue.GetLong();
		if (fam < wxFONTFAMILY_DEFAULT || fam > wxFONTFAMILY_TELETYPE) {
			fam = wxFONTFAMILY_DEFAULT;
		}
		font.m_family = static_cast<wxFontFamily>(fam);
	}
	else if (ind == 2)
	{
		wxString faceName;
		int faceIndex = childValue.GetLong();

		if (faceIndex >= 0)
			faceName = wxPGGlobalVars->m_fontFamilyChoices->GetLabel(faceIndex);

		font.m_faceName = faceName;
	}
	else if (ind == 3)
	{
		int st = childValue.GetLong();
		if (st != wxFONTSTYLE_NORMAL && st != wxFONTSTYLE_SLANT && st != wxFONTSTYLE_ITALIC) {
			st = wxFONTSTYLE_NORMAL;
		}
		font.m_style = static_cast<wxFontStyle>(st);
	}
	else if (ind == 4)
	{
		int wt = childValue.GetLong();
		if (wt != wxFONTWEIGHT_NORMAL &&
			wt != wxFONTWEIGHT_LIGHT &&
			wt != wxFONTWEIGHT_BOLD)
			wt = wxFONTWEIGHT_NORMAL;
		font.m_weight = static_cast<wxFontWeight>(wt);
	}
	else if (ind == 5)
	{
		font.m_underlined = childValue.GetBool();
	}

	thisValue = WXVARIANT(TypeConv::FontToString(font));

	return thisValue;
}

// -----------------------------------------------------------------------
// wxStringControlProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(wxStringControlProperty, wxStringProperty, TextCtrl)

wxString wxStringControlProperty::ValueToString(wxVariant& value, int argFlags) const
{
	wxString s = value.GetString();

	if (GetChildCount() && HasFlag(wxPG_PROP_COMPOSED_VALUE))
	{
		// Value stored in m_value is non-editable, non-full value
		if ((argFlags & wxPG_FULL_VALUE) ||
			(argFlags & wxPG_EDITABLE_VALUE) ||
			s.empty())
		{
			// Calling this under incorrect conditions will fail
			wxASSERT_MSG(argFlags & wxPG_VALUE_IS_CURRENT,
				wxS("Sorry, currently default wxPGProperty::ValueToString() ")
				wxS("implementation only works if value is m_value."));

			DoGenerateComposedValue(s, argFlags);
		}

		return s;
	}

	// If string is password and value is for visual purposes,
	// then return asterisks instead the actual string.
	if ((m_flags & wxPG_PROP_PASSWORD) && !(argFlags & (wxPG_FULL_VALUE | wxPG_EDITABLE_VALUE)))
		return wxString(wxS('*'), s.Length());

	return s;
}

#include "utils/stringUtils.h"

bool wxStringControlProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	int argFlags) const
{
	if (StringUtils::CheckCorrectName(text) > 0)
		return false;

	if (GetChildCount() && HasFlag(wxPG_PROP_COMPOSED_VALUE))
		return wxPGProperty::StringToValue(variant, text, argFlags);

	if (variant != text) {
		variant = text;
		return text.length() > 0;
	}

	return false;
}

// -----------------------------------------------------------------------
// wxEventControlProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(wxEventControlProperty, wxStringProperty, TextCtrlAndButton)

wxString wxEventControlProperty::ValueToString(wxVariant& value, int argFlags) const
{
	wxString s = value.GetString();

	if (GetChildCount() && HasFlag(wxPG_PROP_COMPOSED_VALUE))
	{
		// Value stored in m_value is non-editable, non-full value
		if ((argFlags & wxPG_FULL_VALUE) ||
			(argFlags & wxPG_EDITABLE_VALUE) ||
			s.empty())
		{
			// Calling this under incorrect conditions will fail
			wxASSERT_MSG(argFlags & wxPG_VALUE_IS_CURRENT,
				wxS("Sorry, currently default wxPGProperty::ValueToString() ")
				wxS("implementation only works if value is m_value."));

			DoGenerateComposedValue(s, argFlags);
		}

		return s;
	}

	// If string is password and value is for visual purposes,
	// then return asterisks instead the actual string.
	if ((m_flags & wxPG_PROP_PASSWORD) && !(argFlags & (wxPG_FULL_VALUE | wxPG_EDITABLE_VALUE)))
		return wxString(wxS('*'), s.Length());

	return s;
}

bool wxEventControlProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	int argFlags) const
{
	if (StringUtils::CheckCorrectName(text) > 0)
		return false;

	if (GetChildCount() && HasFlag(wxPG_PROP_COMPOSED_VALUE))
		return wxPGProperty::StringToValue(variant, text, argFlags);

	if (variant != text)
	{
		variant = text;
		return true;
	}

	return false;
}

wxPGEditorDialogAdapter *wxEventControlProperty::GetEditorDialog() const
{
	class wxPGEventAdapter : public wxPGEditorDialogAdapter {
	public:
		wxPGEventAdapter() : wxPGEditorDialogAdapter()
		{
		}

		virtual ~wxPGEventAdapter()
		{
		}

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			wxEventControlProperty* dlgProp = wxDynamicCast(prop, wxEventControlProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");
			wxString eventName = pg->GetUncommittedPropertyValue();
			if (eventName.IsEmpty()) {
				wxPGProperty *pgProp = pg->GetPropertyByLabel(wxT("name"));

				prop->SetValueFromString(
					(pgProp ? pgProp->GetDisplayedString() : wxEmptyString) + wxT("_") + prop->GetLabel());

				SetValue(prop->GetValue());
			}
			else {
				SetValue(eventName);
			}

			return true;
		}
	};

	return new wxPGEventAdapter();
}

// -----------------------------------------------------------------------
// wxPGToolActionProperty
// -----------------------------------------------------------------------

#include "metadata/metadata.h"
#include "frontend/visualView/controls/form.h"

#include "editors.h"

wxPG_IMPLEMENT_PROPERTY_CLASS(wxPGToolActionProperty, wxPGProperty, ComboBoxAndButton)

wxPGToolActionProperty::wxPGToolActionProperty(const wxString& label, const wxString& name, wxPGChoices &choices,
	const wxString& value, IObjectBase *currentObject) : wxPGProperty(label, name),
	m_currentObject(currentObject)
{
	wxVariant selValue = value; long selection = wxNOT_FOUND;
	SetChoices(choices);
	if (selValue.Convert(&selection)) {
		for (unsigned int i = 0; i < m_choices.GetCount(); i++) {
			int val = m_choices.GetValue(i);
			if (selection == val) {
				m_flags &= ~(wxPG_PROP_ACTIVE_BTN);
				SetValue(m_choices.GetLabel(i));
				m_toolData.SetNumber(val);
				return;
			}
		}
		m_toolData.SetString(value);
		m_flags |= wxPG_PROP_ACTIVE_BTN; // Property button always enabled.
		SetValue(value);
	}
	else {
		m_toolData.SetString(value);
		m_flags |= wxPG_PROP_ACTIVE_BTN; // Property button always enabled.
		SetValue(value);
	}
}

wxPGToolActionProperty::~wxPGToolActionProperty()
{
}

wxString wxPGToolActionProperty::ValueToString(wxVariant& value,
	int WXUNUSED(argFlags)) const
{
	return value;
}

bool wxPGToolActionProperty::StringToValue(wxVariant& variant, const wxString& text, int argFlags) const
{
	if (StringUtils::CheckCorrectName(text) > 0)
		return false;

	if (GetChildCount() && HasFlag(wxPG_PROP_COMPOSED_VALUE))
		return wxPGProperty::StringToValue(variant, text, argFlags);
	m_toolData.SetString(text);
	if (variant != text) {
		variant = text;
		return true;
	}

	return false;
}

bool wxPGToolActionProperty::IntToValue(wxVariant &value, int number, int argFlags) const
{
	value = m_choices.GetLabel(number);
	m_toolData.SetNumber(
		m_choices.GetValue(number)
	);
	return true;
}

void wxPGToolActionProperty::OnSetValue()
{
	if (m_toolData.IsCustomEvent()) {
		SetFlag(wxPG_PROP_ACTIVE_BTN); // Property button always enabled.
	}
	else {
		ClearFlag(wxPG_PROP_ACTIVE_BTN); // Property button always disabled.
	}
}

wxPGEditorDialogAdapter *wxPGToolActionProperty::GetEditorDialog() const
{
	class wxPGEventAdapter : public wxPGEditorDialogAdapter {
	public:
		wxPGEventAdapter() : wxPGEditorDialogAdapter()
		{
		}

		virtual ~wxPGEventAdapter()
		{
		}

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			wxPGToolActionProperty* dlgProp = wxDynamicCast(prop, wxPGToolActionProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");
			wxString eventName = pg->GetUncommittedPropertyValue();
			if (eventName.IsEmpty()) {
				wxPGProperty *pgProp = pg->GetPropertyByLabel(wxT("name"));

				prop->SetValueFromString(
					(pgProp ? pgProp->GetDisplayedString() : wxEmptyString) + wxT("_") + prop->GetLabel());

				SetValue(prop->GetValue());
			}
			else {
				SetValue(eventName);
			}

			return true;
		}
	};

	return new wxPGEventAdapter();
}

// -----------------------------------------------------------------------
// wxPGSourceProperty
// -----------------------------------------------------------------------

#include <wx/treectrl.h>

wxPG_IMPLEMENT_PROPERTY_CLASS(wxPGSourceProperty, wxPGProperty, TextCtrlAndButton)

wxPGSourceProperty::wxPGSourceProperty(const wxString& label, const wxString& name,
	const long& value, IObjectBase *currentObject) : wxPGProperty(label, name),
	m_currentObject(currentObject)
{
	m_flags |= wxPG_PROP_ACTIVE_BTN; // Property button always enabled.
	SetValue(value);
}

wxPGSourceProperty::~wxPGSourceProperty() {}

wxString wxPGSourceProperty::ValueToString(wxVariant& value,
	int WXUNUSED(argFlags)) const
{
	meta_identifier_t id = value.GetInteger();

	if (id == wxNOT_FOUND) {
		return _("<not selected>");
	}

	IMetadata *metaData = m_currentObject->GetMetaData();
	if (metaData) {
		IMetaObject *metaObject = metaData->GetMetaObject(id);
		if (metaObject) {
			if (metaObject->IsDeleted()) {
				value = (long)wxNOT_FOUND;
				return _("<not selected>");
			}

			return metaObject->GetName();
		}
	}

	return _("<not selected>");
}

bool wxPGSourceProperty::StringToValue(wxVariant& variant, const wxString& text, int argFlags) const
{
	if (text.IsEmpty()) {
		variant = (long)wxNOT_FOUND;
	}

	return text.IsEmpty();
}

extern wxImageList *GetImageList();

#define icon_attribute 219 
#define icon_table 217

wxPGEditorDialogAdapter *wxPGSourceProperty::GetEditorDialog() const
{
	class wxPGEventAdapter : public wxPGEditorDialogAdapter {

		class metaTreeItemData : public wxTreeItemData {
			const wxString m_nameProp;
			const meta_identifier_t m_id;
			const bool m_tableSection;
		public:
			metaTreeItemData(const wxString &nameProp, const meta_identifier_t &id,
				bool tableSection)
				: wxTreeItemData(), m_nameProp(nameProp), m_id(id), m_tableSection(tableSection)
			{
			};

			const wxString &GetPropName() const { return m_nameProp; }
			const meta_identifier_t &GetID() const { return m_id; }
			const bool IsTableSection() const { return m_tableSection; }
		};

	public:
		wxPGEventAdapter() : wxPGEditorDialogAdapter()
		{
		}

		virtual ~wxPGEventAdapter()
		{
		}

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			wxPGSourceProperty* dlgProp = wxDynamicCast(prop, wxPGSourceProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			IValueFrame *currentControl = wxDynamicCast(
				dlgProp->GetObject(), IValueFrame
			);

			if (!currentControl) {
				return false;
			}

			bool tableBox =
				currentControl->GetClassName() == wxT("tablebox");

			// launch editor dialog
			wxDialog* dlg = new wxDialog(pg, wxID_ANY, _("Choice source"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

			dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

			// Multi-line text editor dialog.
			const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;
			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);

			wxTreeCtrl* tc = new wxTreeCtrl(dlg, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_SINGLE | wxSUNKEN_BORDER);
		
			tc->SetImageList(::GetImageList());

			rowsizer->Add(tc, wxSizerFlags(1).Expand().Border(wxALL, spacing));
			topsizer->Add(rowsizer, wxSizerFlags(1).Expand());

			tc->SetDoubleBuffered(true);

			wxStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
			topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

			dlg->SetSizer(topsizer);
			topsizer->SetSizeHints(dlg);

			if (!wxPropertyGrid::IsSmallScreen()) {
				dlg->SetSize(400, 300);
				dlg->Move(pg->GetGoodEditorDialogPosition(prop, dlg->GetSize()));
			}

			tc->SetFocus();

			CValueForm *ownerForm = currentControl->GetOwnerForm();
			wxASSERT(ownerForm);
			IDataObjectSource *srcObject = ownerForm->GetSourceObject();
			if (srcObject) {
				CSourceExplorer srcExplorer = srcObject->GetSourceExplorer();
				metaTreeItemData *srcItemData = NULL;
				if (currentControl->FilterSource(srcExplorer, srcExplorer.GetMetaIDSource())) {
					if (srcExplorer.IsSelect()) {
						srcItemData = new metaTreeItemData(
							srcExplorer.GetSourceName(),
							srcExplorer.GetMetaIDSource(),
							srcExplorer.IsTableSection()
						);
					}
				}
				wxTreeItemId rootItem = NULL;
				if (srcExplorer.IsTableSection()) {
					rootItem = tc->AddRoot(srcExplorer.GetSourceName(), icon_table, icon_table, srcItemData);
				}
				else {
					rootItem = tc->AddRoot(srcExplorer.GetSourceName(), icon_attribute, icon_attribute, srcItemData);
				}
				for (unsigned int idx = 0; idx < srcExplorer.GetHelperCount(); idx++) {
					CSourceExplorer srcNextExplorer = srcExplorer.GetHelper(idx);
					metaTreeItemData *itemData = NULL;
					if (currentControl->FilterSource(srcNextExplorer, srcExplorer.GetMetaIDSource())) {
						if (srcNextExplorer.IsSelect()) {
							itemData = new metaTreeItemData(
								srcNextExplorer.GetSourceName(),
								srcNextExplorer.GetMetaIDSource(),
								srcNextExplorer.IsTableSection()
							);
						}
					}
					wxTreeItemId newItem = NULL;
					if (srcNextExplorer.IsTableSection()) {
						newItem = tc->AppendItem(rootItem, srcNextExplorer.GetSourceName(), icon_table, icon_table, itemData);
					}
					else {
						newItem = tc->AppendItem(rootItem, srcNextExplorer.GetSourceName(), icon_attribute, icon_attribute, itemData);
					}
					bool needDelete = itemData == NULL;
					if (srcNextExplorer.IsTableSection()) {
						tc->SetItemBold(newItem);
						for (unsigned int a = 0; a < srcNextExplorer.GetHelperCount(); a++) {
							CSourceExplorer srcColExplorer = srcNextExplorer.GetHelper(a);
							metaTreeItemData *itemTableData = NULL;
							if (currentControl->FilterSource(srcColExplorer, srcNextExplorer.GetMetaIDSource())) {
								itemTableData = new metaTreeItemData(
									srcColExplorer.GetSourceName(),
									srcColExplorer.GetMetaIDSource(),
									srcColExplorer.IsTableSection()
								);
								tc->AppendItem(newItem, srcColExplorer.GetSourceName(), icon_attribute, icon_attribute, itemTableData);
								needDelete = !currentControl->FilterSource(srcNextExplorer, srcNextExplorer.GetMetaIDSource());
							}
						}
					}

					if (needDelete) {
						tc->Delete(newItem);
					}
				}

				if (srcExplorer.IsTableSection()) {
					tc->SetItemBold(rootItem);
				}

				tc->Expand(rootItem);
			}

			tc->ExpandAll();

			int res = dlg->ShowModal();

			wxTreeItemId selItem = tc->GetSelection();
			if (selItem.IsOk()) {
				wxTreeItemData *dataItem = tc->GetItemData(selItem);
				if (dataItem
					&& res == wxID_OK) {
					metaTreeItemData *item = dynamic_cast<
						metaTreeItemData *>(dataItem);
					wxASSERT(item);
					if (tableBox == item->IsTableSection()) {
						prop->SetValueFromString(item->GetPropName());
						SetValue(item->GetID());
					}
					else {
						dlg->Destroy();
						return false;
					}
				}
				else if (!dataItem) {
					dlg->Destroy();
					return false;
				}
			}

			dlg->Destroy();
			return res == wxID_OK
				&& selItem.IsOk();
		}
	};

	return new wxPGEventAdapter();
}

// -----------------------------------------------------------------------
// wxStringControlProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(wxPGTypeSelectorProperty, wxEnumProperty, ChoiceAndButton)

wxString wxPGTypeSelectorProperty::ValueToString(wxVariant& value, int argFlags) const
{
	if (value.IsType(wxPG_VARIANT_TYPE_STRING))
		return value.GetString();

	int index = m_choices.Index(value.GetLong());

	if (index < 0)
		return wxEmptyString;

	return m_choices.GetLabel(index);
}

bool wxPGTypeSelectorProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	int argFlags) const
{
	int useIndex = -1;
	long useValue = 0;

	for (unsigned int i = 0; i < m_choices.GetCount(); i++)
	{
		const wxString& entryLabel = m_choices.GetLabel(i);
		if (text.CmpNoCase(entryLabel) == 0)
		{
			useIndex = (int)i;
			useValue = m_choices.GetValue(i);
			break;
		}
	}

	if (useIndex != GetIndex())
	{
		if (useIndex != -1)
		{
			variant = (long)useValue;
			return true;
		}

		variant.MakeNull();
	}

	return false;
}

wxPGEditorDialogAdapter *wxPGTypeSelectorProperty::GetEditorDialog() const
{
	class wxPGEventAdapter : public wxPGEditorDialogAdapter {

		class optionTreeItemData : public wxTreeItemData {
			const Option m_currOpt;
		public:
			optionTreeItemData(const Option &opt)
				: wxTreeItemData(), m_currOpt(opt)
			{
			};
			const Option &GetOption() const { return m_currOpt; }
		};

	public:
		wxPGEventAdapter() : wxPGEditorDialogAdapter()
		{
		}

		virtual ~wxPGEventAdapter()
		{
		}

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			wxPGTypeSelectorProperty* dlgProp = wxDynamicCast(prop, wxPGTypeSelectorProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			IObjectBase *currentObject = dlgProp->GetObject();

			// launch editor dialog
			wxDialog* dlg = new wxDialog(pg, wxID_ANY, _("Choice type"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

			dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

			// Multi-line text editor dialog.
			const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;
			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);

			wxTreeCtrl* tc = new wxTreeCtrl(dlg, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_HIDE_ROOT | wxTR_SINGLE | wxSUNKEN_BORDER);

			wxTreeItemId rootItem = tc->AddRoot(wxEmptyString);

			rowsizer->Add(tc, wxSizerFlags(1).Expand().Border(wxALL, spacing));
			topsizer->Add(rowsizer, wxSizerFlags(1).Expand());

			tc->SetDoubleBuffered(true);

			wxStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
			topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

			dlg->SetSizer(topsizer);
			topsizer->SetSizeHints(dlg);

			if (!wxPropertyGrid::IsSmallScreen()) {
				dlg->SetSize(400, 300);
				dlg->Move(pg->GetGoodEditorDialogPosition(prop, dlg->GetSize()));
			}

			tc->SetFocus();

			OptionList *opt_list = currentObject->GetTypelist();
			if (opt_list) {
				long m_intVal = dlgProp->GetValue();
				for (auto option : opt_list->GetOptions()) {
					optionTreeItemData *itemData = new optionTreeItemData(option);
					wxTreeItemId newItem = tc->AppendItem(rootItem, option.m_option,
						wxNOT_FOUND, wxNOT_FOUND,
						itemData);
					if (option.m_intVal == m_intVal) {
						tc->SelectItem(newItem);
					}
				}
			}

			tc->ExpandAll();

			int res = dlg->ShowModal();

			wxTreeItemId selItem = tc->GetSelection();
			if (selItem.IsOk()) {
				wxTreeItemData *dataItem = tc->GetItemData(selItem);
				if (dataItem
					&& res == wxID_OK) {
					Option item = dynamic_cast<
						optionTreeItemData *>(dataItem)->GetOption();
					prop->SetValueFromString(item.m_option);
					SetValue(item.m_intVal);
				}
			}

			wxDELETE(opt_list);
			dlg->Destroy();
			return res == wxID_OK
				&& selItem.IsOk();
		}
	};

	return new wxPGEventAdapter();
}
