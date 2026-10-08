import type { ReactNode } from "react";
import {
  displayLimit,
  visible,
  type Field,
  type SettingsState,
  type Draft,
} from "../model";
import type { Translate } from "../i18n";

export function SettingRow({
  title,
  description,
  children,
}: {
  title: ReactNode;
  description?: ReactNode;
  children: ReactNode;
}) {
  return (
    <div className="setting-row">
      <div className="setting-copy">
        <div className="setting-label">{title}</div>
        {description && <p className="hint">{description}</p>}
      </div>
      <div className="setting-control">{children}</div>
    </div>
  );
}
export function Switch({
  checked,
  label,
  id,
  disabled,
  onChange,
}: {
  checked: boolean;
  label: string;
  id?: string;
  disabled?: boolean;
  onChange: (value: boolean) => void;
}) {
  return (
    <label className={`switch${disabled ? " disabled" : ""}`}>
      <input
        id={id}
        type="checkbox"
        aria-label={label}
        checked={checked}
        disabled={disabled}
        onChange={(event) => onChange(event.currentTarget.checked)}
      />
      <span className="switch-track" aria-hidden="true" />
    </label>
  );
}
export function FieldControl({
  field,
  draft,
  state,
  t,
  disabled,
  invalid,
  description,
  onChange,
  onBlur,
}: {
  field: Field;
  draft: Draft;
  state: SettingsState;
  t: Translate;
  disabled?: boolean;
  invalid?: boolean;
  description?: string;
  onChange: (key: string, value: string | boolean, numeric: boolean) => void;
  onBlur?: (key: string) => void;
}) {
  if (!visible(field, draft)) return null;
  const label = t(field.label);
  const hintId = `hint-${field.name}`;
  const limits =
    state.limits[field.name] && displayLimit(state.limits[field.name]);
  return (
    <SettingRow
      title={<label htmlFor={field.name}>{label}</label>}
      description={
        <span id={hintId}>
          {description
            ? t(description)
            : field.description
              ? t(field.description)
              : limits
                ? `${limits.min} – ${limits.max}`
                : ""}
        </span>
      }
    >
      {field.type === "toggle" ? (
        <Switch
          id={field.name}
          checked={Boolean(draft[field.name])}
          label={label}
          disabled={disabled}
          onChange={(value) => onChange(field.name, value, false)}
        />
      ) : field.type === "choice" ? (
        <select
          id={field.name}
          aria-describedby={hintId}
          disabled={disabled}
          value={String(draft[field.name])}
          onChange={(event) =>
            onChange(field.name, event.currentTarget.value, false)
          }
        >
          {state.choices[field.name]?.map((choice) => (
            <option key={choice.value} value={choice.value}>
              {t(choice.labelKey)}
            </option>
          ))}
        </select>
      ) : (
        <div className="number-control">
          <input
            id={field.name}
            type="number"
            inputMode="decimal"
            aria-describedby={hintId}
            aria-invalid={invalid || undefined}
            disabled={disabled}
            min={limits?.min}
            max={limits?.max}
            step={limits?.step || "any"}
            value={String(draft[field.name])}
            onChange={(event) =>
              onChange(field.name, event.currentTarget.value, true)
            }
            onBlur={() => onBlur?.(field.name)}
          />
          {field.unit && <span className="unit">{t(field.unit)}</span>}
        </div>
      )}
    </SettingRow>
  );
}
