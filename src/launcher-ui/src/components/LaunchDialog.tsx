import { CircleX, TriangleAlert } from "lucide-react";
import { useLayoutEffect, useRef } from "react";
import type { Translate } from "../i18n";
import type { PreflightReport } from "../preflight";
import type { Notice } from "../useSettings";

export function LaunchDialog({
  report,
  t,
  busy,
  fixing,
  notice,
  onFix,
  onCheck,
  onLaunch,
  onClose,
}: {
  report: PreflightReport;
  t: Translate;
  busy: boolean;
  fixing: string | null;
  notice: Notice | null;
  onFix: (id: string) => void;
  onCheck: () => void;
  onLaunch: () => void;
  onClose: () => void;
}) {
  const ref = useRef<HTMLDialogElement>(null);
  useLayoutEffect(() => {
    const dialog = ref.current!;
    dialog.showModal();
    return () => dialog.close();
  }, []);
  return (
    <dialog
      ref={ref}
      className="preflight-dialog"
      aria-labelledby="preflight-title"
      aria-describedby="preflight-description"
      aria-busy={busy || undefined}
      onCancel={(event) => {
        event.preventDefault();
        if (!busy) onClose();
      }}
    >
      <div className="preflight-heading">
        <h1 id="preflight-title" className="display-heading">
          {t(report.issues.length ? "preflight.title" : "launcher.ready")}
        </h1>
        <p id="preflight-description" className="hint">
          {t(
            report.issues.length ? "preflight.description" : "preflight.clear",
          )}
        </p>
      </div>
      {!!report.issues.length && (
        <div className="preflight-content">
          <table className="preflight-table">
            <thead>
              <tr>
                <th scope="col">{t("preflight.problem")}</th>
                <th scope="col">{t("preflight.details")}</th>
                <th scope="col">{t("preflight.fix")}</th>
              </tr>
            </thead>
            <tbody>
              {report.issues.map((issue) => (
                <tr
                  key={issue.id}
                  data-issue={issue.id}
                  data-severity={issue.severity}
                >
                  <td>
                    <div className="preflight-problem">
                      {issue.severity === "error" ? (
                        <CircleX size={20} aria-hidden="true" />
                      ) : (
                        <TriangleAlert size={20} aria-hidden="true" />
                      )}
                      <span className="sr-only">
                        {t(`preflight.${issue.severity}`)}:{" "}
                      </span>
                      <span>{t(issue.titleKey, issue.values)}</span>
                    </div>
                  </td>
                  <td>{t(issue.detailKey, issue.values)}</td>
                  <td>
                    {issue.fixable ? (
                      <button
                        className="button"
                        type="button"
                        disabled={busy}
                        data-fix={issue.id}
                        onClick={() => onFix(issue.id)}
                      >
                        {t(
                          fixing === issue.id
                            ? "preflight.fixing"
                            : "preflight.fix",
                        )}
                      </button>
                    ) : (
                      <span className="hint" aria-label={t("preflight.manual")}>
                        —
                      </span>
                    )}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
      {notice && (
        <p className="status error preflight-status" role="alert">
          {t(notice.key, notice.values)}
        </p>
      )}
      <div className="preflight-actions">
        <button
          className="button ghost"
          type="button"
          data-preflight-recheck
          disabled={busy}
          onClick={onCheck}
        >
          {t("preflight.recheck")}
        </button>
        <div className="preflight-buttons">
          <button
            className="button"
            type="button"
            data-preflight-close
            autoFocus
            disabled={busy}
            onClick={onClose}
          >
            {t("preflight.close")}
          </button>
          {report.canLaunch && (
            <button
              className="button primary"
              type="button"
              disabled={busy}
              onClick={onLaunch}
            >
              {t(
                report.issues.length ? "preflight.continue" : "launcher.launch",
              )}
            </button>
          )}
        </div>
      </div>
    </dialog>
  );
}
