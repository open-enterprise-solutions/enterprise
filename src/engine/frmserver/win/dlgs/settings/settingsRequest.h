#ifndef __SETTINGS_REQUEST_H__
#define __SETTINGS_REQUEST_H__

// A SETTINGS WINDOW PUT TO THE PERSON — a list's (ibDialogListSettings, ibProtocol::ibRequest::ibKind::ListSettings) and a
// report's (ibDialogComposerSettings, ComposerSettings), through the current session's frame. The window edits a COPY
// of the setting in force; on the way it asks the server what only the configuration answers (ibProtocol::ibRequest::ibListSettings::ibAct:
// the fields of a reference's targets, a value chosen), and its answer is read against the configuration — one the
// schema refuses is asked again, with why. What the window is told besides the setting — its fields, and what it
// arranges — is the door's.

#include <vector>

#include <wx/string.h>

#include "frmserver/frmserver.h"
#include "core/core.h"    // ibMetaID
#include "frmserver/visualView/ctrl/frame.h"       // ibControlFrame — the cell a value is chosen for
#include "frmserver/visualView/ctrl/typeControl.h" // ibTypeControlFactory — …and the type it stands on

class ibDataNode;
class ibMetaData;
struct ibTypeDescription;
struct ibPictureDescription;
struct ibSettingsDescription;

// ONE FIELD, DESCRIBED — what the window's field tree puts up: its name, how it reads, its column, its type, whether the
// options leave it, and its picture — the column's (GetSourcePicture) where the desktop's tree walks a source's explorer;
// none on a flat list, which the tree draws with the attribute picture.
FRMSERVER_API void ibWriteSettingsField(ibDataNode& fields, const ibString& name, const ibString& presentation, ibMetaID id,
	const ibTypeDescription& type, bool available, const ibPictureDescription& picture, const ibMetaData* metaData);

// WHICH OF THESE TYPES ARE REFERENCES, AND TO WHAT — the answer the desktop's field tree asks the configuration for
// (ibValueReferenceDataObject::ConvertToMetaIds) before it gives a field its [+]; the client has no configuration to ask.
FRMSERVER_API void ibWriteSettingsReferences(ibDataNode& references, const std::vector<ibTypeDescription>& types,
	const ibMetaData* metaData);

// …AND WHAT THEY ARE CALLED — the classes of these types, each once, as the configuration names and draws them (Type,
// Name, Picture — as the inspector names its choices'): what a type cell reads, and the client has none to name a kind by.
FRMSERVER_API void ibWriteSettingsClasses(ibDataNode& classes, const std::vector<ibTypeDescription>& types,
	const ibMetaData* metaData);

// THE FIELDS OF A REFERENCE'S TARGETS — a settings window's Act Expand, answered: `expanded` one field per name (its type
// the union of the targets'), `references` which of their types are references. For a door that asks no window (a
// composer's tab, ibProtocol::ibView::ibComposer::ibRequest::Expand).
FRMSERVER_API void ibWriteSettingsTargetFields(ibDataNode& expanded, ibDataNode& references,
	const std::vector<ibMetaID>& targets, const ibMetaData* metaData);

// THE CELL A VALUE IS CHOSEN FOR — the window's on the client (a condition's value), and here what the choice is made for:
// the desktop's filter cell (ibFilterValueRenderer), the owner its quick choice and its choice form hand the value to.
// What is chosen goes to the window with its question (Chosen) — asked again when the choice is made at once, or said
// anew when a choice form, opened over the window, makes it while the window waits.
// Counted: the choice form holds its owner (ControlIncrRef) and may outlive the window — a choice made after it closed
// is told to nobody (Detach).
class ibListSettingsChoice : public ibControlFrame, public ibTypeControlFactory {
public:

	ibListSettingsChoice(const ibMetaData* metaData, const ibClassID& clsid, const ibValue& value, std::int32_t choice)
		: m_metaData(metaData), m_typeDesc(clsid), m_value(value), m_choice(choice) {
	}

	// ibControlFrame
	virtual bool GetControlValue(ibValue& value) const override { value = m_value; return true; }
	virtual bool SetControlValue(const ibValue& value = ibValue()) override { m_value = value; return true; }
	virtual ibGuid GetControlGuid() const override { return m_guid; }
	virtual void ControlIncrRef() override { ++m_refCount; }
	virtual void ControlDecrRef() override {
		if (--m_refCount == 0)
			delete this;
	}
	virtual bool HasQuickChoice() const override;
	virtual void ChoiceProcessing(ibValue& selected) override;

	// ibTypeControlFactory — no column behind the cell: its type is the one it stands on.
	virtual const ibBackendSourceColumn* GetSourceAttributeObject() const override { return nullptr; }
	virtual ibSourceObject* GetSourceObject() const override { return nullptr; }
	virtual ibSourceDescription& GetSourceDesc() const override { return m_sourceDesc; }
	virtual const ibMetaData* GetMetaData() const override { return m_metaData; }
	virtual ibTypeDescription& GetTypeDesc() const override { return m_typeDesc; }

	// The value chosen, put into the window's question — how it reads with it, said by the configuration. A door that
	// asks no window puts it where it answers (a composer's tab: its view's State).
	void WriteChosen(ibDataNode& request) const;

	// The window closed: a choice made later is nobody's.
	void Detach() { m_detached = true; }

private:

	const ibMetaData*           m_metaData;
	mutable ibTypeDescription   m_typeDesc;
	mutable ibSourceDescription m_sourceDesc;
	ibValue                     m_value;
	const std::int32_t          m_choice;
	const ibGuid                m_guid = ibGuid::newGuid();
	unsigned                    m_refCount = 0;
	bool                        m_chosen = false;
	bool                        m_detached = false;
};

// THE CELL'S CHOICE, MADE — what the window's act names (Act Choose: the type, the value in the cell, the number it is
// asked under): the value's quick choice, or its choice form opened with the cell for its owner. The cell is the
// window's to hold (`choosing`, the one before let go), counted. For a door that asks no window too (a composer's tab,
// event Select), which holds the cell the same way.
FRMSERVER_API void ibChooseSettingsValue(const ibDataNode& act, const ibMetaData* metaData, ibListSettingsChoice*& choosing);

// The window, asked with `question` (its Kind, and what the door tells it) and `edited` — the setting it edits. True: OK,
// with `edited` the setting given back, read and taken by the schema. False: cancelled, or nobody to ask — `edited` as
// it was.
FRMSERVER_API bool ibRequestSettings(const ibDataNode& question, ibSettingsDescription& edited, const ibMetaData* metaData);

#endif // __SETTINGS_REQUEST_H__
