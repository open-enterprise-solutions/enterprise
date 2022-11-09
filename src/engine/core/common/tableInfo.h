#ifndef _TABLE_MODEL_H__
#define _TABLE_MODEL_H__

#include "common/actionInfo.h"

#include "compiler/value.h"
#include "compiler/valueTypeDescription.h"

#include "common/tableNotifier.h"

class Guid;

//Общая сущность для таблиц, списка, древа таблиц 
class IValueModel : public CValue,
	public IActionSource, public wxDataViewModel {
	wxDECLARE_ABSTRACT_CLASS(IValueModel);
public:

	class IValueModelColumnCollection : public CValue {
		wxDECLARE_ABSTRACT_CLASS(IValueModelColumnCollection);
	public:
		class IValueModelColumnInfo : public CValue {
			wxDECLARE_ABSTRACT_CLASS(IValueModelColumnInfo);
		public:

			virtual unsigned int GetColumnID() const = 0;
			virtual void SetColumnID(unsigned int col) {};
			virtual wxString GetColumnName() const = 0;
			virtual void SetColumnName(const wxString& name) {};
			virtual wxString GetColumnCaption() const = 0;
			virtual void SetColumnCaption(const wxString& caption) {};
			virtual CValueTypeDescription* GetColumnTypes() const = 0;
			virtual void SetColumnTypes(CValueTypeDescription* td) {};
			virtual int GetColumnWidth() const = 0;
			virtual void SetColumnWidth(int width) {};

			IValueModelColumnInfo();
			virtual ~IValueModelColumnInfo();

			virtual CMethods* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
				PrepareNames(); return m_methods;
			}
			virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов

			virtual CValue GetAttribute(attributeArg_t& aParams); //значение атрибута

		protected:
			CMethods* m_methods;
		};
	public:

		virtual IValueModelColumnInfo* AddColumn(const wxString& colName,
			CValueTypeDescription* types,
			const wxString& caption,
			int width = wxDVC_DEFAULT_WIDTH) {
			return NULL;
		};

		virtual void RemoveColumn(unsigned int col) {};
		virtual bool HasColumnID(unsigned int col) const {
			return GetColumnByID(col) != NULL;
		}

		virtual IValueModelColumnInfo* GetColumnByID(unsigned int col) const;
		virtual IValueModelColumnInfo* GetColumnByName(const wxString& colName) const;

		virtual IValueModelColumnInfo* GetColumnInfo(unsigned int idx) const = 0;
		virtual unsigned int GetColumnCount() const = 0;

		IValueModelColumnCollection() : CValue(eValueTypes::TYPE_VALUE, true) {}
		virtual ~IValueModelColumnCollection() {}

		//Работа с итераторами 
		virtual bool HasIterator() const {
			return true;
		}

		virtual CValue GetItAt(unsigned int idx) {
			if (idx > GetColumnCount())
				return NULL;
			return GetColumnInfo(idx);
		}

		virtual unsigned int GetItSize() const {
			return GetColumnCount();
		}
	};

	class IValueModelReturnLine : public CValue {
		wxDECLARE_ABSTRACT_CLASS(IValueModelReturnLine);
	public:

		wxDataViewItem GetLineItem() const {
			return m_lineItem;
		};

		IValueModelReturnLine(const wxDataViewItem& lineItem) : CValue(eValueTypes::TYPE_VALUE, true), m_lineItem(lineItem) {
			wxRefCounter* refCounter = static_cast<wxRefCounter*>(m_lineItem.GetID());
			if (refCounter != NULL)
				refCounter->IncRef();
		}
		virtual ~IValueModelReturnLine() {
			wxRefCounter* refCounter = static_cast<wxRefCounter*>(m_lineItem.GetID());
			if (refCounter != NULL)
				refCounter->DecRef();
		}

		virtual IValueModel* GetOwnerModel() const = 0;

		//set meta/get meta
		virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal) {}
		virtual CValue GetValueByMetaID(const meta_identifier_t& id) const { return CValue(); }

		//operator '=='
		virtual inline bool CompareValueEQ(const CValue& cParam) const override {
			IValueModelReturnLine* tableReturnLine = NULL;
			if (cParam.ConvertToValue(tableReturnLine)) {
				IValueModel* ownerTable = GetOwnerModel();
				wxASSERT(ownerTable);
				if (ownerTable == tableReturnLine->GetOwnerModel()
					&& m_lineItem == tableReturnLine->GetLineItem()) {
					return true;
				}
			}
			return false;
		}

		//operator '!='
		virtual inline bool CompareValueNE(const CValue& cParam) const override {
			IValueModelReturnLine* tableReturnLine = NULL;
			if (cParam.ConvertToValue(tableReturnLine)) {
				IValueModel* ownerTable = GetOwnerModel();
				wxASSERT(ownerTable);
				if (ownerTable == tableReturnLine->GetOwnerModel()
					&& m_lineItem == tableReturnLine->GetLineItem()) {
					return false;
				}
			}
			return true;
		}

	protected:
		wxDataViewItem m_lineItem;
	};

public:

	IValueModel();
	virtual ~IValueModel() {}

	void AppendNotifier(wxTableModelNotifier* notify) {
		if (m_srcNotifier != NULL)
			wxDELETE(m_srcNotifier);
		m_srcNotifier = notify;
	}

	void RemoveNotifier(wxTableModelNotifier* notify) {
		if (m_srcNotifier == notify)
			wxDELETE(m_srcNotifier);
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	virtual unsigned int GetColumnCount() const override {
		IValueModelColumnCollection* colCollection = GetColumnCollection();
		if (colCollection != NULL)
			return colCollection->GetColumnCount();
		return 0;
	};

	// return type as reported by wxVariant
	virtual wxString GetColumnType(unsigned int col) const override {
		return wxT("string");
	};
	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual wxDataViewItem GetSelection() const;
	virtual void RowValueStartEdit(const wxDataViewItem& item, unsigned int col = 0);

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool ValidateReturnLine(IValueModelReturnLine* retLine) const {
		return true;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual wxDataViewItem FindRowValue(const CValue& cVal, const wxString &colName = wxEmptyString) const {
		return wxDataViewItem(NULL);
	};

	virtual wxDataViewItem FindRowValue(IValueModelReturnLine* retLine) const {
		return wxDataViewItem(NULL);
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool AutoCreateColumns() const { return true; }
	virtual bool EditableLine(const wxDataViewItem& item, unsigned int col) const { return true; }

	virtual void ActivateItem(CValueForm* formOwner,
		const wxDataViewItem& item, unsigned int col) {
		IValueModel::RowValueStartEdit(item, col);
	}

	virtual void AddValue(unsigned int before = 0) = 0;
	virtual void CopyValue() = 0;
	virtual void EditValue() = 0;
	virtual void DeleteValue() = 0;

	virtual IValueModelReturnLine* GetRowAt(const wxDataViewItem& line) = 0;
	virtual IValueModelColumnCollection* GetColumnCollection() const = 0;

	//ref counter 
	virtual void DecrRef() override {
		if (CValue::GetRefCount() > 1) {
			CValue::DecrRef();
		}
		else if (wxRefCounter::GetRefCount() < 2) {
			CValue::DecrRef();
		}
	}

	//Update model 
	virtual void RefreshModel() {};

	/**
	* Override actions
	*/

	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& action, class CValueForm* srcForm);

protected:
	wxTableModelNotifier* m_srcNotifier;
};

//def actions
enum
{
	eAddValue = 1,
	eCopyValue,
	eEditValue,
	eDeleteValue
};

typedef std::map<
	meta_identifier_t, CValue
> modelArray_t;

//Поддержка таблиц
class IValueTable : public IValueModel {
	wxDECLARE_ABSTRACT_CLASS(IValueTable);
public:

	struct wxValueTableRow : public wxRefCounter {

		wxValueTableRow(const modelArray_t& nodeValues) :
			m_valueTable(NULL), m_nodeValues(nodeValues) {
		}

		wxValueTableRow(const wxValueTableRow& tableRow) :
			m_valueTable(tableRow.m_valueTable), m_nodeValues(tableRow.m_nodeValues) {
		}

		bool SetValue(const CValue& variant, const meta_identifier_t& id, bool notify = false) {
			auto foundedColumn = m_nodeValues.find(id);
			if (foundedColumn != m_nodeValues.end()) {
				CValue& cValue = foundedColumn->second;
				wxASSERT(m_valueTable);
				if (notify && cValue != variant)
					m_valueTable->RowValueChanged(this, id);
				cValue.SetValue(variant);
				return true;
			}
			return false;
		}

		bool SetValue(const wxVariant& variant, unsigned int col) {
			auto foundedColumn = m_nodeValues.find(col);
			if (foundedColumn != m_nodeValues.end()) {
				CValue& cValue = foundedColumn->second;
				std::vector<CValue> foundedObjects;
				if (cValue.FindValue(variant.GetString(), foundedObjects))
					cValue.SetValue(foundedObjects.at(0));
				return true;
			}
			return false;
		}

		bool IsEmptyValue(const meta_identifier_t& col) const {
			auto foundedColumn = m_nodeValues.find(col);
			return foundedColumn != m_nodeValues.end();
		}

		bool IsEmptyValue(unsigned int col) const {
			auto foundedColumn = m_nodeValues.find(col);
			return foundedColumn != m_nodeValues.end();
		}

		void EraseValue(const meta_identifier_t& id) {
			auto foundedColumn = m_nodeValues.find(id);
			if (foundedColumn != m_nodeValues.end()) {
				m_nodeValues.erase(id);
			}
		}

		void EraseValue(unsigned int col) {
			auto foundedColumn = m_nodeValues.find(col);
			if (foundedColumn != m_nodeValues.end()) {
				m_nodeValues.erase(col);
			}
		}

		CValue GetValue(const meta_identifier_t& id) const {
			CValue cValue; GetValue(cValue, id);
			return cValue;
		}

		wxVariant GetValue(unsigned int col) const {
			wxVariant vValue; GetValue(vValue, col);
			return vValue;
		}

		void GetValue(CValue& variant, const meta_identifier_t& id) const {
			auto foundedColumn = m_nodeValues.find(id);
			if (foundedColumn != m_nodeValues.end()) {
				const CValue& cValue = foundedColumn->second;
				variant.SetValue(cValue);
			}
		}

		void GetValue(wxVariant& variant, unsigned int col) const {
			auto foundedColumn = m_nodeValues.find(col);
			if (foundedColumn != m_nodeValues.end()) {
				const CValue& cValue = foundedColumn->second;
				variant = cValue.GetString();
			}
		}

	private:
		friend class IValueTable;
	protected:
		modelArray_t m_nodeValues;
		IValueTable* m_valueTable;
	};

public:

	template <class retType = wxValueTableRow>
	inline retType* GetViewData(const wxDataViewItem& item) const {
		if (!item.IsOk())
			return NULL;
		try {
			return static_cast<retType*>(item.GetID());
		}
		catch (...) {
			return NULL;
		}
	}

	IValueTable() : IValueModel() {}
	virtual ~IValueTable() {
		Clear(false);
	}

	/////////////////////////////////////////////////////////

	virtual bool IsEmpty() const override {
		return GetRowCount() == 0;
	}

	/////////////////////////////////////////////////////////

	virtual bool ValidateReturnLine(IValueModelReturnLine* retLine) const {
		wxValueTableRow* node = GetViewData(retLine->GetLineItem());
		wxASSERT(node);
		return node->m_valueTable != NULL;
	}

	/////////////////////////////////////////////////////////

	void Clear(bool notify = true) {
		if (notify) /* wxDataViewModel:: */ BeforeReset();
		for (auto node : m_nodeValues) {
			node->m_valueTable = NULL;
			node->DecRef();
		}
		m_nodeValues.clear();
		if (notify) /* wxDataViewModel:: */ AfterReset();
	}

	/////////////////////////////////////////////////////////

	void RowChanged(wxValueTableRow* item) {
		/* wxDataViewModel:: */ ItemChanged(wxDataViewItem(item));
	}

	void RowValueChanged(wxValueTableRow* item, unsigned int col) {
		/* wxDataViewModel:: */ ValueChanged(wxDataViewItem(item), col);
	}

	/////////////////////////////////////////////////////////

	long Append(wxValueTableRow* child, bool notify = true) {
		wxASSERT(child);
		child->m_valueTable = this;
		m_nodeValues.push_back(child);
		if (notify) /* wxDataViewModel:: */ ItemAdded(
			wxDataViewItem(NULL),
			wxDataViewItem(child)
		);
		return m_nodeValues.size() - 1;
	}

	long Insert(wxValueTableRow* child, unsigned int n, bool notify = true) {
		wxASSERT(child);
		child->m_valueTable = this;
		m_nodeValues.insert(m_nodeValues.begin() + n, child);
		if (notify) /* wxDataViewModel:: */ ItemAdded(
			wxDataViewItem(NULL),
			wxDataViewItem(child)
		);
		return n + 1;
	}

	bool Remove(wxValueTableRow*& child, bool notify = true) {
		wxASSERT(child);
		auto foundedIt = std::find(
			m_nodeValues.begin(),
			m_nodeValues.end(), child
		);

		if (notify && m_srcNotifier != NULL) {
			bool cancel = m_srcNotifier->SendEvent(wxEVT_DATAVIEW_ITEM_START_DELETING,
				wxDataViewItem(child)
			);
			if (cancel)
				return false; 
		}

		if (notify) /* wxDataViewModel:: */ ItemDeleted(
			wxDataViewItem(NULL),
			wxDataViewItem(child)
		);

		if (foundedIt != m_nodeValues.end()) {
			m_nodeValues.erase(foundedIt);
			child->m_valueTable = NULL;
			child->DecRef();
			return true;
		}
		return false;
	}

	long GetRowCount() const {
		return m_nodeValues.size();
	}

	/////////////////////////////////////////////////////////

	// override sorting to always sort branches ascendingly
	virtual int Compare(const wxDataViewItem& item1, const wxDataViewItem& item2,
		unsigned int col, bool ascending) const override {
		wxASSERT(item1.IsOk() && item2.IsOk());
		// should never happen
		if (IsContainer(item1) && IsContainer(item2)) {
			wxVariant value1, value2;
			GetValue(value1, item1, 0);
			GetValue(value2, item2, 0);
			wxString str1 = value1.GetString();
			wxString str2 = value2.GetString();
			int res = str1.Cmp(str2);
			if (res)
				return res;
			// items must be different
			wxUIntPtr litem1 = (wxUIntPtr)item1.GetID();
			wxUIntPtr litem2 = (wxUIntPtr)item2.GetID();
			return litem1 - litem2;
		}

		return wxDataViewModel::Compare(item1, item2, col, ascending);
	}

	// derived classes should override these methods instead of
	// {Get,Set}Value() and GetAttr() inherited from the base class
	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) const = 0;

	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) = 0;

	virtual bool GetAttrByRow(const wxDataViewItem& WXUNUSED(row), unsigned int WXUNUSED(col),
		wxDataViewItemAttr& WXUNUSED(attr)) const {
		return false;
	}

	virtual bool IsEnabledByRow(const wxDataViewItem& WXUNUSED(row),
		unsigned int WXUNUSED(col)) const {
		return true;
	}

	// helper methods provided by list models only
	virtual long GetRow(const wxDataViewItem& item) const {
		wxValueTableRow* node = GetViewData(item);
		if (node == NULL)
			return wxNOT_FOUND;
		auto foundedIt = std::find(m_nodeValues.begin(), m_nodeValues.end(), node);
		if (foundedIt != m_nodeValues.end())
			return std::distance(m_nodeValues.begin(), foundedIt);
		return wxNOT_FOUND;
	}

	virtual wxDataViewItem GetItem(long row) const {
		wxASSERT(row < (long)m_nodeValues.size());
		if (row >= 0 && row < (long)m_nodeValues.size()) {
			auto foundedIt = m_nodeValues.begin();
			std::advance(foundedIt, row);
			return wxDataViewItem(*foundedIt);
		}
		return wxDataViewItem(NULL);
	}

	// implement base methods
	virtual unsigned int GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const override {
		if (parent.IsOk())
			return 0;
		wxValueTableRow* node = (wxValueTableRow*)parent.GetID();
		unsigned int count = m_nodeValues.size();
		if (count == 0)
			return 0;
		for (unsigned int pos = 0; pos < count; pos++) {
			wxValueTableRow* child = m_nodeValues.at(pos);
			array.Add(wxDataViewItem((void*)child));
		}
		return count;
	}

	// implement some base class pure virtual directly
	virtual wxDataViewItem GetParent(const wxDataViewItem& WXUNUSED(item)) const override {
		// items never have valid parent in this model
		return wxDataViewItem(NULL);
	}

	virtual bool IsContainer(const wxDataViewItem& item) const override {
		// only the invisible (and invalid) root item has children
		return !item.IsOk();
	}

	// and implement some others by forwarding them to our own ones
	virtual void GetValue(wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) const override {
		GetValueByRow(variant, item, col);
	}

	virtual bool SetValue(const wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) override {
		return SetValueByRow(variant, item, col);
	}

	virtual bool GetAttr(const wxDataViewItem& item, unsigned int col,
		wxDataViewItemAttr& attr) const override {
		return GetAttrByRow(item, col, attr);
	}

	virtual bool IsEnabled(const wxDataViewItem& item, unsigned int col) const override {
		return IsEnabledByRow(item, col);
	}

private:
	std::vector< wxValueTableRow*> m_nodeValues;
};

//Поддержка дерева таблиц 
class IValueTree : public IValueModel {
	wxDECLARE_ABSTRACT_CLASS(IValueTable);
public:

	struct wxValueTreeNode : public wxRefCounter {

		wxValueTreeNode(IValueTree* valueTree) :
			m_valueTree(valueTree) {
		}

		wxValueTreeNode(wxValueTreeNode* parent, const modelArray_t& nodeValues = modelArray_t()) :
			m_valueTree(NULL), m_parent(parent), m_nodeValues(nodeValues) {
			if (m_parent != NULL)
				m_parent->Append(this);
		}

		virtual ~wxValueTreeNode() {
			// free all our children nodes
			size_t count = m_children.size();
			for (size_t i = 0; i < count; i++) {
				wxValueTreeNode* child = m_children[i];
				wxASSERT(child);
				child->m_valueTree = NULL;
				child->DecRef();
			}
		}

		bool IsContainer() const {
			return m_children.size() > 0;
		}

		void SetParent(wxValueTreeNode* parent) {
			if (m_parent)
				m_parent->Remove(this);
			if (parent != NULL)
				parent->Append(this);
			m_parent = parent;
		}

		wxValueTreeNode* GetParent() const {
			return m_parent;
		}

		std::vector<wxValueTreeNode*>& GetChildren() {
			return m_children;
		}

		wxValueTreeNode* GetChild(unsigned int n) const {
			return m_children.at(n);
		}

		void Append(wxValueTreeNode* child, bool notify = true) {
			child->m_valueTree = m_valueTree;
			m_children.push_back(child);
			if (notify) m_valueTree->ItemAdded(
				wxDataViewItem(this),
				wxDataViewItem(child)
			);
		}

		void Insert(wxValueTreeNode* child, unsigned int n, bool notify = true) {
			child->m_valueTree = m_valueTree;
			m_children.insert(m_children.begin() + n, child);
			if (notify) m_valueTree->ItemAdded(
				wxDataViewItem(this),
				wxDataViewItem(child)
			);
		}

		void Remove(wxValueTreeNode* child, bool notify = true) {
			auto foundedIt = std::find(m_children.begin(), m_children.end(), child);
			if (notify) m_valueTree->ItemDeleted(
				wxDataViewItem(this),
				wxDataViewItem(child)
			);
			if (foundedIt != m_children.end())
				m_children.erase(foundedIt);
			child->m_valueTree = NULL;
			child->DecRef();
		}

		unsigned int GetChildCount() const {
			return m_children.size();
		}

	public:     // public to avoid getters/setters

		bool SetValue(const CValue& variant, const meta_identifier_t& id, bool notify = false) {
			auto foundedColumn = m_nodeValues.find(id);
			if (foundedColumn != m_nodeValues.end()) {
				CValue& cValue = foundedColumn->second;
				if (notify && cValue != variant)
					m_valueTree->RowValueChanged(this, id);
				cValue.SetValue(variant);
				return true;
			}
			return false;
		}

		bool SetValue(const wxVariant& variant, unsigned int col) {
			auto foundedColumn = m_nodeValues.find(col);
			if (foundedColumn != m_nodeValues.end()) {
				CValue& cValue = foundedColumn->second;
				std::vector<CValue> foundedObjects;
				if (cValue.FindValue(variant.GetString(), foundedObjects))
					cValue.SetValue(foundedObjects.at(0));
				return true;
			}
			return false;
		}

		CValue GetValue(const meta_identifier_t& id) const {
			CValue cValue; GetValue(cValue, id);
			return cValue;
		}

		wxVariant GetValue(unsigned int col) const {
			wxVariant vValue; GetValue(vValue, col);
			return vValue;
		}

		void GetValue(CValue& variant, unsigned int id) const {
			auto foundedColumn = m_nodeValues.find(id);
			if (foundedColumn != m_nodeValues.end()) {
				const CValue& cValue = foundedColumn->second;
				variant.SetValue(cValue);
			}
		}

		void GetValue(wxVariant& variant, unsigned int col) const {
			auto foundedColumn = m_nodeValues.find(col);
			if (foundedColumn != m_nodeValues.end()) {
				const CValue& cValue = foundedColumn->second;
				variant = cValue.GetString();
			}
		}

	private:
		friend class IValueTree;
	private:
		wxValueTreeNode* m_parent;
		std::vector<wxValueTreeNode*> m_children;
	protected:
		modelArray_t m_nodeValues;
		IValueTree* m_valueTree;
	};

public:

	template <class retType = wxValueTreeNode>
	inline retType* GetViewData(const wxDataViewItem& item) const {
		if (!item.IsOk())
			return NULL;
		try {
			return static_cast<retType*>(item.GetID());
		}
		catch (...) {
			return NULL;
		}
	}

	IValueTree() : IValueModel() {
		m_root = new wxValueTreeNode(this);
	}

	virtual ~IValueTree() {
		delete m_root;
	}

	/////////////////////////////////////////////////////////

	virtual bool IsEmpty() const override {
		return m_root->GetChildCount() == 0;
	}

	/////////////////////////////////////////////////////////

	virtual bool ValidateReturnLine(IValueModelReturnLine* retLine) const {
		wxValueTreeNode* node = GetViewData(retLine->GetLineItem());
		wxASSERT(node);
		return node->m_valueTree != NULL;
	}


	/////////////////////////////////////////////////////////

	wxValueTreeNode* GetRoot() const {
		return m_root;
	}

	void RowChanged(wxValueTreeNode* item) {
		/* wxDataViewModel:: */ ItemChanged(wxDataViewItem(item));
	}

	void RowValueChanged(wxValueTreeNode* item, unsigned int col) {
		/* wxDataViewModel:: */ ValueChanged(wxDataViewItem(item), col);
	}

	// helper methods to change the model
	void Delete(const wxDataViewItem& item) {
		wxValueTreeNode* node = (wxValueTreeNode*)item.GetID();
		if (!node)      // happens if item.IsOk()==false
			return;
		wxDataViewItem parent(node->GetParent());
		if (!parent.IsOk()) {
			wxASSERT(node == m_root);
			// don't make the control completely empty:
			wxLogError("Cannot remove the root item!");
			return;
		}
		// first remove the node from the parent's array of children;
		// NOTE: MyMusicTreeModelNodePtrArray is only an array of _pointers_
		//       thus removing the node from it doesn't result in freeing it
		std::vector<wxValueTreeNode*>& children = node->GetParent()->GetChildren();
		std::vector<wxValueTreeNode*>::iterator children_iterator = std::find(children.begin(), children.end(), node);
		if (children_iterator != children.end())
			children.erase(children_iterator);
		// notify control
		/* wxDataViewModel:: */ ItemDeleted(parent, item);
		// free the node
		node->m_valueTree = NULL;
		node->DecRef();
	}

	void Clear(bool notify = true) {
		std::vector<wxValueTreeNode*>& children = m_root->GetChildren();
		while (!children.empty()) {
			wxValueTreeNode* node = m_root->GetChild(0);
			std::vector<wxValueTreeNode*>::iterator children_iterator = std::find(children.begin(), children.end(), node);
			if (children_iterator != children.end())
				children.erase(children_iterator);
			node->m_valueTree = NULL;
			node->DecRef();
		}
		if (notify)
			/* wxDataViewModel:: */ Cleared();
	}

	/////////////////////////////////////////////////////////

	// override sorting to always sort branches ascendingly
	virtual int Compare(const wxDataViewItem& item1, const wxDataViewItem& item2,
		unsigned int col, bool ascending) const override {
		wxASSERT(item1.IsOk() && item2.IsOk());
		// should never happen
		if (IsContainer(item1) && IsContainer(item2)) {
			wxVariant value1, value2;
			GetValue(value1, item1, 0);
			GetValue(value2, item2, 0);
			wxString str1 = value1.GetString();
			wxString str2 = value2.GetString();
			int res = str1.Cmp(str2);
			if (res)
				return res;
			// items must be different
			wxUIntPtr litem1 = (wxUIntPtr)item1.GetID();
			wxUIntPtr litem2 = (wxUIntPtr)item2.GetID();
			return litem1 - litem2;
		}

		return wxDataViewModel::Compare(item1, item2, col, ascending);
	}

	// derived classes should override these methods instead of
	// {Get,Set}Value() and GetAttr() inherited from the base class
	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& item, unsigned col) const = 0;

	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& item, unsigned col) = 0;

	virtual bool GetAttrByRow(const wxDataViewItem& WXUNUSED(item),
		unsigned WXUNUSED(col), wxDataViewItemAttr& WXUNUSED(attr)) const {
		return false;
	}

	virtual bool IsEnabledByRow(const wxDataViewItem& WXUNUSED(item),
		unsigned int WXUNUSED(col)) const {
		return true;
	}

	virtual unsigned int GetChildren(const wxDataViewItem& parent,
		wxDataViewItemArray& array) const override {
		wxValueTreeNode* node = GetViewData<wxValueTreeNode>(parent);
		if (node == NULL)
			return GetChildren(wxDataViewItem(m_root), array);
		unsigned int count = node->GetChildCount();
		if (count == 0)
			return 0;
		for (unsigned int pos = 0; pos < count; pos++) {
			wxValueTreeNode* child = node->GetChild(pos);
			array.Add(wxDataViewItem((void*)child));
		}
		return count;
	}

	virtual wxDataViewItem GetParent(const wxDataViewItem& item) const override {
		// the invisible root node has no parent
		if (!item.IsOk())
			return wxDataViewItem(NULL);
		wxValueTreeNode* node = GetViewData<wxValueTreeNode>(item);
		// "root" also has no parent
		if (m_root == node ||
			m_root == node->GetParent())
			return wxDataViewItem(NULL);
		return wxDataViewItem((void*)node->GetParent());
	}

	virtual bool IsContainer(const wxDataViewItem& item) const override {
		// the invisible root node can have children
		// (in our model always "root")
		if (!item.IsOk())
			return true;
		wxValueTreeNode* node = GetViewData<wxValueTreeNode>(item);
		if (node == NULL)
			return false;
		return node->IsContainer();
	}

	// return true if the given item has a value to display in the given
	// column: this is always true except for container items which by default
	// only show their label in the first column (but see HasContainerColumns())
	virtual bool HasValue(const wxDataViewItem& item, unsigned col) const override {
		if (HasContainerColumns(item))
			return false;
		return true;
	}

	// and implement some others by forwarding them to our own ones
	virtual void GetValue(wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) const override {
		GetValueByRow(variant, item, col);
	}

	virtual bool SetValue(const wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) override {
		return SetValueByRow(variant, item, col);
	}

	virtual bool GetAttr(const wxDataViewItem& item, unsigned int col,
		wxDataViewItemAttr& attr) const override {
		return GetAttrByRow(item, col, attr);
	}

	virtual bool IsEnabled(const wxDataViewItem& item, unsigned int col) const override {
		return IsEnabledByRow(item, col);
	}

private:
	wxValueTreeNode* m_root;
};

#endif 