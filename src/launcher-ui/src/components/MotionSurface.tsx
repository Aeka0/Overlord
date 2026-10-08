import { useLayoutEffect, useRef, type HTMLAttributes } from "react";

// Animate the existing DOM surface: drafts, focus and scroll remain owned by
// the page, and saving settings never waits for an animation to finish.
export function MotionSurface({
  as: Tag = "section",
  motionKey,
  ...props
}: HTMLAttributes<HTMLDivElement> & {
  as?: "div" | "section";
  motionKey?: string | number;
}) {
  const ref = useRef<HTMLDivElement>(null);

  useLayoutEffect(() => {
    const element = ref.current;
    if (!element) return;
    const style = getComputedStyle(element);
    // Production CSS may normalize ms to s; Web Animations always takes ms.
    const time = style.getPropertyValue("--motion-enter-duration").trim();
    const duration = parseFloat(time) * (time.endsWith("ms") ? 1 : 1000);
    const animation = element.animate(
      [
        { opacity: 0, transform: "translateY(10px)" },
        { opacity: 1, transform: "translateY(0)" },
      ],
      {
        id: "launcher-enter",
        duration,
        easing: style.getPropertyValue("--motion-ease").trim(),
      },
    );
    return () => animation.cancel();
  }, [motionKey]);

  return <Tag ref={ref} {...props} />;
}
