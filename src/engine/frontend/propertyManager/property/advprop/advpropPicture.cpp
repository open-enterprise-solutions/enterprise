#include "advpropPicture.h"

#include "backend/propertyManager/property/propertyPicture.h"
#include "backend/propertyManager/property/variant/variantPicture.h"

#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"

// -----------------------------------------------------------------------
// wxPGPictureProperty
// -----------------------------------------------------------------------

#if wxCHECK_VERSION(3, 1, 0)
wxPG_IMPLEMENT_PROPERTY_CLASS(wxPGPictureProperty, wxPGProperty, TextCtrl)
#else
WX_PG_IMPLEMENT_PROPERTY_CLASS(wxPGPictureProperty, wxPGProperty, wxString, const wxString&, TextCtrl)
#endif

// register frontend property 
class ibPropertyPictureLoader
{
public:
	ibPropertyPictureLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyPicture* prop) -> wxPGProperty* {
			return new wxPGPictureProperty(prop->GetLabel(), prop->GetName(), prop->GetValue());
		});
		ibPropertyRegistry::Register([](ibPropertyExternalPicture* prop) -> wxPGProperty* {
			return new ibPGExternalImageProperty(prop->GetLabel(), prop->GetName(), prop->GetValue());
		});
	}
}g_pictureLoader;

static int s_indFilter = -1;

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaPictureObject.h"

wxPGPictureProperty::wxPGPictureProperty(const wxString& label,
	const wxString& strName,
	const wxVariant& value) : wxPGProperty(label, strName),
	m_propertySource(nullptr),
	m_propertyBackend(nullptr), m_propertyConfiguration(nullptr), m_propertyFile(nullptr)
{
	ibVariantDataPicture* pictureVariant = property_cast(value, ibVariantDataPicture);
	wxASSERT(pictureVariant);

	wxPGChoices sourceChoices, systemChoices, configurationChoices;

	// Add 'source' property (common for all other children)
	sourceChoices.Add(_("Load from backend"), eFromBackend);
	sourceChoices.Add(_("Load from file"), eFromFile);

	unsigned int system_value = 0, system_config = 0;

	wxPGChoiceEntry& entry =
		systemChoices.AddAsSorted(wxEmptyString, system_value++);

	m_valChoices.insert_or_assign(entry.GetValue(), 0);

	for (auto so : ibBackendPicture::GetArrayPicture()) {
		wxPGChoiceEntry& entry = systemChoices.AddAsSorted(so.m_name, system_value++);
		entry.SetBitmap(so.m_data);
		m_valChoices.insert_or_assign(
			entry.GetValue(), so.m_id
		);
	}

	const ibPropertyObject* ownerValue = pictureVariant->GetOwner();
	if (ownerValue != nullptr) {

		const ibMetaData* metaData = ownerValue->GetMetaData();

		wxPGChoiceEntry& entry = configurationChoices.AddAsSorted(wxEmptyString, system_config++);
		m_confChoices.insert_or_assign(entry.GetValue(), wxNullGuid);

		ibMetaData* metaDataOwner = nullptr;
		if (metaData != nullptr && metaData->GetOwner(metaDataOwner)) {
			for (const auto object : metaDataOwner->GetAnyArrayObject(g_metaPictureCLSID)) {
				ibValueMetaObjectPicture* picture = nullptr;
				if (object->ConvertToValue(picture)) {
					wxPGChoiceEntry& entry = configurationChoices.AddAsSorted(
						object->GetName(), system_config++);
					entry.SetBitmap(picture->GetValueAsBitmap());
					m_confChoices.insert_or_assign(
						entry.GetValue(), picture->GetGuid()
					);
				}
			}
		}
		else if (metaData != nullptr) {
			for (const auto object : metaData->GetAnyArrayObject(g_metaPictureCLSID)) {
				ibValueMetaObjectPicture* picture = nullptr;
				if (object->ConvertToValue(picture)) {
					wxPGChoiceEntry& entry = configurationChoices.AddAsSorted(
						object->GetName(), system_config++);
					entry.SetBitmap(picture->GetValueAsBitmap());
					m_confChoices.insert_or_assign(
						entry.GetValue(), picture->GetGuid()
					);
				}
			}
		}

		sourceChoices.Add(_("Load from configuration"), eFromConfiguration);
	}

	m_propertySource = new wxEnumProperty(_("Source"), wxT("source"), sourceChoices);
	m_propertySource->SetHelpString(_("Select a source picture"));

	AddPrivateChild(m_propertySource);

	m_propertyBackend = new wxEditEnumProperty(_("System"), wxT("system"), systemChoices);
	m_propertyBackend->SetHelpString(_("Image from engine collections"));

	AddPrivateChild(m_propertyBackend);

	// Add 'configuration' property (common for all other children)
	m_propertyConfiguration = new wxEditEnumProperty(_("Configuration"), wxT("configuration"), configurationChoices);
	m_propertyConfiguration->SetHelpString(_("Image from configuration"));

	AddPrivateChild(m_propertyConfiguration);

	// Add 'file_path' property (common for 'Load From File' and 'Load From Embedded File' choices)
	m_propertyFile = new ibPGExternalImageProperty(_("File path"), wxT("file_path"), pictureVariant->CloneExternalPicture());
	m_propertyFile->SetHelpString(_("Path to the image file."));

	AddPrivateChild(m_propertyFile);

	//Set current value
	SetValue(value);
}

wxVariant wxPGPictureProperty::ChildChanged(wxVariant& thisValue, const int childIndex,
	wxVariant& childValue) const
{
	ibVariantDataPicture* pictureVariant = property_cast(thisValue, ibVariantDataPicture);

	if (pictureVariant != nullptr) {

		ibVariantDataPicture* clonedPictureVariant = pictureVariant->Clone();
		wxASSERT(clonedPictureVariant);

		switch (childIndex)
		{
		case 0:
			clonedPictureVariant->SetPictureType(static_cast<ibPictureType>(childValue.GetLong()));
			break;
		case 1:
			clonedPictureVariant->SetFromBackendPicture(m_valChoices.at(childValue.GetLong()));
			break;
		case 2:
			clonedPictureVariant->SetFromConfiguraion(m_confChoices.at(childValue.GetLong()));
			break;
		case 3:
			ibVariantDataExternalPicture * externalPicture = property_cast(childValue, ibVariantDataExternalPicture);
			wxASSERT(externalPicture);
			clonedPictureVariant->SetFromExternalFile(externalPicture->GetExternalPictureDesc());
			break;
		}

		return clonedPictureVariant;
	}

	return wxNullVariant;
}

void wxPGPictureProperty::RefreshChildren()
{
	ibVariantDataPicture* pictureVariant = property_cast(m_value, ibVariantDataPicture);

	if (pictureVariant != nullptr) {

		m_propertySource->Hide(false);
		m_propertySource->SetExpanded(true);

		const ibPictureDescription& pd = pictureVariant->GetPictureDesc();

		if (pd.m_type == ibPictureType::eFromBackend) {
			m_propertyBackend->Hide(false);
			m_propertyBackend->SetExpanded(true);
			m_propertyConfiguration->Hide(true);
			m_propertyConfiguration->SetExpanded(false);
			m_propertyFile->Hide(true);
			m_propertyFile->SetExpanded(false);
		}
		else if (pd.m_type == ibPictureType::eFromConfiguration) {
			m_propertyBackend->Hide(true);
			m_propertyBackend->SetExpanded(false);
			m_propertyConfiguration->Hide(false);
			m_propertyConfiguration->SetExpanded(true);
			m_propertyFile->Hide(true);
			m_propertyFile->SetExpanded(false);
		}
		else if (pd.m_type == ibPictureType::eFromFile) {
			m_propertyBackend->Hide(true);
			m_propertyBackend->SetExpanded(false);
			m_propertyConfiguration->Hide(true);
			m_propertyConfiguration->SetExpanded(false);
			m_propertyFile->Hide(false);
			m_propertyFile->SetExpanded(true);
		}

		m_propertySource->SetValue(pd.m_type);

		if (pd.m_type == ibPictureType::eFromBackend) {
			auto iterator = std::find_if(m_valChoices.begin(), m_valChoices.end(),
				[pd](const auto pair) { return pair.second == pd.m_class_identifier; });
			if (iterator != m_valChoices.end())
				m_propertyBackend->SetValue(iterator->first);
			else
				m_propertyBackend->SetValue(wxNOT_FOUND);
		}
		else if (pd.m_type == ibPictureType::eFromConfiguration) {
			auto iterator = std::find_if(m_confChoices.begin(), m_confChoices.end(),
				[pd](const auto pair) { return pair.second == pd.m_meta_guid; });
			if (iterator != m_confChoices.end())
				m_propertyConfiguration->SetValue(iterator->first);
			else
				m_propertyConfiguration->SetValue(wxNOT_FOUND);
		}
		else if (pd.m_type == ibPictureType::eFromFile) {
			m_propertyFile->SetValue(new ibVariantDataExternalPicture(pd.m_img_data));
		}
	}
	else {

		m_propertySource->Hide(true);
		m_propertySource->SetExpanded(false);

		m_propertyBackend->Hide(true);
		m_propertyBackend->SetExpanded(false);

		m_propertyConfiguration->Hide(true);
		m_propertyConfiguration->SetExpanded(false);

		m_propertyFile->Hide(true);
		m_propertyFile->SetExpanded(false);
	}

	wxPGPictureProperty::SetExpanded(true);
}

// -----------------------------------------------------------------------
// ibPGExternalImageProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGExternalImageProperty, wxEditorDialogProperty, TextCtrlAndButton)

const wxString& wxPGGetDefaultImageWildcard(int& indFilter)
{
	// Form the wildcard, if not done yet
	if (wxPGGlobalVars->m_pDefaultImageWildcard.empty()) {

		wxString str, ext; s_indFilter = 0;

		// TODO: This section may require locking (using global).
		wxList& handlers = wxImage::GetHandlers();

		wxList::iterator node;

		// Let's iterate over the image handler list.
		for (node = handlers.begin(); node != handlers.end(); ++node) {

			wxImageHandler* handler = (wxImageHandler*)*node;

			wxString ext_lo = handler->GetExtension();
			wxString ext_up = ext_lo.Upper();

			str.append(ext_up);
			str.append(wxS(" files (*."));
			str.append(ext_up);
			str.append(wxS(")|*."));
			str.append(ext_lo);
			str.append(wxS("|"));

			if (!ext.empty())
				ext.append(wxS(";"));
			ext.append("*." + ext_lo);

			s_indFilter++;
		}

		str.append(wxS("All image files|") + ext + "|");
		str.append(wxS("All files (*.*)|*.*"));

		wxPGGlobalVars->m_pDefaultImageWildcard = str;
	}

	indFilter = s_indFilter;
	return wxPGGlobalVars->m_pDefaultImageWildcard;
}

ibPGExternalImageProperty::ibPGExternalImageProperty(const wxString& label, const wxString& name, const wxVariant& value)
	: wxEditorDialogProperty(label, name)
{
	m_flags |= wxPGPropertyFlags_ShowFullFileName;
	m_flags &= ~wxPGPropertyFlags_ActiveButton; // Property button enabled only in not read-only mode.

	m_wildcard = wxPGGetDefaultImageWildcard(m_indFilter);

	SetValue(value);
}

void ibPGExternalImageProperty::OnCustomPaint(wxDC& dc,
	const wxRect& rect,
	wxPGPaintData& d)
{
	ibVariantDataExternalPicture* pictureVariant = property_cast(GetValue(), ibVariantDataExternalPicture);

	if (!pictureVariant->IsEmptyPicture()) {

		// Draw the thumbnail
		// Create the bitmap here because required size is not known in OnSetValue().

		// Delete the cache if required size changed
		if (m_bitmap.IsOk() && (m_bitmap.GetSize() != rect.GetSize())) {
			m_bitmap = wxNullBitmap;
		}

		m_bitmap = pictureVariant->GetPictureBitmap(wxSize(d.m_drawnWidth, d.m_drawnHeight));
	}
	else {
		m_bitmap = wxNullBitmap;
	}

	if (m_bitmap.IsOk()) {
		dc.DrawBitmap(m_bitmap, rect.x, rect.y, false);
	}
	else {
		// No file - just draw a white box
		dc.SetBrush(*wxWHITE_BRUSH);
		dc.DrawRectangle(rect);
	}
}

bool ibPGExternalImageProperty::StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const
{
	if (text.IsEmpty()) {
		variant = new ibVariantDataExternalPicture();
		return true;
	}

	return false;
}

bool ibPGExternalImageProperty::DisplayEditorDialog(wxPropertyGrid* pg, wxVariant& value)
{
	ibVariantDataExternalPicture* externalPicture = property_cast(value, ibVariantDataExternalPicture);
	wxASSERT_MSG(externalPicture != nullptr, "Function called for incompatible property");

	wxFileName filename(externalPicture->GetPictureFileName());

	wxString path = filename.GetPath();
	wxString file = filename.GetFullName();

	if (path.empty() && !m_basePath.empty())
		path = m_basePath;

	wxFileDialog dlg(pg->GetPanel(),
		m_dlgTitle.empty() ? _("Choose a picture") : m_dlgTitle,
		m_initialPath.empty() ? path : m_initialPath,
		file,
		m_wildcard.empty() ? _(wxALL_FILES) : m_wildcard,
		m_dlgStyle,
		wxDefaultPosition);

	if (m_indFilter >= 0) dlg.SetFilterIndex(m_indFilter);

	if (dlg.ShowModal() == wxID_OK) {

		// Get from FS
		ibExternalPictureDescription container;

		if (ibBackendPicture::LoadFromFile(dlg.GetPath(), container)) {
			value = new ibVariantDataExternalPicture(container);
			return true;
		}
	}

	return false;
}
