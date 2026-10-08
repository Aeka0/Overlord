import { Play } from "lucide-react";
import type { Translate } from "../i18n";
import type { Notice } from "../useSettings";
import { MotionSurface } from "./MotionSurface";

export function HomePage({
  t,
  product,
  version,
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
      <img
        id="home-background"
        className="home-artwork"
        src="/images/launcher-background.jpg"
        alt=""
      />
      <MotionSurface as="div" className="home-identity">
        <h1>{product}</h1>
        <p>{version}</p>
      </MotionSurface>
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
    </section>
  );
}
