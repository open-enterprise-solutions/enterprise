#ifndef __STATIC_H__
#define __STATIC_H__

#include "window.h"

#include <memory>

// ⭐ THE STATIC — A DECORATION, WHAT IT SHOWS A VIEW (docs/private/form-column-view.md), as the field is one control with its
// view a child (field.h). No source, no value (Max, 2026-11-23): a value shown beside its title is a field's Label. What
// it shows — a text, a line (Max, 2026-11-24: "merge them"; it was a control of its own), a picture next — is a CHILD the view
// makes, attached: its properties show as the static's, and outside there is one control.
//
// The protocol names the meaning, not a widget: a node `Static` with its `View`, and each client draws it with what it
// has (ibProtocol::ibForm::ibProperty::ibStaticView).

constexpr ibClassID g_controlStaticCLSID = control_to_clsid("CT_STAT");

class ibValueStatic;

// ⭐ WHAT A VIEW SHOWS — its properties, saved with the static, and its part of the frame. Made for a view by Create — the
// one place that knows the views.
class ibStaticViewProvider : public ibPropertyObject {
public:

	virtual ~ibStaticViewProvider() = default;

	// …answering as its static: outside, the static is the one object.
	virtual ibString GetClassName() const override;
	virtual ibString GetObjectTypeName() const override;
	virtual bool IsEditable() const override;
	virtual const ibMetaData* GetMetaData() const override;

	// The static's caption (ibValueFrame::GetControlTitle) — a text's own.
	virtual ibString GetControlTitle() const;

	// The view's part of the static's State.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) {}

	// Its properties, read from and written into the static's own node.
	virtual bool ReadData(const ibDataNode& node) = 0;
	virtual bool WriteData(ibDataNode& node) const = 0;

	ibValueStatic* GetStatic() const { return m_static; }

	// …and from the studio's copy blob, once the static is of the pasted view (ibValueStatic::PasteData).
	bool PasteData(const ibDataNode& node) { return PasteProperty(node); }

	// The child of a view; null for a view this build does not know.
	static std::unique_ptr<ibStaticViewProvider> Create(ibStaticView view, ibValueStatic* control);

protected:

	explicit ibStaticViewProvider(ibValueStatic* control) : m_static(control) {}

private:

	ibValueStatic* const m_static;
};

class ibValueStatic : public ibValueWindow {
public:

	ibValueStatic();

	// The view's part.
	virtual ibString GetControlTitle() const override { return m_provider->GetControlTitle(); }
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	virtual ibPicture GetIcon() const override;

	// The view first — it makes the child whose properties the rest of the node holds.
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	// View changed — the child of the new view takes its place; a line chosen runs the full width.
	virtual void OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue) override;

	static ibPicture GetIconGroup();

protected:

	// The studio's paste makes the static of the pasted view first.
	virtual bool PasteData(const ibDataNode& node) override;

private:

	ibStaticView GetView() const { return m_propertyView->GetValueAsEnum(); }
	// The child of `view` takes the place of the one there: what the two have by the same name crosses over.
	void SetView(ibStaticView view);

	std::unique_ptr<ibStaticViewProvider> m_provider;

	ibPropertyCategory* m_categoryStatic = ibPropertyObject::CreatePropertyCategory(ibT("Static"), _("Static"));
	ibPropertyEnum<ibValueEnumStaticView>* m_propertyView = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumStaticView>>(m_categoryStatic, ibT("View"), ibGETTEXT_IN_CONTEXT("static", "View"),
		_("What the static shows: Text - its title, a heading or a note; Line - a line across or down, separating parts of the form. Each view has its own properties."),
		ibStaticView::ibStaticView_Text);
};

// A text — a heading, a note: its title, line breaks where the text has them.
class ibStaticViewText : public ibStaticViewProvider {
public:

	explicit ibStaticViewText(ibValueStatic* control) : ibStaticViewProvider(control) {}

	virtual ibString GetControlTitle() const override { return m_propertyTitle->GetValueAsTranslateString(); }

	// The caption in the session's language.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryText = ibPropertyObject::CreatePropertyCategory(ibT("Text"), ibGETTEXT_IN_CONTEXT("static view", "Text"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryText, ibT("Title"), _("Title"),
		_("The caption text. Can be written per language and changed from code while the form is open."),
		ibT("Static text"));
};

// A line — across, separating stacked parts of a form, or down, separating side-by-side ones. Its way is what a client
// builds it with (the node's Orient): turned, it is built anew.
class ibStaticViewLine : public ibStaticViewProvider {
public:

	explicit ibStaticViewLine(ibValueStatic* control) : ibStaticViewProvider(control) {}

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryLine = ibPropertyObject::CreatePropertyCategory(ibT("Line"), ibGETTEXT_IN_CONTEXT("static view", "Line"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryLine, ibT("Orient"), _("Orient"),
		_("The line's direction: horizontal (the default), separating stacked parts of a form, or vertical, separating side-by-side ones. Set Stretch to Expand for the line to run the full width or height."),
		ibOrientation_Horizontal);
};

#endif
