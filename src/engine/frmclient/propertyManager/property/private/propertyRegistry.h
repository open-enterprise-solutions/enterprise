#ifndef __PROPERTY_REGISTRY_H__
#define __PROPERTY_REGISTRY_H__

#include <wx/propgrid/propgrid.h>

#include <functional>
#include <map>

#include "core/serialize/dataBuilder.h"
#include "frmclient/backend/propertyManager/propertyObject.h"

#include "frmclient/frmclient.h"

// THE PROPERTY'S TYPE -> ITS PRESENTATION, held entirely on the client.
//
// A property shown is the server's (ibPropertySerializable — a metaobject's, filled from its rows): its TYPE (GetType) is the
// class the server names it by (ibPropertyBoolean; a template without its arguments: ibPropertyEnum), its value is as the server
// stores it (GetNodeValue: a scalar, or a composite), its choices are what it may be set to (GetValueList). The client
// is told, per type, which widget shows it and how the widget's value goes back as the property's node holds it.
//
//   ibPropertyRegistry::Register(wxT("ibPropertyBoolean"),
//       [](ibBackendProperty* property) -> wxPGProperty* {
//           return new wxBoolProperty(property->GetLabel(), property->GetName(), property->GetNodeValue().AsBool());
//       },
//       [](const wxVariant& value) { return ibDataValue::Bool(value.GetBool()); });
//
// The widgets are the advprop classes, one copy: the common ones here in frmclient (the designer's and the runtime's),
// the designer's own registered by designer-thin. ⚠ A TYPE NOBODY REGISTERED IS A STRING — read-only, as it reads
// (Max, 2026-10-14): never guessed from the value's shape, so what the client has not been taught shows as not done.
class FRMCLIENT_API ibPropertyRegistry {
public:

	// The widget showing the property — a window it opens asks the property's owner what it needs (a translated
	// string's languages: the configuration the owner holds).
	using ibPropertyMaker = std::function<wxPGProperty* (ibBackendProperty* property)>;
	// …and what it holds, as the property's node holds it. None: by the value's kind.
	using ibPropertyWriter = std::function<ibDataValue (const wxVariant& value)>;

	static void Register(const wxString& type, ibPropertyMaker maker, ibPropertyWriter writer = ibPropertyWriter());

	// The widget for the property — its help and its read-only already said.
	static wxPGProperty* Create(ibBackendProperty* property);
	// The value an edit gives back, as the property's node holds it.
	static ibDataValue GetNodeValue(const ibBackendProperty* property, const wxVariant& value);

	// The choices it offers, as the grid lists them: label, id.
	static wxPGChoices GetChoices(ibBackendProperty* property);

private:

	struct ibPropertyEntry {
		ibPropertyMaker m_maker;
		ibPropertyWriter m_writer;
	};

	// Function-local: the makers register from static initialisers, across the libraries.
	static std::map<wxString, ibPropertyEntry>& GetEntries();
};

#endif
