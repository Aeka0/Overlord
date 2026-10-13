import { useEffect, useLayoutEffect, useMemo, useRef, type CSSProperties } from "react";
import "./DecodedText.css";

const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const defaultDuration = 600;

export interface TextGlowOptions {
  color?: string;
  /** Brightness and random dimming range, both 0–1. Zero flicker is steady. */
  intensity?: number;
  flickerStrength?: number;
  scanlines?: false | {
    direction?: "up" | "down" | "static";
    speedPxPerSecond?: number;
    spacingPx?: number;
    strength?: number;
  };
}

const bounded = (value: number | undefined, fallback: number, min: number, max: number) =>
  Number.isFinite(value) ? Math.max(min, Math.min(max, value!)) : fallback;

function setGlyph(glyph: HTMLElement, value: string) {
  glyph.querySelector<HTMLElement>(".decode-front")!.textContent = value;
  glyph.querySelector<HTMLElement>(".decode-glow")?.setAttribute("data-glyph", value);
}

export interface DecodedTextProps {
  text: string;
  active?: boolean;
  /** Total reveal duration, excluding delay. Zero displays the final text immediately. */
  durationMs?: number;
  delayMs?: number;
  glow?: TextGlowOptions;
}

export function DecodedText({
  text, active = true, durationMs = defaultDuration, delayMs = 0, glow,
}: DecodedTextProps) {
  const rootRef = useRef<HTMLSpanElement>(null);
  const glowEnabled = !!glow;
  const flicker = bounded(glow?.flickerStrength, 0.18, 0, 1);
  const stripes = glow?.scanlines === false ? undefined : glow?.scanlines ?? {};
  const spacing = bounded(stripes?.spacingPx, 4, 2, 32);
  const speed = bounded(stripes?.speedPxPerSecond, 3, 0, 60);
  const direction = stripes?.direction ?? "up";
  const style = glow ? {
    "--decode-glow-color": glow.color ?? "#8dff9a",
    "--decode-glow-intensity": bounded(glow.intensity, 0.8, 0, 1),
    "--decode-stripe-spacing": `${spacing}px`,
    "--decode-stripe-half": `${spacing / 2}px`,
    "--decode-stripe-gap": 1 - bounded(stripes?.strength, 0.65, 0, 1),
    "--decode-stripe-duration": `${speed > 0 ? spacing / speed : 1}s`,
    "--decode-stripe-direction": direction === "up" ? "reverse" : "normal",
  } as CSSProperties : undefined;
  const characters = useMemo(() => Array.from(
    new Intl.Segmenter(undefined, { granularity: "grapheme" }).segment(text),
    ({ segment }) => segment,
  ), [text]);

  useEffect(() => {
    const root = rootRef.current!;
    root.style.setProperty("--decode-flicker", "1");
    if (!active || !glowEnabled || flicker === 0) return;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const schedule = () => {
      timer = setTimeout(() => {
        root.style.setProperty("--decode-flicker", String(1 - Math.random() * flicker));
        schedule();
      }, 80 + Math.random() * 160);
    };
    const visibility = () => {
      clearTimeout(timer);
      if (!document.hidden) schedule();
    };
    visibility();
    document.addEventListener("visibilitychange", visibility);
    return () => {
      clearTimeout(timer);
      document.removeEventListener("visibilitychange", visibility);
    };
  }, [active, glowEnabled, flicker]);

  useLayoutEffect(() => {
    const glyphs = [...rootRef.current!.querySelectorAll<HTMLElement>(".decode-glyph")];
    if (!active) return;
    const total = Number.isFinite(durationMs) ? Math.max(0, durationMs) : defaultDuration;
    if (total === 0) {
      glyphs.forEach((glyph, index) => {
        setGlyph(glyph, characters[index]);
        glyph.style.opacity = "1";
      });
      return () => glyphs.forEach((glyph) => glyph.style.removeProperty("opacity"));
    }
    const randomGlyph = (original: string) => {
      const choices = alphabet.replace(original.toUpperCase(), "");
      return choices[Math.floor(Math.random() * choices.length)];
    };
    let disposed = false;
    let delay = Number.isFinite(delayMs) ? Math.max(0, delayMs) : 0;
    const duration = total / Math.max(1, characters.filter((character) => !/^\s+$/u.test(character)).length);
    const entries = glyphs.flatMap((glyph, index) => {
      const original = characters[index];
      if (/^\s+$/u.test(original)) return [];
      setGlyph(glyph, randomGlyph(original));
      const animation = glyph.animate([{ opacity: 0 }, { opacity: 1 }], {
        id: "launcher-decode", duration, delay, fill: "both", easing: "linear",
      });
      delay += duration;
      void animation.finished.then(() => {
        if (!disposed) setGlyph(glyph, original);
      }, () => {}); // Cancellation is expected when the text or page changes.
      return [{ glyph, original, animation }];
    });
    let frame = 0;
    let lastChange = -Infinity;
    const update = (now: number) => {
      let pending = false;
      for (const { glyph, original, animation } of entries) {
        if (animation.playState === "finished") {
          setGlyph(glyph, original);
          continue;
        }
        pending = true;
        const progress = animation.effect!.getComputedTiming().progress ?? 0;
        if (progress > 0 && progress < 1 && now - lastChange >= 32)
          setGlyph(glyph, randomGlyph(original));
      }
      if (now - lastChange >= 32) lastChange = now;
      if (pending) frame = requestAnimationFrame(update);
    };
    frame = requestAnimationFrame(update);
    return () => {
      disposed = true;
      cancelAnimationFrame(frame);
      for (const { animation } of entries) {
        animation.cancel();
      }
    };
  }, [characters, active, durationMs, delayMs, glowEnabled]);

  return (
    <span ref={rootRef} className="decode-text" style={style}>
      <span className="sr-only">{text}</span>
      <span aria-hidden="true">
        {characters.map((character, index) => (
          <span className="decode-character" key={`${index}-${character}`}>
            <span className="decode-measure">{character}</span>
            <span className="decode-glyph">
              <span className="decode-front">{character}</span>
              {glow && <span
                className="decode-glow"
                data-glyph={character}
                data-scanlines={!!stripes}
                data-moving={active && !!stripes && direction !== "static" && speed > 0}
              />}
            </span>
          </span>
        ))}
      </span>
    </span>
  );
}
