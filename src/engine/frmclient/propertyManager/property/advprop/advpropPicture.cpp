#include "advpropPicture.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the picture's node
#include "frmclient/backend/backend_picture.h"                                        // ibBackendPicture — a file's picture, drawn

#include <wx/filename.h>

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — the row the configuration's pictures come with

#include "frmclient/win/picture.h"   // ibProtocolPicture — a configuration's picture, by its id

#include "protocol/protocol.h"   // ibProtocol::ibSchema::ibProperties — the row's keys

namespace {

// The picture a value holds, as the server stores it…
ibPictureDescription ReadPicture(const wxVariant& value)
{
	ibPictureDescription pictureDesc;
	ibPictureDescriptionMemory::ReadNode(ibVariantDataSerializable::GetNodeValue(value), pictureDesc);
	return pictureDesc;
}

// …and the value of another one: the same property's, read as where it comes from.
wxVariant WritePicture(const wxVariant& value, const ibPictureDescription& pictureDesc)
{
	ibDataValue node;
	ibPictureDescriptionMemory::WriteNode(node, pictureDesc);
	wxString words;
	if (pictureDesc.m_type == ibPictureType::eFromBackend)
		words = _("Backend picture");
	else if (pictureDesc.m_type == ibPictureType::eFromConfiguration)
		words = _("Configuration picture");
	else if (pictureDesc.m_type == ibPictureType::eFromFile)
		words = _("External picture");
	return wxVariant(new ibVariantDataSerializable(value.GetType(), node, words));
}

// A FILE'S PICTURE, carried whole — as the server stores it…
ibExternalPictureDescription ReadExternalPicture(const wxVariant& value)
{
	ibExternalPictureDescription pictureDesc;
	ibExternalPictureDescriptionMemory::ReadNode(ibVariantDataSerializable::GetNodeValue(value), pictureDesc);
	return pictureDesc;
}

// …and a value of it, of the type named: read as the file's name.
wxVariant WriteExternalPicture(const wxString& type, const ibExternalPictureDescription& pictureDesc)
{
	ibDataValue node;
	ibExternalPictureDescriptionMemory::WriteNode(node, pictureDesc);
	return wxVariant(new ibVariantDataSerializable(type, node,
		pictureDesc.IsEmptyPicture() ? wxString() : wxFileName(pictureDesc.m_img_name).GetFullName()));
}

}

// -----------------------------------------------------------------------
// wxPGPictureProperty
// -----------------------------------------------------------------------

#if wxCHECK_VERSION(3, 1, 0)
wxPG_IMPLEMENT_PROPERTY_CLASS(wxPGPictureProperty, wxPGProperty, TextCtrl)
#else
WX_PG_IMPLEMENT_PROPERTY_CLASS(wxPGPictureProperty, wxPGProperty, wxString, const wxString&, TextCtrl)
#endif

// register frontend property — each picture as the server stores it (ibPictureDescriptionMemory, the external one
// ibExternalPictureDescriptionMemory: the file's image carried whole), and back as one.
class ibPropertyPictureLoader
{
public:
	ibPropertyPictureLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyPicture"), [](ibBackendProperty* property) -> wxPGProperty* {
			// …and the configuration's pictures it may be loaded from, by name (the row's Pictures, its GetProtocolValue).
			ibDataNode row;
			property->GetProtocolValue(row);
			return new wxPGPictureProperty(property->GetLabel(), property->GetName(), property->GetValue(),
				row.GetProperty(ibProtocol::ibSchema::ibProperties::Pictures).AsChild());
		});
		ibPropertyRegistry::Register(wxT("ibPropertyExternalPicture"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGExternalImageProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}g_pictureLoader;

static int s_indFilter = -1;

wxPGPictureProperty::wxPGPictureProperty(const wxString& label,
	const wxString& strName,
	const wxVariant& value,
	const std::optional<ibDataNode>& pictures) : wxPGProperty(label, strName),
	m_propertySource(nullptr),
	m_propertyBackend(nullptr), m_propertyConfiguration(nullptr), m_propertyFile(nullptr)
{
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

	// …and the configuration's — a server's property always has an owner, the object its row is of.
	{
		wxPGChoiceEntry& entry = configurationChoices.AddAsSorted(wxEmptyString, system_config++);
		m_confChoices.insert_or_assign(entry.GetValue(), wxNullGuid);

		// The configuration's pictures — the picture metaobjects of the owner's configuration, or of the one it extends:
		// the server's, brought by the row by name and guid.
		if (pictures) {
			// …each drawn with its own picture, as the desktop's: the server's, asked for by its id all at once (picture
			// {Pictures}) — one call for the lot, and none once the client keeps them.
			std::vector<wxString> ids;
			for (const ibDataNode& item : pictures->Children())
				ids.push_back(item.GetValue<ibString>(ibProtocol::ibSchema::ibProperties::Id));
			ibProtocolPictureReceive(ids);
			for (const ibDataNode& item : pictures->Children()) {
				const wxString guid = item.GetValue<ibString>(ibProtocol::ibSchema::ibProperties::Guid);
				wxPGChoiceEntry& entry = configurationChoices.AddAsSorted(
					item.GetValue<ibString>(ibProtocol::ibSchema::ibProperties::Name), system_config++);
				entry.SetBitmap(ibProtocolPicture(item.GetValue<ibString>(ibProtocol::ibSchema::ibProperties::Id)));
				m_confChoices.insert_or_assign(entry.GetValue(), ibGuid(guid));
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
	m_propertyFile = new ibPGExternalImageProperty(_("File path"), wxT("file_path"),
		WriteExternalPicture(wxT("ibPropertyExternalPicture"), ReadPicture(value).m_img_data));
	m_propertyFile->SetHelpString(_("Path to the image file."));

	AddPrivateChild(m_propertyFile);

	//Set current value
	SetValue(value);
}

wxVariant wxPGPictureProperty::ChildChanged(wxVariant& thisValue, const int childIndex,
	wxVariant& childValue) const
{
	ibPictureDescription pictureDesc = ReadPicture(thisValue);
	switch (childIndex)
	{
	case 0:
		pictureDesc.m_type = static_cast<ibPictureType>(childValue.GetLong());
		break;
	case 1:
		pictureDesc.m_class_identifier = m_valChoices.at(childValue.GetLong());
		break;
	case 2:
		pictureDesc.m_meta_guid = m_confChoices.at(childValue.GetLong());
		break;
	case 3:
		pictureDesc.m_img_data = ReadExternalPicture(childValue);
		break;
	}
	return WritePicture(thisValue, pictureDesc);
}

void wxPGPictureProperty::RefreshChildren()
{
	if (!ibVariantDataSerializable::GetNodeValue(m_value).IsEmpty()) {

		m_propertySource->Hide(false);
		m_propertySource->SetExpanded(true);

		const ibPictureDescription pd = ReadPicture(m_value);

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
			m_propertyFile->SetValue(WriteExternalPicture(wxT("ibPropertyExternalPicture"), pd.m_img_data));
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
	const ibExternalPictureDescription pictureDesc = ReadExternalPicture(GetValue());

	if (!pictureDesc.IsEmptyPicture()) {

		// Draw the thumbnail
		// Create the bitmap here because required size is not known in OnSetValue().

		// Delete the cache if required size changed
		if (m_bitmap.IsOk() && (m_bitmap.GetSize() != rect.GetSize())) {
			m_bitmap = wxNullBitmap;
		}

		m_bitmap = ibBackendPicture::CreatePicture(pictureDesc, wxSize(d.m_drawnWidth, d.m_drawnHeight));
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
		variant = WriteExternalPicture(variant.GetType(), ibExternalPictureDescription());
		return true;
	}

	return false;
}

bool ibPGExternalImageProperty::DisplayEditorDialog(wxPropertyGrid* pg, wxVariant& value)
{
	wxFileName filename(ReadExternalPicture(value).m_img_name);

	wxString path = filename.GetPath();
	wxString file = filename.GetFullName();

	if (path.empty() && !m_basePath.empty())
		path = m_basePath;

	wxFileDialog dlg(pg->GetPanel(),
		m_dlgTitle.empty() ? wxString(_("Choose a picture")) : m_dlgTitle,
		m_initialPath.empty() ? path : m_initialPath,
		file,
		m_wildcard.empty() ? wxString(_(wxALL_FILES)) : m_wildcard,
		m_dlgStyle,
		wxDefaultPosition);

	if (m_indFilter >= 0) dlg.SetFilterIndex(m_indFilter);

	if (dlg.ShowModal() == wxID_OK) {

		// Get from FS — the file is the CLIENT's: its image is read here and goes to the server whole, in the value.
		ibExternalPictureDescription container;

		if (ibBackendPicture::LoadFromFile(dlg.GetPath(), container)) {
			value = WriteExternalPicture(value.GetType(), container);
			return true;
		}
	}

	return false;
}
