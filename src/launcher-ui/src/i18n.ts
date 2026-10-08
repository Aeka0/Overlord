import en from "../../client/resources/launcher/locales/en.json";
import zhCN from "../../client/resources/launcher/locales/zh-CN.json";
import zhTW from "../../client/resources/launcher/locales/zh-TW.json";
import fr from "../../client/resources/launcher/locales/fr.json";
import de from "../../client/resources/launcher/locales/de.json";
import es from "../../client/resources/launcher/locales/es.json";
import ru from "../../client/resources/launcher/locales/ru.json";
import ja from "../../client/resources/launcher/locales/ja.json";
import ko from "../../client/resources/launcher/locales/ko.json";
import { shellTranslations } from "./shellTranslations";

export const catalogs: Record<string, Record<string, string>> = {
  en,
  "zh-CN": zhCN,
  "zh-TW": zhTW,
  fr,
  de,
  es,
  ru,
  ja,
  ko,
};
export function translator(language: string) {
  return (key: string, values?: Record<string, string | number>) => {
    const extra = shellTranslations[language] || shellTranslations.en;
    const text =
      catalogs[language]?.[key] ??
      extra[key] ??
      catalogs.en[key] ??
      shellTranslations.en[key] ??
      key;
    return text.replace(/\{([a-zA-Z0-9_]+)\}/g, (match, name: string) =>
      values && Object.hasOwn(values, name) ? String(values[name]) : match,
    );
  };
}
export function errorKey(error: unknown) {
  const message = error instanceof Error ? error.message : "error.saveVR";
  return (
    Object.entries(catalogs.en).find(
      ([key, text]) => key.startsWith("error.") && text === message,
    )?.[0] ?? message
  );
}
export type Translate = ReturnType<typeof translator>;
