import data from "../../client/resources/launcher/shell-locales.json";
import preflightData from "../../client/resources/launcher/preflight-locales.json";

const preflight: Record<string, Record<string, string>> = preflightData;
export const shellTranslations: Record<
  string,
  Record<string, string>
> = Object.fromEntries(
  Object.entries(data).map(([language, catalog]) => [
    language,
    { ...catalog, ...preflight[language] },
  ]),
);
