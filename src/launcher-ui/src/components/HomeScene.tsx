import { useEffect, useRef, type ReactNode } from "react";

export function HomeScene({ children }: { children: ReactNode }) {
  const sceneRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const scene = sceneRef.current!;
    const move = (event: PointerEvent) => {
      if (event.pointerType !== "mouse") return;
      const bounds = scene.getBoundingClientRect();
      if (!bounds.width || !bounds.height) return;
      // Both depth layers share the pointer position without React updates.
      const offset = (position: number, extent: number) =>
        Math.max(-1, Math.min(1, position / extent * 2 - 1)) * 4;
      scene.style.setProperty("--parallax-x", `${offset(event.clientX - bounds.left, bounds.width)}px`);
      scene.style.setProperty("--parallax-y", `${offset(event.clientY - bounds.top, bounds.height)}px`);
    };
    // Keep the last position on leave, focus loss and resize until the next move.
    scene.addEventListener("pointermove", move, { passive: true });
    return () => {
      scene.removeEventListener("pointermove", move);
    };
  }, []);

  return (
    <div ref={sceneRef} className="home-scene">
      <img
        id="home-background"
        className="home-artwork"
        src="/images/launcher-background.jpg"
        alt=""
        draggable={false}
      />
      {children}
    </div>
  );
}
