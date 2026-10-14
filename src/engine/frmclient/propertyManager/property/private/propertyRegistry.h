#ifndef __PROPERTY_REGISTRY_H__
#define __PROPERTY_REGISTRY_H__

#include <wx/propgrid/propgrid.h>

#include <functional>
#include <map>

#include "core/serialize/dataBuilder.h"

#include "frmclient/frmclient.h"

class ibBackendProperty;

// THE PROPERTY'S CLASS -> ITS EDITOR, held entirely on the client.
//
// The server draws every property of the object a person selected as a ROW (frmserver ibPropertyInspector): its
// Name, Title, Help, its CLASS — the backend type's name as the source writes it (ibPropertyBoolean; a template
// without its arguments: ibPropertyEnum) — its Value as the property stores it (GetNodeValue: a scalar a field, a
// composite a node), its Presentation, and what it may be set to (children: Id, Title, Picture; Multiple). The client
// keeps no property of its own: it is told, per class, which widget shows that row and how the widget's value goes
// back as the row carries it.
//
//   ibPropertyRegistry::Register(wxT("ibPropertyBoolean"),
//       [](const ibDataNode& row) -> wxPGProperty* {
//           return new wxBoolProperty(ibPropertyRegistry::GetLabel(row), ibPropertyRegistry::GetName(row),
//               ibPropertyRegistry::GetValue(row).AsBool());
//       },
//       [](const wxVariant& value) { return ibDataValue::Bool(value.GetBool()); });
//
// The widgets are the advprop classes, one copy: the common ones here in frmclient (the designer's and the runtime's),
// the designer's own registered by designer-thin. A class nobody registered is shown by the row's shape — its choices a
// list, its value by its kind — and failing that read-only, as its Presentation.
class FRMCLIENT_API ibPropertyRegistry {
public:

	// The widget showing the row.
	using ibPropertyMaker = std::function<wxPGProperty* (const ibDataNode& row)>;
	// …and what it holds, as the row carries it back (the server's WriteNodeValue shape). None: by the value's kind.
	using ibPropertyWriter = std::function<ibDataValue (const wxVariant& value)>;

	static void Register(const wxString& className, ibPropertyMaker maker, ibPropertyWriter writer = ibPropertyWriter());

	// The widget for a row — its help and its read-only already said. Null: a row that is not a property (a category).
	static wxPGProperty* Create(const ibDataNode& row);
	// The value an edit gives back, as the row's class carries it.
	static ibDataValue GetNodeValue(const ibDataNode& row, const wxVariant& value);

	// A PROPERTY OF THE CLIENT'S OWN OBJECT — a spreadsheet's cell, a form a person re-arranges — written as the server
	// writes its rows (frmserver ibPropertyInspector: Name, Title, Help, Class, Value, Presentation, choices,
	// ReadOnly), so the same makers show it.
	static ibDataNode& AppendProperty(ibDataNode& parent, ibBackendProperty* property);

	// — what a maker reads of its row —
	static wxString GetName(const ibDataNode& row);
	static wxString GetLabel(const ibDataNode& row);
	static wxString GetClass(const ibDataNode& row);
	// Its value as it is stored: a scalar, or a composite (Child) — empty when the row has none.
	static ibDataValue GetValue(const ibDataNode& row);
	// The choices it offers, as the grid lists them: label, id.
	static wxPGChoices GetChoices(const ibDataNode& row);

private:

	struct ibPropertyEntry {
		ibPropertyMaker m_maker;
		ibPropertyWriter m_writer;
	};

	// Function-local: the makers register from static initialisers, across the libraries.
	static std::map<wxString, ibPropertyEntry>& GetEntries();
};

#endif
