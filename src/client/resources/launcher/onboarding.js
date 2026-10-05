// ES5 / MSHTML. Valid guide changes persist immediately through the same
// schema, validation, dropdowns and persistence bridge as the full editor.
var LauncherOnboarding = (function () {
    var active = false, step = 0, offered = false, draft = null, automatic = false;
    var steps = ['language', 'device', 'comfort', 'assistance', 'challenge', 'finish'];
    var fields = [[], ['vr_turnMode', 'vr_turnSpeed', 'vr_snapAngle'],
        ['vr_headStabilization', 'vr_handStabilization', 'vr_disableLensFlare', 'vr_disableBlur', 'vr_cameraBob'],
        ['vr_chamberingGuide', 'vr_quickReload', 'vr_aimAssistStrength'],
        ['vr_recoilPenalty', 'vr_discardAmmoPenalty'], []];
    var status = null;
    var needsInitialization = false;

    function node(id) { return document.getElementById('oobe-' + id); }
    function message(key, values) {
        status = key ? { key: key, values: values } : null;
        node('status').textContent = key ? LauncherI18n.text(key, values) : '';
        updateSettingsScrollbar('oobe');
    }
    function refresh() {
        if (!active) return;
        node('progress').textContent = LauncherI18n.text('oobe.progress', { current: step + 1, total: steps.length });
        node('title').textContent = LauncherI18n.text('oobe.' + steps[step]);
        node('intro').textContent = LauncherI18n.text('oobe.' + steps[step] + 'Hint');
        node('next').textContent = LauncherI18n.text(step === steps.length - 1 ? 'oobe.done' : 'oobe.next');
        node('back').disabled = step === 0;
        node('launcher-language').value = LauncherI18n.language();
        syncSettingsDropdowns();
        for (var i = 0; i < steps.length; ++i) node('step-' + steps[i]).style.display = i === step ? 'block' : 'none';
        var smooth = node('vr_turnMode').value === 'smooth';
        node('speed-row').style.display = smooth ? 'block' : 'none';
        node('angle-row').style.display = smooth ? 'none' : 'block';
        if (status) message(status.key, status.values);
        updateSettingsScrollbar('oobe');
    }
    function showStep(index) {
        endSettingsScroll();
        closeSettingsDropdown();
        step = index;
        message('');
        refresh();
        scrollSettingsTo(0, 'oobe');
        node('title').focus();
    }
    function collect(keys) { return collectVRSettings('oobe-', draft, keys); }
    function commit(values) {
        // Avoid repeating a synchronous profile write on blur or navigation.
        if (needsInitialization || JSON.stringify(values) !== JSON.stringify(draft)) {
            persistVRSettings(values);
            draft = values;
            needsInitialization = false;
        }
        message('');
        return true;
    }
    function change(key) {
        if (!active) return false;
        var input = node(key);
        try {
            commit(collect([key]));
            refresh();
            return true;
        } catch (error) {
            // Text can be temporarily incomplete while typing. Keep it for
            // correction; failed toggles and choices return to the applied value.
            if (input.type !== 'number') fillVRSetting(input, key, draft[key]);
            refresh();
            message(LauncherI18n.errorKey(error.message, 'error.saveVR'), error.values);
            return false;
        }
    }
    function report(error) {
        var key = error.setting;
        if (key) {
            for (var i = 0; i < fields.length; ++i) if (fields[i].indexOf(key) !== -1) { showStep(i); break; }
            if (key === 'vr_turnSpeed' || key === 'vr_snapAngle') {
                node('vr_turnMode').value = key === 'vr_turnSpeed' ? 'smooth' : 'snap';
                refresh();
            }
            node(key).focus();
        }
        message(LauncherI18n.errorKey(error.message, 'error.saveVR'), error.values);
    }
    function open(fromStartup) {
        if (active || !vrSettings.loaded) return;
        try { draft = collectVRSettings(); }
        catch (error) { reportVRSettingsError(error); return; }
        automatic = fromStartup === true;
        needsInitialization = automatic;
        settingsDropdownChoices['oobe-launcher-language'] = LauncherI18n.choices();
        settingsDropdownChoices['oobe-vr_turnMode'] = settingsDropdownChoices.vr_turnMode;
        settingsDropdownChoices['oobe-vr_recoilPenalty'] = settingsDropdownChoices.vr_recoilPenalty;
        settingsDropdownChoices['oobe-controller-preset'] = [];
        var choices = settingsDropdownChoices['controller-preset'];
        for (var c = 0; c < choices.length; ++c) {
            var choice = choices[c];
            settingsDropdownChoices['oobe-controller-preset'].push({ value: choice.value, label: choice.label,
                labelKey: choice.value === 'none' ? 'oobe.otherDevice' : choice.labelKey });
        }
        for (var i = 0; i < fields.length; ++i) for (var j = 0; j < fields[i].length; ++j) {
            var key = fields[i][j], input = node(key);
            fillVRSetting(input, key, draft[key]);
        }
        node('controller-preset').value = document.getElementById('controller-preset').value;
        document.getElementById('launcher-navigation').style.display = 'none';
        showMenu('onboarding');
        active = true;
        showStep(0);
    }
    function close() {
        if (!active) return;
        active = false;
        draft = null;
        closeSettingsDropdown();
        document.getElementById('launcher-navigation').style.display = '';
        showMenu(automatic ? 'play' : 'settings');
        if (!automatic) {
            showSettingsCategory('basics');
            document.getElementById('vr-quick-guide').focus();
        } else document.getElementById('launch-game').focus();
    }
    function next() {
        if (!active) return;
        try {
            commit(collect(fields[step]));
            if (step < steps.length - 1) { showStep(step + 1); return; }
            var keys = [];
            for (var i = 0; i < fields.length; ++i) keys = keys.concat(fields[i]);
            commit(collect(keys));
            close();
        } catch (error) { report(error); }
    }
    function skip() {
        if (!active) return;
        try {
            // An untouched first run still needs a usable default VR profile.
            // Keep successful changes and discard incomplete numeric text.
            commit(draft);
            close();
        } catch (error) { report(error); }
    }
    function choose(id, value) {
        if (!active) return;
        if (id === 'oobe-launcher-language') {
            if (saveLauncherLanguage(value)) message('');
            else message('language.saveError');
        } else if (id === 'oobe-controller-preset') {
            try {
                var values = collect([]);
                for (var i = 0; i < vrSettings.controllerPresets.length; ++i) {
                    var preset = vrSettings.controllerPresets[i];
                    if (preset.id !== value) continue;
                    for (var key in preset.values) if (preset.values.hasOwnProperty(key)) values[key] = preset.values[key];
                }
                commit(values);
            } catch (error) {
                node('controller-preset').value = document.getElementById('controller-preset').value;
                report(error);
            }
        } else if (id.indexOf('oobe-vr_') === 0) {
            change(id.substr(5));
        }
        refresh();
    }
    function consider(state) {
        // Missing bridge metadata and failed reads are not evidence of first use.
        if (offered || !state || state.gameAvailable !== true || state.hasVRConfig !== false) return;
        offered = true;
        open(true);
    }
    return { open: open, skip: skip, next: next, change: change, choose: choose, refresh: refresh, consider: consider,
        back: function () { if (active && step > 0) showStep(step - 1); },
        active: function () { return active; } };
}());
