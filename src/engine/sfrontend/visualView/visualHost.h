#ifndef __VISUAL_HOST_H__
#define __VISUAL_HOST_H__

// THE FORM'S HOST — who holds a form and takes its controls through their life, as the window's visual host did
// for the designer, the desktop and the web alike. With no window here, what it holds is the frame it draws; each
// control's node in it stands where the control's window stood.
//
//   CreateVisualHost   OnCreate → the children → OnCreated      the form opened
//   UpdateVisualHost   OnUpdate → the children → OnUpdated      the frame drawn
//   ClearVisualHost    the children → OnCleanup                 the form closed
//   CreateControl / RemoveControl                               a control added to / taken off an open form
//   SelectControl      OnSelected                               a control picked out (the client's Focus)
//
// A control asks the host what it is held by (IsDesignerHost), never the process: a designer and a client may be
// one process.

#include "sfrontend/sfrontend.h"

class SFRONTEND_API ibVisualHost {
public:

	virtual ~ibVisualHost() = default;

	// The form held.
	virtual class ibValueForm* GetValueForm() const = 0;
	// Held for the designer's picture of the form, not for a person working in it.
	virtual bool IsDesignerHost() const { return false; }

	void CreateVisualHost();
	bool UpdateVisualHost(class ibDataNode& frame);
	void ClearVisualHost();

	void CreateControl(class ibValueFrame* control);
	void RemoveControl(class ibValueFrame* control);
	void SelectControl(class ibValueFrame* control);

private:

	void GenerateControl(class ibValueFrame* control);
	bool RefreshControl(class ibValueFrame* control, class ibDataNode& node);
	void ClearControl(class ibValueFrame* control);
};

//********************************************************************************************
//*                                     The open form                                        *
//********************************************************************************************

// AN OPEN FORM, as the doc/view framework holds it: the document (whether it is modified, how it saves, who
// owns it, when it closes) and its view — the client's view of it, which draws the form into a frame.
// Nothing here is a window: a client draws the frame the view writes.

#include "sfrontend/docView/docView.h"
#include "sfrontend/visualView/ctrl/form.h"

// ⭐ THE VIEW IS THE FORM'S HOST — it holds the form for its client and draws its frame, so it takes the
// controls through their life (ibVisualHost): created as the view is, drawn with every frame, cleaned up when
// its document closes.
class SFRONTEND_API ibFormVisualEditView : public ibView, public ibVisualHost {
public:

	// Made by its document (ibFormVisualDocument::DoCreateView), which it holds as the window host held its own.
	explicit ibFormVisualEditView(class ibFormVisualDocument* document) : m_document(document) {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnUpdate(ibView* sender, wxObject* hint = nullptr) override;
	virtual bool OnClose(bool deleteWindow = true) override;
	virtual void OnClosingDocument() override;

	// ⭐ THE FORM AS IT STANDS NOW — its controls as the designer saved them, each with what it shows at this
	// moment (ibVisualHost::UpdateVisualHost). Written whole every time; what reaches a client is the difference.
	virtual void OnDraw(ibDataNode& frame) override;

	// The designer's picture of a form is a demonstration document.
	virtual bool IsDesignerHost() const override;

	// THE SAME DOC/VIEW, A FACADE OVER THE FORM'S ACTIVE CONTROL (ibValueForm::GetActiveControl): when it holds
	// a view of its own (ibValueFrame::GetControlView — the grid box, the text box), activating the form
	// activates that view, as a view that shows it would be.
	virtual void OnActivateView(bool activate, ibView* activeView, ibView* deactiveView) override;

	// The active control's view — null when it is a bare control, or there is none.
	ibView* GetActiveControlView() const;

	// The form this view shows.
	virtual ibValueForm* GetValueForm() const override;

private:

	class ibFormVisualDocument* m_document;
};

class SFRONTEND_API ibFormVisualCommandProcessor : public ibCommandProcessor {
public:
	virtual bool CanUndo() const override { return false; }
	virtual bool CanRedo() const override { return false; }
};

class SFRONTEND_API ibFormVisualDocument : public ibDocument {
public:

	ibFormVisualDocument(ibValueForm* valueForm);
	virtual ~ibFormVisualDocument();

	// A form document is a runtime document: its metadata is read, never edited, so this is a const accessor
	// of its own rather than the metadata-editing documents' contract.
	virtual const class ibMetaData* GetMetaData() const;

	virtual bool IsVisualDemonstrationDoc() const { return false; }

	virtual bool OnCreate(const wxString& WXUNUSED(path), long flags) override;
	virtual bool OnCloseDocument() override;

	virtual bool IsCloseOnOwnerClose() const override;

	virtual bool IsModified() const override { return m_documentModified; }
	virtual void Modify(bool modify) override;
	virtual bool Save() override;
	virtual bool SaveAs() override { return true; }

	// The facade on the document's side: the undo asked for is the active control's document's while it has
	// one; the form's own otherwise.
	virtual ibCommandProcessor* GetCommandProcessor() const override;

	virtual void SetDocParent(ibDocument* docParent) override;

	ibFormVisualEditView* GetFirstView() const;
	ibValueForm* GetValueForm() const;
	const ibUniqueKey& GetFormKey() const;
	bool CompareFormKey(const ibUniqueKey& formKey) const;

	// THE FORM REGISTRY — which open form a key names. Asked of the CURRENT CLIENT's documents (its document
	// manager's, and their children): two clients opening the same object hold two forms, and neither finds
	// the other's.
	static ibUniqueKey CreateFormUniqueKey(const ibBackendControlFrame* ownerControl,
		const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid);

	static ibValueForm* FindFormByUniqueKey(const ibBackendControlFrame* ownerControl,
		const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid);

	static ibValueForm* FindFormByUniqueKey(const ibUniqueKey& guid);
	static ibValueForm* FindFormByControlUniqueKey(const ibUniqueKey& guid);
	static ibValueForm* FindFormBySourceUniqueKey(const ibUniqueKey& guid);

	static ibFormVisualDocument* FindDocByUniqueKey(const ibUniqueKey& guid);

	// The open form a client names — by its key's guid, as its view draws it (ibFormVisualEditView::OnDraw).
	static ibValueForm* FindFormByGuid(const ibGuid& guid);

	// Every open form of the current client runs its due idle handlers (ibValueForm::RunIdleHandlers) — the
	// client's tick, on its session.
	static void RunIdleHandlers();

	static bool UpdateFormUniqueKey(const ibUniqueKeyPair& guid);

protected:
	virtual ibView* DoCreateView() override;
private:
	ibValuePtr<ibValueForm> m_valueForm;
};

class SFRONTEND_API ibFormVisualDocumentDemo : public ibFormVisualDocument {
public:

	ibFormVisualDocumentDemo(ibValueForm* valueForm) :
		ibFormVisualDocument(valueForm)
	{
	}

	virtual bool IsVisualDemonstrationDoc() const { return true; }
};

#endif
