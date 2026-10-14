#ifndef __PROPERTY_INSPECTOR_H__
#define __PROPERTY_INSPECTOR_H__

// THE DESKTOP'S OBJECT INSPECTOR, ITS SERVER HALF (frontend ibObjectInspector) — the object a person selected, a form's
// control or a metaobject alike, and how its properties read, drawn into the designer's frame as rows (Properties); the
// client's inspector draws them by their class and gives an edit back through one door (schema Properties). What the
// object hides it pushes, as it pushed to the desktop's grid (ibPropertyObjectNotifier); kept here, said on the rows.

#include <set>

#include "backend/propertyManager/propertyObject.h"

#include "core/fileSystem/types.h"   // s32

#include "frmserver/frmserver.h"
#include "protocol/protocol.h"   // ibProtocolName — the rows

class ibDataNode;
class ibDataValue;
class ibValueMetaObject;

class FRMSERVER_API ibPropertyInspector : public ibPropertyObjectNotifier {
public:

	virtual ~ibPropertyInspector();

	// The object hides or shows one of its properties, as its state decides (OnPropertyRefresh); gone, nothing is shown.
	virtual bool PropertyHidden(const ibProperty* property, bool hide) override;

	// ONE PROPERTY'S ROW, under `parent` — a property's or an event's, of any object: Name, Title, Help, Class, Value — as
	// its node holds it, Presentation, ReadOnly; what it may be set to its children (Id, Title, Picture), Multiple. What
	// the rows below are made of — and the header above an external file's tree (ibDataProcessorTree).
	static void AppendProperty(ibDataNode& parent, ibBackendProperty* property);

	// THE OBJECT SHOWN — the desktop's verb, which took any property object: registered on it, and asked which of its
	// properties it hides. `id` — what the edit names it by (a metaobject's id, a form control's); `tabId` — the tab it
	// is in (0: the configuration's tree), said on the rows.
	void SelectObject(ibPropertyObject* object, ibMetaID id, s32 tabId);
	// …a metaobject of a tree, by its own id.
	void SelectObject(ibValueMetaObject* object, s32 tabId);
	// …alive, or none: the object's death clears the owner (ibPropertyObjectNotifier); and its id.
	ibPropertyObject* GetSelectedObject() const;
	ibMetaID GetSelectedId() const { return m_currentId; }

	// THE EDIT — the object shown, its property or event of that name given that value, as its node holds it: through the
	// inspector's door (ibPropertyGate), the object asked first, as on the desktop; then asked again what its state hides.
	// False: refused — the value stays as it was, and the rows say so.
	bool SetValue(const wxString& name, const ibDataValue& value);

	// The rows — the object's id (NodeId) and Tab, its categories (Name, Title) and the properties in them (Name, Title,
	// Help, Class, Value — as its node holds it, Presentation, ReadOnly; what it may be set to its children: Id, Title,
	// Picture; Multiple) — what its state hides not sent. Nothing shown: none.
	void OnDraw(ibDataNode& node) const;

private:

	ibPropertyObject* m_currentSel = nullptr;
	ibMetaID m_currentId = 0;
	s32 m_tabId = 0;
	std::set<const ibProperty*> m_hidden;
};

#endif
