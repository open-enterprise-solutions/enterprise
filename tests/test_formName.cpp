// =============================================================================
// OES Enterprise — a form by its full name (backend/system/formName.h)
//
// GetForm reads the name a script builds at run time. What is pinned here:
//
//   * every spelling — a form the object declares, the object's own form of a
//     kind, a common form — and the kind in the metatype's name, in Russian and
//     in Ukrainian, in any letter case;
//   * a name that is not spelled as one, or that names nothing in the
//     configuration, is refused WITH THE NAME in the sentence: a migrated module
//     builds these out of variables, and "not found" alone says nothing about
//     which of them was wrong;
//   * a name is read against a configuration into the object, the form and the
//     form type the object itself calls it.
//
// DB-free: a fresh file configuration, tree edits only (as test_commonAttribute).
// =============================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>

#include "backend/backend_exception.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaFormObject.h"
#include "backend/metaCollection/genericData.h"
#include "backend/system/formName.h"

namespace {

// The sentence the refusal says, or an empty one when nothing was refused.
ibString RefusalOf(const std::function<void()>& read)
{
	try {
		read();
	}
	catch (const ibCoreException& err) {
		return err.GetErrorDescription();
	}
	return ibString();
}

ibString ParseRefusal(const ibString& fullName)
{
	return RefusalOf([&]() { ibFormName::Parse(fullName); });
}

} // namespace

TEST(FormName, Parse_AFormOfAKind) {
	const ibFormName::ibParts parts = ibFormName::Parse(ibT("Document.Invoice.ObjectForm"));
	EXPECT_EQ(parts.m_kind, g_metaDocumentCLSID);
	EXPECT_EQ(parts.m_objectName, ibString(ibT("Invoice")));
	EXPECT_TRUE(parts.m_formName.IsEmpty());
	ASSERT_FALSE(parts.m_formTypes.empty());
	EXPECT_EQ(parts.m_formTypes.front(), ibString(ibT("FormObject")));
}

TEST(FormName, Parse_EveryKindOfForm) {
	const struct { const ibChar* m_name; const ibChar* m_formType; } cases[] = {
		{ ibT("Catalog.Goods.ObjectForm"),      ibT("FormObject") },
		{ ibT("Catalog.Goods.Form"),            ibT("FormObject") },
		{ ibT("Catalog.Goods.ListForm"),        ibT("FormList") },
		{ ibT("Catalog.Goods.ChoiceForm"),      ibT("FormSelect") },
		{ ibT("Catalog.Goods.GroupForm"),       ibT("FormFolder") },
		{ ibT("Catalog.Goods.GroupChoiceForm"), ibT("FormGroupSelect") },
		{ ibT("InformationRegister.Rates.RecordForm"), ibT("FormRecord") },
	};
	for (const auto& c : cases) {
		const ibFormName::ibParts parts = ibFormName::Parse(c.m_name);
		ASSERT_FALSE(parts.m_formTypes.empty()) << ibString(c.m_name).ToStdString();
		EXPECT_EQ(parts.m_formTypes.front(), ibString(c.m_formType)) << ibString(c.m_name).ToStdString();
	}
}

// A report's and a data processor's object form is its main form: the alias lists those too, after the object's.
TEST(FormName, Parse_TheObjectFormOfAReportIsItsMainForm) {
	const ibFormName::ibParts parts = ibFormName::Parse(ibT("Report.Sales.Form"));
	EXPECT_EQ(parts.m_kind, g_metaReportCLSID);
	EXPECT_NE(std::find(parts.m_formTypes.begin(), parts.m_formTypes.end(), ibString(ibT("FormReport"))), parts.m_formTypes.end());
	EXPECT_NE(std::find(parts.m_formTypes.begin(), parts.m_formTypes.end(), ibString(ibT("FormDataProcessor"))), parts.m_formTypes.end());
}

TEST(FormName, Parse_AFormByItsName) {
	const ibFormName::ibParts parts = ibFormName::Parse(ibT("Report.Sales.Form.Main"));
	EXPECT_EQ(parts.m_kind, g_metaReportCLSID);
	EXPECT_EQ(parts.m_objectName, ibString(ibT("Sales")));
	EXPECT_EQ(parts.m_formName, ibString(ibT("Main")));
	EXPECT_TRUE(parts.m_formTypes.empty());
}

TEST(FormName, Parse_ACommonForm) {
	const ibFormName::ibParts parts = ibFormName::Parse(ibT("CommonForm.Settings"));
	EXPECT_EQ(parts.m_kind, g_metaCommonFormCLSID);
	EXPECT_EQ(parts.m_objectName, ibString(ibT("Settings")));
}

TEST(FormName, Parse_RussianAndUkrainianWords) {
	EXPECT_EQ(ibFormName::Parse(ibT("Документ.Счет.ФормаОбъекта")).m_kind, g_metaDocumentCLSID);
	EXPECT_EQ(ibFormName::Parse(ibT("Справочник.Товары.ФормаСписка")).m_formTypes.front(), ibString(ibT("FormList")));
	EXPECT_EQ(ibFormName::Parse(ibT("Обработка.Загрузка.Форма")).m_kind, g_metaDataProcessorCLSID);
	EXPECT_EQ(ibFormName::Parse(ibT("Отчет.Продажи.Форма.Основная")).m_formName, ibString(ibT("Основная")));
	EXPECT_EQ(ibFormName::Parse(ibT("ОбщаяФорма.Настройки")).m_kind, g_metaCommonFormCLSID);
	EXPECT_EQ(ibFormName::Parse(ibT("Довідник.Товари.ФормаВибору")).m_formTypes.front(), ibString(ibT("FormSelect")));
	EXPECT_EQ(ibFormName::Parse(ibT("Звіт.Продажі.Форма.Основна")).m_kind, g_metaReportCLSID);
	EXPECT_EQ(ibFormName::Parse(ibT("ЗагальнаФорма.Налаштування")).m_kind, g_metaCommonFormCLSID);
}

TEST(FormName, Parse_IgnoresLetterCaseAndSpacesAroundTheDots) {
	const ibFormName::ibParts parts = ibFormName::Parse(ibT(" document . Invoice . objectform "));
	EXPECT_EQ(parts.m_kind, g_metaDocumentCLSID);
	EXPECT_EQ(parts.m_objectName, ibString(ibT("Invoice")));
	EXPECT_EQ(parts.m_formTypes.front(), ibString(ibT("FormObject")));
}

TEST(FormName, Parse_RefusesWhatIsNotAFormNameAndSaysTheName) {
	const ibChar* const names[] = {
		ibT(""),
		ibT("Invoice"),
		ibT("Document.Invoice"),
		ibT("Document..ObjectForm"),
		ibT("Document.Invoice.NoSuchForm"),
		ibT("Document.Invoice.Template.Main"),   // only `Form` names a form by its name
		ibT("CommonForm.Settings.ObjectForm"),
		ibT("Document.Invoice.Form.Main.Extra"),
	};
	for (const ibChar* name : names) {
		const ibString refusal = ParseRefusal(name);
		EXPECT_FALSE(refusal.IsEmpty()) << "accepted: " << ibString(name).ToStdString();
		EXPECT_TRUE(refusal.Contains(ibString(ibT("'")) + name + ibT("'"))) << refusal.ToStdString();
	}
}

TEST(FormName, Parse_AnUnknownKindIsNamed) {
	const ibString refusal = ParseRefusal(ibT("Thing.Invoice.ObjectForm"));
	EXPECT_TRUE(refusal.Contains(ibT("'Thing.Invoice.ObjectForm'"))) << refusal.ToStdString();
	EXPECT_TRUE(refusal.Contains(ibT("'Thing'"))) << refusal.ToStdString();
}

// =============================================================================
// Against a configuration
// =============================================================================

namespace {

struct FormNameFixture {
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObject* document = nullptr;
	ibValueMetaObject* form = nullptr;
	ibValueMetaObject* commonForm = nullptr;

	FormNameFixture() {
		ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
		document = cfg.CreateMetaObject(g_metaDocumentCLSID, root, /*runObject*/ false);
		if (document != nullptr)
			form = cfg.CreateMetaObject(g_metaFormCLSID, document, /*runObject*/ false);
		commonForm = cfg.CreateMetaObject(g_metaCommonFormCLSID, root, /*runObject*/ false);
	}

	ibString DocumentName(const ibString& tail) const {
		return ibString(ibT("Document.")) + document->GetName() + ibT(".") + tail;
	}

	ibString ResolveRefusal(const ibString& fullName) const {
		return RefusalOf([&]() { ibFormName::Resolve(&cfg, fullName); });
	}
};

} // namespace

TEST(FormName, Resolve_TheObjectsOwnFormOfAKind) {
	FormNameFixture f;
	ASSERT_NE(f.document, nullptr);

	const ibFormName object = ibFormName::Resolve(&f.cfg, f.DocumentName(ibT("ObjectForm")));
	EXPECT_EQ(object.m_owner, f.document);
	EXPECT_EQ(object.m_form, nullptr);
	EXPECT_EQ(object.m_formTypeName, ibString(ibT("FormObject")));
	EXPECT_TRUE(object.IsObjectForm());

	const ibFormName list = ibFormName::Resolve(&f.cfg, f.DocumentName(ibT("ListForm")));
	EXPECT_EQ(list.m_owner, f.document);
	EXPECT_EQ(list.m_formTypeName, ibString(ibT("FormList")));
	EXPECT_NE(list.m_formType, object.m_formType);
	EXPECT_FALSE(list.IsObjectForm());
}

TEST(FormName, Resolve_AFormTheObjectDeclares) {
	FormNameFixture f;
	ASSERT_NE(f.form, nullptr);

	const ibFormName name = ibFormName::Resolve(&f.cfg, f.DocumentName(ibString(ibT("Form.")) + f.form->GetName()));
	EXPECT_EQ(name.m_owner, f.document);
	EXPECT_EQ(name.m_form, f.form);
}

TEST(FormName, Resolve_ACommonForm) {
	FormNameFixture f;
	ASSERT_NE(f.commonForm, nullptr);

	const ibFormName name = ibFormName::Resolve(&f.cfg, ibString(ibT("CommonForm.")) + f.commonForm->GetName());
	EXPECT_EQ(name.m_commonForm, f.commonForm);
	EXPECT_EQ(name.m_owner, nullptr);
}

TEST(FormName, Resolve_WhatNamesNothingIsRefusedWithTheName) {
	FormNameFixture f;
	ASSERT_NE(f.document, nullptr);

	const ibString names[] = {
		ibT("Document.NoSuchDocument.ObjectForm"),
		f.DocumentName(ibT("Form.NoSuchForm")),
		f.DocumentName(ibT("GroupForm")),           // a document has no group form
		ibT("CommonForm.NoSuchCommonForm"),
	};
	for (const ibString& name : names) {
		const ibString refusal = f.ResolveRefusal(name);
		EXPECT_FALSE(refusal.IsEmpty()) << "accepted: " << name.ToStdString();
		EXPECT_TRUE(refusal.Contains(ibString(ibT("'")) + name + ibT("'"))) << refusal.ToStdString();
	}
}
