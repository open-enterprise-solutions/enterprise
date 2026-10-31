#include <gtest/gtest.h>

#include "activeMetaDataAccess.h"
#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/system/systemManager.h"
#include "backend/system/value/valueArray.h"
#include "core/xml/xmlDocument.h"

#include <wx/init.h>

#include <chrono>
#include <cstdio>
#include <functional>

// =============================================================================
// What reading XML costs, printed - not a test. Run it by name:
//   oes_tests --gtest_also_run_disabled_tests --gtest_filter=XmlValueBench.*
// An array of 100 000 numbers (12.5 MB of text): the tree alone, and the round trip; JSON's beside it for scale. And
// the other end: 100 000 texts of one number each, read back one by one - what a document costs before it holds
// anything. Best of three, in milliseconds.
// =============================================================================
TEST(XmlValueBench, DISABLED_AnArrayOfAHundredThousandNumbers)
{
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";
	if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
		GTEST_SKIP() << "appData env unavailable headless";
	std::shared_ptr<ibMetaDataConfigurationFile> cfg = ibMetaData::MakeShared<ibMetaDataConfigurationFile>();
	ActiveMetaDataAccess::Replace(cfg);
	struct Release {
		~Release() { ibApplicationInstance::DestroyAppDataEnv(); }
	} release;
	ASSERT_TRUE(cfg->RunDatabase());

	ibValuePtr<ibValueArray> list(new ibValueArray());
	for (int at = 0; at < 100000; ++at)
		list->Add(ibValue(at));
	const ibValue holder(list);
	const ibString xml = ibValueSystemFunction::XMLString(holder);
	const ibString json = ibValueSystemFunction::JSONString(holder);

	const auto best = [](const std::function<void()>& body) {
		long long fastest = -1;
		for (int run = 0; run < 3; ++run) {
			const auto start = std::chrono::steady_clock::now();
			body();
			const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
			if (fastest < 0 || ms < fastest)
				fastest = ms;
		}
		return fastest;
	};
	const long long tree = best([&] { ibXmlDocument document; document.Load(xml); });
	const long long toXml = best([&] { const ibString text = ibValueSystemFunction::XMLString(holder); (void)text; });
	const long long fromXml = best([&] { const ibValue read = ibValueSystemFunction::XMLValue(xml); (void)read; });
	const long long fromJson = best([&] { const ibValue read = ibValueSystemFunction::JSONValue(json); (void)read; });
	const ibString one = ibValueSystemFunction::XMLString(ibValue(42));
	const long long ones = best([&] {
		for (int at = 0; at < 100000; ++at) {
			const ibValue read = ibValueSystemFunction::XMLValue(one);
			(void)read;
		}
	});
	std::printf("100000 numbers: the tree %lld ms, XMLString %lld ms, XMLValue %lld ms, JSONValue %lld ms; "
		"100000 texts of one number: XMLValue %lld ms\n", tree, toXml, fromXml, fromJson, ones);
}
