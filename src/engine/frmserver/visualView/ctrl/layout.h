#ifndef __LAYOUT_H__
#define __LAYOUT_H__

#include "control.h"

#include <memory>

// ⭐ THE LAYOUT — ONE SIZER, ITS KIND A VIEW (docs/private/form-layout.md), as the field is one control with its view a
// child (field.h). What every view shares is the layout's: the area it keeps (MinimumSize), its place in the sizer
// above, what it takes as children. How it lays them out — a row or a column, wrapping, a grid, a framed group — is a
// CHILD the view makes, attached: its properties show as the layout's, and outside there is one control.
//
// The protocol names the meaning, not a widget: a node `Layout` with its `View`, and each client lays it out with what it
// has (ibProtocol::ibForm::ibProperty::ibLayoutView).

constexpr ibClassID g_controlLayoutCLSID = control_to_clsid("CT_LAYT");

class ibValueLayout;

// ⭐ WHAT A VIEW LAYS OUT BY — its properties, saved with the layout, and its part of the frame. Made for a view by Create
// — the one place that knows the views.
class ibLayoutViewProvider : public ibPropertyObject {
public:

	virtual ~ibLayoutViewProvider() = default;

	// …answering as its layout: outside, the layout is the one object.
	virtual ibString GetClassName() const override;
	virtual ibString GetObjectTypeName() const override;
	virtual bool IsEditable() const override;
	virtual const ibMetaData* GetMetaData() const override;

	// The layout's caption (ibValueFrame::GetControlTitle) — a group's own title when it has one.
	virtual ibString GetControlTitle() const;

	// The view's part of the layout's State.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) {}

	// Its properties, read from and written into the layout's own node.
	virtual bool ReadData(const ibDataNode& node) = 0;
	virtual bool WriteData(ibDataNode& node) const = 0;

	ibValueLayout* GetLayout() const { return m_layout; }

	// …and from the designer's copy blob, once the layout is of the pasted view (ibValueLayout::PasteData).
	bool PasteData(const ibDataNode& node) { return PasteProperty(node); }

	// The child of a view; null for a view this build does not know.
	static std::unique_ptr<ibLayoutViewProvider> Create(ibLayoutView view, ibValueLayout* layout);

protected:

	explicit ibLayoutViewProvider(ibValueLayout* layout) : m_layout(layout) {}

private:

	ibValueLayout* const m_layout;
};

class ibValueLayout : public ibValueControl {
public:

	ibValueLayout();

	virtual int GetComponentType() const override { return COMPONENT_TYPE_SIZER; }

	// What it lays out.
	virtual bool FilterChild(const ibClassID& clsid) const override { return IsPlaceableControl(clsid); }

	// The view's part.
	virtual ibString GetControlTitle() const override { return m_provider->GetControlTitle(); }
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	virtual ibPicture GetIcon() const override;

	// The view first — it makes the child whose properties the rest of the node holds.
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	// View changed — the child of the new view takes its place.
	virtual void OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue) override;

	static ibPicture GetIconGroup();

protected:

	// The designer's paste makes the layout of the pasted view first.
	virtual bool PasteData(const ibDataNode& node) override;

private:

	ibLayoutView GetView() const { return m_propertyView->GetValueAsEnum(); }
	// The child of `view` takes the place of the one there: what the two have by the same name crosses over.
	void SetView(ibLayoutView view);

	std::unique_ptr<ibLayoutViewProvider> m_provider;

	ibPropertyCategory* m_categoryLayout = ibPropertyObject::CreatePropertyCategory(ibT("Layout"), _("Layout"));
	ibPropertyEnum<ibValueEnumLayoutView>* m_propertyView = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumLayoutView>>(m_categoryLayout, ibT("View"), ibGETTEXT_IN_CONTEXT("layout", "View"),
		_("How the layout lays out its controls: Box - in a row or a column; Wrap - in a row that wraps to a new one when there is no room; Grid - in cells of one size; Group - in a row or a column inside a frame with a caption. Each view has its own properties."),
		ibLayoutView::ibLayoutView_Box);
	ibPropertySize* m_propertyMinSize = ibPropertyObject::CreateProperty<ibPropertySize>(m_categoryLayout, ibT("MinimumSize"), _("Minimum size"), _("The smallest size the sizer keeps for the area it lays out, in pixels; -1 on a side means no limit there. The form cannot shrink the area below it."), ibDefaultSize);
};

// A row or a column.
class ibLayoutViewBox : public ibLayoutViewProvider {
public:

	explicit ibLayoutViewBox(ibValueLayout* layout) : ibLayoutViewProvider(layout) {}

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryBox = ibPropertyObject::CreatePropertyCategory(ibT("Box"), ibGETTEXT_IN_CONTEXT("layout view", "Box"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryBox, ibT("Orient"), _("Orient"),
		_("How the sizer lays out its controls: vertically (the default, one under another) or horizontally (side by side)."),
		ibOrientation_Vertical);
};

// A row (or a column) that wraps to a new one when the next control has no room.
class ibLayoutViewWrap : public ibLayoutViewProvider {
public:

	explicit ibLayoutViewWrap(ibValueLayout* layout) : ibLayoutViewProvider(layout) {}

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryWrap = ibPropertyObject::CreatePropertyCategory(ibT("Wrap"), ibGETTEXT_IN_CONTEXT("layout view", "Wrap"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryWrap, ibT("Orient"), _("Orient"),
		_("The direction controls are laid out in. When a row (or column) has no room for the next control, it wraps to a new one. Horizontal by default."),
		ibOrientation_Horizontal);
};

// Cells of one size, filled row by row.
class ibLayoutViewGrid : public ibLayoutViewProvider {
public:

	explicit ibLayoutViewGrid(ibValueLayout* layout) : ibLayoutViewProvider(layout) {}

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryGrid = ibPropertyObject::CreatePropertyCategory(ibT("Grid"), ibGETTEXT_IN_CONTEXT("layout view", "Grid"));
	ibPropertyUInteger* m_propertyRows = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryGrid, ibT("Rows"), _("Rows"),
		_("The number of rows in the grid. 0 (the default): as many as the controls need, given the column count."), 0);
	ibPropertyUInteger* m_propertyCols = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryGrid, ibT("Cols"), _("Cols"),
		_("The number of columns in the grid; controls fill it row by row, left to right. All cells get the same size. Default 2."), 2);
};

// A row or a column inside a frame with a caption — the one view that is also seen.
class ibLayoutViewGroup : public ibLayoutViewProvider {
public:

	explicit ibLayoutViewGroup(ibValueLayout* layout) : ibLayoutViewProvider(layout) {}

	virtual ibString GetControlTitle() const override;

	// Its caption in the user's language, and whether the frame is enabled and shown — shown only while the feature
	// options of this base leave the layout available.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryGroup = ibPropertyObject::CreatePropertyCategory(ibT("Group"), ibGETTEXT_IN_CONTEXT("layout view", "Group"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryGroup, ibT("Orient"), _("Orient"),
		_("How the framed group lays out its controls: horizontally (the default, side by side) or vertically (one under another)."),
		ibOrientation_Horizontal);
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryGroup, ibT("Title"), _("Title"),
		_("The caption drawn on the group's frame. Empty: a frame with no caption. Can be written per language."), ibT(""));
	ibPropertyFont* m_propertyFont = ibPropertyObject::CreateProperty<ibPropertyFont>(m_categoryGroup, ibT("Font"), _("Font"), _("The font of the frame's caption. Unset: the form's font."));
	ibPropertyColour* m_propertyFG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryGroup, ibT("ForegroundColour"), _("Foreground"), _("The colour of the frame's caption. Default: the system colour."), ibDefaultStyleFGColour);
	ibPropertyColour* m_propertyBG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryGroup, ibT("BackgroundColour"), _("Background"), _("The frame's background colour. Default: the system colour."), ibDefaultStyleBGColour);
	ibPropertyString* m_propertyTooltip = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryGroup, ibT("Tooltip"), _("Tooltip"), _("Text shown when the mouse pointer rests over the frame. Empty: no tooltip."), ibT(""));
	ibPropertyBoolean* m_propertyEnabled = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryGroup, ibT("Enabled"), _("Enabled"), _("Whether the controls inside the frame take input. Off: the frame and everything in it are greyed out."), true);
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryGroup, ibT("Visible"), _("Visible"), _("Whether the frame is shown. Hiding it hides the frame itself; its controls keep their data."), true);
};

#endif
