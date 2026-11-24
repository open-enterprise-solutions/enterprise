#ifndef _FRMCLIENT_VIEW_STATIC_H__
#define _FRMCLIENT_VIEW_STATIC_H__

#include "frmclient/visualView/ctrl/window.h"

class ibStaticText;
class ibStaticLine;

// THE VIEW A STATIC IS DRAWN WITH — the server's ibStaticViewProvider, this client's half: the widget of the view, made
// in the static's place. A view is this client's own widget for a meaning (ibProtocol::ibForm::ibProperty::
// ibStaticView), never one the server names.
class ibStaticViewProvider {
public:

	virtual ~ibStaticViewProvider() = default;

	// Its widget, made under `parent` — and drawn from the node: what the view shows of the State.
	virtual void Create(wxWindow* parent, const ibProtocolNode& node) = 0;
	virtual void Update(const ibProtocolNode& node) = 0;

	virtual wxWindow* GetWindow() const = 0;

	// The child of a view, by its number; a Text for one this client does not know.
	static std::unique_ptr<ibStaticViewProvider> Make(long long view);
};

// ⭐ THE STATIC — a decoration, drawn with the widget its View is: a text, a line. No value, no link (a value shown is a
// field's Label view, field.h). A new view, or a line turned, builds it anew (visualHostClient.cpp, BuiltWith).
class ibValueStatic : public ibValueWindow {
public:

	ibValueStatic(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override;

private:

	std::unique_ptr<ibStaticViewProvider> m_provider;
};

// ⭐ TEXT — its caption (the State's Caption): a heading, a note.
class ibStaticViewText : public ibStaticViewProvider {
public:

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override;

private:

	ibStaticText* m_staticText = nullptr;
};

// ⭐ LINE — across or down (Orient), separating parts of a form; it shows nothing of the State.
class ibStaticViewLine : public ibStaticViewProvider {
public:

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override;

private:

	ibStaticLine* m_staticLine = nullptr;
};

#endif
