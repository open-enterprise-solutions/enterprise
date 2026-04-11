#ifndef __PROPERTY_LIST_H__
#define __PROPERTY_LIST_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "list"
class BACKEND_API ibPropertyList : public ibProperty {

	wxPGChoices GetValueList() const {
		wxPGChoices constants;
		if (m_functor->Invoke(const_cast<ibPropertyList*>(this))) {
			for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount(); idx++) {
				wxPGChoiceEntry item(
					m_listPropValue.GetItemLabel(idx),
					m_listPropValue.GetItemId(idx)
				);
				item.SetBitmap(m_listPropValue.GetItemBitmap(idx));
				constants.Add(item);
			}
		}
		return constants;
	}

	class BACKEND_API ibPropertyOptionValue {
		enum eValType {
			eValType_pointer,
			eValType_value,
		} m_valType;
		struct {
			ibValue* m_pValue, m_cValue;
		};
	public:

		operator ibValue* () { return GetOptionValue(); }
		ibPropertyOptionValue& operator = (const ibPropertyOptionValue& src) {

			if (src.m_valType == eValType::eValType_pointer)
				m_pValue = src.m_pValue;
			else if (src.m_valType == eValType::eValType_value)
				m_cValue = src.m_cValue;

			m_valType = src.m_valType;
			return *this;
		}

		ibPropertyOptionValue(ibValue* p = nullptr) : m_valType(eValType::eValType_pointer), m_pValue(p), m_cValue() {}
		ibPropertyOptionValue(const ibValue& v) : m_valType(eValType::eValType_value), m_pValue(nullptr), m_cValue(v) {}

		template <typename T1> ibPropertyOptionValue(T1* v) : m_valType(eValType::eValType_pointer), m_pValue(v), m_cValue() {}
		template <typename T1> ibPropertyOptionValue(const T1& v) : m_valType(eValType::eValType_value), m_pValue(nullptr), m_cValue(v) {}

		ibPropertyOptionValue(const ibPropertyOptionValue& val) : m_valType(val.m_valType), m_pValue(val.m_pValue), m_cValue(val.m_cValue) {}
		~ibPropertyOptionValue() {}

		ibValue* GetOptionValue() {
			return (m_valType == eValType::eValType_pointer) ? m_pValue : new ibValue(m_cValue.GetValue());
		}
	};

	class BACKEND_API ibPropertyOptionList {

		struct ibPropertyOptionItem {

			ibPropertyOptionItem() :
				m_isOk(true), m_strName(), m_strLabel(), m_id(-1), m_value()
			{
			}

			ibPropertyOptionItem(const wxString& name, const long& l, const wxBitmap& b, const ibPropertyOptionValue& v) :
				m_isOk(true), m_strName(name), m_strLabel(name), m_bmp(b), m_id(l), m_value(v)
			{
			}

			ibPropertyOptionItem(const wxString& name, const wxString& label, const long& l, const wxBitmap& b, const ibPropertyOptionValue& v) :
				m_isOk(true), m_strName(name), m_strLabel(label), m_bmp(b), m_id(l), m_value(v)
			{
			}

			ibPropertyOptionItem(const wxString& name, const wxString& label, const wxString& help, const long& l, const wxBitmap& b, const ibPropertyOptionValue& v) :
				m_isOk(true), m_strName(name), m_strLabel(label), m_strHelp(help), m_bmp(b), m_id(l), m_value(v)
			{
			}

			ibPropertyOptionItem(const ibPropertyOptionItem& item) :
				m_isOk(true), m_strName(item.m_strName), m_strLabel(item.m_strLabel), m_strHelp(item.m_strHelp), m_bmp(item.m_bmp), m_id(item.m_id), m_value(item.m_value)
			{
			}

			ibPropertyOptionItem& operator = (const ibPropertyOptionItem& src) {
				m_strName = src.m_strName;
				m_strLabel = src.m_strLabel;
				m_strHelp = src.m_strHelp;
				m_id = src.m_id;
				m_value = src.m_value;
				return *this;
			}

			operator const long() const { return m_id; }

			bool m_isOk;
			wxString m_strName;
			wxString m_strLabel;
			wxString m_strHelp;
			wxBitmap m_bmp;
			long m_id;
			ibPropertyOptionValue m_value;
		};

		ibPropertyOptionItem GetItemAt(const unsigned int idx) const {
			if (idx > m_listValue.size())
				return ibPropertyOptionItem();
			auto it = m_listValue.begin();
			std::advance(it, idx);
			return *it;
		};

		ibPropertyOptionItem GetItemById(const long& id) const {
			auto it = std::find_if(m_listValue.begin(), m_listValue.end(),
				[id](const ibPropertyOptionItem& p) { return id == p.m_id; }
			);
			if (it != m_listValue.end())
				return *it;
			return ibPropertyOptionItem();
		};

	public:

		void ResetListItem() { m_listValue.clear(); }

		void AppendItem(const wxString& name, const int& l, const wxBitmap& b, const ibPropertyOptionValue& v) { (void)m_listValue.emplace_back(name, name, l, b, v); }
		void AppendItem(const wxString& name, const wxString& label, const int& l, const wxBitmap& b, const ibPropertyOptionValue& v) { (void)m_listValue.emplace_back(name, label, l, b, v); }
		void AppendItem(const wxString& name, const wxString& label, const wxString& help, const int& l, const wxBitmap& b, const ibPropertyOptionValue& v) { (void)m_listValue.emplace_back(name, label, help, l, b, v); }

		bool HasValue(const long& l) const { return GetItemById(l); }

		wxString GetItemName(const unsigned int idx) const { return GetItemAt(idx).m_strName; }
		wxString GetItemLabel(const unsigned int idx) const { return GetItemAt(idx).m_strLabel; }
		wxString GetItemHelp(const unsigned int idx) const { return GetItemAt(idx).m_strHelp; }
		wxBitmap GetItemBitmap(const unsigned int idx) const { return GetItemAt(idx).m_bmp; }
		long GetItemId(const unsigned int idx) const { return GetItemAt(idx).m_id; }
		ibValue* GetItemValue(const unsigned int idx) const { return GetItemAt(idx).m_value; }

		unsigned int GetItemCount() const { return (unsigned int)m_listValue.size(); }

	private:
		std::vector<ibPropertyOptionItem> m_listValue;
	};

	class BACKEND_API ibPropertyFunctor {
	public:
		virtual ~ibPropertyFunctor() {}
		virtual bool Invoke(ibPropertyList* property) = 0;
	};

	template <typename optClass>
	class ibPropertyValueFunctor : public ibPropertyFunctor {
		bool (optClass::* m_funcHandler)(ibPropertyList* prop);
	public:
		ibPropertyValueFunctor(bool (optClass::* funcHandler)(ibPropertyList* prop), optClass* handler)
			: m_funcHandler(funcHandler), m_handler(handler)
		{
		}
		virtual bool Invoke(ibPropertyList* property) override {
			const ibPropertyOptionList listPropValue = property->m_listPropValue;
			if (property != nullptr) property->ResetListItem();
			return (m_handler->*m_funcHandler)(property);
		}
	private:
		optClass* m_handler;
	};
#pragma region item 
	void ResetListItem() { (void)m_listPropValue.ResetListItem(); }
#pragma endregion
public:
	int GetValueAsInteger() const {
		const long sel = m_propValue;
		if (m_functor->Invoke(const_cast<ibPropertyList*>(this))) {
			for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount(); idx++) {
				if (m_listPropValue.GetItemId(idx) == sel) {
					return sel;
				}
			}
		}
		return wxNOT_FOUND;
	}

#pragma region item 
	void AppendItem(const wxString& name, const int& l, const wxBitmap& b, const ibPropertyOptionValue& v = ibPropertyOptionValue()) { (void)m_listPropValue.AppendItem(name, l, b, v); }
	void AppendItem(const wxString& name, const wxString& label, const int& l, const wxBitmap& b, const ibPropertyOptionValue& v = ibPropertyOptionValue()) { (void)m_listPropValue.AppendItem(name, label, l, b, v); }
	void AppendItem(const wxString& name, const wxString& label, const wxString& help, const int& l, const wxBitmap& b, const ibPropertyOptionValue& v = ibPropertyOptionValue()) { (void)m_listPropValue.AppendItem(name, label, help, l, b, v); }
#pragma endregion

	template <typename optClass>
	ibPropertyList(ibPropertyCategory* cat, const wxString& name,
		bool (optClass::* funcHandler)(ibPropertyList* prop), const long& value = wxNOT_FOUND) : ibProperty(cat, name, value)
	{
		m_functor = new ibPropertyValueFunctor<optClass>(funcHandler, (optClass*)cat->GetPropertyObject());
	}

	template <typename optClass>
	ibPropertyList(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		bool (optClass::* funcHandler)(ibPropertyList* prop), const long& value = wxNOT_FOUND) : ibProperty(cat, name, label, value)
	{
		m_functor = new ibPropertyValueFunctor<optClass>(funcHandler, (optClass*)cat->GetPropertyObject());
	}

	template <typename optClass>
	ibPropertyList(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		bool (optClass::* funcHandler)(ibPropertyList* prop), const long& value = wxNOT_FOUND) : ibProperty(cat, name, label, helpString, value)
	{
		m_functor = new ibPropertyValueFunctor<optClass>(funcHandler, (optClass*)cat->GetPropertyObject());
	}

	virtual ~ibPropertyList() { wxDELETE(m_functor); }

	virtual bool IsEmptyProperty() const { return GetValueAsInteger() == wxNOT_FOUND; }

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (!m_functor->Invoke(const_cast<ibPropertyList*>(this)))
			return nullptr;
		if (ms_propertyList != nullptr)
			return ms_propertyList(m_propLabel, m_propName, GetValueList(), GetValueAsInteger());
		return nullptr;
	}

	// Set/Get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

public:

	static wxObject* (*ms_propertyList)(const wxString&, const wxString&, const wxPGChoices&, const int&);

protected:

	virtual void DoSetValue(const wxVariant& val) {
		if (m_functor != nullptr) m_functor->Invoke(this);
		ibProperty::DoSetValue(val);
	}

private:

	ibPropertyOptionList m_listPropValue;
	ibPropertyFunctor* m_functor;
};

#endif