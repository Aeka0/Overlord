import { Play } from "lucide-react";
import type { Translate } from "../i18n";
import type { Notice } from "../useSettings";
import { MotionSurface } from "./MotionSurface";
import { HomeScene } from "./HomeScene";
import { DecodedText } from "./DecodedText";

export function HomePage({
  t,
  product,
  version,
  revealReady,
  runtime,
  choices,
  launching,
  checking,
  launchDisabled,
  runtimeDisabled,
  notice,
  onRuntimeChange,
  onLaunch,
}: {
  t: Translate;
  product: string;
  version: string;
  revealReady: boolean;
  runtime: string;
  choices: Array<{ value: string; labelKey: string }>;
  launching: boolean;
  checking: boolean;
  launchDisabled: boolean;
  runtimeDisabled: boolean;
  notice: Notice | null;
  onRuntimeChange: (value: string) => void;
  onLaunch: () => void;
}) {
  return (
    <section className="home-page">
      <HomeScene>
        <div className="home-identity">
          <h1><DecodedText text={product} active={revealReady} durationMs={960}
            glow={{}} /></h1>
          <p><DecodedText text={version} active={revealReady} durationMs={600} delayMs={120} glow={{}} /></p>
        </div>
        <MotionSurface as="div" className="home-controls">
          <div className="home-runtime">
            <label htmlFor="vr_runtimeBackend">{t("launcher.runtime")}</label>
            <select
              id="vr_runtimeBackend"
              value={runtime}
              aria-label={t("launcher.runtime")}
              disabled={runtimeDisabled}
              onChange={(event) => onRuntimeChange(event.currentTarget.value)}
            >
              {choices.map((choice) => (
                <option key={choice.value} value={choice.value}>
                  {t(choice.labelKey)}
                </option>
              ))}
            </select>
          </div>
          <button
            className="button primary home-launch"
            disabled={launchDisabled}
            onClick={onLaunch}
          >
            <Play size={20} />
            {t(
              checking
                ? "preflight.checking"
                : launching
                  ? "launcher.launching"
                  : "launcher.launch",
            )}
          </button>
          {notice && (
            <p
              className={`status${notice.error ? " error" : ""}`}
              role={notice.error ? "alert" : "status"}
            >
              {t(notice.key, notice.values)}
            </p>
          )}
        </MotionSurface>
      </HomeScene>
    </section>
  );
}
