#ifndef _FRMCLIENT_VIEW_LAYOUT_H__
#define _FRMCLIENT_VIEW_LAYOUT_H__

#include <wx/statbox.h>
#include <wx/wrapsizer.h>

#include "frmclient/visualView/ctrl/control.h"
#include "deskgui/univ/ctrl/staticBox.h"

// THE LAYOUT OF A VIEW — one control, its View the sizer it is laid out by (the server's ibValueLayout): a sizer is no
// window, it lays out what is in it, as its node says; its properties are the server's (ibValueFrame). The view is
// what the layout is built with (visualHostClient.cpp, BuiltWith): a new one builds it anew.

// What every sizer does with its minimum size.
void ibViewUpdateSizer(wxSizer* sizer, const ibProtocolNode& node);
// …and what every control a sizer lays out does with its place there — its share of the room, its stretch, its border,
// as its own node says (the server's ibSizerItem).
void ibViewUpdateSizer(wxSizerItem* item, const ibProtocolNode& node);

// THE VIEW A LAYOUT IS LAID OUT WITH — the server's ibLayoutViewProvider, this client's half: the sizer of the view, made
// in the layout's place. A view is this client's own sizer for a meaning (ibProtocol::ibForm::ibProperty::ibLayoutView),
// never one the server names.
class ibLayoutViewProvider {
public:

	virtual ~ibLayoutViewProvider() = default;

	// Its sizer, made for `parent` — and laid out as the node says.
	virtual void Create(wxWindow* parent, const ibProtocolNode& node) = 0;
	virtual void Update(const ibProtocolNode& node) = 0;

	virtual wxSizer* GetSizer() const = 0;
	// The window what is in it is built in — a Group's frame; none, the layout's own parent.
	virtual wxWindow* GetChildParent() const { return nullptr; }

	// The child of a view, by its number; a Box for one this client does not know.
	static std::unique_ptr<ibLayoutViewProvider> Make(long long view);

protected:

	// The look of a window the view has, as the layout's (ibValueFrame::ApplyLook).
	static void ApplyLook(wxWindow* window, const ibProtocolNode& node);
};

class ibValueLayout : public ibValueControl {
public:

	ibValueLayout(ibVisualHostClient& host, long long controlId) : ibValueControl(host, controlId) {}

	virtual int GetComponentType() const override { return COMPONENT_TYPE_SIZER; }

	// The sizer of the node's view. A view this build does not know is a Box.
	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxSizer* GetSizer() const override;
	virtual wxWindow* GetChildParent() const override;

private:

	friend class ibLayoutViewProvider;   // a view's window takes the layout's look

	std::unique_ptr<ibLayoutViewProvider> m_provider;
};

// ⭐ BOX — a row or a column (Orient).
class ibLayoutViewBox : public ibLayoutViewProvider {
public:

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxSizer* GetSizer() const override;

private:

	wxBoxSizer* m_box = nullptr;
};

// ⭐ WRAP — a row or a column that goes on in the next one where it runs out of room.
class ibLayoutViewWrap : public ibLayoutViewProvider {
public:

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxSizer* GetSizer() const override;

private:

	wxWrapSizer* m_wrap = nullptr;
};

// ⭐ GRID — cells of one size, Rows by Cols.
class ibLayoutViewGrid : public ibLayoutViewProvider {
public:

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxSizer* GetSizer() const override;

private:

	wxGridSizer* m_grid = nullptr;
};

// ⭐ GROUP — a row or a column in a frame with a caption (the State's Title); what is in it is built in the frame.
class ibLayoutViewGroup : public ibLayoutViewProvider {
public:

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxSizer* GetSizer() const override;
	virtual wxWindow* GetChildParent() const override;

private:

	ibStaticBoxSizer* m_group = nullptr;
};

#endif
