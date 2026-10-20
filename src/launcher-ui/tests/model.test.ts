import assert from "node:assert/strict";
import preflightLocales from "../../client/resources/launcher/preflight-locales.json";
import test from "node:test";
import {
  collect,
  displayLimit,
  makeDraft,
  fields,
  categories,
  SettingError,
  visible,
  type SettingsState,
} from "../src/model";
import { catalogs } from "../src/i18n";
import { shellTranslations } from "../src/shellTranslations";
import {
  searchHelp,
  initialHelpState,
  instruction,
  articles,
} from "../src/helpModel";

const state = {
  values: {
    vr_recoil: true,
    vr_desktopStabilizationStrength: 0.3,
    vr_turnMode: "smooth",
  },
  limits: {
    vr_desktopStabilizationStrength: {
      min: 0,
      max: 1,
      step: 0.01,
      displayScale: 0.01,
    },
  },
  choices: {
    vr_turnMode: [
      { value: "smooth", labelKey: "" },
      { value: "snap", labelKey: "" },
    ],
  },
} as unknown as SettingsState;
test("CPU wait timing is an optional localized Debug toggle", () => {
  const field = fields.find((entry) => entry.name === "vr_debugWaitTiming");
  assert.ok(field);
  assert.equal(field.category, "debug");
  assert.equal(field.type, "toggle");
  for (const catalog of Object.values(catalogs)) {
    assert.ok(catalog["settings.debugWaitTiming"]);
    assert.ok(catalog["settings.debugWaitTimingHint"]);
  }
  for (const enabled of [false, true]) {
    const selected = { ...state, values: { vr_debugWaitTiming: enabled } } as SettingsState;
    assert.equal(collect(makeDraft(selected.values, selected.limits), selected).vr_debugWaitTiming, enabled);
  }
});
test("help searches all words and translations without exposing unacknowledged story entries", () => {
  assert.ok(
    searchHelp({ ...initialHelpState, query: "beretta trigger" }).some(
      (article) => article.id === "m9",
    ),
  );
  assert.ok(
    searchHelp({ ...initialHelpState, query: "手柄 Grip" }).some(
      (article) => article.id === "controls",
    ),
  );
  assert.equal(
    searchHelp({ ...initialHelpState, category: "story" }).length,
    0,
  );
  assert.ok(
    searchHelp({ ...initialHelpState, category: "story", storyAccepted: true })
      .length > 0,
  );
  assert.ok(
    instruction(articles.find((article) => article.id === "m9")!, "en").steps
      .length > 1,
  );
});
test("native units and the inverse no-recoil control round-trip", () => {
  const draft = makeDraft(state.values, state.limits);
  assert.equal(draft.vr_recoil, false);
  assert.equal(draft.vr_desktopStabilizationStrength, "30");
  assert.deepEqual(collect(draft, state), state.values);
  assert.equal(collect({ ...draft, vr_recoil: true }, state).vr_recoil, false);
  assert.equal(
    collect({ ...draft, vr_desktopStabilizationStrength: "12.3" }, state)
      .vr_desktopStabilizationStrength,
    0.123,
  );
});
test("invalid and unfinished numeric text never becomes a saved number", () => {
  const draft = makeDraft(state.values, state.limits);
  for (const text of [
    "",
    " ",
    "-",
    "0x20",
    "NaN",
    "Infinity",
    "-1",
    "101",
    "1".repeat(1000),
  ])
    assert.throws(
      () => collect({ ...draft, vr_desktopStabilizationStrength: text }, state),
      SettingError,
    );
  assert.throws(() => collect({ ...draft, vr_turnMode: "unsupported" }, state));
});
test("world size percentages invert native scale without drifting saved precision", () => {
  const scale = 39.370079;
  const fixture = {
    ...state,
    values: { vr_worldScale: scale },
    limits: { vr_worldScale: { min: 0.01, max: 10000, step: 1, displayScale: scale, displayReciprocal: true } },
  } as SettingsState;
  const draft = makeDraft(fixture.values, fixture.limits);
  assert.equal(draft.vr_worldScale, "100");
  assert.equal(displayLimit(fixture.limits.vr_worldScale).step, 1);
  for (const percent of [50, 80, 100, 120, 200]) {
    const saved = collect({ vr_worldScale: String(percent) }, fixture);
    assert.ok(Math.abs(Number(saved.vr_worldScale) - scale * 100 / percent) < 1e-6);
    const loaded = { ...fixture, values: saved };
    assert.deepEqual(collect(makeDraft(saved, fixture.limits), loaded), saved);
  }
  for (const value of [0.01, 32.8083989167, 10000]) {
    const loaded = { ...fixture, values: { vr_worldScale: value } };
    assert.deepEqual(collect(makeDraft(loaded.values, fixture.limits), loaded), loaded.values);
  }
  const bounds = displayLimit(fixture.limits.vr_worldScale);
  for (const percent of [bounds.min, bounds.max]) {
    const saved = Number(collect({ vr_worldScale: String(percent) }, fixture).vr_worldScale);
    assert.ok(saved >= 0.01 && saved <= 10000);
  }
  for (const value of ["", "0", "-1", "NaN", "Infinity", String(bounds.max + 1), "1".repeat(1000)])
    assert.throws(() => collect({ vr_worldScale: value }, fixture), SettingError);
});
test("guide changes leave unrelated incomplete draft text untouched", () => {
  const draft = {
    ...makeDraft(state.values, state.limits),
    vr_desktopStabilizationStrength: "",
  };
  assert.deepEqual(
    collect(draft, state, state.values, ["vr_recoil"]),
    state.values,
  );
});
test("every migrated field has translations in every existing locale", () => {
  assert.equal(new Set(fields.map((field) => field.name)).size, fields.length);
  for (const [language, catalog] of Object.entries(catalogs))
    for (const field of fields)
      for (const key of [
        field.label,
        field.group,
        field.description,
        field.unit,
        field.row,
        field.row && `${field.row}Hint`,
      ].filter(Boolean))
        assert.ok(Object.hasOwn(catalog, key!), `${language}: ${key}`);
  for (const [language, catalog] of Object.entries(catalogs))
    for (const category of categories)
      assert.ok(Object.hasOwn(catalog, `settings.${category}`), `${language}: ${category}`);
  for (const language of Object.keys(catalogs))
    assert.deepEqual(
      Object.keys(shellTranslations[language]),
      Object.keys(shellTranslations.en),
    );
});

test("native comfort effects are independent switches beside background blur", () => {
  const names = ["r_dof_limit", "r_mbLimit", "vr_disableBlur"];
  for (const name of names) {
    const field = fields.find((field) => field.name === name)!;
    assert.equal(field.category, "basics");
    assert.equal(field.group, "settings.comfort");
    assert.equal(field.type, "toggle");
  }
  const fixture = { ...state, values: { r_dof_limit: true, r_mbLimit: false, vr_disableBlur: true } } as SettingsState;
  const draft = makeDraft(fixture.values, fixture.limits);
  assert.deepEqual(collect({ ...draft, r_dof_limit: false, r_mbLimit: true }, fixture), {
    r_dof_limit: false, r_mbLimit: true, vr_disableBlur: true,
  });
});

test("subtitles are the second Other option and save independently of hiding HUD", () => {
  const other = fields.filter((field) => field.category === "other");
  assert.equal(other[1].name, "vr_subtitles");
  assert.equal(other[1].type, "toggle");
  assert.equal([...new Set(other.map((field) => field.group))][1], "settings.subtitles");
  const fixture = { ...state, values: { vr_subtitles: true, vr_hideHud: false } } as SettingsState;
  const draft = makeDraft(fixture.values, fixture.limits);
  assert.deepEqual(collect({ ...draft, vr_subtitles: false }, fixture), { vr_subtitles: false, vr_hideHud: false });
});

test("EXO Punch is a Cheats toggle and saves independently of native intel cheats", () => {
  const field = fields.find((field) => field.name === "vr_exoPunch")!;
  assert.equal(field.category, "cheats");
  assert.equal(field.group, "settings.funFeatures");
  assert.equal(field.type, "toggle");
  const fixture = { ...state, values: { vr_exoPunch: false } } as SettingsState;
  const draft = makeDraft(fixture.values, fixture.limits);
  assert.equal(draft.vr_exoPunch, false);
  assert.deepEqual(collect({ ...draft, vr_exoPunch: true }, fixture), { vr_exoPunch: true });
});

test("HEI rounds add an independent Fun Features toggle", () => {
  const field = fields.find((field) => field.name === "vr_heiRound")!;
  assert.equal(field.category, "cheats");
  assert.equal(field.group, "settings.funFeatures");
  const fixture = { ...state, values: { vr_heiRound: false, vr_exoPunch: true } } as SettingsState;
  const draft = makeDraft(fixture.values, fixture.limits);
  assert.deepEqual(collect({ ...draft, vr_heiRound: true }, fixture), { vr_heiRound: true, vr_exoPunch: true });
});

test("Cheats use the requested shared groups and localized explosive ammunition label", () => {
  const cheats = fields.filter((field) => field.category === "cheats");
  assert.deepEqual([...new Set(cheats.map((field) => field.group))],
    ["settings.playerAttributes", "settings.weaponHandling", "settings.funFeatures"]);
  assert.deepEqual(cheats.filter((field) => field.group === "settings.playerAttributes").map((field) => field.name),
    ["vr_cheatHealth", "vr_cheatNotarget"]);
  assert.deepEqual(cheats.filter((field) => field.group === "settings.weaponHandling").map((field) => field.name),
    ["vr_cheatAmmo", "vr_recoil"]);
  assert.equal(cheats.find((field) => field.name === "vr_recoil")!.inverted, true);
  assert.equal(catalogs["zh-CN"]["settings.heiRound"], "高爆弹药");
  assert.equal(catalogs["zh-CN"]["settings.playerAttributes"], "玩家属性");
  assert.equal(catalogs["zh-CN"]["settings.weaponHandling"], "武器操作");
  for (const catalog of Object.values(catalogs)) {
    assert.ok(catalog["settings.playerAttributes"] && catalog["settings.weaponHandling"]);
    assert.ok(catalog["settings.heiRound"] && catalog["settings.heiRound"] !== "HEI Round");
  }
});

test("turning follows the backend in Basics and sight attraction belongs to Aiming", () => {
  const basicsGroups = [...new Set(fields.filter((field) => field.category === "basics").map((field) => field.group))];
  assert.deepEqual(basicsGroups.slice(0, 2), ["settings.runtimeBackend", "settings.turning"]);
  for (const name of ["vr_turnMode", "vr_turnSpeed", "vr_snapAngle"])
    assert.equal(fields.find((field) => field.name === name)?.category, "basics");
  for (const [name, mode] of [["vr_turnSpeed", "smooth"], ["vr_snapAngle", "snap"]]) {
    const field = fields.find((field) => field.name === name)!;
    assert.equal(visible(field, { vr_turnMode: mode }), true);
    assert.equal(visible(field, { vr_turnMode: mode === "smooth" ? "snap" : "smooth" }), false);
  }
  const attraction = fields.find((field) => field.name === "vr_adsComfort")!;
  assert.equal(attraction.category, "gameplay");
  assert.equal(attraction.group, "settings.aiming");
  assert.equal(attraction.type, "toggle");
});

test("ADS sprint belongs to Aiming and saves independently of sight attraction", () => {
  const field = fields.find((field) => field.name === "vr_adsSprint")!;
  assert.equal(field.category, "gameplay");
  assert.equal(field.group, "settings.aiming");
  assert.equal(field.type, "toggle");
  const fixture = { ...state, values: { vr_adsSprint: true, vr_adsComfort: true } } as SettingsState;
  const draft = makeDraft(fixture.values, fixture.limits);
  assert.deepEqual(collect({ ...draft, vr_adsSprint: false }, fixture), { vr_adsSprint: false, vr_adsComfort: true });
});

test("startup declaration is translated and retains the version and report details", () => {
  const keys = ["notice.title", "notice.maintenance", "notice.expectations", "notice.feedback", "notice.issues", "notice.close"];
  for (const [language, catalog] of Object.entries(catalogs)) {
    for (const key of keys) assert.ok(catalog[key]?.length, `${language}: ${key}`);
    assert.ok(catalog["notice.maintenance"].includes("{version}"), language);
    assert.ok(catalog["notice.feedback"].includes("vr_diagnose") && catalog["notice.feedback"].includes("minidump"), language);
  }
  assert.ok(catalogs["zh-CN"]["notice.maintenance"].endsWith("版本更迭也可能破坏游戏存档和进度。"));
});

test("launch preflight messages exist in all supported languages", () => {
  const expected = Object.keys(preflightLocales.en).sort();
  for (const catalog of Object.values(preflightLocales)) {
    assert.deepEqual(Object.keys(catalog).sort(), expected);
    assert.ok(Object.values(catalog).every((text) => text.length > 0));
  }
});
