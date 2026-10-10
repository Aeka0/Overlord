import assert from "node:assert/strict";
import preflightLocales from "../../client/resources/launcher/preflight-locales.json";
import test from "node:test";
import {
  collect,
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
