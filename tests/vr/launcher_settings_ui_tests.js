// Run with Node.js. Exercises the embedded script without launching the game
// or reading/writing a real player profile.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../../src/client/resources/main.html'), 'utf8');
const resources = path.join(__dirname, '../../src/client/resources/launcher');
const script = ['i18n.js', 'app.js', 'onboarding.js'].map(file => fs.readFileSync(path.join(resources, file), 'utf8')).join('\n');
const localeIds = ['en', 'zh-CN', 'zh-TW', 'ru', 'fr', 'de', 'es', 'ja', 'ko'];
const catalog = Object.fromEntries(localeIds.map(id => [id, JSON.parse(fs.readFileSync(path.join(resources, 'locales', id + '.json'), 'utf8'))]));
const productVersion = JSON.parse(fs.readFileSync(path.join(__dirname, '../../version.json'), 'utf8')).name;
for (const messages of Object.values(catalog)) {
    assert.equal(messages['about.stage'], '{version}');
    messages['about.stage'] = productVersion; // Native bootstrap uses the same product version.
}
const defaults = {
    vr_recordingMode: false, vr_recordingDim: 65, vr_quickReload: true, vr_chamberingGuide: false, vr_physicalLadders: true, vr_discardAmmoPenalty: false, vr_hideHud: false, vr_disableBlur: false,
    vr_disableDogPounce: true, vr_enemyMeleeDamageScale: 0.5,
    vr_cheatHealth: 'off', vr_cheatNotarget: 'off', vr_cheatAmmo: 'off',
    vr_turnMode: 'smooth', vr_disableLensFlare: false, vr_cameraBob: true, vr_recoil: true, vr_recoilPenalty: 'long',
    vr_debugViewProbes: false, vr_debugAutoSnapshots: false, vr_debugPerfCapture: false, vr_debugSceneModels: false,
    vr_debugMenuInput: false, vr_debugWeaponEvents: false, vr_debugVehicle: false,
    vr_reloadWellDebug: false, vr_hkSlapDebug: false, vr_boltDebug: false, vr_coverPushDebug: false,
    vr_turnSpeed: 90, vr_snapAngle: 30, vr_aimAssistStrength: 0, vr_grenadeThrowSpeedScale: 2.5,
    vr_desktopStabilization: false, vr_headStabilization: false, vr_handStabilization: false,
    vr_desktopStabilizationStrength: 50, vr_headStabilizationStrength: 30, vr_handStabilizationStrength: 40,
    vr_handOffsetInward: 0, vr_handOffsetBack: 0, vr_handOffsetUp: 0,
    vr_handAnglePitch: 0, vr_handAngleYaw: 0, vr_handAngleRoll: 0
};
const quest3 = {
    vr_handOffsetInward: -0.02, vr_handOffsetBack: 0.12, vr_handOffsetUp: -0.1,
    vr_handAnglePitch: -20, vr_handAngleYaw: 0, vr_handAngleRoll: 0
};
const alignmentKeys = Object.keys(quest3);
const debugKeys = Object.keys(defaults).filter(key => key.startsWith('vr_debug') || key.endsWith('Debug'));
const choiceCatalog = {
    vr_turnMode: [{value: 'smooth',labelKey: 'choice.smooth'},{value: 'snap',labelKey: 'choice.snap'}],
    vr_recoilPenalty: [{value: 'all',labelKey: 'choice.allWeapons'},{value: 'long',labelKey: 'choice.longWeapons'},{value: 'off',labelKey: 'choice.off'}],
    vr_cheatHealth: [{value: 'off',labelKey: 'choice.disabled'},{value: 'demigod',labelKey: 'choice.demigod'},{value: 'god',labelKey: 'choice.god'}],
    vr_cheatNotarget: [{value: 'off',labelKey: 'choice.disabled'},{value: 'on',labelKey: 'choice.enabled'}],
    vr_cheatAmmo: [{value: 'off',labelKey: 'choice.disabled'},{value: 'reserve',labelKey: 'choice.infiniteReserve'},{value: 'infinite',labelKey: 'choice.infiniteAmmo'}]
};
const controllerPresets = [
    { id: 'none', label: 'None', values: Object.fromEntries(alignmentKeys.map(key => [key, 0])) },
    { id: 'meta_quest_3', label: 'Meta Quest 3', values: quest3 }
];
const limits = Object.fromEntries([
    ['vr_recordingDim', 0, 100, 1],
    ['vr_enemyMeleeDamageScale', 0.1, 2, 0.1],
    ['vr_grenadeThrowSpeedScale', 0.25, 10, 0.25], ['vr_turnSpeed', 15, 360, 5], ['vr_snapAngle', 5, 90, 5], ['vr_aimAssistStrength', 0, 100, 1],
    ...['vr_desktopStabilizationStrength','vr_headStabilizationStrength','vr_handStabilizationStrength'].map(key => [key,0,100,1]),
    ...['vr_handOffsetInward', 'vr_handOffsetBack', 'vr_handOffsetUp'].map(key => [key, -0.5, 0.5, 0.01]),
    ...['vr_handAnglePitch', 'vr_handAngleYaw', 'vr_handAngleRoll'].map(key => [key, -180, 180, 1])
].map(([key, min, max, step]) => [key, { min, max, step }]));

limits.vr_grenadeThrowSpeedScale.displayScale = 2.5;

function setup(initialLanguage = 'en', build = { configuration: 'RelWithDebInfo', optimized: true }, onboarding) {
    const nodes = {};
    for (const match of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)) {
        nodes[match[1]] = {
            type: /type="([^"]+)"/.exec(match[0])?.[1] || '',
            value: '', checked: false, style: {}, attributes: {}, disabled: false,
            scrollTop: 0, clientHeight: 300, scrollHeight: 900, offsetHeight: 500,
            getBoundingClientRect() { return { top: 70, bottom: 110, left: 600, right: 756 }; },
            contains(node) { return node === this; },
            removeAttribute(name) { delete this.attributes[name]; },
            setAttribute(name, value) { this.attributes[name] = value; },
            focus() { this.focused = true; }
        };
        let value = /\bvalue="([^"]*)"/.exec(match[0])?.[1] || '';
        Object.defineProperty(nodes[match[1]], 'value', {
            get: () => value, set: next => { value = String(next); }
        });
    }
    for (const category of ['basics', 'gameplay', 'cheats', 'other', 'debug']) {
        const panel = html.split('id="vr-panel-' + category + '"')[1].split('</section>')[0];
        nodes['vr-panel-' + category].contains = node => Object.keys(nodes).some(id => nodes[id] === node && panel.includes('id="' + id + '"'));
    }
    let stored = { ...defaults }, failLoad = false, failSave = false;
    let writes = 0, launches = 0;
    let savedLanguage = initialLanguage, languageWrites = 0, failLanguageLoad = false, failLanguageSave = false;
    let gameLanguage = 'english', gameWrites = 0, gameFailure = false, pendingGame = false, pendingLanguage = null;
    let gameChoices = [{value:'english', label:'English'}, {value:'simplified_chinese', label:'简体中文'}];
    const timers = [];
    const gameResponse = saved => JSON.stringify({ok:true, language:gameLanguage, choices:gameChoices, saved});
    const localizedNodes = [];
    for (const match of html.matchAll(/<[^>]+\bdata-i18n(?:-[a-z-]+)?="[^"]+"[^>]*>/g)) {
        const attributes = Object.fromEntries([...match[0].matchAll(/(data-i18n(?:-[a-z-]+)?)="([^"]+)"/g)].map(m => [m[1], m[2]]));
        localizedNodes.push({ attributes, textContent: '',
            getAttribute(name) { return this.attributes[name]; },
            setAttribute(name, value) { this.attributes[name] = value; } });
    }
    const documentEvents = {}, rootAttributes = {};
    const context = vm.createContext({
        launcherLocaleCatalog: catalog,
        document: {
            getElementById: id => nodes[id],
            querySelectorAll: selector => localizedNodes.filter(node => Object.hasOwn(node.attributes, selector.slice(1, -1))),
            documentElement: { setAttribute(name, value) { rootAttributes[name] = value; } },
            addEventListener(name, listener) { documentEvents[name] = listener; }
        },
        window: { innerHeight: 640, innerWidth: 960, addEventListener() {}, setTimeout(fn) { timers.push(fn); }, external: {
            scanGameLanguages() { return pendingGame ? '{"pending":true}' : gameResponse(false); },
            pollGameLanguages() {
                if (pendingGame) return '{"pending":true}';
                if (pendingLanguage) { gameLanguage = pendingLanguage; pendingLanguage = null; ++gameWrites; return gameResponse(true); }
                return gameResponse(false);
            },
            saveGameLanguage(language) {
                if (gameFailure) return '{"ok":false,"errorKey":"language.gameUnavailable"}';
                if (pendingGame) { pendingLanguage = language; return '{"pending":true}'; }
                gameLanguage = language; ++gameWrites; return gameResponse(true);
            },
            loadLauncherLanguage() { return JSON.stringify(failLanguageLoad ? { ok: false, errorKey: 'language.loadError' } : { ok: true, language: savedLanguage }); },
            saveLauncherLanguage(language) {
                if (failLanguageSave) return JSON.stringify({ ok: false, errorKey: 'language.saveError' });
                savedLanguage = language; ++languageWrites;
                return JSON.stringify({ ok: true, language });
            },
            loadVRSettings() { return JSON.stringify(failLoad ? { ok: false, error: 'Read failed' } :
                { ok: true, values: stored, defaults, limits, choices:choiceCatalog, controllerPresets, build, onboarding }); },
            saveVRSettings(payload) {
                if (failSave) return JSON.stringify({ ok: false, error: 'Disk full' });
                stored = JSON.parse(payload); ++writes;
                if (onboarding) onboarding.hasVRConfig = true;
                return JSON.stringify({ ok: true, values: stored });
            },
            selectMode(mode) { assert.equal(mode, 1); ++launches; }
        } }
    });
    vm.runInContext(script, context);
    return { context, nodes, documentEvents, rootAttributes, localizedNodes,
        gameLanguage: () => gameLanguage, gameWrites: () => gameWrites,
        gameFailure: value => { gameFailure = value; }, gameChoices: value => { gameChoices = value; },
        pendingGame: value => { pendingGame = value; }, timers,
        language: () => savedLanguage, languageWrites: () => languageWrites,
        failLanguageLoad: value => { failLanguageLoad = value; }, failLanguageSave: value => { failLanguageSave = value; }, stored: () => stored, writes: () => writes, launches: () => launches,
        failLoad: value => { failLoad = value; }, failSave: value => { failSave = value; } };
}

const app = setup(), c = app.context, n = app.nodes;
c.window.onload();

// Game language IO is asynchronous, independent of the VR draft and launcher locale.
{
    const a = setup(), c = a.context, n = a.nodes;
    a.pendingGame(true); c.window.onload();
    assert.equal(c.gameLanguage.busy, true);
    assert.equal(c.dropdownAvailable('game-language'), false);
    assert.equal(n['game-language'].disabled, true);
    c.launchGame(); assert.equal(a.launches(), 0, 'launch waits for the initial inventory');
    c.saveLauncherLanguage('ja');
    assert.equal(a.rootAttributes['data-font-group'], 'asian');
    assert.equal(n['game-language-status'].textContent, catalog.ja['language.gameScanning']);
    a.pendingGame(false); a.timers.shift()();
    assert.equal(c.gameLanguage.busy, false);
    assert.equal(a.gameWrites(), 0, 'scanning does not save game language');
    n.vr_turnSpeed.value = '123'; c.markVRDirty();
    a.gameFailure(true); c.saveGameLanguage('simplified_chinese');
    assert.equal(n['game-language'].value, 'english', 'failed save keeps selection');
    assert.equal(a.gameWrites(), 0);
    assert.equal(c.vrSettings.dirty, true);
    assert.equal(n.vr_turnSpeed.value, '123');
    assert.equal(n['game-language-status'].textContent, catalog.ja['language.gameUnavailable']);
    a.gameFailure(false); a.pendingGame(true); c.saveGameLanguage('simplified_chinese');
    c.launchGame(); assert.equal(a.launches(), 0, 'pending language save must finish before launch');
    assert.equal(a.writes(), 0);
    c.saveLauncherLanguage('ru');
    assert.equal(a.rootAttributes['data-font-group'], 'latin');
    a.pendingGame(false); a.timers.shift()();
    assert.equal(a.gameLanguage(), 'simplified_chinese');
    assert.equal(a.gameWrites(), 1);
    assert.equal(n['game-language-status'].textContent, catalog.ru['language.gameSaved']);
    a.gameChoices([]); c.loadGameLanguages();
    assert.equal(c.dropdownAvailable('game-language'), false);
    assert.equal(n['game-language-label'].textContent, catalog.ru['language.gameChoose']);
    assert.equal(n['game-language-status'].textContent, catalog.ru['language.gameEmpty']);
    assert.equal(a.gameWrites(), 1, 'empty inventory cannot silently choose a replacement');
    c.launchGame(); assert.equal(a.launches(), 0, 'a known missing saved language requires an installed choice');
    for (const locale of localeIds) {
        c.saveLauncherLanguage(locale);
        assert.equal(a.rootAttributes['data-font-group'], ['zh-CN','zh-TW','ja','ko'].includes(locale) ? 'asian' : 'latin');
    }
}
assert.match(n['vr-build-hint'].textContent, /RelWithDebInfo/);
assert.match(n['vr-build-hint'].textContent, /optimized for gameplay/);
for (const optimized of [false, true]) {
    const buildApp = setup('en', { configuration: optimized ? 'RelWithDebInfo' : 'Debug', optimized });
    buildApp.context.window.onload();
    const hintKey = optimized ? 'settings.buildOptimized' : 'settings.buildDebug';
    assert.equal(buildApp.nodes['vr-build-hint'].textContent,
        catalog.en[hintKey].replace('{configuration}', optimized ? 'RelWithDebInfo' : 'Debug'));
    buildApp.context.applyLauncherLanguage('zh-CN');
    assert.equal(buildApp.nodes['vr-build-hint'].textContent,
        catalog['zh-CN'][hintKey].replace('{configuration}', optimized ? 'RelWithDebInfo' : 'Debug'));
    buildApp.nodes.vr_debugViewProbes.checked = true;
    buildApp.context.markVRDirty();
    assert.equal(buildApp.context.saveVRSettings(), true);
    assert.equal(Object.hasOwn(buildApp.stored(), 'build'), false);
    assert.equal(buildApp.stored().vr_debugViewProbes, true);
}
// Keyboard focus hints must disappear on pointer use without blurring the input.
app.documentEvents.keydown({ keyCode: 9 });
assert.equal(app.rootAttributes['data-input-mode'], 'keyboard');
app.documentEvents.mousedown();
assert.equal(app.rootAttributes['data-input-mode'], 'pointer');
app.documentEvents.keydown({ keyCode: 65 });
assert.equal(app.rootAttributes['data-input-mode'], 'pointer');
app.documentEvents.keydown({ keyCode: 9, ctrlKey: true });
assert.equal(app.rootAttributes['data-input-mode'], 'pointer');
app.documentEvents.keydown({ keyCode: 40 });
assert.equal(app.rootAttributes['data-input-mode'], 'keyboard');
app.documentEvents.touchstart();
assert.equal(app.rootAttributes['data-input-mode'], 'pointer');
assert.equal(c.vrSettings.loaded, true);
assert.equal(n['controller-preset'].value, 'none');
assert.equal(n['controller-preset-label'].textContent, 'None');
assert.equal(n['vr-save'].disabled, true);
assert.equal(n['vr-angle-row'].style.display, 'none');
assert.equal(n.vr_aimAssistStrength.value, '0');
assert.equal(n.vr_aimAssistStrength.min, 0);
assert.equal(n.vr_aimAssistStrength.max, 100);
assert.equal(n.vr_aimAssistStrength.step, 1);
for (const invalid of ['', 'NaN', 'Infinity', '-1', '100.001', '1e999']) {
    n.vr_aimAssistStrength.value = invalid; c.markVRDirty();
    const before = app.writes();
    c.launchGame();
    assert.equal(app.writes(), before);
    assert.equal(app.launches(), 0);
    assert.equal(n.vr_aimAssistStrength.attributes['aria-invalid'], 'true');
}
for (const value of [0, 50, 100]) {
    n.vr_aimAssistStrength.value = value; c.markVRDirty();
    assert.equal(c.saveVRSettings(), true);
    c.loadVRSettings();
    assert.equal(n.vr_aimAssistStrength.value, String(value));
}
for (const key of ['vr_handAnglePitch', 'vr_handAngleYaw', 'vr_handAngleRoll']) {
    assert.equal(n[key].value, String(defaults[key]));
    assert.equal(n[key].min, -180);
    assert.equal(n[key].max, 180);
    for (const invalid of ['', 'NaN', 'Infinity', '-180.001', '180.001', '1e999']) {
        c.showSettingsCategory('gameplay');
        n[key].value = invalid; c.markVRDirty();
        const before = app.writes();
        assert.equal(c.saveVRSettings(), false);
        assert.equal(app.writes(), before);
        assert.equal(c.settingsCategory, 'basics');
        assert.equal(n[key].attributes['aria-invalid'], 'true');
        assert.equal(n[key].focused, true);
    }
    for (const value of [-180, -7.5, 0, 180]) {
        n[key].value = value; c.markVRDirty();
        assert.equal(c.saveVRSettings(), true);
        c.loadVRSettings();
        assert.equal(n[key].value, String(value));
    }
    n[key].value = defaults[key]; c.markVRDirty(); c.saveVRSettings();
}
// Throw speed lives in Gameplay; football-only tuning stays private.
assert.equal(html.includes('id="vr_footballThrowSpeedScale"'), false);
assert.equal(n.vr_grenadeThrowSpeedScale.value, '1');
assert.equal(n.vr_grenadeThrowSpeedScale.min, 0.1);
for (const value of [0.1, 1.75, 2.5, 4]) {
    n.vr_grenadeThrowSpeedScale.value = String(value); c.markVRDirty();
    c.showSettingsCategory('basics'); c.showSettingsCategory('gameplay');
    assert.equal(n.vr_grenadeThrowSpeedScale.value, String(value));
    assert.equal(c.saveVRSettings(), true);
    assert.equal(app.stored().vr_grenadeThrowSpeedScale, value * 2.5);
    c.loadVRSettings();
    assert.equal(n.vr_grenadeThrowSpeedScale.value, String(value));
}
for (const value of ['', 'NaN', '0.099', '4.001']) {
    n.vr_grenadeThrowSpeedScale.value = value; c.markVRDirty();
    c.showSettingsCategory('basics'); const before = app.writes();
    assert.equal(c.saveVRSettings(), false);
    assert.equal(app.writes(), before);
    assert.equal(c.settingsCategory, 'gameplay');
}
n.vr_grenadeThrowSpeedScale.value = '1'; c.markVRDirty(); c.saveVRSettings();
// Keep a precise saved calibration when editing a different field.
app.stored().vr_handOffsetBack = 0.123456789;
c.loadVRSettings();
n.vr_turnMode.value = 'snap'; c.updateTurnControls(); c.markVRDirty();
assert.equal(n['vr-speed-row'].style.display, 'none');
assert.equal(n['vr-angle-row'].style.display, 'flex');
assert.equal(c.saveVRSettings(), true);
assert.equal(app.stored().vr_handOffsetBack, 0.123456789);
assert.equal(app.stored().vr_turnMode, 'snap');
n.vr_turnSpeed.value = ''; c.markVRDirty();
assert.equal(c.saveVRSettings(), false);
assert.equal(n['vr-speed-row'].style.display, 'flex');
assert.equal(n.vr_turnMode.value, 'smooth');
n.vr_turnSpeed.value = '90';
for (const invalid of ['', ' ', 'abc', 'Infinity', '0.5001', '-0.5001']) {
    n.vr_handOffsetUp.value = invalid; c.markVRDirty();
    const before = app.writes();
    c.launchGame();
    assert.equal(app.writes(), before);
    assert.equal(app.launches(), 0);
    assert.equal(n.vr_handOffsetUp.attributes['aria-invalid'], 'true');
}
n.vr_handOffsetUp.value = '-0.5'; n.vr_disableLensFlare.checked = true;
n.vr_cameraBob.checked = false;
app.failSave(true);
assert.equal(c.saveVRSettings(), false);
assert.equal(c.vrSettings.dirty, true);
assert.equal(n['vr-save'].disabled, false);
assert.equal(n['vr-status'].textContent, 'Disk full');
app.failSave(false); c.launchGame();
assert.equal(app.launches(), 1);
assert.equal(app.stored().vr_handOffsetUp, -0.5);
assert.equal(app.stored().vr_disableLensFlare, true);
assert.equal(app.stored().vr_cameraBob, false);
const beforeReset = app.writes();
c.resetVRSettings();
assert.equal(app.writes(), beforeReset);
assert.equal(c.vrSettings.dirty, true);
assert.equal(c.saveVRSettings(), true);
assert.deepEqual(app.stored(), defaults);
assert.equal(n['controller-preset'].value, 'none');

// Changing categories preserves the shared draft and each category's scroll position.
c.showSettingsCategory('basics');
n.vr_handOffsetBack.value = '0.25'; c.markVRDirty();
n['vr-settings-viewport'].scrollTop = 120;
c.showSettingsCategory('gameplay');
assert.equal(n['vr-settings-viewport'].scrollTop, 0);
assert.equal(n['vr-tab-gameplay'].attributes['aria-selected'], 'true');
assert.equal(n['vr-tab-basics'].tabIndex, -1);
n.vr_aimAssistStrength.value = '60';
c.showSettingsCategory('basics');
assert.equal(n['vr-settings-viewport'].scrollTop, 120);
assert.equal(n.vr_handOffsetBack.value, '0.25');
assert.equal(c.saveVRSettings(), true);
assert.equal(app.stored().vr_aimAssistStrength, 60);
assert.equal(app.stored().vr_handOffsetBack, 0.25);
// Invalid fields in a hidden category must be revealed before focus/launch.
n.vr_aimAssistStrength.value = '-1'; c.markVRDirty();
c.launchGame();
assert.equal(c.settingsCategory, 'gameplay');
assert.equal(n.vr_aimAssistStrength.focused, true);
n.vr_aimAssistStrength.value = '0'; n.vr_handOffsetUp.value = '2';
assert.equal(c.saveVRSettings(), false);
assert.equal(c.settingsCategory, 'basics');
assert.equal(n.vr_handOffsetUp.focused, true);

const viewport = n['vr-settings-viewport'], track = n['vr-scrollbar'];
const event = values => ({ preventDefault() { this.prevented = true; }, ...values });
c.scrollSettingsTo(0);
c.onSettingsScrollKey(event({ keyCode: 35 }));
assert.equal(viewport.scrollTop, 600);
c.onSettingsScrollKey(event({ keyCode: 34 }));
assert.equal(viewport.scrollTop, 600);
c.onSettingsScrollKey(event({ keyCode: 36 }));
assert.equal(viewport.scrollTop, 0);
c.onSettingsScrollKey(event({ keyCode: 40 }));
assert.equal(viewport.scrollTop, 40);
c.beginSettingsScroll(event({ button: 0, target: n['vr-scrollbar-thumb'], clientY: 10 }));
c.moveSettingsScroll(event({ clientY: 110 }));
assert.equal(viewport.scrollTop, 340);
c.endSettingsScroll();
c.moveSettingsScroll(event({ clientY: 210 }));
assert.equal(viewport.scrollTop, 340);
c.beginSettingsScroll(event({ button: 0, target: track, clientY: 320 }));
assert.equal(viewport.scrollTop, 600);
c.showSettingsCategory('gameplay');
assert.equal(c.settingsScrollDrag, null);
c.onSettingsCategoryKey(event({ keyCode: 36 }));
assert.equal(c.settingsCategory, 'basics');
assert.equal(n['vr-tab-basics'].focused, true);
c.showSettingsCategory('unknown');
assert.equal(c.settingsCategory, 'basics');
// Category keyboard navigation, independent scroll positions, and debug
// selections all use the same draft/save/launch path as gameplay settings.
c.onSettingsCategoryKey(event({ keyCode: 35 }));
assert.equal(c.settingsCategory, 'debug');
assert.equal(n['vr-tab-debug'].focused, true);
assert.equal(n['vr-tab-debug'].attributes['aria-selected'], 'true');
n['vr-settings-viewport'].scrollTop = 75;
for (const key of debugKeys) n[key].checked = true;
c.markVRDirty();
c.onSettingsCategoryKey(event({ keyCode: 40 }));
assert.equal(c.settingsCategory, 'basics');
c.onSettingsCategoryKey(event({ keyCode: 38 }));
assert.equal(c.settingsCategory, 'debug');
assert.equal(n['vr-settings-viewport'].scrollTop, 75);
c.applyLauncherLanguage('zh-CN');
assert.equal(c.settingsCategory, 'debug');
assert.equal(n.vr_debugViewProbes.checked, true);
assert.equal(c.vrSettings.dirty, true);
// Repair the earlier invalid number before testing a launch from Debug.
n.vr_handOffsetUp.value = '0';
const debugLaunches = app.launches();
app.failSave(true); c.launchGame();
assert.equal(app.launches(), debugLaunches);
assert.equal(c.vrSettings.dirty, true);
app.failSave(false); c.launchGame();
assert.equal(app.launches(), debugLaunches + 1);
for (const key of debugKeys) {
    assert.equal(app.stored()[key], true);
}
c.loadVRSettings();
assert.equal(n.vr_debugPerfCapture.checked, true);
assert.equal(n.vr_debugSceneModels.checked, true);
c.resetVRSettings();
for (const key of debugKeys) {
    assert.equal(n[key].checked, false);
}
assert.equal(c.saveVRSettings(), true);
c.applyLauncherLanguage('en');
c.showSettingsCategory('basics');
// Zero overflow and tiny/hidden tracks cannot divide by zero or leave a focusable bar.
viewport.scrollHeight = viewport.clientHeight;
c.scrollSettingsTo(0);
assert.equal(track.style.visibility, 'hidden');
assert.equal(track.tabIndex, -1);
assert.equal(n['vr-scrollbar-thumb'].style.top, '0px');
track.clientHeight = 0; viewport.clientHeight = 0; viewport.scrollHeight = 0;
c.updateSettingsScrollbar();
assert.equal(n['vr-scrollbar-thumb'].style.height, '0px');

const unavailable = setup(); unavailable.failLoad(true); unavailable.context.window.onload();
assert.equal(unavailable.context.vrSettings.loaded, false);
assert.equal(unavailable.nodes['vr-retry'].style.display, 'inline-block');
assert.equal(unavailable.context.saveVRSettings(), false);
unavailable.context.resetVRSettings();
assert.equal(unavailable.context.vrSettings.dirty, false);
assert.equal(unavailable.writes(), 0);
unavailable.failLoad(false); unavailable.context.loadVRSettings();
assert.equal(unavailable.context.vrSettings.loaded, true);
// Custom dropdowns share keyboard/cancel behavior and save recoil selection.
const dropdownApp = setup(), d = dropdownApp.context, dn = dropdownApp.nodes;
d.window.onload();
assert.equal(dn['vr_recoilPenalty'].value, 'long');
assert.equal(dn['vr_recoilPenalty-label'].textContent, 'Long weapons');
d.toggleSettingsDropdown('vr_recoilPenalty');
assert.equal(dn['vr_recoilPenalty'].attributes['aria-expanded'], 'true');
d.onSettingsDropdownKey(event({ keyCode: 36 }), 'vr_recoilPenalty');
d.onSettingsDropdownKey(event({ keyCode: 13 }), 'vr_recoilPenalty');
assert.equal(dn['vr_recoilPenalty'].value, 'all');
assert.equal(d.vrSettings.dirty, true);
assert.equal(dropdownApp.writes(), 0);
d.showSettingsCategory('basics'); d.showSettingsCategory('gameplay');
assert.equal(dn['vr_recoilPenalty'].value, 'all');
d.toggleSettingsDropdown('vr_turnMode');
d.onSettingsDropdownKey(event({ keyCode: 40 }), 'vr_turnMode');
d.onSettingsDropdownKey(event({ keyCode: 27 }), 'vr_turnMode');
assert.equal(dn.vr_turnMode.value, 'smooth');
assert.equal(d.vrSettings.dirty, true);
d.toggleSettingsDropdown('vr_turnMode');
d.onSettingsDropdownKey(event({ keyCode: 40 }), 'vr_turnMode');
d.onSettingsDropdownKey(event({ keyCode: 13 }), 'vr_turnMode');
assert.equal(dn.vr_turnMode.value, 'snap');
assert.equal(dn['vr_turnMode-label'].textContent, 'Snap');
assert.equal(dn['vr-angle-row'].style.display, 'flex');
assert.equal(d.vrSettings.dirty, true);
assert.equal(d.saveVRSettings(), true);
assert.deepEqual(Object.keys(dropdownApp.stored()).sort(), Object.keys(defaults).sort());
assert.equal(dropdownApp.stored().vr_turnMode, 'snap');
assert.equal(dn['vr_recoilPenalty'].value, 'all');
assert.equal(dropdownApp.stored().vr_recoilPenalty, 'all');
d.toggleSettingsDropdown('vr_recoilPenalty'); d.chooseSettingsOption(2);
assert.equal(dn['vr_recoilPenalty'].value, 'off');
assert.equal(d.vrSettings.dirty, true);
dn.vr_recoil.checked = true; d.markVRDirty();
assert.equal(d.saveVRSettings(), true);
assert.equal(dropdownApp.stored().vr_recoilPenalty, 'off');
assert.equal(dropdownApp.stored().vr_recoil, false);
d.loadVRSettings();
assert.equal(dn['vr_recoilPenalty'].value, 'off');
assert.equal(dn.vr_recoil.checked, true);
d.toggleSettingsDropdown('vr_turnMode');
dropdownApp.documentEvents.mousedown({ target: dn['vr-save'] });
assert.equal(d.settingsDropdown, null);
assert.equal(dn.vr_turnMode.attributes['aria-expanded'], 'false');
d.toggleSettingsDropdown('vr_turnMode');
d.onSettingsDropdownKey(event({ keyCode: 9 }), 'vr_turnMode');
assert.equal(d.settingsDropdown, null);
dn['vr_recoilPenalty'].getBoundingClientRect = () => ({ top: 550, bottom: 590, left: 850, right: 1006 });
d.toggleSettingsDropdown('vr_recoilPenalty');
const menuStyle = dn['settings-dropdown-menu'].style;
assert.ok(parseFloat(menuStyle.top) + parseFloat(menuStyle.maxHeight) < 550);
assert.ok(parseFloat(menuStyle.left) + parseFloat(menuStyle.width) <= 952);
d.layoutSettings();
assert.equal(d.settingsDropdown, null);
d.resetVRSettings();
assert.equal(dn['vr_recoilPenalty'].value, 'long');
assert.equal(dn['vr_recoilPenalty-label'].textContent, 'Long weapons');
assert.equal(dn.vr_recoil.checked, false);
const unloadedDropdown = setup();
unloadedDropdown.context.toggleSettingsDropdown('vr_turnMode');
assert.equal(unloadedDropdown.context.settingsDropdown, null);
unloadedDropdown.context.toggleSettingsDropdown('controller-preset');
assert.equal(unloadedDropdown.context.settingsDropdown, null);

// Presets change only alignment fields, derive from saved numbers, and keep the
// existing save/validation path. No extra preset setting is sent to the game.
const presetApp = setup(), p = presetApp.context, pn = presetApp.nodes;
Object.assign(presetApp.stored(), quest3);
p.window.onload();
assert.equal(pn['controller-preset'].value, 'meta_quest_3');
assert.equal(pn['controller-preset-label'].textContent, 'Meta Quest 3');
assert.equal(p.vrSettings.dirty, false);
assert.equal(presetApp.writes(), 0);
const choosePreset = index => { p.toggleSettingsDropdown('controller-preset'); p.chooseSettingsOption(index); };
const alignment = () => Object.fromEntries(alignmentKeys.map(key => [key, Number(pn[key].value)]));
pn.vr_handAnglePitch.value = '-20.0'; p.onControllerAlignmentInput();
assert.equal(pn['controller-preset'].value, 'meta_quest_3');
pn.vr_turnSpeed.value = '135'; p.markVRDirty();
pn.vr_disableLensFlare.checked = true;
pn.vr_aimAssistStrength.value = '60';
pn['vr_recoilPenalty'].value = 'off';
for (const key of alignmentKeys) {
    // Ensure real HTML input events, not only direct helper calls, are wired.
    assert.ok(html.includes('id="' + key + '" aria-describedby='));
    const handler = html.match(new RegExp('id="' + key + '"[^>]+oninput="([^"]+)"'))[1];
    pn[key].value = Number(pn[key].value) + 0.001;
    vm.runInContext(handler, p);
    assert.equal(pn['controller-preset'].value, 'custom');
    assert.equal(pn['controller-preset-label'].textContent, 'Custom');
    choosePreset(1);
    assert.deepEqual(alignment(), quest3);
}
assert.equal(pn.vr_turnSpeed.value, '135');
assert.equal(pn.vr_disableLensFlare.checked, true);
assert.equal(pn.vr_aimAssistStrength.value, '60');
assert.equal(pn['vr_recoilPenalty'].value, 'off');
for (const invalid of ['', ' ', 'NaN', 'Infinity', '1e999', '180.001']) {
    pn.vr_handAngleYaw.value = invalid; p.onControllerAlignmentInput();
    assert.equal(pn['controller-preset'].value, 'custom');
    assert.equal(p.saveVRSettings(), false);
    assert.equal(presetApp.writes(), 0);
    choosePreset(1);
    assert.equal(pn.vr_handAngleYaw.attributes['aria-invalid'], undefined);
}
pn.vr_handOffsetBack.value = '0.123456789'; p.onControllerAlignmentInput();
choosePreset(2); // Custom never overwrites the draft.
assert.equal(pn.vr_handOffsetBack.value, '0.123456789');
presetApp.failSave(true);
assert.equal(p.saveVRSettings(), false);
assert.equal(pn['controller-preset'].value, 'custom');
assert.equal(pn.vr_handOffsetBack.value, '0.123456789');
presetApp.failSave(false);
assert.equal(p.saveVRSettings(), true);
p.loadVRSettings();
assert.equal(pn['controller-preset'].value, 'custom');
assert.equal(pn.vr_handOffsetBack.value, '0.123456789');
assert.deepEqual(Object.keys(presetApp.stored()).sort(), Object.keys(defaults).sort());
choosePreset(0);
assert.deepEqual(alignment(), controllerPresets[0].values);
assert.equal(pn.vr_turnSpeed.value, '135');
assert.equal(pn.vr_disableLensFlare.checked, true);
assert.equal(p.saveVRSettings(), true);
p.loadVRSettings();
assert.equal(pn['controller-preset'].value, 'none');
choosePreset(0);
assert.equal(p.vrSettings.dirty, false); // Selecting the current values is a no-op.
p.toggleSettingsDropdown('controller-preset');
p.onSettingsDropdownKey(event({ keyCode: 40 }), 'controller-preset');
p.onSettingsDropdownKey(event({ keyCode: 27 }), 'controller-preset');
assert.deepEqual(alignment(), controllerPresets[0].values);
assert.equal(p.vrSettings.dirty, false);
p.toggleSettingsDropdown('controller-preset');
p.onSettingsDropdownKey(event({ keyCode: 40 }), 'controller-preset');
p.onSettingsDropdownKey(event({ keyCode: 13 }), 'controller-preset');
assert.deepEqual(alignment(), quest3);
assert.equal(pn['controller-preset'].value, 'meta_quest_3');
assert.equal(p.saveVRSettings(), true);
p.loadVRSettings();
assert.equal(pn['controller-preset'].value, 'meta_quest_3');
const presetWrites = presetApp.writes();
p.resetVRSettings();
assert.equal(presetApp.writes(), presetWrites);
assert.equal(pn['controller-preset-label'].textContent, 'None');
assert.deepEqual(alignment(), controllerPresets[0].values);
assert.equal(p.saveVRSettings(), true);
assert.deepEqual(presetApp.stored(), defaults);
// The former preview now participates in native save/load/reset and launch.
const ammoDrop = setup('zh-CN');
ammoDrop.context.window.onload();
assert.equal(ammoDrop.nodes.vr_discardAmmoPenalty.checked, false);
assert.equal(ammoDrop.context.LauncherI18n.text('settings.ammoDrop'), '丢弃弹药惩罚');
ammoDrop.nodes.vr_discardAmmoPenalty.checked = true;
ammoDrop.context.markVRDirty();
ammoDrop.context.launchGame();
assert.equal(ammoDrop.writes(), 1);
assert.equal(ammoDrop.launches(), 1);
assert.equal(ammoDrop.stored().vr_discardAmmoPenalty, true);
ammoDrop.context.loadVRSettings();
assert.equal(ammoDrop.nodes.vr_discardAmmoPenalty.checked, true);
const ammoDropWrites = ammoDrop.writes();
ammoDrop.context.resetVRSettings();
assert.equal(ammoDrop.nodes.vr_discardAmmoPenalty.checked, false);
assert.equal(ammoDrop.writes(), ammoDropWrites);
assert.equal(ammoDrop.context.saveVRSettings(), true);
assert.equal(ammoDrop.stored().vr_discardAmmoPenalty, false);
console.log('Launcher VR UI logic tests passed');


// Full launcher localization is independent of the VR draft and game language.
{
    assert.deepEqual(Object.keys(catalog.en).sort(), Object.keys(catalog['zh-CN']).sort());
    for (const [locale, messages] of Object.entries(catalog)) {
        assert.deepEqual(Object.keys(messages).sort(), Object.keys(catalog.en).sort(), locale);
        for (const [key, value] of Object.entries(messages)) {
            assert.equal(typeof value, 'string', locale + ':' + key);
            assert(value.length > 0 && !value.includes('\uFFFD'), locale + ':' + key);
            assert.deepEqual((value.match(/\{[a-zA-Z0-9_]+\}/g) || []).sort(),
                (catalog.en[key].match(/\{[a-zA-Z0-9_]+\}/g) || []).sort(), key);
        }
    }
    const app = setup(), c = app.context, n = app.nodes;
    c.window.onload();
    assert.equal(app.rootAttributes.lang, 'en');
    n.vr_turnSpeed.value = '123'; c.markVRDirty();
    c.showSettingsCategory('gameplay');
    n.vr_discardAmmoPenalty.checked = true;
    c.toggleSettingsDropdown('launcher-language');
    app.documentEvents.scroll({ target: n['settings-dropdown-menu'] });
    assert.equal(c.settingsDropdown.id, 'launcher-language');
    c.chooseSettingsOption(1);
    assert.equal(app.language(), 'zh-CN');
    assert.equal(app.languageWrites(), 1);
    assert.equal(app.writes(), 0);
    assert.equal(app.rootAttributes.lang, 'zh-CN');
    assert.equal(n.vr_turnSpeed.value, '123');
    assert.equal(c.vrSettings.dirty, true);
    assert.equal(c.settingsCategory, 'gameplay');
    assert.equal(n.vr_discardAmmoPenalty.checked, true);
    assert.equal(n['vr-status'].textContent, catalog['zh-CN']['status.dirty']);
    assert.equal(n['vr_turnMode-label'].textContent, catalog['zh-CN']['choice.smooth']);
    for (const node of app.localizedNodes) {
        for (const [attribute, key] of Object.entries(node.attributes)) {
            if (!attribute.startsWith('data-i18n')) continue;
            assert(Object.hasOwn(catalog.en, key), 'Unknown translation key: ' + key);
            const actual = attribute === 'data-i18n' ? node.textContent : node.attributes[attribute.slice(10)];
            assert.equal(actual, catalog['zh-CN'][key], key);
        }
    }
    // Validation messages are localized, including their numeric bounds.
    n.vr_turnSpeed.value = 'invalid';
    assert.equal(c.saveVRSettings(), false);
    assert.equal(n['vr-status'].textContent, c.LauncherI18n.text('error.range', { min: 15, max: 360 }));
    n.vr_turnSpeed.value = '123';
    const reloaded = setup(app.language()); reloaded.context.window.onload();
    assert.equal(reloaded.rootAttributes.lang, 'zh-CN');

    // A failed preference write must keep the applied language and VR draft.
    app.failLanguageSave(true);
    assert.equal(c.saveLauncherLanguage('en'), false);
    assert.equal(app.rootAttributes.lang, 'zh-CN');
    assert.equal(n['launcher-language'].value, 'zh-CN');
    assert.equal(n['language-status'].textContent, catalog['zh-CN']['language.saveError']);
    assert.equal(c.saveLauncherLanguage('__proto__'), false);
    assert.equal(app.languageWrites(), 1);
    app.failLanguageSave(false);
    assert.equal(c.saveLauncherLanguage('en'), true);
    assert.equal(app.rootAttributes.lang, 'en');
    assert.equal(n['vr_turnMode-label'].textContent, 'Smooth');
    assert.equal(c.vrSettings.dirty, true);
    c.toggleSettingsDropdown('game-language'); c.chooseSettingsOption(1);
    assert.equal(n['game-language'].value, 'simplified_chinese');
    assert.equal(app.gameLanguage(), 'simplified_chinese');
    assert.equal(app.gameWrites(), 1);
    assert.equal(app.language(), 'en');
    assert.equal(app.languageWrites(), 2);
    assert.equal(app.writes(), 0);
    assert.equal(app.launches(), 0);

    // Launcher language still works when the separate VR configuration fails.
    const unavailable = setup(); unavailable.failLoad(true); unavailable.failLanguageLoad(true);
    unavailable.context.window.onload();
    assert.equal(unavailable.rootAttributes.lang, 'en');
    assert.equal(unavailable.nodes['language-retry'].style.display, 'inline-block');
    unavailable.context.toggleSettingsDropdown('launcher-language');
    unavailable.context.chooseSettingsOption(1);
    assert.equal(unavailable.rootAttributes.lang, 'zh-CN');
    assert.equal(unavailable.context.vrSettings.loaded, false);
    assert.equal(unavailable.writes(), 0);
    assert.equal(setup('unknown').context.LauncherI18n.apply('unknown'), 'en');
    // Missing translations fall back to English without interpreting markup.
    const key = 'common.retry', previous = catalog['zh-CN'][key];
    delete catalog['zh-CN'][key];
    c.LauncherI18n.apply('zh-CN');
    assert.equal(c.LauncherI18n.text(key), 'Retry');
    catalog['zh-CN'][key] = previous;
}
console.log('Launcher localization tests passed');
{
    const app=setup('zh-CN'), c=app.context, n=app.nodes;
    c.window.onload();
    for(const [name,value] of [['vr_desktopStabilization',50],['vr_headStabilization',30],['vr_handStabilization',40]]) {
        assert.equal(n[name].checked,false);
        assert.equal(Number(n[name+'Strength'].value),value);
        n[name].checked=true;n[name+'Strength'].value='67';c.markVRDirty();
        assert.equal(c.saveVRSettings(),true);
        assert.equal(app.stored()[name],true);assert.equal(app.stored()[name+'Strength'],67);
        n[name+'Strength'].value='101';c.markVRDirty();assert.equal(c.saveVRSettings(),false);
        assert.equal(app.stored()[name+'Strength'],67);
        n[name+'Strength'].value='0';c.markVRDirty();assert.equal(c.saveVRSettings(),true);
    }
    c.applyLauncherLanguage('en');c.applyLauncherLanguage('zh-CN');
    assert.equal(n.vr_headStabilization.checked,true);
    assert.equal(n.vr_headStabilizationStrength.value,'0');
    c.resetVRSettings();
    assert.equal(n.vr_headStabilization.checked,false);assert.equal(n.vr_headStabilizationStrength.value,'30');
}
console.log('Launcher stabilization controls tests passed');
{
    const app=setup('zh-CN'), c=app.context, n=app.nodes;
    c.window.onload();
    assert.equal(n.vr_recordingMode.checked,false);
    assert.equal(c.LauncherI18n.text('settings.recordingMode'),'直播画面预览模式');
    assert.equal(Number(n.vr_recordingDim.value),65);
    assert.equal(c.LauncherI18n.text('settings.recordingDim'),'直播预览框外压暗程度（%）');
    n.vr_recordingDim.value='100';
    n.vr_recordingMode.checked=true;c.markVRDirty();
    c.showSettingsCategory('gameplay');c.applyLauncherLanguage('en');
    assert.equal(n.vr_recordingMode.checked,true);
    assert.equal(Number(n.vr_recordingDim.value),100);
    assert.equal(c.saveVRSettings(),true);
    c.loadVRSettings();assert.equal(n.vr_recordingMode.checked,true);
    assert.equal(Number(n.vr_recordingDim.value),100);
    for(const value of ['-1','101','invalid']) {
        n.vr_recordingDim.value=value;c.markVRDirty();
        assert.equal(c.saveVRSettings(),false);
        assert.equal(app.stored().vr_recordingDim,100);
    }
    n.vr_recordingDim.value='0';c.markVRDirty();
    n.vr_recordingMode.checked=false;c.markVRDirty();app.failSave(true);
    c.launchGame();assert.equal(app.launches(),0);assert.equal(app.stored().vr_recordingMode,true);
    app.failSave(false);c.launchGame();
    assert.equal(app.launches(),1);assert.equal(app.stored().vr_recordingMode,false);
    assert.equal(app.stored().vr_recordingDim,0);
    n.vr_recordingMode.checked=true;c.markVRDirty();c.resetVRSettings();
    assert.equal(n.vr_recordingMode.checked,false);
    assert.equal(Number(n.vr_recordingDim.value),65);
}
console.log('Launcher live stream preview controls tests passed');
{
    const app=setup('zh-CN'), c=app.context, n=app.nodes;
    c.window.onload();
    assert.equal(n.vr_quickReload.checked,true);
    assert.equal(c.LauncherI18n.text('settings.weaponInteraction'),'武器互动');
    n.vr_quickReload.checked=false;c.markVRDirty();
    c.showSettingsCategory('gameplay');c.applyLauncherLanguage('en');
    assert.equal(n.vr_quickReload.checked,false);
    assert.equal(c.saveVRSettings(),true);
    c.loadVRSettings();assert.equal(n.vr_quickReload.checked,false);
    c.resetVRSettings();assert.equal(n.vr_quickReload.checked,true);
    assert.equal(c.saveVRSettings(),true);
    assert.equal(app.stored().vr_quickReload,true);
}
console.log('Launcher quick reload controls tests passed');

{
    const app=setup('zh-CN'), c=app.context, n=app.nodes;
    c.window.onload();
    assert.equal(n.vr_physicalLadders.checked,true);
    assert.equal(c.LauncherI18n.text('settings.physicalLadders'),'物理梯子攀爬');
    n.vr_physicalLadders.checked=false;c.markVRDirty();
    c.applyLauncherLanguage('en');assert.equal(c.saveVRSettings(),true);
    c.loadVRSettings();assert.equal(n.vr_physicalLadders.checked,false);
    c.resetVRSettings();assert.equal(n.vr_physicalLadders.checked,true);
}

{
    const app=setup('zh-CN'), c=app.context, n=app.nodes;
    c.window.onload();
    assert.equal(n.vr_chamberingGuide.checked,false);
    assert.equal(c.LauncherI18n.text('settings.chamberingGuide'),'上膛高亮指引');
    n.vr_chamberingGuide.checked=true;c.markVRDirty();
    c.applyLauncherLanguage('en');assert.equal(n.vr_chamberingGuide.checked,true);
    assert.equal(c.saveVRSettings(),true);c.loadVRSettings();
    assert.equal(n.vr_chamberingGuide.checked,true);
    c.resetVRSettings();assert.equal(n.vr_chamberingGuide.checked,false);
    assert.equal(c.saveVRSettings(),true);assert.equal(app.stored().vr_chamberingGuide,false);
}

{
    const app = setup('zh-CN'), c = app.context, n = app.nodes;
    c.window.onload();
    assert.equal(n.vr_enemyMeleeDamageScale.value, '0.5');
    assert.equal(n.vr_disableDogPounce.checked, true);
    assert.equal(c.LauncherI18n.text('settings.disableDogPounce'), '禁止军犬扑倒');
    n.vr_enemyMeleeDamageScale.value = '1.2';
    n.vr_disableDogPounce.checked = false;
    c.markVRDirty(); assert.equal(c.saveVRSettings(), true);
    c.loadVRSettings();
    assert.equal(n.vr_enemyMeleeDamageScale.value, '1.2');
    assert.equal(n.vr_disableDogPounce.checked, false);
    for (const value of ['', 'NaN', '0', '2.1']) {
        n.vr_enemyMeleeDamageScale.value = value;
        c.markVRDirty(); const writes = app.writes();
        assert.equal(c.saveVRSettings(), false);
        assert.equal(app.writes(), writes);
    }
    c.resetVRSettings();
    assert.equal(n.vr_enemyMeleeDamageScale.value, '0.5');
    assert.equal(n.vr_disableDogPounce.checked, true);
    assert.equal(c.saveVRSettings(), true);
}
console.log('Launcher enemy combat controls tests passed');

// First-use admission is explicit and separate from the availability of defaults.
for (const state of [undefined, {}, { gameAvailable: false, hasVRConfig: false },
    { gameAvailable: true, hasVRConfig: true }, { gameAvailable: true, hasVRConfig: false }]) {
    const app = setup('en', undefined, state);
    app.context.window.onload();
    assert.equal(app.context.LauncherOnboarding.active(), state?.gameAvailable === true && state?.hasVRConfig === false);
    assert.equal(app.writes(), 0);
}
{
    const app = setup('en', undefined, { gameAvailable: true, hasVRConfig: false });
    app.failLoad(true); app.context.window.onload();
    assert.equal(app.context.LauncherOnboarding.active(), false);
    app.failLoad(false); app.context.loadVRSettings();
    const guide = app.context.LauncherOnboarding, n = app.nodes, c = app.context;
    assert.equal(guide.active(), true);
    app.failSave(true); guide.skip();
    assert.equal(guide.active(), true); assert.equal(app.writes(), 0);
    app.failSave(false); guide.skip();
    assert.equal(guide.active(), false); assert.equal(app.writes(), 1, 'Skipping initializes the default VR profile');
    c.loadVRSettings(); assert.equal(guide.active(), false);
    guide.open(false);
    app.failLanguageSave(true);
    c.toggleSettingsDropdown('oobe-launcher-language'); c.chooseSettingsOption(1);
    assert.equal(app.language(), 'en'); assert.match(n['oobe-status'].textContent, /Could not save/);
    app.failLanguageSave(false);
    c.toggleSettingsDropdown('oobe-launcher-language'); c.chooseSettingsOption(1);
    assert.equal(app.language(), 'zh-CN');
    assert.equal(n['oobe-title'].textContent, catalog['zh-CN']['oobe.language']);
    guide.next();
    c.toggleSettingsDropdown('oobe-controller-preset'); c.chooseSettingsOption(1);
    assert.equal(n.vr_handOffsetBack.value, '0.12');
    for (const [key, expected] of Object.entries(quest3)) assert.equal(app.stored()[key], expected, key);
    const beforeInvalid = app.writes();
    n['oobe-vr_turnSpeed'].value = ''; assert.equal(guide.change('vr_turnSpeed'), false);
    assert.equal(app.writes(), beforeInvalid); assert.equal(app.stored().vr_turnSpeed, 90);
    guide.next(); assert.equal(n['oobe-step-device'].style.display, 'block');
    assert.equal(n['oobe-vr_turnSpeed'].attributes['aria-invalid'], 'true');
    n['oobe-vr_turnSpeed'].value = '125'; assert.equal(guide.change('vr_turnSpeed'), true);
    assert.equal(app.stored().vr_turnSpeed, 125); assert.equal(n.vr_turnSpeed.value, '125');
    const afterSpeed = app.writes(); guide.change('vr_turnSpeed');
    assert.equal(app.writes(), afterSpeed, 'Blur does not repeat a successful input save');
    app.failSave(true);
    c.toggleSettingsDropdown('oobe-controller-preset'); c.chooseSettingsOption(0);
    assert.equal(n['oobe-controller-preset'].value, 'meta_quest_3');
    assert.equal(app.stored().vr_handOffsetBack, 0.12);
    c.toggleSettingsDropdown('oobe-vr_turnMode'); c.chooseSettingsOption(1);
    assert.equal(n['oobe-vr_turnMode'].value, 'smooth'); assert.equal(app.stored().vr_turnMode, 'smooth');
    app.failSave(false); guide.next();
    n['oobe-vr_disableBlur'].checked = true; guide.change('vr_disableBlur');
    assert.equal(app.stored().vr_disableBlur, true); assert.equal(n.vr_disableBlur.checked, true);
    app.failSave(true); n['oobe-vr_disableBlur'].checked = false;
    assert.equal(guide.change('vr_disableBlur'), false);
    assert.equal(n['oobe-vr_disableBlur'].checked, true); assert.equal(app.stored().vr_disableBlur, true);
    app.failSave(false); guide.next();
    app.failSave(true); n['oobe-vr_aimAssistStrength'].value = '45';
    assert.equal(guide.change('vr_aimAssistStrength'), false);
    assert.equal(n['oobe-vr_aimAssistStrength'].value, '45'); assert.equal(app.stored().vr_aimAssistStrength, 0);
    assert.match(n['oobe-status'].textContent, /Disk full/);
    guide.next(); assert.equal(n['oobe-step-assistance'].style.display, 'block');
    c.launchGame(); assert.equal(app.launches(), 0);
    app.failSave(false); assert.equal(guide.change('vr_aimAssistStrength'), true);
    assert.equal(app.stored().vr_aimAssistStrength, 45);
    n['oobe-vr_chamberingGuide'].checked = true; guide.change('vr_chamberingGuide');
    assert.equal(app.stored().vr_chamberingGuide, true);
    guide.next(); n['oobe-vr_discardAmmoPenalty'].checked = true; guide.change('vr_discardAmmoPenalty');
    assert.equal(app.stored().vr_discardAmmoPenalty, true);
    guide.next(); assert.equal(n['oobe-next'].textContent, catalog['zh-CN']['oobe.done']);
    const beforeFinish = app.writes(); guide.next();
    assert.equal(guide.active(), false); assert.equal(app.writes(), beforeFinish);
    // Skip keeps applied changes and drops unfinished numeric text.
    guide.open(false); n['oobe-vr_aimAssistStrength'].value = '90'; guide.change('vr_aimAssistStrength');
    n['oobe-vr_aimAssistStrength'].value = ''; guide.change('vr_aimAssistStrength'); guide.skip();
    assert.equal(n.vr_aimAssistStrength.value, '90'); assert.equal(app.stored().vr_aimAssistStrength, 90);
    n.vr_recordingMode.checked = true; c.markVRDirty();
    guide.open(false); guide.skip(); assert.equal(c.vrSettings.dirty, true);
    guide.open(false); n['oobe-vr_aimAssistStrength'].value = '80'; guide.change('vr_aimAssistStrength');
    assert.equal(app.stored().vr_recordingMode, true, 'Applying guide edits preserves the full current settings');
    assert.equal(c.vrSettings.dirty, false); guide.skip();
}
console.log('Launcher first-use and quick-guide tests passed');

// Both scrollbars use the same interactions but retain separate scroll owners.
{
    const app = setup('en', undefined, { gameAvailable: true, hasVRConfig: false });
    const c = app.context, n = app.nodes;
    c.window.onload();
    const viewport = n['oobe-viewport'], track = n['oobe-scrollbar'];
    n['vr-settings-viewport'].scrollTop = 121;
    c.onSettingsScrollKey(event({ keyCode: 35 }), 'oobe');
    assert.equal(viewport.scrollTop, 600);
    assert.equal(n['vr-settings-viewport'].scrollTop, 121);
    c.beginSettingsScroll(event({ button: 0, target: n['oobe-scrollbar-thumb'], clientY: 110 }), 'oobe');
    c.moveSettingsScroll(event({ clientY: 10 }));
    assert.equal(viewport.scrollTop, 300);
    assert.equal(n['vr-settings-viewport'].scrollTop, 121);
    c.LauncherOnboarding.next();
    assert.equal(c.settingsScrollDrag, null, 'Changing guide steps releases the scrollbar');
    assert.equal(viewport.scrollTop, 0);
    c.moveSettingsScroll(event({ clientY: 210 }));
    assert.equal(viewport.scrollTop, 0);
    c.onSettingsScrollKey(event({ keyCode: 34 }), 'oobe');
    assert.equal(viewport.scrollTop, 270);
    viewport.scrollHeight = viewport.clientHeight;
    c.scrollSettingsTo(0, 'oobe');
    assert.equal(track.style.visibility, 'hidden');
    assert.equal(track.tabIndex, -1);
    track.clientHeight = 0;
    c.updateSettingsScrollbar('oobe');
    assert.equal(n['oobe-scrollbar-thumb'].style.height, '0px');
}
console.log('Launcher shared scrollbar tests passed');

// New presentation toggles share save/reset across category and language changes.
{
    const app = setup(), c = app.context, n = app.nodes;
    c.window.onload();
    c.showSettingsCategory('gameplay');
    c.onSettingsCategoryKey({keyCode:40,preventDefault(){}});
    assert.equal(c.settingsCategory, 'cheats');
    c.onSettingsCategoryKey({keyCode:40,preventDefault(){}});
    assert.equal(c.settingsCategory, 'other');
    n.vr_hideHud.checked = true; n.vr_disableBlur.checked = true; c.markVRDirty();
    c.applyLauncherLanguage('zh-CN'); c.showSettingsCategory('debug');
    assert.equal(c.saveVRSettings(), true); c.loadVRSettings();
    assert.equal(n.vr_hideHud.checked, true); assert.equal(n.vr_disableBlur.checked, true);
    c.resetVRSettings(); assert.equal(c.saveVRSettings(), true);
    assert.equal(app.stored().vr_hideHud, false); assert.equal(app.stored().vr_disableBlur, false);
    assert.equal(n.vr_grenadeThrowSpeedScale.value, '1');
    assert.equal(app.stored().vr_grenadeThrowSpeedScale, 2.5);
}

// Cheat choices keep their draft across navigation/localization, persist through
// the same bridge and reset off. A failed save still prevents launching.
{
    const app = setup(), c = app.context, n = app.nodes;
    c.window.onload(); c.showSettingsCategory('cheats');
    for (const [key, choices] of Object.entries({
        vr_cheatHealth: ['off', 'demigod', 'god'],
        vr_cheatNotarget: ['off', 'on'], vr_cheatAmmo: ['off', 'reserve', 'infinite']
    })) {
        assert.equal(n[key].value, 'off');
        for (let i = 0; i < choices.length; ++i) {
            c.toggleSettingsDropdown(key); c.chooseSettingsOption(i);
            assert.equal(n[key].value, choices[i]);
            if (i) assert.equal(c.vrSettings.dirty, true);
            assert.equal(c.saveVRSettings(), true);
            c.loadVRSettings(); assert.equal(n[key].value, choices[i]);
        }
    }
    c.showSettingsCategory('basics'); c.applyLauncherLanguage('zh-CN'); c.showSettingsCategory('cheats');
    assert.equal(n['vr_cheatHealth-label'].textContent, '无敌');
    assert.equal(n['vr_cheatNotarget-label'].textContent, '启用');
    assert.equal(n['vr_cheatAmmo-label'].textContent, '无限弹药');
    c.resetVRSettings();
    for (const key of ['vr_cheatHealth', 'vr_cheatNotarget', 'vr_cheatAmmo']) assert.equal(n[key].value, 'off');
    app.failSave(true); c.launchGame(); assert.equal(app.launches(), 0);
    app.failSave(false); c.launchGame(); assert.equal(app.launches(), 1);
    for (const key of ['vr_cheatHealth', 'vr_cheatNotarget', 'vr_cheatAmmo']) assert.equal(app.stored()[key], 'off');
}
console.log('Launcher cheat controls tests passed');

// No recoil is an inverted view of the existing setting. The guide only edits
// single-hand penalty and keeps the hidden recoil preference intact.
{
    const app = setup('zh-CN'), c = app.context, n = app.nodes;
    c.window.onload();
    assert.equal(n['oobe-vr_recoil'], undefined);
    assert.equal(n['vr-panel-gameplay'].contains(n.vr_recoil), false);
    assert.equal(n['vr-panel-cheats'].contains(n.vr_recoil), true);
    assert.equal(n['vr-panel-gameplay'].contains(n.vr_recoilPenalty), true);
    assert.equal(c.LauncherI18n.text('settings.noRecoil'), '无后坐力');
    assert.equal(n.vr_recoil.checked, false);
    n.vr_recoil.checked = true; c.markVRDirty(); c.saveVRSettings(); c.loadVRSettings();
    assert.equal(app.stored().vr_recoil, false); assert.equal(n.vr_recoil.checked, true);
    const guide = c.LauncherOnboarding;
    guide.open(false);
    for (let i = 0; i < 4; ++i) guide.next();
    c.toggleSettingsDropdown('oobe-vr_recoilPenalty'); c.chooseSettingsOption(0);
    assert.equal(app.stored().vr_recoilPenalty, 'all'); assert.equal(n.vr_recoilPenalty.value, 'all');
    guide.next(); guide.next();
    assert.equal(guide.active(), false);
    assert.equal(app.stored().vr_recoil, false); assert.equal(n.vr_recoil.checked, true);
    n.vr_recoil.checked = false; c.markVRDirty(); c.saveVRSettings();
    assert.equal(app.stored().vr_recoil, true); assert.equal(app.stored().vr_recoilPenalty, 'all');
    c.resetVRSettings(); assert.equal(n.vr_recoil.checked, false);
}
console.log('Launcher inverted no-recoil and unchanged guide penalty tests passed');
