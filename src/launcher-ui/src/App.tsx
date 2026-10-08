import { ExternalLink } from "lucide-react";
import {
  lazy,
  Suspense,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import credits from "../../client/resources/launcher/credits.json";
import { bridge } from "./bridge";
import { catalogs, errorKey, translator, type Translate } from "./i18n";
import {
  categories,
  collect,
  fields,
  makeDraft,
  type Field,
  type SettingsState,
  type Values,
} from "./model";
import { FieldControl, SettingRow } from "./components/controls";
import { WindowControls } from "./components/WindowControls";
import { HomePage } from "./components/HomePage";
import { MotionSurface } from "./components/MotionSurface";
import { LaunchDialog } from "./components/LaunchDialog";
import type { PreflightReport, LaunchResult } from "./preflight";
import { useSettings, type Notice } from "./useSettings";
import { useGameLanguages } from "./useGameLanguages";
import { initialHelpState } from "./helpState";
import {
  inspectHomeLayout,
  inspectLauncherLayout,
  inspectLauncherMotion,
  inspectLauncherSelect,
  inspectLaunchDialog,
} from "./layoutProbe";
import "./styles.css";
import "./select.css";
import "./preflight.css";

interface Preferences {
  ok: boolean;
  language: string;
  warningKey?: string;
}
interface Bootstrap {
  ok: boolean;
  settings: SettingsState;
  preferences: Preferences;
  product: string;
  version: string;
  renderProbe?: boolean;
  renderProbePage?: "settings" | "home";
}
const mainPages = ["play", "settings", "language", "help", "about"];
const Help = lazy(() =>
  import("./Help").then((module) => ({ default: module.Help })),
);
const guideSteps = [
  "language",
  "device",
  "comfort",
  "assistance",
  "challenge",
  "finish",
];
const guideFields = [
  [],
  ["vr_turnMode", "vr_turnSpeed", "vr_snapAngle"],
  [
    "vr_headStabilization",
    "vr_handStabilization",
    "vr_disableLensFlare",
    "vr_disableBlur",
    "vr_cameraBob",
  ],
  ["vr_chamberingGuide", "vr_quickReload", "vr_aimAssistStrength"],
  ["vr_recoilPenalty", "vr_discardAmmoPenalty"],
  [],
];
const guideDescriptions: Record<string, string> = {
  vr_turnMode: "oobe.turnHelp",
  vr_turnSpeed: "oobe.turnSpeedHelp",
  vr_snapAngle: "oobe.snapAngleHelp",
  vr_headStabilization: "oobe.headHelp",
  vr_handStabilization: "oobe.handHelp",
  vr_disableLensFlare: "oobe.flareHelp",
  vr_disableBlur: "oobe.blurHelp",
  vr_cameraBob: "oobe.bobHelp",
  vr_chamberingGuide: "oobe.chamberHelp",
  vr_quickReload: "oobe.reloadHelp",
  vr_aimAssistStrength: "oobe.aimHelp",
  vr_recoilPenalty: "oobe.penaltyHelp",
  vr_discardAmmoPenalty: "oobe.ammoHelp",
};

function Status({ notice, t }: { notice: Notice | null; t: Translate }) {
  return (
    <span
      className={`status${notice?.error ? " error" : ""}`}
      role={notice?.error ? "alert" : "status"}
    >
      {notice ? t(notice.key, notice.values) : ""}
    </span>
  );
}

export default function App() {
  const [data, setData] = useState<Bootstrap | null>(null);
  const [language, setLanguage] = useState("en");
  const [error, setError] = useState<string | null>(null);
  const load = useCallback(async () => {
    setError(null);
    setData(null);
    try {
      const result = await bridge!.request<Bootstrap>("bootstrap");
      setLanguage(
        Object.hasOwn(catalogs, result.preferences.language)
          ? result.preferences.language
          : "en",
      );
      if (!result.settings.ok)
        throw new Error(result.settings.error || "error.loadVR");
      setData(result);
    } catch (error) {
      setError(errorKey(error));
    }
  }, []);
  useEffect(() => {
    void load();
  }, [load]);
  const t = translator(language);
  if (!data)
    return (
      <div className="launcher-shell">
        <header className="app-header">
          <span className="brand">Overlord</span>
          <WindowControls
            t={t}
            onError={(error) => setError(errorKey(error))}
          />
        </header>
        <main className="startup">
          <p role={error ? "alert" : "status"}>
            {t(error || "launcher.loading")}
          </p>
          {error && (
            <button className="button" onClick={() => void load()}>
              {t("common.retry")}
            </button>
          )}
        </main>
      </div>
    );
  return <Launcher data={data} />;
}

function Launcher({ data }: { data: Bootstrap }) {
  const controller = useSettings(data.settings);
  const gameLanguage = useGameLanguages(data.settings.onboarding.gameAvailable);
  const [preferences, setPreferences] = useState<Preferences>({
    ...data.preferences,
    language: data.preferences.language || "en",
  });
  const t = useMemo(
    () => translator(preferences.language),
    [preferences.language],
  );
  const [page, setPage] = useState(
    data.renderProbe && data.renderProbePage !== "home" ? "settings" : "play",
  );
  const [category, setCategory] = useState("basics");
  const [helpState, setHelpState] = useState(initialHelpState);
  const [guide, setGuide] = useState(
    data.settings.onboarding.gameAvailable &&
      !data.settings.onboarding.hasVRConfig,
  );
  const [guideAutomatic, setGuideAutomatic] = useState(guide);
  const [guideStep, setGuideStep] = useState(0);
  const [preferenceBusy, setPreferenceBusy] = useState(false);
  const [riskBusy, setRiskBusy] = useState(false);
  const [launching, setLaunching] = useState(false);
  const [checking, setChecking] = useState(false);
  const [preflight, setPreflight] = useState<PreflightReport | null>(null);
  const [fixing, setFixing] = useState<string | null>(null);
  const [preflightNotice, setPreflightNotice] = useState<Notice | null>(null);
  const preflightBusy = useRef(false);
  const [notice, setNotice] = useState<Notice | null>(
    data.preferences.warningKey
      ? { key: data.preferences.warningKey, error: true }
      : !data.preferences.ok
        ? { key: "language.loadError", error: true }
        : null,
  );
  const disabled = launching || checking || !!preflight;
  useEffect(() => {
    if (!data.renderProbe) return;
    void Promise.all([
      document.fonts.load('15px "HModMix-Medium"', "OpenXR 123"),
      document.fonts.load('18px "HModMixGothic-Regular"', "OVERLORD"),
    ])
      .then(async (fonts) => {
        const motion = await inspectLauncherMotion();
        const selects = inspectLauncherSelect();
        const layout =
          data.renderProbePage === "home"
            ? await inspectHomeLayout()
            : inspectLauncherLayout();
        const preflight = await inspectLaunchDialog();
        return bridge!.request("renderer.ready", {
          ok:
            layout.ok &&
            motion.ok &&
            selects.ok &&
            preflight.ok &&
            fonts.every(
              (group) =>
                group.length > 0 &&
                group.every((font) => font.status === "loaded"),
            ),
          fonts: fonts.map((group) => group.map((font) => font.family)),
          settings: fields.length,
          controls: document.querySelectorAll('[id^="vr_"]').length,
          language: preferences.language,
          layout,
          motion,
          selects,
          preflight,
        });
      })
      .catch((error) =>
        bridge!
          .request("renderer.ready", { ok: false, error: String(error) })
          .catch(() => {}),
      );
  }, [data.renderProbe]);
  useEffect(() => {
    document.documentElement.lang = preferences.language;
    document.documentElement.dataset.fontGroup = /^(zh-CN|zh-TW|ja|ko)$/.test(
      preferences.language,
    )
      ? "asian"
      : "latin";
  }, [preferences.language]);
  useEffect(() => {
    const error = controller.notice;
    if (!error?.setting) return;
    if (!guide) {
      setPage("settings");
      setCategory(
        fields.find((field) => field.name === error.setting)?.category ||
          "basics",
      );
    }
    requestAnimationFrame(() =>
      document.getElementById(error.setting!)?.focus(),
    );
  }, [controller.notice, guide]);

  async function updateLanguage(language: string) {
    if (preferenceBusy) return;
    setPreferenceBusy(true);
    setNotice(null);
    try {
      const result = await bridge!.request<Preferences>(
        "preferences.language",
        { language },
      );
      setPreferences(result);
    } catch (error) {
      setNotice({ key: errorKey(error), error: true });
    } finally {
      setPreferenceBusy(false);
    }
  }
  function change(key: string, value: string | boolean, numeric: boolean) {
    const previous = controller.draft[key];
    setNotice(null);
    const revision = controller.edit(key, value);
    if (numeric) controller.autoSave(key, true);
    else
      void controller.save([key]).then((success) => {
        if (!success) controller.revert(key, value, previous, revision);
      });
  }
  function selectPreset(id: string) {
    const preset = controller.state.controllerPresets.find(
      (preset) => preset.id === id,
    );
    if (!preset) return;
    const values = makeDraft(preset.values, controller.state.limits);
    const previous = { ...controller.draft };
    const revisions: Record<string, number> = {};
    for (const [key, value] of Object.entries(values))
      revisions[key] = controller.edit(key, value);
    void controller.save(Object.keys(values)).then((success) => {
      if (!success)
        for (const [key, value] of Object.entries(values))
          controller.revert(key, value, previous[key], revisions[key]);
    });
  }
  let selectedPreset = "custom";
  for (const preset of controller.state.controllerPresets) {
    try {
      const values = collect(
        controller.draft,
        controller.state,
        controller.state.values,
        Object.keys(preset.values),
      );
      if (
        Object.entries(preset.values).every(
          ([key, value]) => values[key] === value,
        )
      ) {
        selectedPreset = preset.id;
        break;
      }
    } catch {
      /* Incomplete alignment values are custom. */
    }
  }
  const presetControl = (
    <SettingRow
      title={t(guide ? "oobe.deviceLabel" : "settings.preset")}
      description={t(guide ? "oobe.deviceNote" : "settings.offsetHint")}
    >
      <select
        aria-label={t("settings.preset")}
        disabled={disabled}
        value={selectedPreset}
        onChange={(event) => selectPreset(event.currentTarget.value)}
      >
        {controller.state.controllerPresets.map((preset) => (
          <option key={preset.id} value={preset.id}>
            {preset.id === "none"
              ? t(guide ? "oobe.otherDevice" : "choice.none")
              : preset.id === "meta_quest_3"
                ? t("choice.quest3")
                : preset.label}
          </option>
        ))}
        <option value="custom">{t("choice.custom")}</option>
      </select>
    </SettingRow>
  );
  function control(field: Field, onboarding = false) {
    return (
      <FieldControl
        key={field.name}
        field={field}
        draft={controller.draft}
        state={controller.state}
        t={t}
        disabled={disabled}
        invalid={controller.notice?.setting === field.name}
        description={onboarding ? guideDescriptions[field.name] : undefined}
        onChange={change}
        onBlur={(key) => {
          void controller.flushField(key);
        }}
      />
    );
  }
  async function openGuide() {
    if (!controller.prepareGuide()) return;
    setGuideAutomatic(false);
    setGuideStep(0);
    setGuide(true);
    setNotice(null);
  }
  async function nextGuide() {
    controller.clearTimers();
    if (!(await controller.save(guideFields[guideStep]))) return;
    if (guideStep < guideSteps.length - 1) setGuideStep((step) => step + 1);
    else if (await controller.finishGuide(false)) {
      setGuide(false);
      setPage(guideAutomatic ? "play" : "settings");
      setCategory("basics");
    }
  }
  async function skipGuide() {
    if (await controller.finishGuide(true)) {
      setGuide(false);
      setPage(guideAutomatic ? "play" : "settings");
      setCategory("basics");
    }
  }
  async function launch() {
    if (
      launching ||
      preflightBusy.current ||
      preflight ||
      guide ||
      gameLanguage.busy ||
      preferenceBusy ||
      controller.pending
    )
      return;
    preflightBusy.current = true;
    setChecking(true);
    setNotice(null);
    controller.clearTimers();
    try {
      let report = await bridge!.request<PreflightReport>("game.preflight");
      if (report.gameAvailable && controller.dirty) {
        if (!(await controller.save())) return;
        report = await bridge!.request<PreflightReport>("game.preflight");
      }
      if (report.issues.length) {
        setPreflightNotice(null);
        setPreflight(report);
      } else if (gameLanguage.available === false) setPage("language");
      else await startGame(report);
    } catch (error) {
      setNotice({ key: errorKey(error), error: true });
    } finally {
      preflightBusy.current = false;
      setChecking(false);
    }
  }
  async function startGame(report: PreflightReport) {
    setLaunching(true);
    try {
      const result = await bridge!.request<LaunchResult>("game.launch", {
        warnings: report.issues
          .filter((issue) => issue.severity === "warning")
          .map((issue) => issue.id),
      });
      if (!result.launched) {
        setLaunching(false);
        setPreflight(result.preflight!);
      }
    } catch (error) {
      setLaunching(false);
      throw error;
    }
  }
  async function updatePreflight(id?: string) {
    if (preflightBusy.current) return;
    preflightBusy.current = true;
    setChecking(true);
    setFixing(id ?? null);
    setPreflightNotice(null);
    try {
      setPreflight(
        await bridge!.request<PreflightReport>(
          id ? "game.fix" : "game.preflight",
          id ? { id } : {},
        ),
      );
    } catch (error) {
      setPreflightNotice({ key: errorKey(error), error: true });
    } finally {
      preflightBusy.current = false;
      setChecking(false);
      setFixing(null);
    }
  }
  async function continueLaunch() {
    if (!preflight?.canLaunch || preflightBusy.current) return;
    preflightBusy.current = true;
    setChecking(true);
    setPreflightNotice(null);
    try {
      if (gameLanguage.available === false) {
        setPreflight(null);
        setPage("language");
      } else {
        if (
          preflight.gameAvailable &&
          controller.dirty &&
          !(await controller.save())
        ) {
          setPreflight(null);
          return;
        }
        await startGame(preflight);
      }
    } catch (error) {
      setPreflightNotice({ key: errorKey(error), error: true });
    } finally {
      preflightBusy.current = false;
      setChecking(false);
    }
  }
  async function disableRisk() {
    if (riskBusy) return;
    setRiskBusy(true);
    setNotice(null);
    try {
      await bridge!.request("settings.disableRisk");
      setNotice({ key: "status.riskDisabled" });
    } catch (error) {
      setNotice({ key: errorKey(error), error: true });
    } finally {
      setRiskBusy(false);
    }
  }
  const languageControl = (
    <SettingRow
      title={t("language.launcher")}
      description={t(guide ? "oobe.languageNote" : "language.autoSave")}
    >
      <select
        aria-label={t("language.launcher")}
        disabled={preferenceBusy || disabled}
        value={preferences.language}
        onChange={(event) => void updateLanguage(event.currentTarget.value)}
      >
        {Object.entries(catalogs).map(([id, catalog]) => (
          <option key={id} value={id}>
            {catalog["language.name"]}
          </option>
        ))}
      </select>
    </SettingRow>
  );
  const currentNotice = notice || controller.notice;
  const groups = [
    ...new Set(
      fields
        .filter((field) => field.category === category)
        .map((field) => field.group),
    ),
  ];

  return (
    <div className="launcher-shell">
      <header className="app-header">
        <span className="brand">{data.product}</span>
        <span className="build-label">{data.version}</span>
        <WindowControls
          t={t}
          onError={(error) => setNotice({ key: errorKey(error), error: true })}
        />
      </header>
      {!guide && (
        <nav className="main-navigation" aria-label={data.product}>
          {mainPages.map((item) => (
            <button
              key={item}
              type="button"
              aria-current={page === item ? "page" : undefined}
              disabled={disabled}
              onClick={() => {
                setPage(item);
                setNotice(null);
              }}
            >
              {t(`nav.${item}`)}
            </button>
          ))}
          {currentNotice?.error && page !== "play" && (
            <Status notice={currentNotice} t={t} />
          )}
        </nav>
      )}
      <main className="workspace">
        {guide ? (
          <MotionSurface className="page guide-page" motionKey={guideStep}>
            <div className="page-heading guide-heading">
              <div>
                <h1>{t(`oobe.${guideSteps[guideStep]}`)}</h1>
                <p className="hint">{t(`oobe.${guideSteps[guideStep]}Hint`)}</p>
              </div>
              <span className="hint">
                {t("oobe.progress", {
                  current: guideStep + 1,
                  total: guideSteps.length,
                })}
              </span>
            </div>
            {currentNotice?.error && <Status notice={currentNotice} t={t} />}
            {guideStep === 0 ? (
              <div className="panel">{languageControl}</div>
            ) : guideStep === 5 ? (
              <div className="panel finish-guide">
                <p>{t("oobe.finishNote")}</p>
              </div>
            ) : (
              <div className="panel">
                {guideStep === 1 && presetControl}
                {guideFields[guideStep]
                  .map((key) => fields.find((field) => field.name === key))
                  .filter((field): field is Field => !!field)
                  .map((field) => control(field, true))}
              </div>
            )}
            <div className="guide-actions">
              <button
                className="button ghost"
                disabled={controller.pending || preferenceBusy}
                onClick={() => void skipGuide()}
              >
                {t("oobe.skip")}
              </button>
              <div className="guide-navigation">
                <button
                  className="button"
                  disabled={
                    guideStep === 0 || controller.pending || preferenceBusy
                  }
                  onClick={() => {
                    controller.clearTimers();
                    setGuideStep((step) => step - 1);
                  }}
                >
                  {t("oobe.back")}
                </button>
                <button
                  className="button primary"
                  disabled={controller.pending || preferenceBusy}
                  onClick={() => void nextGuide()}
                >
                  {t(
                    guideStep === guideSteps.length - 1
                      ? "oobe.done"
                      : "oobe.next",
                  )}
                </button>
              </div>
            </div>
          </MotionSurface>
        ) : page === "play" ? (
          <HomePage
            t={t}
            product={data.product}
            version={data.version}
            runtime={String(controller.draft.vr_runtimeBackend)}
            choices={controller.state.choices.vr_runtimeBackend}
            launching={launching}
            checking={checking}
            launchDisabled={
              launching ||
              checking ||
              !!preflight ||
              controller.pending ||
              gameLanguage.busy ||
              preferenceBusy
            }
            runtimeDisabled={
              disabled ||
              controller.pending ||
              !controller.state.onboarding.gameAvailable
            }
            notice={currentNotice?.error || launching ? currentNotice : null}
            onRuntimeChange={(value) =>
              change("vr_runtimeBackend", value, false)
            }
            onLaunch={() => void launch()}
          />
        ) : page === "settings" ? (
          <section className="settings-layout">
            <aside className="settings-sidebar">
              <nav
                className="settings-navigation"
                aria-label={t("a11y.categories")}
              >
                {categories.map((item) => (
                  <button
                    key={item}
                    aria-current={category === item ? "page" : undefined}
                    disabled={disabled}
                    onClick={() => {
                      setCategory(item);
                      setNotice(null);
                    }}
                  >
                    {t(`settings.${item}`)}
                  </button>
                ))}
              </nav>
              <div className="settings-sidebar-actions">
                <button
                  id="vr-reset"
                  type="button"
                  className="button"
                  disabled={controller.pending || disabled}
                  onClick={() => {
                    setNotice(null);
                    void controller.reset();
                  }}
                >
                  {t("common.reset")}
                </button>
              </div>
            </aside>
            <MotionSurface
              as="div"
              className="page settings-page"
              motionKey={category}
            >
              <div className="page-heading">
                <h1 className="display-heading">{t(`settings.${category}`)}</h1>
                <p className="hint">
                  {t(`settings.${category}Hint`) === `settings.${category}Hint`
                    ? t("settings.basicsHint")
                    : t(`settings.${category}Hint`)}
                </p>
              </div>
              {category === "basics" && (
                <div className="panel guide-entry">
                  <SettingRow
                    title={t("oobe.quickGuide")}
                    description={t("oobe.quickGuideHint")}
                  >
                    <button
                      className="button"
                      disabled={disabled}
                      onClick={() => void openGuide()}
                    >
                      {t("oobe.open")}
                    </button>
                  </SettingRow>
                </div>
              )}
              {category === "cheats" && (
                <p className="cheat-notice">{t("settings.cheatsNotice")}</p>
              )}
              {category === "debug" && (
                <p className="hint build-notice">
                  {t(
                    controller.state.build.optimized
                      ? "settings.buildOptimized"
                      : "settings.buildDebug",
                    { configuration: controller.state.build.configuration },
                  )}
                </p>
              )}
              {groups.map((group) => (
                <div className="setting-group" key={group}>
                  <h2>{t(group)}</h2>
                  <div className="panel">
                    {category === "basics" &&
                      group === "settings.alignment" &&
                      presetControl}
                    {fields
                      .filter(
                        (field) =>
                          field.category === category && field.group === group,
                      )
                      .map((field) => control(field))}
                  </div>
                </div>
              ))}
              {category === "debug" && (
                <div className="setting-group">
                  <h2>{t("settings.riskHeading")}</h2>
                  <div className="panel">
                    <SettingRow
                      title={t("settings.disableRisk")}
                      description={t("settings.riskHint")}
                    >
                      <button
                        className="button"
                        disabled={riskBusy || disabled}
                        onClick={() => void disableRisk()}
                      >
                        {t("settings.disableRisk")}
                      </button>
                    </SettingRow>
                  </div>
                </div>
              )}
            </MotionSurface>
          </section>
        ) : page === "language" ? (
          <MotionSurface className="page language-page" motionKey="language">
            <div className="page-heading">
              <h1 className="display-heading">{t("nav.language")}</h1>
              <p className="hint">{t("language.headingHint")}</p>
            </div>
            <div className="panel">
              {languageControl}
              <SettingRow
                title={t("language.game")}
                description={t("language.gameHint")}
              >
                <select
                  aria-label={t("language.game")}
                  value={gameLanguage.result?.language || ""}
                  disabled={
                    gameLanguage.busy ||
                    disabled ||
                    !gameLanguage.result?.choices.length
                  }
                  onChange={(event) =>
                    void gameLanguage.save(event.currentTarget.value)
                  }
                >
                  <option value="" disabled>
                    {t("language.gameChoose")}
                  </option>
                  {gameLanguage.result?.choices.map((choice) => (
                    <option key={choice.value} value={choice.value}>
                      {choice.label}
                    </option>
                  ))}
                </select>
              </SettingRow>
            </div>
            <div className="language-actions">
              <Status notice={gameLanguage.notice} t={t} />
              <button
                className="button"
                disabled={gameLanguage.busy || disabled}
                onClick={() => void gameLanguage.scan()}
              >
                {t("language.gameRefresh")}
              </button>
            </div>
          </MotionSurface>
        ) : page === "help" ? (
          <Suspense
            fallback={<p className="page hint">{t("launcher.loading")}</p>}
          >
            <Help
              language={preferences.language}
              t={t}
              state={helpState}
              onChange={setHelpState}
            />
          </Suspense>
        ) : (
          <MotionSurface className="page about-page" motionKey="about">
            <div className="page-heading">
              <h1 className="display-heading">{data.product}</h1>
              <p className="hint">
                {t("about.stage", { version: data.version })}
              </p>
            </div>
            <button
              className="button"
              onClick={() =>
                void bridge!
                  .request("links.open", { id: "project" })
                  .catch((error) =>
                    setNotice({ key: errorKey(error), error: true }),
                  )
              }
            >
              <ExternalLink size={16} />
              {t("about.github")}
            </button>
            {credits.map((group) => (
              <div className="credits-group" key={group.title}>
                <h2>{t(group.title)}</h2>
                <ul>
                  {group.people.map((person) => (
                    <li key={person}>{person}</li>
                  ))}
                </ul>
              </div>
            ))}
          </MotionSurface>
        )}
      </main>
      {preflight && (
        <LaunchDialog
          report={preflight}
          t={t}
          busy={checking || launching}
          fixing={fixing}
          notice={preflightNotice}
          onFix={(id) => void updatePreflight(id)}
          onCheck={() => void updatePreflight()}
          onLaunch={() => void continueLaunch()}
          onClose={() => {
            setPreflight(null);
            setPreflightNotice(null);
          }}
        />
      )}
    </div>
  );
}
