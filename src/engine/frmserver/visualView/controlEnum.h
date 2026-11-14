#ifndef __CONTROL_ENUM_H__
#define __CONTROL_ENUM_H__

enum ibTitleLocation {
	eLeft = 1,
	eRight
};

// Where a button's picture stands: Auto where the button puts it (on the left), on the left or the right of its caption,
// or a tile - the picture large over the caption and the Note under them. The numbers are the ones a saved form holds
// and the client reads (ibProtocol::ibForm::ibProperty::ibPictureLocation).
enum ibPictureLocation {
	ibPictureLocation_Auto,
	ibPictureLocation_Left,
	ibPictureLocation_Right,
	ibPictureLocation_Tile
};

// How a button is drawn: a button, or a hyperlink - the same button with no face and no frame, its caption underlined.
// The numbers are the ones a saved form holds and the client reads (ibProtocol::ibForm::ibProperty::ibButtonView).
enum ibButtonView {
	ibButtonView_Button,
	ibButtonView_Hyperlink
};

enum ibRepresentation {
	ibRepresentation_Auto,
	ibRepresentation_Text,
	ibRepresentation_Picture,
	ibRepresentation_PictureAndText
};

// Where a notebook's tabs are (its OrientPage) - none: the notebook is a panel, the active page alone. The numbers are
// the ones a saved form holds (Top and Bottom were wx's flags) and the ones the client reads
// (ibProtocol::ibForm::ibNotebook::ibTabLocation).
enum ibTabLocation {
	ibTabLocation_None = 0,
	ibTabLocation_Top = 1,
	ibTabLocation_Bottom = 8
};

// How a form field is filled, over what its type says (the metaobject's QuickChoice): Auto takes the type's answer,
// Use offers the drop-down of the matching items for a reference whose type does not, DontUse opens the list form for
// one whose type does. A field of a number or a date keeps its own window either way.
enum ibQuickChoiceMode {
	ibQuickChoiceMode_Auto = 0,
	ibQuickChoiceMode_Use,
	ibQuickChoiceMode_DontUse
};

// What a form field may be given from a hierarchical catalog, over what its attribute says (the attribute's Select):
// Auto takes the attribute's answer, the other three name an ibSelectMode (ibTypeControlFactory::GetSelectMode).
enum ibChoiceFoldersAndItems {
	ibChoiceFoldersAndItems_Auto = 0,
	ibChoiceFoldersAndItems_Items,
	ibChoiceFoldersAndItems_Folders,
	ibChoiceFoldersAndItems_FoldersAndItems
};

#include "frmserver/frmserver.h"
#include "core/graphics/geometry.h"   // ibOrientation, ibStretch, ibAlignment - in wx's numbers, which the client draws by

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class FRMSERVER_API ibValueEnumOrient :
	public ibValueEnumeration<ibOrientation> {
	public:
	ibValueEnumOrient() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibOrientation_Horizontal, ibT("Horizontal"), ibTRANSLATE("Horizontal"));
		AddEnumeration(ibOrientation_Vertical, ibT("Vertical"), ibTRANSLATE("Vertical"));
	}
private:
};

class FRMSERVER_API ibValueEnumStretch :
	public ibValueEnumeration<ibStretch> {
	public:
	ibValueEnumStretch() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibStretch_Shrink, ibT("Shrink"), ibTRANSLATE_IN_CONTEXT("sizer flag", "Shrink"), ibT("sizer flag"));
		AddEnumeration(ibStretch_Expand, ibT("Expand"), ibTRANSLATE_IN_CONTEXT("sizer flag", "Expand"), ibT("sizer flag"));
	}
private:
};

class FRMSERVER_API ibValueEnumOrientNotebookPage :
	public ibValueEnumeration<ibTabLocation> {
	public:
	ibValueEnumOrientNotebookPage() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibTabLocation::ibTabLocation_Top, ibT("Top"), ibTRANSLATE("Top"));
		AddEnumeration(ibTabLocation::ibTabLocation_Bottom, ibT("Bottom"), ibTRANSLATE("Bottom"));
		AddEnumeration(ibTabLocation::ibTabLocation_None, ibT("None"), ibTRANSLATE("None"));
	}
private:
};

class FRMSERVER_API ibValueEnumHorizontalAlignment :
	public ibValueEnumeration<ibAlignment> {
	public:
	ibValueEnumHorizontalAlignment() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibAlignment_Left, ibT("Left"), ibTRANSLATE("Left"));
		AddEnumeration(ibAlignment_Center, ibT("Center"), ibTRANSLATE("Center"));
		AddEnumeration(ibAlignment_Right, ibT("Right"), ibTRANSLATE("Right"));
	}
private:
};

class FRMSERVER_API ibValueEnumVerticalAlignment :
	public ibValueEnumeration<ibAlignment> {
	public:
	ibValueEnumVerticalAlignment() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibAlignment_Top, ibT("Top"), ibTRANSLATE("Top"));
		AddEnumeration(ibAlignment_Center, ibT("Center"), ibTRANSLATE("Center"));
		AddEnumeration(ibAlignment_Bottom, ibT("Bottom"), ibTRANSLATE("Bottom"));
	}
private:
};

class FRMSERVER_API ibValueEnumTitleLocation :
	public ibValueEnumeration<ibTitleLocation> {
	public:
	ibValueEnumTitleLocation() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibTitleLocation::eLeft, ibT("Left"), ibTRANSLATE("Left"));
		AddEnumeration(ibTitleLocation::eRight, ibT("Right"), ibTRANSLATE("Right"));
	}
private:
};

class FRMSERVER_API ibValueEnumPictureLocation :
	public ibValueEnumeration<ibPictureLocation> {
	public:
	ibValueEnumPictureLocation() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibPictureLocation::ibPictureLocation_Auto, ibT("Auto"), ibTRANSLATE("Auto"));
		AddEnumeration(ibPictureLocation::ibPictureLocation_Left, ibT("Left"), ibTRANSLATE("Left"));
		AddEnumeration(ibPictureLocation::ibPictureLocation_Right, ibT("Right"), ibTRANSLATE("Right"));
		AddEnumeration(ibPictureLocation::ibPictureLocation_Tile, ibT("Tile"), ibTRANSLATE("Tile"));
	}
private:
};

class FRMSERVER_API ibValueEnumButtonView :
	public ibValueEnumeration<ibButtonView> {
	public:
	ibValueEnumButtonView() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibButtonView::ibButtonView_Button, ibT("Button"), ibTRANSLATE_IN_CONTEXT("button view", "Button"), ibT("button view"));
		AddEnumeration(ibButtonView::ibButtonView_Hyperlink, ibT("Hyperlink"), ibTRANSLATE("Hyperlink"));
	}
private:
};

class FRMSERVER_API ibValueEnumRepresentation :
	public ibValueEnumeration<ibRepresentation> {
	public:
	ibValueEnumRepresentation() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibRepresentation::ibRepresentation_Auto, ibT("Auto"), ibTRANSLATE("Auto"));
		AddEnumeration(ibRepresentation::ibRepresentation_Text, ibT("Text"), ibTRANSLATE("Text"));
		AddEnumeration(ibRepresentation::ibRepresentation_Picture, ibT("Picture"), ibTRANSLATE("Picture"));
		AddEnumeration(ibRepresentation::ibRepresentation_PictureAndText, ibT("PictureAndText"), ibTRANSLATE("Picture and text"));
	}
private:
};

class FRMSERVER_API ibValueEnumQuickChoiceMode :
	public ibValueEnumeration<ibQuickChoiceMode> {
	public:
	ibValueEnumQuickChoiceMode() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibQuickChoiceMode::ibQuickChoiceMode_Auto, ibT("Auto"), ibTRANSLATE("Auto"));
		AddEnumeration(ibQuickChoiceMode::ibQuickChoiceMode_Use, ibT("Use"), ibTRANSLATE("Use"));
		AddEnumeration(ibQuickChoiceMode::ibQuickChoiceMode_DontUse, ibT("DontUse"), ibTRANSLATE("Don't use"));
	}
private:
};

class FRMSERVER_API ibValueEnumChoiceFoldersAndItems :
	public ibValueEnumeration<ibChoiceFoldersAndItems> {
	public:
	ibValueEnumChoiceFoldersAndItems() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_Auto, ibT("Auto"), ibTRANSLATE("Auto"));
		AddEnumeration(ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_Items, ibT("Items"), ibTRANSLATE("Items"));
		AddEnumeration(ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_Folders, ibT("Folders"), ibTRANSLATE("Folders"));
		AddEnumeration(ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_FoldersAndItems, ibT("FoldersAndItems"), ibTRANSLATE("Folders and items"));
	}
private:
};

#pragma endregion 
#endif // !__CONTROL_ENUM_H__
