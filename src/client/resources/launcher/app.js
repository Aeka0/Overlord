var vrSettings = { loaded: false, dirty: false, defaults: {}, limits: {}, controllerPresets: [], build: null };
var settingsCategory = 'basics';
var settingsCategories = ['basics', 'gameplay', 'cheats', 'other', 'debug'];
var settingsScrollPositions = { basics: 0, gameplay: 0, cheats: 0, other: 0, debug: 0 };
var settingsScrollDrag = null;
// Settings and onboarding share one scrollbar implementation and CSS surface.
var settingsScrollAreas = {
    vr: { viewport: 'vr-settings-viewport', track: 'vr-scrollbar', thumb: 'vr-scrollbar-thumb' },
    oobe: { viewport: 'oobe-viewport', track: 'oobe-scrollbar', thumb: 'oobe-scrollbar-thumb' }
};
var settingsDropdownChoices = {};
var settingsDropdown = null;


var vrStatusState = null;
var languageStatusState = null;
var gameLanguage = { busy: false, saving: false, available: null, selected: '', status: '', error: false };

function choiceLabel(choice) {
    return choice.labelKey ? LauncherI18n.text(choice.labelKey) : choice.label;
}

function escapeDropdownLabel(value) {
    return value.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function dropdownAvailable(id) {
    if (id === 'game-language') return !gameLanguage.busy && settingsDropdownChoices[id] && settingsDropdownChoices[id].length > 0;
    return settingsDropdownChoices.hasOwnProperty(id) &&
        (id === 'launcher-language' || vrSettings.loaded);
}

function gameLanguageStatus(key, error) {
    gameLanguage.status = key; gameLanguage.error = error;
    var node = document.getElementById('game-language-status');
    node.textContent = LauncherI18n.text(key);
    node.className = 'hint' + (error ? ' error' : '');
    document.getElementById('game-language').disabled = gameLanguage.busy || !settingsDropdownChoices['game-language'].length;
    document.getElementById('game-language-retry').disabled = gameLanguage.busy;
}

function receiveGameLanguages(raw) {
    try {
        var response = JSON.parse(raw);
        if (response.pending) { window.setTimeout(pollGameLanguages, 100); return; }
        gameLanguage.busy = false;
        if (!response.ok) throw new Error(response.errorKey || 'language.gameLoadError');
        settingsDropdownChoices['game-language'] = response.choices;
        gameLanguage.selected = response.language;
        document.getElementById('game-language').value = response.language;
        var present = false;
        for (var i = 0; i < response.choices.length; ++i) if (response.choices[i].value === response.language) present = true;
        gameLanguage.available = present;
        gameLanguageStatus(response.saved ? 'language.gameSaved' : !response.choices.length ? 'language.gameEmpty' :
            !present ? 'language.gameCurrentUnavailable' : '', !present);
    } catch (error) {
        gameLanguage.busy = false;
        gameLanguageStatus(error.message || (gameLanguage.saving ? 'language.gameSaveError' : 'language.gameLoadError'), true);
    }
    gameLanguage.saving = false;
    syncSettingsDropdowns();
}

function pollGameLanguages() {
    try { receiveGameLanguages(window.external.pollGameLanguages()); }
    catch (error) { receiveGameLanguages('{"ok":false,"errorKey":"language.gameLoadError"}'); }
}

function loadGameLanguages() {
    if (gameLanguage.busy) return;
    closeSettingsDropdown();
    gameLanguage.busy = true;
    gameLanguageStatus('language.gameScanning', false);
    try { receiveGameLanguages(window.external.scanGameLanguages()); }
    catch (error) { receiveGameLanguages('{"ok":false,"errorKey":"language.gameLoadError"}'); }
}

function saveGameLanguage(language) {
    if (gameLanguage.busy || language === gameLanguage.selected) return;
    gameLanguage.busy = true; gameLanguage.saving = true;
    gameLanguageStatus('language.gameSaving', false);
    try { receiveGameLanguages(window.external.saveGameLanguage(language)); }
    catch (error) { receiveGameLanguages('{"ok":false,"errorKey":"language.gameSaveError"}'); }
}

function languageStatus(key, error) {
    languageStatusState = { key: key, error: error };
    var status = document.getElementById('language-status');
    status.textContent = LauncherI18n.text(key);
    status.className = 'hint' + (error ? ' error' : '');
}

function applyLauncherLanguage(language) {
    closeSettingsDropdown();
    var selected = LauncherI18n.apply(language);
    document.getElementById('launcher-language').value = selected;
    syncSettingsDropdowns();
    updateBuildNotice();
    if (vrStatusState) vrStatus(vrStatusState.key, vrStatusState.error, vrStatusState.values);
    if (languageStatusState) languageStatus(languageStatusState.key, languageStatusState.error);
    gameLanguageStatus(gameLanguage.status, gameLanguage.error);
    layoutSettings();
    if (typeof LauncherHelp !== 'undefined') LauncherHelp.refresh();
    if (typeof LauncherOnboarding !== 'undefined') LauncherOnboarding.refresh();
}

function loadLauncherLanguage() {
    try {
        var response = JSON.parse(window.external.loadLauncherLanguage());
        if (!response.ok) throw new Error(response.errorKey || 'language.loadError');
        applyLauncherLanguage(response.language);
        document.getElementById('language-retry').style.display = 'none';
        languageStatus(response.warningKey || '', !!response.warningKey);
    } catch (error) {
        applyLauncherLanguage('en');
        languageStatus('language.loadError', true);
        document.getElementById('language-retry').style.display = 'inline-block';
    }
}

function saveLauncherLanguage(language) {
    if (!LauncherI18n.supported(language)) { languageStatus('language.unsupported', true); return false; }
    try {
        var response = JSON.parse(window.external.saveLauncherLanguage(language));
        if (!response.ok || response.language !== language) throw new Error(response.errorKey || 'language.saveError');
        applyLauncherLanguage(language);
        document.getElementById('language-retry').style.display = 'none';
        languageStatus('language.saved', false);
        return true;
    } catch (error) {
        syncSettingsDropdowns();
        languageStatus(error.message === 'language.unsupported' ? error.message : 'language.saveError', true);
        return false;
    }
}

function loadControllerPresets(presets) {
    vrSettings.controllerPresets = presets;
    var choices = [];
    for (var i = 0; i < presets.length; ++i) choices.push({ value: presets[i].id, label: presets[i].label, labelKey: presets[i].id === 'none' ? 'choice.none' : presets[i].id === 'meta_quest_3' ? 'choice.quest3' : null });
    choices.push({ value: 'custom', labelKey: 'choice.custom' });
    settingsDropdownChoices['controller-preset'] = choices;
}

function matchesControllerValue(input, value) {
    return input.value.replace(/\s/g, '') !== '' && isFinite(Number(input.value)) && Number(input.value) === value;
}

function syncControllerPreset() {
    var selected = 'custom';
    for (var i = 0; i < vrSettings.controllerPresets.length; ++i) {
        var preset = vrSettings.controllerPresets[i], matches = true;
        for (var key in preset.values) {
            if (preset.values.hasOwnProperty(key) && !matchesControllerValue(document.getElementById(key), preset.values[key])) {
                matches = false;
                break;
            }
        }
        if (matches) { selected = preset.id; break; }
    }
    document.getElementById('controller-preset').value = selected;
    syncSettingsDropdowns();
}

function applyControllerPreset(id) {
    for (var i = 0; i < vrSettings.controllerPresets.length; ++i) {
        var preset = vrSettings.controllerPresets[i];
        if (preset.id !== id) continue;
        var changed = false;
        for (var key in preset.values) {
            if (!preset.values.hasOwnProperty(key)) continue;
            var input = document.getElementById(key);
            if (!matchesControllerValue(input, preset.values[key])) changed = true;
            input.value = preset.values[key];
            input.removeAttribute('aria-invalid');
        }
        if (changed) markVRDirty();
        return;
    }
    // Custom keeps the current draft for manual editing.
}

function onControllerAlignmentInput() {
    if (!vrSettings.loaded) return;
    syncControllerPreset();
    markVRDirty();
}

function settingsChoiceIndex(id) {
    var choices = settingsDropdownChoices[id];
    var value = document.getElementById(id).value;
    for (var i = 0; i < choices.length; ++i) if (choices[i].value === value) return i;
    return 0;
}

function syncSettingsDropdowns() {
    for (var id in settingsDropdownChoices) {
        if (!settingsDropdownChoices.hasOwnProperty(id)) continue;
        var choice = settingsDropdownChoices[id][settingsChoiceIndex(id)];
        document.getElementById(id + '-label').textContent = id === 'game-language' &&
            (!choice || choice.value !== gameLanguage.selected) ? LauncherI18n.text('language.gameChoose') : choice ? choiceLabel(choice) : '';
    }
}

function closeSettingsDropdown() {
    if (!settingsDropdown) return;
    var button = document.getElementById(settingsDropdown.id);
    button.setAttribute('aria-expanded', 'false');
    button.removeAttribute('aria-activedescendant');
    document.getElementById('settings-dropdown-menu').style.display = 'none';
    settingsDropdown = null;
}

function renderSettingsDropdown() {
    var choices = settingsDropdownChoices[settingsDropdown.id];
    var selected = settingsChoiceIndex(settingsDropdown.id);
    var html = '';
    // Labels come only from local definitions and the compiled preset catalog.
    for (var i = 0; i < choices.length; ++i) {
        html += '<div role="option" id="settings-option-' + i + '" class="dropdown-option' + (i === settingsDropdown.index ? ' active' : '') +
            '" aria-selected="' + (i === selected ? 'true' : 'false') + '" onclick="chooseSettingsOption(' + i + ')">' + escapeDropdownLabel(choiceLabel(choices[i])) + '</div>';
    }
    var menu = document.getElementById('settings-dropdown-menu');
    menu.innerHTML = html;
    document.getElementById(settingsDropdown.id).setAttribute('aria-activedescendant', 'settings-option-' + settingsDropdown.index);
    // Keep the keyboard highlight in view even in an unusually short window.
    var optionTop = settingsDropdown.index * 36;
    if (optionTop < menu.scrollTop) menu.scrollTop = optionTop;
    if (optionTop + 44 > menu.scrollTop + menu.clientHeight) menu.scrollTop = optionTop + 44 - menu.clientHeight;
}

function toggleSettingsDropdown(id) {
    if (!dropdownAvailable(id)) return;
    if (settingsDropdown && settingsDropdown.id === id) { closeSettingsDropdown(); return; }
    closeSettingsDropdown();
    var button = document.getElementById(id);
    var menu = document.getElementById('settings-dropdown-menu');
    var rect = button.getBoundingClientRect();
    var wantedHeight = settingsDropdownChoices[id].length * 36 + 10;
    var below = Math.max(0, window.innerHeight - rect.bottom - 12);
    var above = Math.max(0, rect.top - 12);
    var openAbove = below < wantedHeight && above > below;
    var height = Math.min(wantedHeight, openAbove ? above : below);
    var width = Math.min(rect.right - rect.left, Math.max(0, window.innerWidth - 16));
    menu.style.width = width + 'px';
    menu.style.maxHeight = height + 'px';
    menu.style.left = Math.max(8, Math.min(rect.left, window.innerWidth - width - 8)) + 'px';
    menu.style.top = (openAbove ? rect.top - height - 4 : rect.bottom + 4) + 'px';
    menu.style.display = 'block';
    menu.setAttribute('aria-labelledby', id);
    settingsDropdown = { id: id, index: settingsChoiceIndex(id) };
    button.setAttribute('aria-expanded', 'true');
    renderSettingsDropdown();
}

function chooseSettingsOption(index) {
    if (!settingsDropdown) return;
    var id = settingsDropdown.id;
    var choice = settingsDropdownChoices[id][index];
    if (!choice || !dropdownAvailable(id)) return;
    var button = document.getElementById(id);
    var changed = button.value !== choice.value;
    closeSettingsDropdown();
    if (id === 'launcher-language') { saveLauncherLanguage(choice.value); button.focus(); return; }
    if (id === 'game-language') { saveGameLanguage(choice.value); button.focus(); return; }
    if (id.indexOf('oobe-') === 0) {
        button.value = choice.value;
        LauncherOnboarding.choose(id, choice.value);
        button.focus();
        return;
    }
    button.value = choice.value;
    if (id === 'controller-preset') applyControllerPreset(choice.value);
    syncSettingsDropdowns();
    if (changed && id === 'vr_turnMode') updateTurnControls();
    if (changed && vrSettings.defaults.hasOwnProperty(id)) markVRDirty();
    button.focus();
}

function onSettingsDropdownKey(event, id) {
    var key = event.keyCode;
    if (key === 9 || key === 27) {
        closeSettingsDropdown();
        if (key === 27) event.preventDefault();
        return;
    }
    if (key !== 13 && key !== 32 && key !== 38 && key !== 40 && key !== 36 && key !== 35) return;
    event.preventDefault();
    if (!settingsDropdown || settingsDropdown.id !== id) { toggleSettingsDropdown(id); return; }
    if (key === 13 || key === 32) { chooseSettingsOption(settingsDropdown.index); return; }
    var last = settingsDropdownChoices[id].length - 1;
    settingsDropdown.index = key === 36 ? 0 : key === 35 ? last : Math.max(0, Math.min(last, settingsDropdown.index + (key === 38 ? -1 : 1)));
    renderSettingsDropdown();
}

function usePointerFocus(event) {
    document.documentElement.setAttribute('data-input-mode', 'pointer');
    if (event && settingsDropdown && !document.getElementById(settingsDropdown.id).contains(event.target) &&
        !document.getElementById('settings-dropdown-menu').contains(event.target)) closeSettingsDropdown();
}

function updateBuildNotice() {
    var build = vrSettings.build;
    var key = !build ? 'settings.buildUnknown' : build.optimized ? 'settings.buildOptimized' : 'settings.buildDebug';
    document.getElementById('vr-build-hint').textContent = LauncherI18n.text(key, {
        configuration: build ? build.configuration : ''
    });
}

function onLauncherScroll(event) {
    // Scrolling the option list must not dismiss its own keyboard selection.
    if (event.target !== document.getElementById('settings-dropdown-menu')) closeSettingsDropdown();
}

function useKeyboardFocus(event) {
    if (event.altKey || event.ctrlKey || event.metaKey) return;
    var key = event.keyCode;
    if (key === 9 || key === 13 || key === 32 || (key >= 33 && key <= 40)) {
        document.documentElement.setAttribute('data-input-mode', 'keyboard');
    }
}

function showSettingsCategory(category) {
    if (settingsCategories.indexOf(category) === -1) return;
    closeSettingsDropdown();
    endSettingsScroll();
    var viewport = document.getElementById('vr-settings-viewport');
    settingsScrollPositions[settingsCategory] = viewport.scrollTop;
    settingsCategory = category;
    var categories = settingsCategories;
    for (var i = 0; i < categories.length; ++i) {
        var selected = categories[i] === category;
        var tab = document.getElementById('vr-tab-' + categories[i]);
        tab.setAttribute('aria-selected', selected ? 'true' : 'false');
        tab.tabIndex = selected ? 0 : -1;
        document.getElementById('vr-panel-' + categories[i]).className = 'category-panel' + (selected ? ' active' : '');
    }
    viewport.scrollTop = settingsScrollPositions[category];
    updateSettingsScrollbar();
}

function onSettingsCategoryKey(event) {
    var key = event.keyCode;
    if (key !== 38 && key !== 40 && key !== 36 && key !== 35) return;
    var index = settingsCategories.indexOf(settingsCategory), count = settingsCategories.length;
    var category = settingsCategories[key === 36 ? 0 : key === 35 ? count - 1 : (index + (key === 38 ? -1 : 1) + count) % count];
    showSettingsCategory(category);
    document.getElementById('vr-tab-' + category).focus();
    event.preventDefault();
}

function layoutSettings() {
    closeSettingsDropdown();
    var panels = document.querySelectorAll('.launcher-panel');
    for (var i = 0; i < panels.length; ++i) {
        var panel = panels[i];
        if (panel.offsetHeight) panel.style.height = Math.max(0, window.innerHeight - panel.getBoundingClientRect().top - 18) + 'px';
    }
    for (var area in settingsScrollAreas) {
        if (settingsScrollAreas.hasOwnProperty(area)) updateSettingsScrollbar(area);
    }
}

function settingsScrollMetrics(area) {
    var ids = settingsScrollAreas[area || 'vr'];
    var viewport = document.getElementById(ids.viewport);
    var track = document.getElementById(ids.track);
    var height = track.clientHeight;
    var maximum = Math.max(0, viewport.scrollHeight - viewport.clientHeight);
    var thumb = Math.min(height, Math.max(32, height * viewport.clientHeight / Math.max(1, viewport.scrollHeight)));
    return { maximum: maximum, thumb: thumb, travel: Math.max(0, height - thumb) };
}

function updateSettingsScrollbar(area) {
    var ids = settingsScrollAreas[area || 'vr'];
    var viewport = document.getElementById(ids.viewport);
    var track = document.getElementById(ids.track);
    var thumb = document.getElementById(ids.thumb);
    var metrics = settingsScrollMetrics(area);
    var visible = metrics.maximum > 0 && metrics.travel > 0;
    track.style.visibility = visible ? 'visible' : 'hidden';
    track.tabIndex = visible ? 0 : -1;
    track.setAttribute('aria-hidden', visible ? 'false' : 'true');
    track.setAttribute('aria-valuemax', Math.round(metrics.maximum));
    track.setAttribute('aria-valuenow', Math.round(Math.max(0, Math.min(metrics.maximum, viewport.scrollTop))));
    thumb.style.height = metrics.thumb + 'px';
    thumb.style.top = (metrics.maximum ? viewport.scrollTop / metrics.maximum * metrics.travel : 0) + 'px';
}

function scrollSettingsTo(position, area) {
    var metrics = settingsScrollMetrics(area);
    document.getElementById(settingsScrollAreas[area || 'vr'].viewport).scrollTop = Math.max(0, Math.min(metrics.maximum, position));
    updateSettingsScrollbar(area);
}

function beginSettingsScroll(event, area) {
    if (event.button !== 0) return;
    var ids = settingsScrollAreas[area || 'vr'];
    var metrics = settingsScrollMetrics(area);
    if (!metrics.maximum || !metrics.travel) return;
    endSettingsScroll();
    closeSettingsDropdown();
    var track = document.getElementById(ids.track);
    var thumb = document.getElementById(ids.thumb);
    track.focus();
    if (event.target !== thumb) {
        scrollSettingsTo((event.clientY - track.getBoundingClientRect().top - metrics.thumb / 2) / metrics.travel * metrics.maximum, area);
    }
    settingsScrollDrag = { area: area || 'vr', y: event.clientY, top: document.getElementById(ids.viewport).scrollTop };
    if (track.setCapture) track.setCapture();
    event.preventDefault();
}

function moveSettingsScroll(event) {
    if (!settingsScrollDrag) return;
    if (typeof event.buttons === 'number' && !(event.buttons & 1)) {
        endSettingsScroll();
        return;
    }
    var area = settingsScrollDrag.area;
    var metrics = settingsScrollMetrics(area);
    if (metrics.travel) scrollSettingsTo(settingsScrollDrag.top + (event.clientY - settingsScrollDrag.y) * metrics.maximum / metrics.travel, area);
    event.preventDefault();
}

function endSettingsScroll() {
    if (!settingsScrollDrag) return;
    var track = document.getElementById(settingsScrollAreas[settingsScrollDrag.area].track);
    settingsScrollDrag = null;
    if (track.releaseCapture) track.releaseCapture();
}

function onSettingsScrollKey(event, area) {
    var viewport = document.getElementById(settingsScrollAreas[area || 'vr'].viewport);
    var key = event.keyCode;
    var position = viewport.scrollTop;
    if (key === 38 || key === 40) position += key === 38 ? -40 : 40;
    else if (key === 33 || key === 34) position += (key === 33 ? -1 : 1) * viewport.clientHeight * .9;
    else if (key === 36) position = 0;
    else if (key === 35) position = viewport.scrollHeight;
    else return;
    scrollSettingsTo(position, area);
    event.preventDefault();
}

function vrStatus(message, error, values) {
    vrStatusState = { key: message, error: error, values: values };
    var status = document.getElementById('vr-status');
    status.textContent = LauncherI18n.text(message, values);
    status.className = error ? 'error' : '';
    updateSettingsScrollbar();
}

function updateTurnControls() {
    syncSettingsDropdowns();
    var smooth = document.getElementById('vr_turnMode').value === 'smooth';
    document.getElementById('vr-speed-row').style.display = smooth ? 'flex' : 'none';
    document.getElementById('vr-angle-row').style.display = smooth ? 'none' : 'flex';
    updateSettingsScrollbar();
}

function displayLimit(key) {
    var limit = vrSettings.limits[key], scale = limit.displayScale || 1;
    return { min: limit.min / scale, max: limit.max / scale, step: limit.step / scale, scale: scale };
}

// This conversion is symmetric: the no-recoil checkbox is the inverse of the
// existing native recoil setting, both when loading and when saving a draft.
function settingCheckboxValue(key, value) {
    return key === 'vr_recoil' ? !value : value;
}

function fillVRSetting(input, key, value) {
    if (typeof value === 'boolean') input.checked = settingCheckboxValue(key, value);
    else input.value = value;
    input.removeAttribute('aria-invalid');
    if (vrSettings.limits.hasOwnProperty(key)) {
        var limit = displayLimit(key);
        input.value = value / limit.scale;
        input.min = limit.min;
        input.max = limit.max;
        // Accept precise existing calibration; buttons use the shared increment.
        input.step = Number(limit.step.toFixed(6));
    }
}

function fillVRSettings(values) {
    for (var key in values) {
        if (values.hasOwnProperty(key)) fillVRSetting(document.getElementById(key), key, values[key]);
    }
    syncControllerPreset();
    updateTurnControls();
}

function markVRDirty() {
    if (!vrSettings.loaded) return;
    vrSettings.dirty = true;
    document.getElementById('vr-save').disabled = false;
    vrStatus('status.dirty', false);
}

// Native settings define option order, values and localized labels for both editors.
function loadSettingChoices(catalog) {
    if (!catalog || typeof catalog !== 'object') throw new Error('error.loadVR');
    for (var key in catalog) {
        if (!catalog.hasOwnProperty(key)) continue;
        var options = catalog[key];
        if (key.indexOf('vr_') !== 0 || !Array.isArray(options) || !options.length || options.length > 4)
            throw new Error('error.loadVR');
        for (var i = 0; i < options.length; ++i) {
            if (!options[i] || typeof options[i].value !== 'string' || typeof options[i].labelKey !== 'string')
                throw new Error('error.loadVR');
        }
        settingsDropdownChoices[key] = options;
    }
}

function loadVRSettings() {
    try {
        var response = JSON.parse(window.external.loadVRSettings());
        if (!response.ok) throw new Error(response.error);
        loadSettingChoices(response.choices);
        vrSettings.defaults = response.defaults;
        vrSettings.limits = response.limits;
        vrSettings.build = response.build || null;
        updateBuildNotice();
        loadControllerPresets(response.controllerPresets);
        fillVRSettings(response.values);
        vrSettings.loaded = true;
        vrSettings.dirty = false;
        document.getElementById('vr-fields').disabled = false;
        document.getElementById('vr-save').disabled = true;
        document.getElementById('vr-reset').disabled = false;
        document.getElementById('vr-retry').style.display = 'none';
        vrStatus('status.loaded', false);
        if (typeof LauncherOnboarding !== 'undefined') LauncherOnboarding.consider(response.onboarding);
    } catch (error) {
        vrStatus(LauncherI18n.errorKey(error.message, 'error.loadVR'), true);
        document.getElementById('vr-retry').style.display = 'inline-block';
    }
}

// Both editors share validation; the guide reads its fields over a full draft.
function collectVRSettings(prefix, base, keys) {
    var values = {}, key;
    base = base || vrSettings.defaults;
    prefix = prefix || '';
    for (key in base) if (base.hasOwnProperty(key)) values[key] = base[key];
    keys = keys || Object.keys(base);
    for (var i = 0; i < keys.length; ++i) {
        key = keys[i];
        var input = document.getElementById(prefix + key);
        input.removeAttribute('aria-invalid');
        if (input.type === 'checkbox') values[key] = settingCheckboxValue(key, input.checked);
        else if (vrSettings.limits.hasOwnProperty(key)) {
            var limit = displayLimit(key);
            var text = input.value.replace(/^\s+|\s+$/g, '');
            var number = Number(text);
            if (!text || !isFinite(number) || number < limit.min || number > limit.max) {
                input.setAttribute('aria-invalid', 'true');
                var invalid = new Error('error.range');
                invalid.setting = key;
                invalid.values = { min: limit.min, max: limit.max };
                throw invalid;
            }
            values[key] = number * limit.scale;
        } else values[key] = input.value;
    }
    return values;
}

function reportVRSettingsError(error) {
    if (error.setting) {
        var key = error.setting, input = document.getElementById(key);
        if (key === 'vr_turnSpeed' || key === 'vr_snapAngle') {
            document.getElementById('vr_turnMode').value = key === 'vr_turnSpeed' ? 'smooth' : 'snap';
            updateTurnControls();
        }
        showMenu('settings');
        for (var i = 0; i < settingsCategories.length; ++i) {
            if (document.getElementById('vr-panel-' + settingsCategories[i]).contains(input)) {
                showSettingsCategory(settingsCategories[i]);
                break;
            }
        }
        input.focus();
        updateSettingsScrollbar();
    }
    vrStatus(LauncherI18n.errorKey(error.message, 'error.saveVR'), true, error.values);
}

function persistVRSettings(values) {
    var response = JSON.parse(window.external.saveVRSettings(JSON.stringify(values)));
    if (!response.ok) throw new Error(response.error);
    fillVRSettings(response.values);
    vrSettings.dirty = false;
    document.getElementById('vr-save').disabled = true;
    vrStatus('status.saved', false);
}

function saveVRSettings() {
    if (!vrSettings.loaded) return false;
    try {
        persistVRSettings(collectVRSettings());
        return true;
    } catch (error) {
        reportVRSettingsError(error);
        return false;
    }
}

function resetVRSettings() {
    if (!vrSettings.loaded) return;
    closeSettingsDropdown();
    fillVRSettings(vrSettings.defaults);
    markVRDirty();
}

function launchGame() {
    if (typeof LauncherOnboarding !== 'undefined' && LauncherOnboarding.active()) return;
    if (gameLanguage.busy || gameLanguage.available === false) { showMenu('language'); return; }
    if (vrSettings.dirty && !saveVRSettings()) {
        showMenu('settings');
        return;
    }
    window.external.selectMode(1);
}

function activateElement(name, elements) {
    for (var i = 0; i < elements.length; ++i) {
        var element = elements[i];
        var classList = element.classList;
        var show = false;

        for (var j = 0; j < classList.length; ++j) {
            if (classList[j] == '_' + name) {
                show = true;
                break;
            }
        }

        if (show) {
            classList.add('active');
        } else {
            classList.remove('active');
        }
    }
}

function showMenu(menu) {
    if (typeof LauncherOnboarding !== 'undefined' && LauncherOnboarding.active() && menu !== 'onboarding') return;
    closeSettingsDropdown();
    var menus = document.querySelectorAll("div.menus>div");
    var buttons = document.querySelectorAll("nav a.nav-link");

    activateElement(menu, menus);
    activateElement(menu, buttons);
    endSettingsScroll();
    layoutSettings();
}

window.onload = function () {
    document.addEventListener('mousedown', usePointerFocus, true);
    document.addEventListener('touchstart', usePointerFocus, true);
    document.addEventListener('keydown', useKeyboardFocus, true);
    settingsDropdownChoices['launcher-language'] = LauncherI18n.choices();
    settingsDropdownChoices['game-language'] = [];
    loadLauncherLanguage();
    loadGameLanguages();
    showMenu("play");
    loadVRSettings();
    document.addEventListener('mousemove', moveSettingsScroll);
    document.addEventListener('mouseup', endSettingsScroll);
    window.addEventListener('blur', endSettingsScroll);
    window.addEventListener('blur', closeSettingsDropdown);
    window.addEventListener('resize', layoutSettings);
    document.addEventListener('scroll', onLauncherScroll, true);
};
