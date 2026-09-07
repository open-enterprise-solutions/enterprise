// @ts-nocheck
import { registerI18nLoader } from "@ui5/webcomponents-base/dist/asset-registries/i18n.js";
const importMessageBundle = async (localeId) => {
    switch (localeId) {
        case "en": return (await import(/* webpackChunkName: "ui5-webcomponents-icons-messagebundle-en" */ "../assets/i18n/messagebundle_en.js")).default;
        case "ru": return (await import(/* webpackChunkName: "ui5-webcomponents-icons-messagebundle-ru" */ "../assets/i18n/messagebundle_ru.js")).default;
        case "uk": return (await import(/* webpackChunkName: "ui5-webcomponents-icons-messagebundle-uk" */ "../assets/i18n/messagebundle_uk.js")).default;
        default: throw "unknown locale";
    }
};
const importAndCheck = async (localeId) => {
    const data = await importMessageBundle(localeId);
    if (typeof data === "string" && data.endsWith(".json")) {
        throw new Error(`[i18n] Invalid bundling detected - dynamic JSON imports bundled as URLs. Switch to inlining JSON files from the build. Check the "Assets" documentation for more information.`);
    }
    return data;
};
const localeIds = ["en", "ru", "uk"];
localeIds.forEach(localeId => {
    registerI18nLoader("@" + "u" + "i" + "5" + "/" + "w" + "e" + "b" + "c" + "o" + "m" + "p" + "o" + "n" + "e" + "n" + "t" + "s" + "-" + "i" + "c" + "o" + "n" + "s", localeId, importAndCheck);
});
