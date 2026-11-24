#ifndef _SIZER_ITEM_H_
#define _SIZER_ITEM_H_

#include "frmserver/frmserver.h"
#include "backend/propertyManager/propertyObject.h"             // ibPropertyObject — what it is
#include "backend/propertyManager/property/propertyBoolean.h"   // ibPropertyBoolean — the sides a margin is kept on
#include "backend/propertyManager/property/propertyNumber.h"    // ibPropertyUInteger — the share, the margin
#include "backend/propertyManager/property/propertyEnum.h"      // ibPropertyEnum — the stretch
#include "frmserver/visualView/controlEnum.h"                   // ibValueEnumStretch

class ibValueFrame;
class ibDataNode;   // serialize/dataBuilder.h — universal node (control -> node)

// ⭐ THE CONTROL'S PLACE IN THE SIZER ABOVE — its share of the spare room, how it stretches across, its margins. A CHILD
// of its control, as a field's view is (field.h): a window and a sizer make one and attach it (ibValueFrame::
// GetSizerItem), so its properties show, save and are scripted as the control's, in the control's node. Was a node of
// the form's tree that wrapped each control (Max, 2026-11-23: "the same child — back then it could not attach to
// properties"). The server reads none of them: a client lays the control out by them.
class FRMSERVER_API ibSizerItem : public ibPropertyObject {
	public:

	explicit ibSizerItem(ibValueFrame* control) : m_control(control) {}

	// …answering as its control: outside, the control is the one object.
	virtual ibString GetClassName() const override;
	virtual ibString GetObjectTypeName() const override;
	virtual bool IsEditable() const override;
	virtual const ibMetaData* GetMetaData() const override;

	void SetProportion(int proportion) {
		m_propertyProportion->SetValue(proportion);
	}

	int GetProportion() const {
		return m_propertyProportion->GetValueAsUInteger();
	}

	void SetFlagBorder(long flag_border) const {
		m_propertyFlagBorderLeft->SetValue((flag_border & ibDirection_Left) != 0);
		m_propertyFlagBorderRight->SetValue((flag_border & ibDirection_Right) != 0);
		m_propertyFlagBorderTop->SetValue((flag_border & ibDirection_Up) != 0);
		m_propertyFlagBorderBottom->SetValue((flag_border & ibDirection_Down) != 0);
	}

	long GetFlagBorder() const {
		long flag = 0;
		if (m_propertyFlagBorderLeft->GetValueAsBoolean()) flag |= ibDirection_Left;
		if (m_propertyFlagBorderRight->GetValueAsBoolean()) flag |= ibDirection_Right;
		if (m_propertyFlagBorderTop->GetValueAsBoolean()) flag |= ibDirection_Up;
		if (m_propertyFlagBorderBottom->GetValueAsBoolean()) flag |= ibDirection_Down;
		return flag;
	}

	void SetFlagState(const ibStretch& s) const {
		m_propertyFlagState->SetValue(s);
	}

	ibStretch GetFlagState() const {
		return m_propertyFlagState->GetValueAsEnum();
	}

	int GetBorder() const {
		return m_propertyBorder->GetValueAsUInteger();
	}

	void SetBorder(long border) const {
		m_propertyBorder->SetValue(border);
	}

	// Its properties, read from and written into its control's node.
	bool ReadData(const ibDataNode& node);
	bool WriteData(ibDataNode& node) const;

private:

	ibValueFrame* const m_control;

	ibPropertyCategory* m_categorySizerItem = ibPropertyObject::CreatePropertyCategory(ibT("SizerItem"), _("Sizer item"));
	ibPropertyUInteger* m_propertyProportion = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categorySizerItem, ibT("Proportion"), _("Proportion"),
		_("How much of its sizer's spare room the control takes along the sizer's direction. 0 (the default): the control keeps its natural size. Otherwise the spare room is shared in these proportions - controls with 1 and 2 get one third and two thirds."),
		0);
	ibPropertyCategory* m_categorySizerBorder = ibPropertyObject::CreatePropertyCategory(ibT("SizerItemBorder"), _("Border"));
	ibPropertyUInteger* m_propertyBorder = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categorySizerBorder, ibT("BorderSize"), _("Size"),
		_("The empty margin kept around the control, in pixels, on the sides switched on below. Default 5."), 5);
	ibPropertyBoolean* m_propertyFlagBorderLeft = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizerBorder, ibT("BorderLeft"), _("Left"),
		_("Whether the margin (Size) is kept on the control's left side."), true);
	ibPropertyBoolean* m_propertyFlagBorderRight = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizerBorder, ibT("BorderRight"), _("Right"),
		_("Whether the margin (Size) is kept on the control's right side."), true);
	ibPropertyBoolean* m_propertyFlagBorderTop = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizerBorder, ibT("BorderTop"), _("Top"),
		_("Whether the margin (Size) is kept above the control."), true);
	ibPropertyBoolean* m_propertyFlagBorderBottom = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categorySizerBorder, ibT("BorderBottom"), _("Bottom"),
		_("Whether the margin (Size) is kept below the control."), true);
	ibPropertyEnum<ibValueEnumStretch>* m_propertyFlagState = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumStretch>>(m_categorySizerItem, ibT("Stretch"), _("Stretch"),
		_("The control's size across its sizer's direction. Shrink (the default): its natural size. Expand: the full width of a vertical sizer, or the full height of a horizontal one."),
		ibStretch_Shrink);
};

#endif
