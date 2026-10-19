#ifndef __SERVER_TREE_CONFIGURATION_H__
#define __SERVER_TREE_CONFIGURATION_H__

// THE DESIGNER'S TREES, AS THE SERVER HOLDS THEM — the desktop's ibMetaTreeBase and its heirs (designer/mainFrame/
// metaTree), their fill and their watch, copied with their names. The fill writes ROWS instead of wxTreeItems: the
// configuration's into the designer's frame (ibClientFrameDesigner::OnDraw), an external file's into its tab's view;
// they travel as a form's controls do — the first frame whole, then a patch. The client's trees (designer-thin) show
// them; what a person does in them comes back as the doors of the tree's schema (ibSchemaMetaData), and the rows follow
// from the metadata, as on the desktop the row followed the stage and never the click.
//
// A row (protocol.h, ibProtocol::ibSchema::ibKind::MetaData): NodeId — its object's id, or below 0 a group row's place among its
// siblings; Title, Picture; Group — the metatype New makes under it; Type — its object's class; Synonym; Position —
// its object's place among its parent's children. The node it is drawn into IS the root row, the metadata's own.
//
// What the desktop's trees did besides is the client's, with the window: the selection, the toolbar, the menu, the
// keys, the clipboard, the search box — a filter over these rows.

#include <memory>
#include <vector>

#include "backend/metadataConfiguration.h"   // ibMetaDataConfigurationBase — what the configuration's tree shows

#include "frmserver/frmserver.h"

class ibClientFrame;
class ibDataNode;
class ibMetaDocument;

// WHAT EVERY TREE DOES — the watch, the editors it opened, and the three ways a row is made.
class FRMSERVER_API ibMetaTreeBase {
public:

	virtual ~ibMetaTreeBase();

	ibMetaTreeBase(const ibMetaTreeBase&) = delete;
	ibMetaTreeBase& operator=(const ibMetaTreeBase&) = delete;

	// What it shows.
	virtual ibMetaData* GetMetaData() const = 0;

	// ⭐ THE WATCH — the desktop's answers, less the rows: a new form asked its kind, a removed object's editors closed,
	// a renamed one's retitled, and the client told to ask for its frame when the change was not its own.
	virtual void MetaDataChanged();
	virtual void MetaObjectChanged(ibMetaDataNotifier::ibMetaStage stage, ibValueMetaObject* object);

protected:

	// A clsid as a row carries it — a decimal: sixty-four bits are past what a JSON number keeps on the far side.
	static wxString ClsidText(const ibClassID& clsid);

	// Drawn for `frame`, whose client it shows to.
	explicit ibMetaTreeBase(ibClientFrame* frame);

	// The ONE place a subscription starts, changes and ends.
	void WatchMetaData(ibMetaData* metaData);

	// A form is asked what kind it is — the desktop's wizard, put to the person as a choice. False when the person
	// refused: the form is not wanted, and it has been taken away again.
	bool AskFormKind(ibValueMetaObject* object);

	// The open editors of this client — each re-gathers; those over this metadata close; those over the object and
	// what is under it close (what the row stood for); the one over the object.
	void NotifyDocuments() const;
	void CloseDocuments();
	void EraseItem(const ibValueMetaObject* object);
	ibMetaDocument* GetDocument(ibValueMetaObject* metaObject) const;

	// THE THREE WAYS A ROW IS MADE — a group row for a metatype, a group row that IS a metaobject (a table shows its
	// columns under itself), and a plain object row; and a command, with its sub-commands under it.
	ibDataNode& AppendGroupItem(ibDataNode& parent, const ibClassID& clsid, const wxString& name);
	ibDataNode& AppendGroupItem(ibDataNode& parent, const ibClassID& clsid, ibValueMetaObject* metaObject);
	ibDataNode& AppendItem(ibDataNode& parent, ibValueMetaObject* metaObject);
	void AppendCommandNode(ibDataNode& parent, ibValueMetaObject* command);

	// …and what an object's row offers to open — its context menu, as the desktop's tree asked it of the object
	// (CollectContextMenu): the row's Menu. Every object row has it, the roots too.
	void AppendMenu(ibDataNode& item, ibValueMetaObject* metaObject);

	ibClientFrame* const m_frame;   // borrowed: it outlives the tree

	// The form whose kind the person is being asked (AskFormKind) — no row until it is answered.
	const ibValueMetaObject* m_askedForm = nullptr;

private:

	// The subscription — declared here, defined in the module; and whose list it is on.
	class ibMetaTreeNotifier* m_notifier = nullptr;
	ibMetaData* m_watched = nullptr;
};

// THE CONFIGURATION'S TREE — the designer's frame holds it.
class FRMSERVER_API ibConfigurationTree : public ibMetaTreeBase {
public:

	// Drawn into `frame`, which holds it.
	explicit ibConfigurationTree(ibClientFrame* frame);

	virtual ibMetaDataConfigurationBase* GetMetaData() const override { return m_metaData.get(); }

	// What it shows, held while it shows it — and watched from now on.
	bool Load(std::shared_ptr<ibMetaDataConfigurationBase> metaData);

	// THE ROWS, into `root` — the configuration's row, everything under it. Read from the metadata as it stands now,
	// so no stage draws a row: the frame drawn after it does.
	void OnDraw(ibDataNode& root);

private:

	// The fill — the desktop's FillData, InitTree's group rows in it: a row is written whole, with everything under
	// it, before its next sibling, as a row's children are held by value.
	void FillData(ibDataNode& root);

	// HOW A METAOBJECT UNFOLDS — the one dispatcher, and the kinds it unfolds as.
	void ExpandMetaItem(ibValueMetaObject* metaItem, ibDataNode& item);

	void AddInterfaceItem(ibValueMetaObject* obj, ibDataNode& item);

	void AddCatalogItem(ibValueMetaObject* obj, ibDataNode& item);
	void AddChartOfAccountsItem(ibValueMetaObject* obj, ibDataNode& item);
	void AddDocumentItem(ibValueMetaObject* obj, ibDataNode& item);
	void AddEnumerationItem(ibValueMetaObject* obj, ibDataNode& item);
	void AddDataProcessorItem(ibValueMetaObject* obj, ibDataNode& item);
	void AddReportItem(ibValueMetaObject* obj, ibDataNode& item);
	void AddInformationRegisterItem(ibValueMetaObject* obj, ibDataNode& item);
	void AddAccumulationRegisterItem(ibValueMetaObject* obj, ibDataNode& item);
	void AddCalculationRegisterItem(ibValueMetaObject* obj, ibDataNode& item);
	void AddSequenceItem(ibValueMetaObject* obj, ibDataNode& item);

	// One group of an object, its rows; tables, each a group of its columns; commands, nesting.
	template <typename TArray>
	void AppendObjectGroup(ibDataNode& parent, const ibClassID& groupClsid, const wxString& label, const TArray& objects);
	template <typename TArray>
	void AppendTableGroup(ibDataNode& parent, const ibClassID& tableClsid, const wxString& label, const TArray& tables);
	template <typename TArray>
	void AppendCommandGroup(ibDataNode& parent, const wxString& label, const TArray& commands);

	// IS THIS A TABLE, and what are its columns.
	bool TableColumns(ibValueMetaObject* table, ibClassID& columnClsid, std::vector<ibValueMetaObject*>& columns) const;

	std::shared_ptr<ibMetaDataConfigurationBase> m_metaData;   // what it shows, held while it shows it
};

#endif
