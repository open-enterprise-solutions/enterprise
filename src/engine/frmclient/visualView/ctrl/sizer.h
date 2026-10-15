#ifndef _FRMCLIENT_VIEW_SIZER_H__
#define _FRMCLIENT_VIEW_SIZER_H__

#include <wx/statbox.h>
#include <wx/wrapsizer.h>

#include "frmclient/visualView/ctrl/control.h"

// THE SIZERS OF A VIEW — the desktop's (frontend/visualView/ctrl/sizer.h): a sizer is no window, it lays out what is
// in it, as its node says; its properties are the server's (ibValueFrame).

// What every sizer does with its minimum size (the desktop's ibValueSizer::UpdateSizer).
void ibViewUpdateSizer(wxSizer* sizer, const ibProtocolNode& node);

class ibValueSizer : public ibValueControl {
public:

	ibValueSizer(ibVisualHostClient& host, long long controlId) : ibValueControl(host, controlId) {}

	virtual int GetComponentType() const override { return COMPONENT_TYPE_SIZER; }
};

// A SIZER ITEM — the desktop's ibValueSizerItem: how its one control sits in the sizer above — its share of the room,
// its stretch, its border, as its node says. Built by the host's walk as the desktop's was; it draws nothing.
class FRMCLIENT_API ibValueSizerItem : public ibValueFrame {
public:

	ibValueSizerItem(ibVisualHostClient& host, long long controlId) : ibValueFrame(host, controlId) {}

	virtual void Create(wxWindow* WXUNUSED(parent), const ibProtocolNode& WXUNUSED(node)) override {}
	// The desktop's OnUpdated: its child's place in the sizer it stands in, set to what it holds now.
	virtual void Update(const ibProtocolNode& node) override;

	virtual int GetComponentType() const override {
		return COMPONENT_TYPE_SIZERITEM;
	}

	// The flag its node lays its child out with — the borders it has, and how it stretches (wxSizerItem::GetFlag).
	static int GetFlag(const ibProtocolNode& node);
	// The item a control is laid out in — what Update sets; the editor's highlight is drawn out to its border.
	static wxSizerItem* GetItem(const ibValueFrame* child);
};

class ibValueBoxSizer : public ibValueSizer {
public:
	ibValueBoxSizer(ibVisualHostClient& host, long long controlId) : ibValueSizer(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxSizer* GetSizer() const override { return m_sizer; }

private:
	wxBoxSizer* m_sizer = nullptr;
};

class ibValueWrapSizer : public ibValueSizer {
public:
	ibValueWrapSizer(ibVisualHostClient& host, long long controlId) : ibValueSizer(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxSizer* GetSizer() const override { return m_sizer; }

private:
	wxWrapSizer* m_sizer = nullptr;
};

class ibValueGridSizer : public ibValueSizer {
public:
	ibValueGridSizer(ibVisualHostClient& host, long long controlId) : ibValueSizer(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxSizer* GetSizer() const override { return m_sizer; }

private:
	wxGridSizer* m_sizer = nullptr;
};

// A framed group — a sizer with a box of its own, which what is in it is built in.
class ibValueStaticBoxSizer : public ibValueSizer {
public:
	ibValueStaticBoxSizer(ibVisualHostClient& host, long long controlId) : ibValueSizer(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxSizer*  GetSizer() const override { return m_sizer; }
	virtual wxWindow* GetChildParent() const override { return m_sizer != nullptr ? m_sizer->GetStaticBox() : nullptr; }

private:
	wxStaticBoxSizer* m_sizer = nullptr;
};

#endif
