import { flushSync } from "react-dom";

export function inspectStartupNotice(version: string) {
  const dialog = document.querySelector<HTMLDialogElement>(".startup-notice")!;
  for (const animation of document.getAnimations()) animation.finish();
  const bounds = dialog.getBoundingClientRect();
  const content = dialog.querySelector<HTMLElement>(".notice-content")!;
  const text = content.innerText;
  const result = {
    modal: dialog.matches(":modal"),
    version: text.includes(version) && !!version,
    paragraphs: content.querySelectorAll("p").length === 3,
    diagnostics: text.includes("vr_status") && text.includes("minidump"),
    fits: bounds.left >= 0 && bounds.top >= 0 && bounds.right <= innerWidth && bounds.bottom <= innerHeight,
    contentFits: content.scrollWidth <= content.clientWidth + 1,
    dismissed: false,
  };
  flushSync(() => dialog.querySelector<HTMLButtonElement>("[data-notice-close]")!.click());
  result.dismissed = !document.querySelector(".startup-notice");
  return { ...result, ok: Object.values(result).every(Boolean) };
}

// The native smoke run uses an isolated, hidden window and never loads the game.
function focusOutlineHidden() {
  const button = document.querySelector<HTMLButtonElement>(
    '.main-navigation button[aria-current="page"]',
  );
  if (!button) return false;
  const previous = document.activeElement;
  button.focus({ preventScroll: true });
  const hidden = getComputedStyle(button).outlineStyle === "none";
  button.blur();
  if (previous instanceof HTMLElement) previous.focus({ preventScroll: true });
  return hidden;
}

export async function inspectLauncherMotion() {
  const current = document.querySelector<HTMLButtonElement>(
    '.main-navigation button[aria-current="page"]',
  );
  const alternate = document.querySelector<HTMLButtonElement>(
    ".main-navigation button:last-of-type",
  );
  if (!current || !alternate || current === alternate)
    throw new Error("Page navigation is unavailable");
  const entries = () =>
    document
      .getAnimations()
      .filter((animation) => animation.id === "launcher-enter");
  flushSync(() => alternate.click());
  const entering = entries();
  const frames = entering.map((animation) => {
    const effect = animation.effect as KeyframeEffect;
    const duration = Number(effect.getTiming().duration);
    animation.pause();
    animation.currentTime = duration / 4;
    const style = getComputedStyle(effect.target as Element);
    return {
      durationMs: duration,
      opacity: Number(style.opacity),
      offsetY: new DOMMatrixReadOnly(style.transform).m42,
    };
  });
  flushSync(() => current.click());
  const interrupted = entering.every(
    (animation) => animation.playState === "idle",
  );
  // A hidden WebView suspends animation frames. Finish the finite UI effects
  // explicitly so geometry is measured at rest without a wall-clock wait.
  for (const animation of document.getAnimations()) animation.finish();
  const durations = getComputedStyle(current)
    .transitionDuration.split(",")
    .map(parseFloat);
  const result = {
    pageEnter: entering.length > 0,
    visibleFrames:
      frames.length > 0 &&
      frames.every(
        (frame) =>
          frame.durationMs >= 100 &&
          frame.durationMs <= 500 &&
          frame.opacity > 0 &&
          frame.opacity < 0.99 &&
          frame.offsetY > 0.1,
      ),
    frames,
    interrupted,
    controlTransitions: durations.some((value) => value > 0),
  };
  return {
    ...result,
    ok:
      result.pageEnter &&
      result.visibleFrames &&
      result.interrupted &&
      result.controlTransitions,
  };
}

export function inspectLauncherSelect() {
  const controls = [...document.querySelectorAll<HTMLSelectElement>("select")];
  const result = {
    supported: CSS.supports("appearance", "base-select"),
    styled:
      controls.length > 0 &&
      controls.every((control) => {
        const style = getComputedStyle(control);
        const picker = getComputedStyle(control, "::picker(select)");
        const option = control.querySelector("option");
        return (
          style.appearance === "base-select" &&
          picker.borderTopLeftRadius === "8px" &&
          picker.backdropFilter.includes("blur(32px)") &&
          picker.transitionProperty.includes("opacity") &&
          picker.maxBlockSize === "stretch" &&
          !!option &&
          getComputedStyle(option).fontWeight === "400"
        );
      }),
  };
  return { ...result, ok: result.supported && result.styled };
}

export async function inspectLaunchDialog() {
  function until(predicate: () => boolean) {
    return new Promise<void>((resolve, reject) => {
      const observer = new MutationObserver(check);
      const timer = setTimeout(() => {
        observer.disconnect();
        reject(new Error("Preflight dialog timed out"));
      }, 5000);
      function check() {
        if (!predicate()) return;
        clearTimeout(timer);
        observer.disconnect();
        resolve();
      }
      observer.observe(document.body, {
        childList: true,
        subtree: true,
        attributes: true,
      });
      check();
    });
  }
  const original = document.querySelector<HTMLButtonElement>(
    '.main-navigation button[aria-current="page"]',
  )!;
  const home = document.querySelector<HTMLButtonElement>(
    ".main-navigation button",
  )!;
  flushSync(() => home.click());
  document.querySelector<HTMLButtonElement>(".home-launch")!.click();
  await until(() => !!document.querySelector(".preflight-dialog[open]"));
  const dialog =
    document.querySelector<HTMLDialogElement>(".preflight-dialog")!;
  for (const animation of document.getAnimations()) animation.finish();
  const bounds = dialog.getBoundingClientRect();
  const result = {
    modal: dialog.matches(":modal"),
    errorIcons:
      dialog.querySelectorAll('[data-severity="error"] svg').length > 0,
    warningIcons:
      dialog.querySelectorAll('[data-severity="warning"] svg').length > 0,
    ssaaBlocks: !!dialog.querySelector(
      '[data-issue="ssaa"][data-severity="error"]',
    ),
    noSuppression: !dialog.querySelector('input[type="checkbox"]'),
    fits:
      bounds.left >= 0 &&
      bounds.top >= 0 &&
      bounds.right <= innerWidth &&
      bounds.bottom <= innerHeight,
    loaderRestored: false,
  };
  dialog
    .querySelector<HTMLButtonElement>('[data-fix="loader.openxr"]')!
    .click();
  await until(
    () =>
      dialog.getAttribute("aria-busy") !== "true" &&
      !dialog.querySelector('[data-issue="loader.openxr"]'),
  );
  result.loaderRestored = !!dialog.querySelector('[data-issue="game.binary"]');
  flushSync(() =>
    dialog.querySelector<HTMLButtonElement>("[data-preflight-close]")!.click(),
  );
  flushSync(() => original.click());
  for (const animation of document.getAnimations()) animation.finish();
  return { ...result, ok: Object.values(result).every(Boolean) };
}

export function inspectLauncherLayout() {
  const sidebar = document.querySelector<HTMLElement>(".settings-navigation");
  const content = document.querySelector<HTMLElement>(".settings-page");
  const workspace = document.querySelector<HTMLElement>(".workspace");
  if (!sidebar || !content || !workspace)
    throw new Error("Settings layout is unavailable");
  const before = sidebar.getBoundingClientRect();
  const viewport = workspace.getBoundingClientRect();
  const previousScroll = content.scrollTop;
  content.scrollTop = content.scrollHeight;
  const after = sidebar.getBoundingClientRect();
  const buttonsVisible = [...sidebar.querySelectorAll("button")].every(
    (button) => {
      const rect = button.getBoundingClientRect();
      return rect.top >= viewport.top - 1 && rect.bottom <= viewport.bottom + 1;
    },
  );
  const result = {
    viewport: [window.innerWidth, window.innerHeight],
    contentScrolls: content.scrollTop > 0,
    sidebarStable:
      Math.abs(before.top - after.top) < 1 &&
      Math.abs(before.bottom - after.bottom) < 1,
    sidebarFits:
      before.top >= viewport.top - 1 &&
      before.bottom <= viewport.bottom + 1 &&
      buttonsVisible,
    workspaceFits: workspace.scrollHeight <= workspace.clientHeight + 1,
    footerHidden: !document.querySelector(".app-footer"),
    resetVisible: (() => {
      const reset = document
        .getElementById("vr-reset")!
        .getBoundingClientRect();
      return (
        reset.left >= viewport.left &&
        reset.bottom <= viewport.bottom + 1 &&
        reset.top >= before.bottom - 1
      );
    })(),
    dark: getComputedStyle(document.documentElement).colorScheme === "dark",
    controls: document.querySelectorAll(".window-control").length === 3,
    calibrationRows: (() => {
      const rows = [...document.querySelectorAll(".calibration-row")];
      return rows.length === 2 && rows.every((row) => {
        const inputs = [...row.querySelectorAll("input")].map((input) => input.getBoundingClientRect());
        return inputs.length === 3 && inputs.every((bounds) =>
          Math.abs(bounds.top - inputs[0].top) < 1 && bounds.width >= 60 &&
          bounds.left >= viewport.left && bounds.right <= viewport.right);
      });
    })(),
    pipelineFirst: content.querySelector('[id^="vr_"]')?.id === "vr_controllerPoseMode",
    calibrationCommands: [...content.querySelectorAll(".calibration-command code")].length === 6,
    focusOutlineHidden: focusOutlineHidden(),
  };
  content.scrollTop = previousScroll;
  return {
    ...result,
    ok:
      result.contentScrolls &&
      result.sidebarStable &&
      result.sidebarFits &&
      result.workspaceFits &&
      result.footerHidden &&
      result.resetVisible &&
      result.dark &&
      result.controls &&
      result.calibrationRows &&
      result.pipelineFirst &&
      result.calibrationCommands &&
      result.focusOutlineHidden,
  };
}
export async function inspectHomeLayout() {
  const image = document.querySelector<HTMLImageElement>("#home-background");
  const home = document.querySelector<HTMLElement>(".home-page");
  if (!image || !home) throw new Error("Home background is unavailable");
  await image.decode();
  const content = home
    .querySelector<HTMLElement>(".home-controls")!
    .getBoundingClientRect();
  const bounds = home.getBoundingClientRect();
  const identity = home
    .querySelector<HTMLElement>(".home-identity")!
    .getBoundingClientRect();
  const result = {
    viewport: [window.innerWidth, window.innerHeight],
    image: image.naturalWidth === 1376 && image.naturalHeight === 768,
    launch: home.querySelectorAll("button").length === 1,
    runtime: home.querySelectorAll("select").length === 1,
    footerHidden: !document.querySelector(".app-footer"),
    identityFits:
      identity.left >= bounds.left &&
      identity.bottom <= bounds.bottom &&
      identity.right <= content.left,
    imageRounded: parseFloat(getComputedStyle(image).borderTopLeftRadius) > 0,
    controlsFit:
      content.left >= bounds.left &&
      content.right <= bounds.right &&
      content.top >= bounds.top &&
      content.bottom <= bounds.bottom,
    dark: getComputedStyle(document.documentElement).colorScheme === "dark",
    controls: document.querySelectorAll(".window-control").length === 3,
    focusOutlineHidden: focusOutlineHidden(),
  };
  return {
    ...result,
    ok:
      result.image &&
      result.launch &&
      result.runtime &&
      result.footerHidden &&
      result.identityFits &&
      result.imageRounded &&
      result.controlsFit &&
      result.dark &&
      result.controls &&
      result.focusOutlineHidden,
  };
}
