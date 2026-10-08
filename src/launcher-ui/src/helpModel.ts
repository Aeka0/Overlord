import content from "../../client/resources/launcher/help-content.json";
import type { HelpState } from "./helpState";
export { initialHelpState, type HelpState } from "./helpState";

interface HelpText {
  title: string;
  intro?: string;
  steps?: string[];
  note?: string;
}
export interface Article {
  id: string;
  category: string;
  recipe?: string;
  related?: string[];
  aliases?: string[];
  text: Record<string, HelpText>;
}
export const articles = content.articles as Article[];
const recipes = content.recipes as Record<
  string,
  Record<string, { steps: string[] }>
>;
export function instruction(article: Article, language: string) {
  const text = article.text[language] || article.text.en;
  const recipe =
    article.recipe &&
    (recipes[article.recipe]?.[language] || recipes[article.recipe]?.en);
  return {
    ...text,
    steps: [...(recipe ? recipe.steps : []), ...(text.steps || [])],
  };
}
const searchable = new Map(
  articles.map((article) => [
    article.id,
    [
      (article.aliases || []).join(" "),
      ...Object.keys(article.text).map((language) => {
        const text = instruction(article, language);
        return [text.title, text.intro, ...text.steps, text.note]
          .filter(Boolean)
          .join(" ");
      }),
    ]
      .join(" ")
      .toLocaleLowerCase(),
  ]),
);
export function searchHelp(state: HelpState) {
  const tokens = state.query
    .trim()
    .toLocaleLowerCase()
    .split(/\s+/)
    .filter(Boolean);
  return articles.filter(
    (article) =>
      (article.category !== "story" || state.storyAccepted) &&
      (tokens.length
        ? tokens.every((token) => searchable.get(article.id)!.includes(token))
        : article.category === state.category),
  );
}
const terms = [...new Set(Object.values(content.highlights).flat())].sort(
  (a, b) => b.length - a.length,
);
export const keywordPattern = new RegExp(
  "(" +
    terms
      .map((term) => {
        const literal = term.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
        return (
          (/^[a-z]/i.test(term) ? "\\b" : "") +
          literal +
          (/[a-z]$/i.test(term) ? "\\b" : "")
        );
      })
      .join("|") +
    ")",
  "gi",
);
