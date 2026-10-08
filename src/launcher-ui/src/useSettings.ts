import { useEffect, useRef, useState } from "react";
import { bridge } from "./bridge";
import {
  collect,
  makeDraft,
  SettingError,
  type Draft,
  type SettingsState,
  type Values,
} from "./model";
import { errorKey } from "./i18n";

export interface Notice {
  key: string;
  error?: boolean;
  values?: Record<string, string | number>;
  setting?: string;
}
export function useSettings(initial: SettingsState) {
  const [state, setState] = useState(initial);
  const stateRef = useRef(initial);
  const [draft, setDraft] = useState(() =>
    makeDraft(initial.values, initial.limits),
  );
  const draftRef = useRef(draft);
  const versions = useRef<Record<string, number>>({});
  const base = useRef(initial.values);
  const queue = useRef(Promise.resolve());
  const timers = useRef(new Map<string, ReturnType<typeof setTimeout>>());
  const [pending, setPending] = useState(0);
  const [notice, setNotice] = useState<Notice | null>(null);
  const live = useRef(true);
  useEffect(() => {
    live.current = true;
    return () => {
      live.current = false;
      for (const timer of timers.current.values()) clearTimeout(timer);
    };
  }, []);
  const report = (error: unknown) =>
    setNotice({
      key: errorKey(error),
      error: true,
      ...(error instanceof SettingError
        ? { setting: error.setting, values: error.values }
        : {}),
    });
  function edit(key: string, value: string | boolean) {
    versions.current[key] = (versions.current[key] || 0) + 1;
    draftRef.current = { ...draftRef.current, [key]: value };
    setDraft(draftRef.current);
    setNotice(null);
    return versions.current[key];
  }
  function replace(values: Values) {
    const next = makeDraft(values, stateRef.current.limits);
    for (const key of Object.keys(next))
      versions.current[key] = (versions.current[key] || 0) + 1;
    draftRef.current = next;
    setDraft(next);
    setNotice(null);
  }
  function clearTimers() {
    for (const timer of timers.current.values()) clearTimeout(timer);
    timers.current.clear();
  }
  function revert(
    key: string,
    expected: string | boolean,
    previous: string | boolean,
    revision?: number,
  ) {
    if (
      draftRef.current[key] !== expected ||
      (revision !== undefined && versions.current[key] !== revision)
    )
      return;
    versions.current[key] = (versions.current[key] || 0) + 1;
    draftRef.current = { ...draftRef.current, [key]: previous };
    setDraft(draftRef.current);
  }
  async function persist(values?: Values, keys?: string[]) {
    const captured = { ...versions.current };
    try {
      const payload =
        values ??
        collect(draftRef.current, stateRef.current, base.current, keys);
      if (
        stateRef.current.onboarding.hasVRConfig &&
        JSON.stringify(payload) === JSON.stringify(stateRef.current.values)
      )
        return true;
      const result = await bridge!.request<SettingsState>("settings.save", {
        values: payload,
      });
      stateRef.current = result;
      base.current = result.values;
      if (!live.current) return true;
      setState(result);
      const canonical = makeDraft(result.values, result.limits);
      const next = { ...draftRef.current };
      for (const key of Object.keys(canonical))
        if (
          captured[key] === versions.current[key] &&
          (!keys || keys.includes(key) || values)
        )
          next[key] = canonical[key];
      draftRef.current = next;
      setDraft(next);
      setNotice({ key: "launcher.saved" });
      return true;
    } catch (error) {
      if (live.current) report(error);
      return false;
    }
  }
  function save(keys?: string[], values?: Values): Promise<boolean> {
    setPending((count) => count + 1);
    let success = false;
    const task = queue.current.then(async () => {
      success = await persist(values, keys);
    });
    queue.current = task.catch(report);
    return task
      .then(() => success)
      .finally(() => {
        if (live.current) setPending((count) => count - 1);
      });
  }
  function autoSave(key: string, numeric: boolean) {
    const old = timers.current.get(key);
    if (old) clearTimeout(old);
    if (!numeric) {
      void save([key]);
      return;
    }
    timers.current.set(
      key,
      setTimeout(() => {
        timers.current.delete(key);
        try {
          collect(draftRef.current, stateRef.current, base.current, [key]);
        } catch {
          return;
        } // Incomplete numeric text stays local until corrected.
        void save([key]);
      }, 180),
    );
  }
  function flushField(key: string) {
    const timer = timers.current.get(key);
    if (timer) {
      clearTimeout(timer);
      timers.current.delete(key);
    }
    return save([key]);
  }
  async function reset() {
    clearTimers();
    const previous = { ...draftRef.current };
    replace(stateRef.current.defaults);
    const captured = { ...versions.current };
    const success = await save(undefined, stateRef.current.defaults);
    if (!success)
      for (const [key, value] of Object.entries(previous))
        revert(key, draftRef.current[key], value, captured[key]);
    return success;
  }
  function prepareGuide() {
    try {
      base.current = collect(draftRef.current, stateRef.current);
      return true;
    } catch (error) {
      report(error);
      return false;
    }
  }
  async function finishGuide(skip: boolean) {
    clearTimers();
    await queue.current;
    const success = skip ? await save([], base.current) : await save();
    if (success && skip) replace(base.current);
    return success;
  }
  let dirty = true;
  try {
    dirty =
      JSON.stringify(collect(draft, state)) !== JSON.stringify(state.values);
  } catch {
    /* Incomplete numeric text remains a draft. */
  }
  return {
    state,
    draft,
    pending: pending > 0,
    notice,
    dirty,
    edit,
    replace,
    save,
    autoSave,
    flushField,
    reset,
    prepareGuide,
    finishGuide,
    clearTimers,
    report,
    setNotice,
    revert,
  };
}
