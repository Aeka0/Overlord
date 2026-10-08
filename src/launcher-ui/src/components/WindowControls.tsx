import { Copy, Minus, Square, X } from "lucide-react";
import { useEffect, useState } from "react";
import { bridge } from "../bridge";
import type { Translate } from "../i18n";

export function WindowControls({
  t,
  onError,
}: {
  t: Translate;
  onError: (error: unknown) => void;
}) {
  const [maximized, setMaximized] = useState(false);
  useEffect(() => {
    let live = true;
    const unsubscribe = bridge!.onWindowState((state) =>
      setMaximized(state.maximized),
    );
    void bridge!
      .request<{ maximized: boolean }>("window.state")
      .then((state) => {
        if (live) setMaximized(state.maximized);
      })
      .catch(() => {});
    return () => {
      live = false;
      unsubscribe();
    };
  }, []);
  const action = (action: string) => {
    void bridge!.request("window.control", { action }).catch(onError);
  };
  return (
    <div className="window-controls">
      <button
        type="button"
        className="window-control"
        aria-label={t("launcher.minimize")}
        title={t("launcher.minimize")}
        onClick={() => action("minimize")}
      >
        <Minus size={15} />
      </button>
      <button
        type="button"
        className="window-control"
        aria-label={t(maximized ? "launcher.restore" : "launcher.maximize")}
        title={t(maximized ? "launcher.restore" : "launcher.maximize")}
        onClick={() => action("toggleMaximize")}
      >
        {maximized ? <Copy size={13} /> : <Square size={13} />}
      </button>
      <button
        type="button"
        className="window-control close"
        aria-label={t("launcher.close")}
        title={t("launcher.close")}
        onClick={() => action("close")}
      >
        <X size={17} />
      </button>
    </div>
  );
}
