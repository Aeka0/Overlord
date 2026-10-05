// ES5 / MSHTML. Catalogs are compiled resources, injected by the native host.
var LauncherI18n = (function (catalog) {
    var current = 'en';
    var own = Object.prototype.hasOwnProperty;
    function supported(language) { return own.call(catalog, language); }
    function text(key, values) {
        var messages = catalog[current];
        var value = own.call(messages, key) ? messages[key] :
            own.call(catalog.en, key) ? catalog.en[key] : key;
        return value.replace(/\{([a-zA-Z0-9_]+)\}/g, function (match, name) {
            return values && own.call(values, name) ? String(values[name]) : match;
        });
    }
    function apply(language) {
        current = supported(language) ? language : 'en';
        document.documentElement.setAttribute('lang', current);
        document.documentElement.setAttribute('data-font-group', /^(zh-CN|zh-TW|ja|ko)$/.test(current) ? 'asian' : 'latin');
        var attributes = ['text', 'aria-label', 'title'];
        for (var a = 0; a < attributes.length; ++a) {
            var attribute = attributes[a];
            var marker = attribute === 'text' ? 'data-i18n' : 'data-i18n-' + attribute;
            var nodes = document.querySelectorAll('[' + marker + ']');
            for (var i = 0; i < nodes.length; ++i) {
                var value = text(nodes[i].getAttribute(marker));
                if (attribute === 'text') nodes[i].textContent = value;
                else nodes[i].setAttribute(attribute, value);
            }
        }
        return current;
    }
    function errorKey(message, fallback) {
        // Existing VR callbacks return English errors. Keep this adapter at
        // the bridge boundary; translated status state always stores a key.
        for (var key in catalog.en) {
            if (own.call(catalog.en, key) && key.indexOf('error.') === 0 && catalog.en[key] === message) return key;
        }
        return message || fallback;
    }
    function choices() {
        var result = [];
        for (var id in catalog) if (own.call(catalog, id)) result.push({ value: id, label: catalog[id]['language.name'] });
        return result;
    }
    return { text: text, apply: apply, supported: supported, choices: choices, errorKey: errorKey,
        language: function () { return current; } };
}(launcherLocaleCatalog));
