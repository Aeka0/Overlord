import type { Translate } from "../i18n";
import { LauncherDialog } from "./LauncherDialog";

export function StartupNotice({
  version,
  t,
  onIssues,
  onClose,
}: {
  version: string;
  t: Translate;
  onIssues: () => void;
  onClose: () => void;
}) {
  return (
    <LauncherDialog
      className="startup-notice"
      titleId="startup-notice-title"
      descriptionId="startup-notice-content"
      onClose={onClose}
    >
      <div className="preflight-heading">
        <h1 id="startup-notice-title" className="display-heading">
          {t("notice.title")}
        </h1>
      </div>
      <div id="startup-notice-content" className="notice-content">
        <p>{t("notice.maintenance", { version })}</p>
        <p>{t("notice.expectations")}</p>
        <p>{t("notice.feedback")}</p>
      </div>
      <div className="preflight-actions">
        <button className="button ghost" type="button" onClick={onIssues}>
          {t("notice.issues")}
        </button>
        <button className="button primary" type="button" data-notice-close autoFocus onClick={onClose}>
          {t("notice.close")}
        </button>
      </div>
    </LauncherDialog>
  );
}
