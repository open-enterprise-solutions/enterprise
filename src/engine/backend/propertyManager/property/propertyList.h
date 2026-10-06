#ifndef __PROPERTY_LIST_H__
#define __PROPERTY_LIST_H__

#include "backend/propertyManager/propertyObject.h"

#include <atomic>
#include <mutex>
#include <shared_mutex>

//base property for "list"
//
// ⭐⭐ THE CHOICES ARE FILLED ONCE, AND A READ ONLY READS. The functor asks the owner what it offers and fills
// m_listPropValue; it used to run on EVERY read of the value, so a read wrote. A configuration is one object for
// all the sessions of a base, and two sessions reading its language at once (each compiling its root at login)
// refilled the same vector under each other — the application server and the web host fell on six logins at
// once (2026-10-06). So a configuration fills its lists when it loads (ibValueMetaObject::RunSubtree, the resolve
// phase — sequential, every object present), anything else on its first read; afterwards a read takes the shared
// lock and reads. The list is refilled where what is offered may have changed: when the value is SET (the next
// read fills afresh) and when the choices are SHOWN (GetValueList — the designer has just been editing).
class BACKEND_API ibPropertyList : public ibProperty {
public:

	// The choices this list offers. Public because the FRONT builds the editor now and
	// reads them there; it fires the functor itself, since that is what fills the list —
	// this used to sit in GetPGProperty, which is the one caller it had.
	//
	// NOT const: the choices are read afresh here (FillList) — shown, they must be the owner's as it is now.
	// ⭐ THE ONE THAT GAVE THE BASE ITS SHAPE. This worked, and it worked here alone — an enumeration
	// answered the same question under another name, and a relationship could not be asked at all.
	// Now it is the family's own verb and this is one member of it.
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override {
		std::unique_lock<std::shared_mutex> lock(m_listMutex);
		if (!FillListLocked())
			return ibPropertyChoiceMode::None;
		for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount(); idx++) {
			list.Add(
				m_listPropValue.GetItemLabel(idx),
				m_listPropValue.GetItemId(idx),
				m_listPropValue.GetItemBitmap(idx)
			);
		}
		return ibPropertyChoiceMode::Single;
	}

private:

	class BACKEND_API ibPropertyOptionValue {
		enum eValType {
			eValType_pointer,
			eValType_value,
		} m_valType;
		// Two plain members. They used to sit inside an ANONYMOUS struct, which is an MSVC
		// extension and illegal here anyway: an anonymous aggregate may not hold a member
		// with a constructor, and `ibValue* m_pValue, m_cValue;` declares m_cValue as an
		// ibValue BY VALUE (the comma binds the `*` to the first name only). The wrapper
		// carried no meaning — it was not a union, and every constructor below initialises
		// both members — so it is gone and the declarations are spelled out.
		ibValue* m_pValue;
		ibValue  m_cValue;
	public:

		operator ibValue* () const { return GetOptionValue(); }
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

		ibValue* GetOptionValue() const {
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

		// The item itself, not a copy: a copy copied its bitmap, and a bitmap's reference count in wx is a plain int —
		// readers of one list on several threads counted it up and down under each other.
		const ibPropertyOptionItem& GetItemAt(const unsigned int idx) const {
			static const ibPropertyOptionItem s_none;
			return idx < m_listValue.size() ? m_listValue[idx] : s_none;
		};

	public:

		void ResetListItem() { m_listValue.clear(); }

		void AppendItem(const wxString& name, const int& l, const wxBitmap& b, const ibPropertyOptionValue& v) { (void)m_listValue.emplace_back(name, name, l, b, v); }
		void AppendItem(const wxString& name, const wxString& label, const int& l, const wxBitmap& b, const ibPropertyOptionValue& v) { (void)m_listValue.emplace_back(name, label, l, b, v); }
		void AppendItem(const wxString& name, const wxString& label, const wxString& help, const int& l, const wxBitmap& b, const ibPropertyOptionValue& v) { (void)m_listValue.emplace_back(name, label, help, l, b, v); }

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
			return (m_handler->*m_funcHandler)(property);
		}
	private:
		optClass* m_handler;
	};

	// Under the exclusive lock: the list emptied and filled by the owner, and the owner's answer kept — false offers
	// nothing.
	bool FillListLocked() {
		m_listPropValue.ResetListItem();
		m_listOffered = m_functor != nullptr && m_functor->Invoke(this);
		m_listFilled.store(true, std::memory_order_release);
		return m_listOffered;
	}

	// A list nobody has filled yet — its owner is not loaded with a configuration (a form's control), or its value
	// has just been set — is filled on its first read, once.
	void EnsureList() const {
		if (m_listFilled.load(std::memory_order_acquire))
			return;
		std::unique_lock<std::shared_mutex> lock(m_listMutex);
		if (!m_listFilled.load(std::memory_order_relaxed))
			const_cast<ibPropertyList*>(this)->FillListLocked();
	}

public:
	// The value, when it is among the choices; wxNOT_FOUND otherwise. Reads — never fills a list that was filled.
	int GetValueAsInteger() const {
		const long sel = m_propValue;
		EnsureList();
		std::shared_lock<std::shared_mutex> lock(m_listMutex);
		if (m_listOffered) {
			for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount(); idx++) {
				if (m_listPropValue.GetItemId(idx) == sel)
					return sel;
			}
		}
		return wxNOT_FOUND;
	}

	// The choices read afresh from the owner: by a configuration as it loads (every list of every object, once), and
	// by whoever knows what is offered has changed. A read waits for the fill and never sees half of it.
	void FillList() {
		std::unique_lock<std::shared_mutex> lock(m_listMutex);
		FillListLocked();
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

	// Set/Get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:


protected:

	// A value set — what is offered may have changed with it (the designer has been editing the owner): the next
	// read fills the list afresh.
	virtual void DoSetValue(const wxVariant& val) {
		ibProperty::DoSetValue(val);
		m_listFilled.store(false, std::memory_order_release);
	}

private:

	ibPropertyOptionList m_listPropValue;
	ibPropertyFunctor* m_functor;

	mutable std::shared_mutex m_listMutex;   // readers shared, a fill exclusive
	std::atomic<bool>         m_listFilled { false };
	bool                      m_listOffered = false;
};

#endif