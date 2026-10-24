import { createElement } from "react";
import { createRoot } from "react-dom/client";
import { flushSync } from "react-dom";
import { MotionSurface } from "./components/MotionSurface";

function finishFiniteAnimations(animations = document.getAnimations()) {
  for (const animation of animations)
    if (Number.isFinite(animation.effect?.getComputedTiming().endTime)) animation.finish();
}

async function untilVisible(selector: string) {
  if (document.querySelector(selector)) return;
  await new Promise<void>((resolve, reject) => {
    const observer = new MutationObserver(() => {
      if (!document.querySelector(selector)) return;
      clearTimeout(timer);
      observer.disconnect();
      resolve();
    });
    const timer = setTimeout(() => {
      observer.disconnect();
      reject(new Error(`Launcher page did not appear: ${selector}`));
    }, 5000);
    observer.observe(document.body, { childList: true, subtree: true });
  });
}

export async function inspectPageTransitions() {
  const buttons = [...document.querySelectorAll<HTMLButtonElement>(".main-navigation > button")];
  if (buttons.length < 5) throw new Error("Launcher navigation is unavailable");
  const previous = buttons.findIndex((button) => button.hasAttribute("aria-current"));
  const select = (index: number) => flushSync(() => buttons[index].click());
  select(2);
  const languageTitle = !!document.querySelector(".language-page .page-heading h1 .decode-glow");
  const titleAnimations = [...document.querySelectorAll(".language-page h1 .decode-glyph")]
    .flatMap((glyph) => glyph.getAnimations().filter((animation) => animation.id === "launcher-decode"));
  const pageTitleDurationMs = Math.max(...titleAnimations.map((animation) =>
    Number(animation.effect!.getComputedTiming().endTime)));
  select(4);
  const aboutTitle = !!document.querySelector(".about-page .page-heading h1 .decode-glow");
  const aboutRoles = [...document.querySelectorAll(".about-page .credits-group h2")].every((heading) =>
    !!heading.querySelector(".decode-text") && !heading.querySelector(".decode-glow"));
  const aboutIds = [...document.querySelectorAll(".about-page .credits-group li")].every((person) =>
    !!person.querySelector(".decode-glow"));
  select(3);
  await untilVisible(".help-page .page-heading h1 .decode-glow");
  const helpTitle = !!document.querySelector(".help-page .page-heading h1 .decode-glow");
  const help = document.querySelector<HTMLElement>(".help-page")!;
  finishFiniteAnimations();
  const chrome = [".page-heading", ".help-search", ".sub-navigation"]
    .map((selector) => help.querySelector<HTMLElement>(selector)!);
  const chromeBounds = chrome.map((element) => element.getBoundingClientRect());
  flushSync(() => document.querySelectorAll<HTMLButtonElement>(".help-page .sub-navigation button")[1].click());
  const helpContent = help.querySelector<HTMLElement>(".help-content")!;
  const fade = helpContent.getAnimations().find((animation) => animation.id === "launcher-enter");
  if (!fade) throw new Error("Help category fade animation is unavailable");
  fade.pause();
  fade.currentTime = Number(fade.effect!.getTiming().duration) / 2;
  const fadeStyle = getComputedStyle(helpContent);
  const helpFadeOnly = Number(fadeStyle.opacity) > 0 && Number(fadeStyle.opacity) < 1 &&
    new DOMMatrixReadOnly(fadeStyle.transform).m41 === 0 &&
    new DOMMatrixReadOnly(fadeStyle.transform).m42 === 0;
  const helpChromeStable = getComputedStyle(help).opacity === "1" &&
    !help.getAnimations().some((animation) => animation.id === "launcher-enter") &&
    chrome.every((element, index) => {
      const bounds = element.getBoundingClientRect();
      return element.isConnected && getComputedStyle(element).opacity === "1" &&
        bounds.x === chromeBounds[index].x && bounds.y === chromeBounds[index].y;
    });
  fade.finish();
  select(1);
  const categories = [...document.querySelectorAll<HTMLButtonElement>(".settings-navigation button")];
  const previousCategory = categories.findIndex((button) => button.hasAttribute("aria-current"));
  flushSync(() => categories[0].click());
  const settingsTitle = !!document.querySelector(".settings-page .page-heading h1 .decode-glow");
  const fixture = document.createElement("div");
  fixture.style.cssText = "position:fixed;visibility:hidden;pointer-events:none";
  document.body.append(fixture);
  const root = createRoot(fixture);
  function direction(key: number, motion: "from-left" | "from-right") {
    flushSync(() => root.render(createElement(MotionSurface, { as: "div", motionKey: key, motion }, "guide")));
    const surface = fixture.firstElementChild!;
    const animation = surface.getAnimations().find((item) => item.id === "launcher-enter");
    if (!animation) throw new Error("Quick Guide direction animation is unavailable");
    animation.pause();
    animation.currentTime = Number(animation.effect!.getTiming().duration) / 2;
    return new DOMMatrixReadOnly(getComputedStyle(surface).transform).m41;
  }
  let forwardX = 0, backX = 0;
  try {
    forwardX = direction(1, "from-right");
    backX = direction(2, "from-left");
  } finally {
    flushSync(() => root.unmount());
    fixture.remove();
  }
  if (previousCategory >= 0) flushSync(() => categories[previousCategory].click());
  if (previous >= 0) select(previous);
  finishFiniteAnimations();
  const result = { languageTitle, pageTitleDurationMs, aboutTitle, aboutRoles, aboutIds, helpTitle, helpFadeOnly, helpChromeStable,
    settingsTitle, forwardX, backX };
  return { ...result, ok: languageTitle && Math.abs(pageTitleDurationMs - 600) < 0.1 &&
    aboutTitle && aboutRoles && aboutIds && helpTitle && helpFadeOnly && helpChromeStable &&
    settingsTitle && forwardX > 0 && backX < 0 };
}

function inspectTextGlow() {
  const title = document.querySelector<HTMLElement>(".home-identity h1")!;
  const glows = [...title.querySelectorAll<HTMLElement>(".decode-glow")];
  const result = {
    titleAndVersion: glows.length > 0 && !!document.querySelector(".home-identity p .decode-glow"),
    green: glows.every((glow) => getComputedStyle(glow, "::before").color === "rgb(141, 255, 154)"),
    synced: glows.every((glow) => glow.dataset.glyph === glow.previousElementSibling?.textContent),
    foregroundClear: glows.every((glow) =>
      getComputedStyle(glow.previousElementSibling!).maskImage === "none" &&
      getComputedStyle(glow.parentElement!).maskImage === "none" &&
      getComputedStyle(glow).maskImage !== "none"),
    enlarged: parseFloat(getComputedStyle(title).fontSize) >= 36,
    moving: false,
    flickerIndependent: false,
  };
  const glow = glows[0];
  const scan = glow?.getAnimations().find((animation) => animation instanceof CSSAnimation && animation.animationName === "decode-scan");
  if (scan) {
    scan.pause();
    scan.currentTime = 0;
    const start = getComputedStyle(glow).maskPosition;
    scan.currentTime = Number(scan.effect!.getTiming().duration) / 2;
    result.moving = getComputedStyle(glow).maskPosition !== start;
    scan.play();
    const label = glow.closest<HTMLElement>(".decode-text")!;
    const previous = label.style.getPropertyValue("--decode-flicker");
    const before = Number(getComputedStyle(glow).opacity);
    label.style.setProperty("--decode-flicker", "0.5");
    finishFiniteAnimations(glow.getAnimations());
    result.flickerIndependent = Number(getComputedStyle(glow).opacity) < before &&
      getComputedStyle(glow.previousElementSibling!).opacity === "1";
    label.style.setProperty("--decode-flicker", previous);
    finishFiniteAnimations(glow.getAnimations());
  }
  return { ...result, ok: Object.values(result).every(Boolean) };
}

export function inspectStartupNotice(version: string) {
  const dialog = document.querySelector<HTMLDialogElement>(".startup-notice")!;
  finishFiniteAnimations();
  const bounds = dialog.getBoundingClientRect();
  const content = dialog.querySelector<HTMLElement>(".notice-content")!;
  const text = content.innerText;
  const result = {
    modal: dialog.matches(":modal"),
    version: text.includes(version) && !!version,
    paragraphs: content.querySelectorAll("p").length === 3,
    diagnostics: text.includes("vr_diagnose") && text.includes("minidump"),
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

async function inspectDecodedText() {
  const labels = [...document.querySelectorAll<HTMLElement>(".home-identity .decode-text")];
  const results = [];
  for (const label of labels) {
    const glyphs = [...label.querySelectorAll<HTMLElement>(".decode-glyph")];
    const entries = glyphs.flatMap((glyph) => {
      const animation = glyph.getAnimations().find((item) => item.id === "launcher-decode");
      return animation ? [{ glyph, animation }] : [];
    });
    if (entries.length < 2) throw new Error("Text reveal animations are unavailable");
    const original = (glyph: HTMLElement) => glyph.previousElementSibling!.textContent;
    const width = label.getBoundingClientRect().width;
    const [first, next] = entries;
    const timing = first.animation.effect!.getTiming();
    const duration = Number(timing.duration);
    for (const { animation } of entries) {
      animation.pause();
      animation.currentTime = (timing.delay ?? 0) + duration / 2;
    }
    const opacity = Number(getComputedStyle(first.glyph).opacity);
    const sequential = opacity > 0 && opacity < 1 &&
      entries.slice(1).every(({ glyph }) => Number(getComputedStyle(glyph).opacity) === 0);
    const scrambled = first.glyph.textContent !== original(first.glyph);
    first.animation.finish();
    await first.animation.finished;
    next.animation.currentTime = (next.animation.effect!.getTiming().delay ?? 0) + duration / 2;
    const settledBeforeNext = first.glyph.textContent === original(first.glyph) &&
      Number(getComputedStyle(first.glyph).opacity) === 1 &&
      Number(getComputedStyle(next.glyph).opacity) > 0 &&
      Number(getComputedStyle(next.glyph).opacity) < 1;
    for (const { animation } of entries) animation.finish();
    await Promise.all(entries.map(({ animation }) => animation.finished));
    const stable = glyphs.every((glyph) => glyph.textContent === original(glyph)) &&
      label.getBoundingClientRect().width === width;
    results.push({ sequential, scrambled, settledBeforeNext, stable,
      durationMs: duration * entries.length, delayMs: timing.delay });
  }
  return { labels: results, ok: results.every((result) =>
    result.sequential && result.scrambled && result.settledBeforeNext && result.stable) };
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
  const previousText = document.getAnimations().filter((animation) => animation.id === "launcher-decode");
  flushSync(() => alternate.click());
  const textCancelled = previousText.every((animation) => animation.playState === "idle");
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
  const textReveal = await inspectDecodedText();
  // A hidden WebView suspends animation frames. Finish the finite UI effects
  // explicitly so geometry is measured at rest without a wall-clock wait.
  finishFiniteAnimations();
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
    textCancelled,
    textReveal,
    controlTransitions: durations.some((value) => value > 0),
  };
  return {
    ...result,
    ok:
      result.pageEnter &&
      result.visibleFrames &&
      result.interrupted &&
      result.textCancelled &&
      result.textReveal.ok &&
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
  finishFiniteAnimations();
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
  finishFiniteAnimations();
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
    worldSizeFirst: content.querySelector('[id^="vr_"]')?.id === "vr_worldScale" &&
      content.querySelector<HTMLInputElement>("#vr_worldScale")?.value === "100" &&
      content.querySelector<HTMLInputElement>("#vr_worldScale")?.validity.valid &&
      !!content.querySelector("#vr_controllerPoseMode"),
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
      result.worldSizeFirst &&
      result.calibrationCommands &&
      result.focusOutlineHidden,
  };
}

export function inspectCampaignUnlock() {
  const navigation = document.querySelector(".settings-navigation");
  const previous = navigation?.querySelector<HTMLButtonElement>('[aria-current="page"]');
  const cheats = navigation?.querySelector<HTMLButtonElement>('button[data-category="cheats"]');
  if (!previous || !cheats) throw new Error("Cheats navigation is unavailable");
  flushSync(() => cheats.click());
  finishFiniteAnimations();
  const page = document.querySelector<HTMLElement>(".settings-page")!;
  const control = page.querySelector<HTMLInputElement>("#campaign-unlock");
  const bounds = control?.closest(".switch")?.getBoundingClientRect();
  const groups = [...page.querySelectorAll<HTMLElement>(".setting-group")];
  const groupOf = (name: string) => document.getElementById(name)?.closest(".setting-group");
  const result = {
    first: page.querySelector(".setting-row input") === control && !!control,
    defaultOff: control?.checked === false,
    boolean: control?.type === "checkbox",
    descriptionAtHeading: !!page.querySelector(".page-heading > .hint")?.textContent && !page.querySelector(".cheat-notice"),
    fits: !!bounds && bounds.left >= 0 && bounds.right <= innerWidth && page.scrollWidth <= page.clientWidth + 1,
    playerGroup: groups[0]?.dataset.settingGroup === "settings.playerAttributes" &&
      groupOf("vr_cheatHealth") === groups[0] && groupOf("vr_cheatNotarget") === groups[0],
    weaponGroup: groups[1]?.dataset.settingGroup === "settings.weaponHandling" &&
      groupOf("vr_cheatAmmo") === groups[1] && groupOf("vr_recoil") === groups[1],
  };
  flushSync(() => previous.click());
  finishFiniteAnimations();
  return { ...result, ok: Object.values(result).every(Boolean) };
}

export function inspectNativeComfort() {
  const navigation = document.querySelector(".settings-navigation");
  const previous = navigation?.querySelector<HTMLButtonElement>('[aria-current="page"]');
  const basics = navigation?.querySelector<HTMLButtonElement>("button");
  if (!previous || !basics) throw new Error("Basics navigation is unavailable");
  flushSync(() => basics.click());
  finishFiniteAnimations();
  const names = ["r_dof_limit", "r_mbLimit"];
  const result = {
    controls: names.every((name) => {
      const control = document.getElementById(name) as HTMLInputElement | null;
      return control?.type === "checkbox" && !control.checked && !control.disabled;
    }),
    descriptions: names.every((name) => {
      const hint = document.getElementById(`hint-${name}`)?.textContent;
      return !!hint && !hint.startsWith("settings.");
    }),
    fits: names.every((name) => {
      const row = document.getElementById(name)?.closest(".setting-row");
      const bounds = row?.getBoundingClientRect();
      return !!bounds && bounds.left >= 0 && bounds.right <= innerWidth &&
        row!.scrollWidth <= row!.clientWidth + 1;
    }),
    subtitleSecond: false,
    subtitleDefaultOn: false,
    subtitleDescription: false,
    riskFirst: false,
  };
  const other = navigation!.querySelectorAll<HTMLButtonElement>("button")[4];
  flushSync(() => other.click());
  finishFiniteAnimations();
  const subtitle = document.getElementById("vr_subtitles") as HTMLInputElement | null;
  const controls = document.querySelectorAll<HTMLInputElement | HTMLSelectElement>(".setting-row input, .setting-row select");
  result.subtitleSecond = !!subtitle && controls[1] === subtitle;
  result.subtitleDefaultOn = subtitle?.type === "checkbox" && subtitle.checked && !subtitle.disabled;
  const hint = document.getElementById("hint-vr_subtitles")?.textContent;
  result.subtitleDescription = !!hint && !hint.startsWith("settings.");
  const debug = navigation!.querySelectorAll<HTMLButtonElement>("button")[5];
  flushSync(() => debug.click());
  finishFiniteAnimations();
  const firstGroup = document.querySelector<HTMLElement>(".settings-page .setting-group");
  result.riskFirst = firstGroup?.dataset.settingGroup === "settings.riskHeading" &&
    !!firstGroup.querySelector("button.button");
  flushSync(() => previous.click());
  finishFiniteAnimations();
  return { ...result, ok: Object.values(result).every(Boolean) };
}
function inspectHomeParallax(scene: HTMLElement, image: HTMLImageElement) {
  const bounds = scene.getBoundingClientRect();
  const controls = scene.querySelector<HTMLElement>(".home-controls")!;
  const identity = scene.querySelector<HTMLElement>(".home-identity")!;
  const before = controls.getBoundingClientRect();
  let moved = true;
  let covered = true;
  let foregroundMoved = true;
  for (const direction of [-1, 1]) {
    scene.dispatchEvent(new PointerEvent("pointermove", {
      pointerType: "mouse",
      clientX: direction < 0 ? bounds.left : bounds.right,
      clientY: direction < 0 ? bounds.top : bounds.bottom,
    }));
    for (const animation of image.getAnimations()) animation.finish();
    for (const animation of identity.getAnimations()) animation.finish();
    const transform = new DOMMatrixReadOnly(getComputedStyle(image).transform);
    moved &&= transform.m41 * direction > 0 && transform.m42 * direction > 0 &&
      Math.abs(transform.m41) <= 4 && Math.abs(transform.m42) <= 4;
    const foreground = getComputedStyle(identity).translate.split(" ").map(parseFloat);
    foregroundMoved &&= foreground.length === 2 && foreground.every((offset) =>
      offset * direction > 4 && Math.abs(offset) <= 6);
    const artwork = image.getBoundingClientRect();
    covered &&= artwork.left <= bounds.left && artwork.top <= bounds.top &&
      artwork.right >= bounds.right && artwork.bottom >= bounds.bottom;
  }
  const lastImage = getComputedStyle(image).transform;
  const lastIdentity = getComputedStyle(identity).translate;
  scene.dispatchEvent(new PointerEvent("pointerleave", { pointerType: "mouse" }));
  for (const animation of image.getAnimations()) animation.finish();
  for (const animation of identity.getAnimations()) animation.finish();
  const after = controls.getBoundingClientRect();
  return {
    moved, covered, foregroundMoved,
    retained: getComputedStyle(image).transform === lastImage &&
      getComputedStyle(identity).translate === lastIdentity,
    controlsStable: before.x === after.x && before.y === after.y,
  };
}

export async function inspectHomeLayout() {
  const image = document.querySelector<HTMLImageElement>("#home-background");
  const home = document.querySelector<HTMLElement>(".home-page");
  const scene = home?.querySelector<HTMLElement>(".home-scene");
  if (!image || !home || !scene) throw new Error("Home background is unavailable");
  await image.decode();
  const content = home
    .querySelector<HTMLElement>(".home-controls")!
    .getBoundingClientRect();
  const bounds = scene.getBoundingClientRect();
  const pageBounds = home.getBoundingClientRect();
  const parallax = inspectHomeParallax(scene, image);
  const glow = inspectTextGlow();
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
    imageRounded: getComputedStyle(scene).borderTopLeftRadius === "6px" &&
      getComputedStyle(scene).overflow === "hidden",
    imageInset: [bounds.left - pageBounds.left, bounds.top - pageBounds.top,
      pageBounds.right - bounds.right, pageBounds.bottom - bounds.bottom]
      .every((inset) => Math.abs(inset - 8) < 0.1),
    parallax,
    glow,
    resizeGrips: (() => {
      const grips = [...document.querySelectorAll<HTMLElement>(".window-resize")];
      // The WebView viewport can be fractional CSS pixels at non-integer DPI.
      const viewport = document.body.getBoundingClientRect();
      return grips.length === 8 && grips.every((grip) => {
        const rect = grip.getBoundingClientRect();
        return rect.width >= 6 && rect.height >= 6 &&
          rect.left >= 0 && rect.top >= 0 &&
          rect.right <= viewport.right && rect.bottom <= viewport.bottom &&
          document.elementFromPoint(rect.left + rect.width / 2, rect.top + rect.height / 2) === grip;
      });
    })(),
    resizeGripDetails: [...document.querySelectorAll<HTMLElement>(".window-resize")].map((grip) => {
      const rect = grip.getBoundingClientRect();
      return { edge: grip.className, bounds: rect.toJSON(),
        target: document.elementFromPoint(rect.left + rect.width / 2, rect.top + rect.height / 2)?.className };
    }),
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
      result.resizeGrips &&
      result.launch &&
      result.runtime &&
      result.footerHidden &&
      result.identityFits &&
      result.imageRounded &&
      result.imageInset &&
      Object.values(result.parallax).every(Boolean) &&
      result.glow.ok &&
      result.controlsFit &&
      result.dark &&
      result.controls &&
      result.focusOutlineHidden,
  };
}
