import fieldsJson from "../../client/resources/launcher/settings-schema.json";

export type Values = Record<string, string | number | boolean>;
export type Draft = Record<string, string | boolean>;
export interface NumberLimit {
  min: number;
  max: number;
  step: number;
  displayScale: number;
  displayReciprocal?: boolean;
}
export interface SettingsState {
  ok: boolean;
  values: Values;
  defaults: Values;
  limits: Record<string, NumberLimit>;
  choices: Record<string, Array<{ value: string; labelKey: string }>>;
  controllerPresets: Array<{ id: string; label: string; values: Values }>;
  onboarding: { gameAvailable: boolean; hasVRConfig: boolean };
  build: { configuration: string; optimized: boolean };
  error?: string;
}
export interface Field {
  name: string;
  category: string;
  group: string;
  label: string;
  type: "number" | "choice" | "toggle";
  description?: string;
  unit?: string;
  inverted?: boolean;
  visibleWhen?: [string, string | boolean];
  row?: string;
}
export const fields = fieldsJson as Field[];
export const categories = [
  "basics",
  "calibration",
  "gameplay",
  "cheats",
  "other",
  "debug",
] as const;

export class SettingError extends Error {
  constructor(
    readonly setting: string,
    readonly values: Record<string, string | number>,
  ) {
    super("error.range");
  }
}
const rounded = (value: number) => Math.round(value * 1e6) / 1e6;
function displayNumber(value: number, limit?: NumberLimit) {
  const scale = limit?.displayScale || 1;
  return rounded(limit?.displayReciprocal ? 100 * scale / value : value / scale);
}
export function displayLimit(limit: NumberLimit) {
  const scale = limit.displayScale || 1;
  return {
    min: displayNumber(limit.displayReciprocal ? limit.max : limit.min, limit),
    max: displayNumber(limit.displayReciprocal ? limit.min : limit.max, limit),
    step: limit.displayReciprocal ? limit.step : rounded(limit.step / scale),
    scale,
  };
}
export function makeDraft(
  values: Values,
  limits: SettingsState["limits"],
): Draft {
  return Object.fromEntries(
    Object.entries(values).map(([key, value]) => {
      const field = fields.find((field) => field.name === key);
      return [
        key,
        typeof value === "boolean"
          ? field?.inverted
            ? !value
            : value
          : typeof value === "number"
            ? String(displayNumber(value, limits[key]))
            : value,
      ];
    }),
  );
}
export function collect(
  draft: Draft,
  state: SettingsState,
  base: Values = state.values,
  keys = Object.keys(base),
): Values {
  const values = { ...base };
  for (const key of keys) {
    const value = draft[key];
    const field = fields.find((field) => field.name === key);
    if (typeof base[key] === "boolean") {
      if (typeof value !== "boolean") throw new Error("error.invalidVR");
      values[key] = field?.inverted ? !value : value;
    } else if (state.limits[key]) {
      const limit = displayLimit(state.limits[key]);
      const text = typeof value === "string" ? value.trim() : "";
      const number = Number(text);
      if (
        !text ||
        text.length > 64 ||
        !/^[-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?$/i.test(text) ||
        !Number.isFinite(number) ||
        number < limit.min ||
        number > limit.max
      )
        throw new SettingError(key, { min: limit.min, max: limit.max });
      const nativeLimit = state.limits[key];
      // Preserve console precision on unrelated saves. Reciprocal display
      // rounding must not repeatedly change the underlying spatial scale.
      values[key] = nativeLimit.displayReciprocal
        ? number === displayNumber(Number(base[key]), nativeLimit)
          ? base[key]
          : Math.min(nativeLimit.max, Math.max(nativeLimit.min, rounded(100 * limit.scale / number)))
        : rounded(number * limit.scale);
    } else {
      if (
        typeof value !== "string" ||
        !state.choices[key]?.some((choice) => choice.value === value)
      )
        throw new Error("error.invalidVR");
      values[key] = value;
    }
  }
  return values;
}
export function visible(field: Field, draft: Draft) {
  return (
    !field.visibleWhen || draft[field.visibleWhen[0]] === field.visibleWhen[1]
  );
}
