#ifndef _COMMON_H_
#define _COMMON_H_

#include "window.h"
#include "backend/propertyManager/property/propertyCommandSource.h"   // ibPropertyCommandSource — assign a COMMAND to the button
#include "frmserver/visualView/layers/commandReceiver.h"             // ibFrontendCommandReceiver — the command door the button IS-A (IS-A ibBackendCommandReceiver too)

/////////////////////////////////////////////////////////////////////////////////////
//                                 COMMON ELEMENTS                                 //
/////////////////////////////////////////////////////////////////////////////////////

// ibValueButton's control clsid — kind-typed identity (mirror g_controlTableBoxCLSID), so a control's TYPE is
// read from its clsid, never via dynamic_cast.
constexpr ibClassID g_controlButtonCLSID = control_to_clsid("CT_BUTN");

class ibValueButton : public ibValueWindow, public ibFrontendCommandReceiver {
	public:

	void SetCaption(const ibString& caption) { return m_propertyTitle->SetValue(caption); }
	ibString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	ibValueButton();

	//get title
	virtual ibString GetControlTitle() const { return GetCaption(); }

	// Added in the studio with no command: a fresh form command named after the button, bound — as a field gets an attribute.
	virtual void OnCreated(ibVisualHost* host, bool firstCreated) override;

	// The caption, the picture and the representation as they come out once the bound command has filled
	// in what the button leaves unset; hidden when that command no longer resolves, disabled when it
	// modifies data on a view-only form.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	// Press — the bound command runs; with Member, one command of its group.
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

	/**
	* Property events
	*/
	// Note is offered on a tile alone, the one picture location that draws it.
	virtual void OnPropertyRefresh() override;

private:

	// The press — runs the bound command.
	void OnButtonPressed();
	// …and when the bound command is a GROUP, the press on one of its leaves: `member` is the leaf's place
	// in the order the frame listed them (WalkGroup). A group runs nothing by itself.
	void OnButtonPressed(size_t member);

	// ⭐ THE GROUP'S SUBMENU, walked ONCE for both ends: the frame (`node` given — the tree with captions and
	// pictures by their ids, a nested group as a sub-node, a leaf carrying its `Member` place) and the press (`node`
	// null — only the leaves, in the same order). Two walks would be two orders, and a press would run a neighbour.
	void WalkGroup(const std::vector<ibFrontendCommandReceiver::ibCommandSubItem>& subs,
		std::vector<ibCommandDescription>& leaves, ibDataNode* node) const;

	ibPropertyCategory* m_categoryButton = ibPropertyObject::CreatePropertyCategory(ibT("Button"), _("Button"));
	ibPropertyEnum<ibValueEnumButtonView>* m_propertyView = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumButtonView>>(m_categoryButton, ibT("View"), ibGETTEXT_IN_CONTEXT("button", "View"),
		_("How the button is drawn: a button, or a hyperlink - no face and no frame, its caption underlined. Pressed the same way, and its picture, its representation and its command are the same."),
		ibButtonView::ibButtonView_Button);
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryButton, ibT("Title"), _("Title"),
		_("The text on the button. Empty: the bound command's own caption. Can be written per language and changed from code while the form is open."), ibT(""));
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_categoryButton, ibT("Representation"), _("Representation"),
		_("What the button shows: text, picture, or both. Auto: what the bound command prefers (Close is a picture alone, Add a picture with text); picture and text when no command is bound."),
		ibRepresentation::ibRepresentation_Auto);
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryButton, ibT("Picture"), _("Picture"),
		_("The button's icon, beside or instead of the text (see Representation)."));
	ibPropertyEnum<ibValueEnumPictureLocation>* m_propertyPictureLocation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumPictureLocation>>(m_categoryButton, ibT("PictureLocation"), _("Picture location"),
		_("Where the picture stands. Auto: where the button puts it, on the left of the text. Left, Right: on that side of the text. Tile: the picture large over the text and the Note under them, the button filling what its sizer gives it - a big button for a finger; put tiles in a grid sizer for a panel of them."),
		ibPictureLocation::ibPictureLocation_Auto);
	ibPropertyTString* m_propertyNote = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryButton, ibT("Note"), _("Note"),
		_("The line under a tile's title - a price, a count, a state. Shown only when the picture location is Tile. Can be written per language and changed from code while the form is open."), ibT(""));
	// A button has NO event — it carries ONLY a bound COMMAND (command-source picker, the command-door twin of a
	// control's data source). A press RUNS the command through ibFrontendCommandReceiver::ExecuteValueByPath. Internal
	// name MUST differ from any category name (wxPropertyGrid keys both in one map); lives in the Button category.
	ibPropertyCommandSource* m_propertyCommand = ibPropertyObject::CreateProperty<ibPropertyCommandSource>(m_categoryButton, ibT("Command"), _("Command"),
		_("The command a press runs: a form command, a standard command of the form or of one of its tables, or a command of the application. A button has no click event of its own - the code goes into the command's handler."));

public:
	// ibFrontendCommandReceiver gate — the button IS-A command door; the walk starts at its owner form. WalkCommand
	// (the backend receiver's validate) is inherited from the door — no per-control impl.
	virtual ibValueForm* GetCommandGateForm() const override;
	// the button's bound command-hop path (empty = none) — the click handler runs it.
	const ibCommandDescription& GetCommandDesc() const { return m_propertyCommand->GetValueAsCommandDesc(); }
	// Bind a command to this button (a command drop / the picker): desc = the hop path, display = its caption.
	void SetCommandDesc(const ibCommandDescription& desc, const ibString& display) { m_propertyCommand->SetValue(desc, display); }
};

#endif
