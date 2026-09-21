#ifndef __SEQUENCE_H__
#define __SEQUENCE_H__

#include "commonObject.h"
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibValueMetaObjectRegisterTotals — the identity a second table is held by

////////////////////////////////////////////////////////////////////////////
//	Description : a sequence — up to which point the documents of a key
//	              have been posted in order (docs/private/sequence-arc.md)
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ WHAT IT ANSWERS: a document posted out of turn, or changed after later documents were already
// posted, leaves everything after it in doubt — and nothing about those later documents says so. A
// sequence is where that doubt is written down.
//
// TWO TABLES, and they read like an accumulation register's movements and totals:
//
//   the REGISTRATIONS — a row per document per key: the recorder, its moment, the dimensions.
//                       Written by the document's POSTING HANDLER, which is the only place that
//                       knows which key the document belongs to.
//   the BORDERS       — a row per key: the moment up to which everything is done. Kept by the
//                       engine, read as `Sequence.<Name>.Borders`, moved deliberately by SetBorder.
//
// NO FIGURES AND NO DESCRIPTION. A registration carries a key and a moment; if something describes a
// row and rows are picked by it, it is a DIMENSION. So a sequence takes dimensions and nothing else
// — no resources, no attributes (ResolveChild below).
//
// ⭐ AND NO AUTO-REGISTRATION, not even as a switch (decided with Max, 2026-09-17). Writing a
// registration row means knowing WHICH dimension value to write — which organization, which
// warehouse — and the engine cannot know: it would write an empty one and fold the whole base onto
// one key. The reference system offers that switch and every packaged solution turns it off. What
// the engine does instead is READ the rows the configuration wrote, which is what makes the border's
// two automatic moves safe: the keys of a touched document are its own registrations.
//
////////////////////////////////////////////////////////////////////////////

class ibValueMetaObjectSequence;

// ---- the border, moved and read (sequenceBorderRules.cpp) -------------------------------------------
//
// The two the WRITE PATH calls, each with the recorder whose registrations name the keys: after a
// posting's registrations are written, and before they are cleared. The engine reads those rows — it
// never invents a dimension value — and moves exactly the keys they name.
BACKEND_API void ibSequenceBorderWritten(const ibValueMetaObjectSequence* seq, const ibValue& recorder);
BACKEND_API void ibSequenceBorderCleared(const ibValueMetaObjectSequence* seq, const ibValue& recorder);

// …and the two the manager offers: said outright, and read. An empty moment takes the border away.
BACKEND_API void ibSequenceBorderSet(const ibValueMetaObjectSequence* seq, const std::vector<ibValue>& key, const ibValue& moment);
BACKEND_API ibValue ibSequenceBorderGet(const ibValueMetaObjectSequence* seq, const std::vector<ibValue>& key);

// ⭐⭐ THE REGISTRATIONS' READING SURFACE — the register's, and the MOMENT beside it.
//
// The same shape the document takes, and for the same reason: a metaobject holds what it DECLARES,
// and everything it declares is physical. The moment is CONSTRUCTED, so it belongs where the reading
// is — `ibRecorderQueryable` stands over the record queryable exactly so (commonObject.h).
//
// ⚠ AND IT ANSWERS BEFORE THE ATTRIBUTES. `PointInTime` is the name of the predefined attribute that
// TYPES the moment and stores nothing; asked of the attributes, the register's surface answered with
// a column over fields no table has — the value read back empty and an ORDER BY had nothing to sort
// (2026-09-18).
class BACKEND_API ibSequenceQueryable : public ibRegisterDataQueryable {
public:
	explicit ibSequenceQueryable(const ibValueMetaObjectSequence* seq);

	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override;

private:
	const ibValueMetaObjectSequence* m_seq;
};

// `Sequence.<Name>.Borders` — A ROW PER KEY: the dimensions, and the moment that key has got to.
//
// The moment is said the way a registration says it — the recorder and its date — because that is
// what the border IS: the last registration everything is done up to. The same columns as the
// registrations, so a border is laid out as a registration is and a dimension added to the sequence
// is one more column in both.
class BACKEND_API ibSequenceBordersQueryable : public ibBackendQueryable {
public:
	explicit ibSequenceBordersQueryable(const ibValueMetaObjectSequence* seq) : m_seq(seq) {}

	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override;
	virtual wxString GetQueryTableName() const override;
	virtual const ibUniqueKey& GetQueryTableGuid() const override;
	virtual wxString GetQueryName() const override;
	virtual ibMetaID GetQueryTableId() const override;
	virtual const ibMetaData* GetMetaData() const override;
	// ONE BORDER PER KEY — the dimensions are the whole key, and the moment is what the row says
	// about it. (A sequence with no dimensions has exactly one row.)
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override;

private:
	const ibValueMetaObjectSequence* m_seq;
};

class ibSequenceBordersSourceDescriptor : public ibQueryableSourceDescriptor {
public:
	explicit ibSequenceBordersSourceDescriptor(ibValueMetaObjectSequence* meta) : m_meta(meta), m_queryable(meta) {}
	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
	const ibSequenceBordersQueryable* GetQueryable() const { return &m_queryable; }
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
private:
	ibValueMetaObjectSequence* m_meta;
	ibSequenceBordersQueryable m_queryable;
};

class BACKEND_API ibValueMetaObjectSequence : public ibValueMetaObjectRegisterData {
public:

	// ⭐⭐ THE MOMENT OF A REGISTRATION, AS A COLUMN OF THE SELECTION — its period together with the
	// document that registered it, so two registrations of one period are still ordered, and a border
	// is COMPARED and SORTED by one thing rather than by a date with a tie-break written out at every
	// caller (Max, 2026-09-18: "the moment has to be in the selection itself").
	//
	// It is INSIDE the kind that publishes it, the way the document's own moment is inside its
	// reading surface (ibRecorderQueryable::ibBackendColumnPointInTime): a constructed column is not a
	// thing of the header, it is a thing of its owner.
	//
	// Nothing stores it — its layout is the period's fields and then the recorder's, which is what
	// makes the existing machinery work on it: sorting by the moment IS sorting by the date and then
	// by the reference's own two fields, and a comparison decomposes over the same ones. Its IDENTITY
	// is the PointInTime attribute's: the attribute names and types it, this builds it.
	class BACKEND_API ibBackendColumnPointInTime : public ibBackendQueryColumn {
	public:
		explicit ibBackendColumnPointInTime(const ibValueMetaObjectSequence* seq) : m_seq(seq) {}

		wxString GetName()         const override;
		wxString GetSynonym()      const override;
		wxString GetPhysicalName() const override;
		ibMetaID GetColumnId()     const override;
		ibTypeDescription& GetTypeDesc() const override;

		// Where it lies: nowhere of its own — in the PERIOD, then in the RECORDER.
		std::vector<ibColumnSlot> DescribeLayout() const override;
		Kind GetColumnKind() const override { return Kind::Synthetic; }

		bool ReadValue(const wxString& fieldName, const class ibMetaData* metaData,
		               class ibValue& retValue, class ibQueryResult& result, bool createData = false) const override;
		void BindValue(class ibQueryStatement& statement, const class ibMetaData* metaData,
		               const class ibValue& value, int& position) const override;

	private:
		const ibValueMetaObjectSequence* m_seq;
	};

private:
	// A LIST FORM ONLY — of the registrations: what is registered, for which key, at which moment.
	// There is no record to open on its own (a registration belongs to its recorder, as a movement
	// does), and the border is not edited by hand but said through SetBorder.
	enum {
		eFormList = 2,
	};

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(wxT("FormList"), _("Form list"), eFormList);
		return formList;
	}

public:

	ibValueMetaObjectSequence();
	virtual ~ibValueMetaObjectSequence();

	// ⭐ NO FIGURES — and nothing else refused. A registration is a key, a moment and what DESCRIBES
	// that row: dimensions and attributes, as any register line has. What it has no room for is a
	// RESOURCE: a registration measures nothing, it only says that this document stands here.
	//
	// ⚠ THE BORDER IS THE ONE WITH NOTHING BUT A KEY (Max, 2026-09-17: "you get the borders by the
	// dimensions; there are no attributes there"). That is a fact about the BORDERS table, and reading
	// it as a fact about the sequence left the registrations with no attributes at all (2026-09-18).
	//
	// 🛑 And refusing everything but the dimensions refused the PREDEFINED fields with them, so the
	// table came out with one column: `CREATE INDEX SEQUENCE…_INDEX failed — Unknown columns in
	// index`, because the base's key index names the recorder and the line number.
	virtual ibClassID ResolveChild(const ibClassID& clsid) const override {
		if (clsid == g_metaResourceCLSID)
			return 0;
		return ibValueMetaObjectRegisterData::ResolveChild(clsid);
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags) override;
	virtual bool OnLoadMetaObject(ibMetaData* metaData) override;
	virtual bool OnSaveMetaObject(int flags) override;
	virtual bool OnDeleteMetaObject() override;

	// A registration is written as its recorder's set, and it has a moment of its own — the two facts
	// the register base asks about.
	virtual bool HasRecordManager() const override { return false; }
	virtual bool HasPeriod() const override { return true; }
	virtual bool HasRecorder() const override { return true; }

	// ⭐ A FULL RECORD SET, with the two modules a register's set has (Max, 2026-09-18: "it is a
	// record set in full"): the set's own code runs for every registration written, whoever writes it,
	// and the manager's is the sequence as a whole — where a configuration puts what it wants said
	// about the border beside GetBorder / SetBorder.
	virtual const ibValueMetaObjectModule* GetObjectModule() const override { return m_propertyObjectModule->GetMetaObject(); }
	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const override { return m_propertyManagerModule->GetMetaObject(); }

	// …and the two of them in the context menu, where every kind with modules offers them.
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items) override;

	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const override;
	virtual ibSourceDataObject* CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const override;

	//support form
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString,
		ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const override;

	// ⭐ WHO TAKES THE BORDER BACK. On (the default) the platform does it itself, by the rows the
	// document already carries. Off, it does not — and the configuration says when the sequence is
	// broken, through SetBorder. That is a real choice rather than a switch nobody touches: a retreat
	// is what sets a mass reposting going, and a solution may want to decide the moment itself
	// (Max, 2026-09-18).
	//
	// ⚠ IT GOVERNS THE RETREAT ONLY. Moving FORWARD never lies on its own — the border steps onto a
	// document that was posted in its turn, and no further. What lies is a border that stays ahead of
	// a document somebody has changed, and switching this off is saying "I will say when".
	bool IsAutomaticBorder() const { return m_propertyAutomaticBorder->GetValueAsBoolean(); }

	// ---- the borders, the second table --------------------------------------------------------------
	// Its identity is held the way a derived table's is: a holder created with the sequence, saved with
	// it and numbered with it, which is what gives the table a name and an id of its own.
	wxString GetBordersTableName() const {
		return wxString::Format(wxT("%s%i_%s"), GetClassName(), GetMetaID(), m_borders->GetName());
	}
	const ibValueMetaObjectRegisterTotals* GetBordersObject() const { return m_borders; }
	const ibSequenceBordersQueryable* GetBordersQueryable() const { return m_bordersSource.GetQueryable(); }
	bool HasBorders() const { return m_borders->GetMetaID() != 0; }

	virtual void ContributeTables(class ibSchemaSnapshot& out) const override;
	virtual bool OnAfterRunMetaObject(int flags) override;
	virtual bool OnBeforeCloseMetaObject() override;

	// …and everything that reads the registrations meets the surface that knows about the moment.
	virtual const ibBackendQueryable* GetQueryable() const override { return m_ownQueryable.GetQueryable(); }

	// ⭐ AND THE FIELD TREE OFFERS IT TOO. What a query can RESOLVE and what the tree SHOWS are two
	// readers of one surface; taken from anywhere else they drift, and the moment was already
	// readable while `Sequence.<Name>` still listed only the stored columns (Max saw it in the tree,
	// 2026-09-18). Written the way the document writes it (commonObjectAction.cpp).
	virtual void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;

	// …and the metatype's own data: the switch, the moment it publishes, the two modules, and the
	// borders' holder — written for its identity, which is what names their table.
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	// The manager — `Sequences.<Name>`, where the border is read and said (sequenceManager.h).
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() const override;
	// …and the registrations of ONE recorder, as a set: what the document's posting handler fills.
	virtual ibValueRecordSetObject* CreateRecordSetObjectRegValue(
		const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) const override;

	// ⭐⭐ WHAT A REGISTRATION IS MADE OF, said to everything that asks: this list becomes the table's
	// columns, the queryable's columns and the set's members. The base leaves it empty on purpose —
	// each register names its own — and leaving it so gave a table with nothing but the dimension:
	// `CREATE INDEX SEQUENCE…_INDEX failed — Unknown columns in index`, the index being over the
	// recorder and the line number (2026-09-18).
	//
	// ⚠ The MOMENT is not here. It is published to readers as a column (ibBackendColumnPointInTime) but
	// stored nowhere — it IS the period and the recorder, and listing it would ask for a column to
	// hold what those two already hold.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRegisterData::FillArrayObjectByPredefinedAttribute(array);
		array.push_back(m_propertyAttributeLineActive->GetMetaObject());
		array.push_back(m_propertyAttributePeriod->GetMetaObject());
		array.push_back(m_propertyAttributeRecorder->GetMetaObject());
		array.push_back(m_propertyAttributeLineNumber->GetMetaObject());
		return true;
	}

	// 🛑⭐⭐ THE KEY A SET IS ADDRESSED BY IS THE RECORDER — not the dimensions, whatever the word
	// "dimension" suggests here. A set of registrations belongs to its document exactly as a set of
	// movements does: it holds that document's rows, and writing it REPLACES them. The write path
	// builds its `WHERE` from this list (ibValueRecordSetObject::DeleteData), so answering with the
	// dimensions left the key unfound — and a set with no key addresses the WHOLE register: every
	// posting silently wiped every other document's registrations, leaving exactly one row in the
	// table (measured 2026-09-18). Answered here the way the accumulation register answers it.
	virtual bool FillArrayObjectByDimension(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		array = { m_propertyAttributeRecorder->GetMetaObject() };
		return true;
	}

	// The moment, as the selection sees it — named and typed here, built out of the period and the
	// recorder by ibBackendColumnPointInTime. Declared the way the document declares its own: a special
	// type, registered like an attribute, stored nowhere.
	ibValueMetaObjectAttributePredefined* GetPointInTime() const { return m_propertyAttributePointInTime->GetMetaObject(); }
	const ibBackendColumnPointInTime* GetMomentColumn() const { return &m_momentColumn; }

private:
	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));

	ibPropertyBoolean* m_propertyAutomaticBorder = ibPropertyObject::CreateProperty<ibPropertyBoolean>(
		m_categoryData, wxT("AutomaticBorder"), _("Automatic border"),
		_("The platform takes the border BACK by itself: a document at or before it - written again, posted again, "
		  "unposted, deleted - sends the border to the registration before it, by the keys the document's own "
		  "registrations name. Off: the configuration says when the sequence is broken, through SetBorder; the border "
		  "still moves forward as documents are posted in their turn. A retreat is what sets a mass reposting going, "
		  "so a solution may want to decide that moment itself."), true);

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyObjectModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(
		m_categoryContext, wxT("RecordSetModule"), _("Record set module"),
		_("Code that runs with a set of registrations: its BeforeWrite and OnWrite handlers, and the procedures they "
		  "call. It runs for every set written, whoever writes it - a document's posting or a script."));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(
		m_categoryContext, wxT("ManagerModule"), _("Manager module"),
		_("Code of the sequence as a whole rather than of one set: its exported procedures and functions are called on "
		  "the manager, as Sequences.<Name>.<Function>() - beside GetBorder and SetBorder."));

	ibPropertyContainer<>* m_propertyAttributePointInTime = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(
		m_categoryCommon, ibValueMetaObjectCompositeData::CreateSpecialType(wxT("PointInTime"), _("Point in time"),
			_("The registration's moment: the period it carries together with the document that registered it, so two "
			  "registrations of one period are still ordered. Not stored - built from the two; a border is compared and "
			  "sorted by it."), value_to_clsid(wxT("PointInTime"))));

	ibBackendColumnPointInTime m_momentColumn{ this };

	// ⭐ THIS KIND'S OWN READING SURFACE, and the reason it exists: only the descriptor of the kind
	// that BUILDS a column can hand it out. Registered in OnAfterRun after the register base has
	// registered its own — sources are kept by (namespace, name), so the later registration is the
	// one that answers, and every reader then meets the surface that knows about the moment.
	ibMetaCommandDescriptor<ibSequenceQueryable, ibValueMetaObjectSequence> m_ownQueryable{ this };

	ibValuePtr<ibValueMetaObjectRegisterTotals> m_borders{
		CreateMetaObjectAndSetParent<ibValueMetaObjectRegisterTotals>(wxT("Borders"), _("Borders")) };

	ibSequenceBordersSourceDescriptor m_bordersSource{ this };
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

// THE REGISTRATIONS OF ONE RECORDER. A sequence is written the way a register is — the document owns
// its rows, fills them in its posting handler and the set is written with the posting; there is no
// record to write on its own.
class ibValueRecordSetObjectSequence : public ibValueRecordSetObject {
public:
	ibValueRecordSetObjectSequence(const ibValueMetaObjectSequence* metaObject, const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) :
		ibValueRecordSetObject(metaObject, uniqueKey) {
		m_members.Bind(this, &ibValueRecordSetObjectSequence::FillMembers);
	}
	ibValueRecordSetObjectSequence(const ibValueRecordSetObjectSequence& source) :
		ibValueRecordSetObject(source) {
		m_members.Bind(this, &ibValueRecordSetObjectSequence::FillMembers);
	}

	virtual ibValueRecordSetObject* CopyRegisterValue() override {
		return new ibValueRecordSetObjectSequence(*this);
	}

	const ibValueMetaObjectSequence* GetSequenceMetaObject() const {
		return static_cast<const ibValueMetaObjectSequence*>(GetMetaObject());
	}

	// ⭐⭐ THE SET MOVES ITS OWN BORDER. The write path writes sets and clears them; WHAT that means for
	// a sequence is the sequence's own business, said here rather than asked about there — the caller
	// would have had to recognise the kind to ask, and recognising a kind by casting is the question
	// put to the wrong side (Max, 2026-09-18).
	//
	// The order matters both times: forward AFTER the rows are written (they are what the border steps
	// onto), back BEFORE they are cleared (they are the last thing that still names the keys).
	virtual bool WriteRecordSet(bool replace = true, bool clearTable = true) override;
	virtual bool DeleteRecordSet() override;

	// The recorder this set is addressed by — empty for a set nobody keyed.
	ibValue Recorder() const;

	// The verbs a set answers to — DECLARED here (FillMembers) and ANSWERED beside it (CallAsFunc).
	// They are one pair: the member table gives a method its call NUMBER, and a declaration with
	// nobody to run it answers an empty value instead of refusing.
	void FillMembers(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
};

#endif // !__SEQUENCE_H__
