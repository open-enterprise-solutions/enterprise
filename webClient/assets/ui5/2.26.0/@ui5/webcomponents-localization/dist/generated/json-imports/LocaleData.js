// @ts-nocheck
import { registerLocaleDataLoader } from "@ui5/webcomponents-base/dist/asset-registries/LocaleData.js";
const availableLocales = ["en", "ru", "uk"];
const importCldrJson = async (localeId) => {
    switch (localeId) {
        case "en": return (await import(/* webpackChunkName: "ui5-webcomponents-cldr-en" */ "../assets/cldr/en.json")).default;
        case "ru": return (await import(/* webpackChunkName: "ui5-webcomponents-cldr-ru" */ "../assets/cldr/ru.json")).default;
        case "uk": return (await import(/* webpackChunkName: "ui5-webcomponents-cldr-uk" */ "../assets/cldr/uk.json")).default;
        default: throw "unknown locale";
    }
};
const importAndCheck = async (localeId) => {
    const data = await importCldrJson(localeId);
    if (typeof data === "string" && data.endsWith(".json")) {
        throw new Error(`[LocaleData] Invalid bundling detected - dynamic JSON imports bundled as URLs. Switch to inlining JSON files from the build. Check the "Assets" documentation for more information.`);
    }
    return data;
};
availableLocales.forEach(localeId => registerLocaleDataLoader(localeId, importAndCheck));
