import { useLayoutEffect, useRef, type HTMLAttributes } from "react";

// Animate the existing DOM surface: drafts, focus and scroll remain owned by
// the page, and saving settings never waits for an animation to finish.
export function MotionSurface({
  as: Tag = "section",
  motionKey,
  motion = "up",
  ...props
}: HTMLAttributes<HTMLDivElement> & {
  as?: "div" | "section";
  motionKey?: string | number;
  motion?: "up" | "fade" | "from-left" | "from-right";
}) {
  const ref = useRef<HTMLDivElement>(null);

  useLayoutEffect(() => {
    const element = ref.current;
    if (!element) return;
    const style = getComputedStyle(element);
    // Production CSS may normalize ms to s; Web Animations always takes ms.
    const time = style.getPropertyValue("--motion-enter-duration").trim();
    const duration = parseFloat(time) * (time.endsWith("ms") ? 1 : 1000);
    const offset = motion === "from-left" ? "translateX(-18px)" :
      motion === "from-right" ? "translateX(18px)" :
      motion === "up" ? "translateY(10px)" : "none";
    const animation = element.animate(
      [
        { opacity: 0, transform: offset },
        { opacity: 1, transform: "none" },
      ],
      {
        id: "launcher-enter",
        duration,
        easing: style.getPropertyValue("--motion-ease").trim(),
      },
    );
    return () => animation.cancel();
  }, [motionKey, motion]);

  return <Tag ref={ref} {...props} />;
}
