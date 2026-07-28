#ifndef __PROPERTY_COMMAND_SOURCE_H__
#define __PROPERTY_COMMAND_SOURCE_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/commandDescription.h"

// The button's COMMAND-source property — the command-door twin of ibPropertySource. It holds an
// ibCommandDescription (a command-hop path: own object / main attribute / table / general) and, like the
// source property, gets a registered FRONTEND picker (ibPGCommandSourceProperty — a "Вид" selector + a
// command-source DIALOG). The button binds to this instead of a flat id, and renders from the resolved command.
class BACKEND_API ibPropertyCommandSource : public ibProperty {
	// The owning property object (a button / a bar item) becomes the variant's factory — cast to ibBackendCommandReceiver,
	// its WalkCommand validates the binding. Mirrors ibPropertySource::CreateVariantData taking the property.
	wxVariantData* CreateVariantData(const ibPropertyObject* property, const ibCommandDescription& desc, const wxString& display = wxEmptyString) const;
public:

#pragma region _value_
	// The binding address — the command-hop path stored in the variant (fed to the button, serialised here).
	ibCommandDescription& GetValueAsCommandDesc() const;
	// The binding rendered as its NAME — the same string the inspector cell shows ("<not selected>" if unbound).
	wxString GetValueAsString() const;

	void SetValue(const ibCommandDescription& desc, const wxString& display = wxEmptyString);
#pragma endregion

	ibPropertyCommandSource(ibPropertyCategory* cat, const wxString& name)
		: ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject(), ibCommandDescription())) {}
	ibPropertyCommandSource(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject(), ibCommandDescription())) {}
	ibPropertyCommandSource(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject(), ibCommandDescription())) {}

	virtual bool IsEmptyProperty() const;

	// set/get property data — a command source is not a script value (bound + run by the button, not read).
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	// Load & save — the DUMB command-hop path (ibCommandDescriptionMemory, symmetric to the source path).
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;
};

#endif // !__PROPERTY_COMMAND_SOURCE_H__
