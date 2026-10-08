import { useLayoutEffect, useRef, type ReactNode } from "react";

export function LauncherDialog({
  children,
  className = "",
  titleId,
  descriptionId,
  busy,
  onClose,
}: {
  children: ReactNode;
  className?: string;
  titleId: string;
  descriptionId?: string;
  busy?: boolean;
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
      className={`preflight-dialog ${className}`}
      aria-labelledby={titleId}
      aria-describedby={descriptionId}
      aria-busy={busy || undefined}
      onCancel={(event) => {
        event.preventDefault();
        if (!busy) onClose();
      }}
    >
      {children}
    </dialog>
  );
}
