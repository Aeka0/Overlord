import { useMemo, type ReactNode } from "react";
import { ChevronRight } from "lucide-react";
import type { Translate } from "./i18n";
import { MotionSurface } from "./components/MotionSurface";
import {
  articles,
  instruction,
  searchHelp,
  keywordPattern,
  type HelpState,
} from "./helpModel";

const categories = ["movement", "world", "equipment", "weapons", "story"];
function highlighted(text: string): ReactNode {
  return text
    .split(keywordPattern)
    .map((part, index) => (index % 2 ? <mark key={index}>{part}</mark> : part));
}

export function Help({
  language,
  t,
  state,
  onChange,
}: {
  language: string;
  t: Translate;
  state: HelpState;
  onChange: (state: HelpState) => void;
}) {
  const visible = useMemo(
    () => searchHelp(state),
    [state.category, state.query, state.storyAccepted],
  );
  function openArticle(id: string) {
    const article = articles.find((article) => article.id === id);
    if (!article || (article.category === "story" && !state.storyAccepted))
      return;
    onChange({ ...state, category: article.category, query: "", expanded: id });
    requestAnimationFrame(() =>
      document
        .getElementById("help-article-" + id)
        ?.scrollIntoView({ block: "start" }),
    );
  }
  return (
    <MotionSurface className="page help-page" motionKey={state.category}>
      <div className="page-heading">
        <h1 className="display-heading">{t("nav.help")}</h1>
        <p className="hint">{t("help.searchHint")}</p>
      </div>
      <input
        className="help-search"
        type="search"
        maxLength={256}
        value={state.query}
        aria-label={t("help.search")}
        placeholder={t("help.search")}
        onChange={(event) =>
          onChange({ ...state, query: event.currentTarget.value })
        }
      />
      <nav className="sub-navigation" aria-label={t("nav.help")}>
        {categories.map((item) => (
          <button
            type="button"
            key={item}
            aria-current={state.category === item ? "page" : undefined}
            onClick={() => onChange({ ...state, category: item, query: "" })}
          >
            {t(`help.category.${item}`)}
          </button>
        ))}
      </nav>
      {state.category === "story" && !state.storyAccepted ? (
        <div className="panel spoiler">
          <p>{t("help.storyWarning")}</p>
          <button
            className="button primary"
            onClick={() => onChange({ ...state, storyAccepted: true })}
          >
            {t("help.storyUnderstood")}
          </button>
        </div>
      ) : (
        <div className="help-articles">
          {visible.map((article) => {
            const text = instruction(article, language);
            return (
              <details
                className="help-article"
                id={"help-article-" + article.id}
                key={article.id}
                open={state.expanded === article.id}
                onToggle={(event) => {
                  const open = event.currentTarget.open;
                  if (open && state.expanded !== article.id)
                    onChange({ ...state, expanded: article.id });
                  else if (!open && state.expanded === article.id)
                    onChange({ ...state, expanded: "" });
                }}
              >
                <summary>
                  <ChevronRight
                    className="help-chevron"
                    size={16}
                    aria-hidden="true"
                  />
                  <span>{text.title}</span>
                </summary>
                <div className="help-body">
                  {text.intro && <p>{highlighted(text.intro)}</p>}
                  <ol>
                    {text.steps.map((step, index) => (
                      <li key={index}>{highlighted(step)}</li>
                    ))}
                  </ol>
                  {text.note && <p>{highlighted(text.note)}</p>}
                  {!!article.related?.length && (
                    <div className="help-related">
                      <span className="hint">{t("help.related")}</span>
                      {article.related.map((id) => {
                        const target = articles.find(
                          (article) => article.id === id,
                        );
                        return target &&
                          (target.category !== "story" ||
                            state.storyAccepted) ? (
                          <button
                            className="button ghost"
                            key={id}
                            onClick={() => openArticle(id)}
                          >
                            {instruction(target, language).title}
                          </button>
                        ) : null;
                      })}
                    </div>
                  )}
                </div>
              </details>
            );
          })}
          {!visible.length && <p className="hint">{t("help.noResults")}</p>}
        </div>
      )}
    </MotionSurface>
  );
}
