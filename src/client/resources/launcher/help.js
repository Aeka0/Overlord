// ES5 / MSHTML. Player instructions remain literal text; highlights are safe DOM spans.
var LauncherHelp = (function (catalog) {
    var categories = ['movement', 'world', 'equipment', 'weapons', 'story'];
    // Acknowledgement belongs only to this launcher document, never to saved preferences.
    var category = 'movement', query = '', storyAcknowledged = false;
    var expanded = 'controls', scrollPositions = {}, searchIndex = null;
    var keywordPattern = makeKeywordPattern(catalog.highlights || {});

    function makeKeywordPattern(highlights) {
        var terms = [];
        for (var language in highlights) if (Object.prototype.hasOwnProperty.call(highlights, language)) terms = terms.concat(highlights[language]);
        terms.sort(function (a, b) { return b.length - a.length; });
        var patterns = [];
        for (var i = 0; i < terms.length; ++i) {
            var term = terms[i];
            if (!term) continue;
            var literal = term.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
            // English action names must not match fragments of other words.
            patterns.push((/^[a-z]/i.test(term) ? '\\b' : '') + literal +
                (/[a-z]$/i.test(term) ? '\\b' : ''));
        }
        return patterns.length ? new RegExp(patterns.join('|'), 'gi') : null;
    }

    function translated(entry, language) {
        return entry[language] || entry.en;
    }
    function instruction(article, language) {
        var prose = translated(article.text, language);
        var recipe = article.recipe ? translated(catalog.recipes[article.recipe], language) : null;
        return { title: prose.title, intro: prose.intro || '',
            steps: (recipe ? recipe.steps : []).concat(prose.steps || []), note: prose.note || '' };
    }
    function index() {
        if (searchIndex) return searchIndex;
        searchIndex = {};
        for (var i = 0; i < catalog.articles.length; ++i) {
            var article = catalog.articles[i], words = (article.aliases || []).join(' ');
            // Search every translated catalog, independent of the display language.
            for (var language in article.text) {
                if (!Object.prototype.hasOwnProperty.call(article.text, language)) continue;
                var prose = instruction(article, language);
                words += ' ' + prose.title + ' ' + prose.intro + ' ' + prose.steps.join(' ') + ' ' + prose.note;
            }
            searchIndex[article.id] = words.toLowerCase();
        }
        return searchIndex;
    }
    function results() {
        var tokens = query.toLowerCase().replace(/^\s+|\s+$/g, '').split(/\s+/);
        var searching = tokens[0] !== '', result = [];
        for (var i = 0; i < catalog.articles.length; ++i) {
            var article = catalog.articles[i];
            if (article.category === 'story' && !storyAcknowledged) continue;
            if (!searching && article.category !== category) continue;
            var match = true;
            if (searching) for (var t = 0; t < tokens.length; ++t) {
                if (index()[article.id].indexOf(tokens[t]) === -1) { match = false; break; }
            }
            if (match) result.push(article);
        }
        return result;
    }
    function element(tag, className, text) {
        var node = document.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined) node.textContent = text;
        return node;
    }
    function proseElement(tag, className, text) {
        var node = element(tag, className);
        if (!keywordPattern) { node.textContent = text; return node; }
        keywordPattern.lastIndex = 0;
        var match, start = 0;
        while ((match = keywordPattern.exec(text)) !== null) {
            if (match.index > start) node.appendChild(document.createTextNode(text.slice(start, match.index)));
            node.appendChild(element('span', 'help-keyword', match[0]));
            start = match.index + match[0].length;
        }
        if (start < text.length) node.appendChild(document.createTextNode(text.slice(start)));
        return node;
    }
    function renderArticle(article) {
        var prose = instruction(article, LauncherI18n.language()), open = expanded === article.id;
        var card = element('section', 'help-article'), heading = element('h3');
        var button = element('button', 'help-article-toggle');
        button.type = 'button'; button.id = 'help-article-' + article.id;
        button.setAttribute('aria-expanded', open ? 'true' : 'false');
        button.setAttribute('aria-controls', 'help-body-' + article.id);
        button.appendChild(element('span', '', prose.title));
        var indicator = element('span', 'help-disclosure', open ? '\u2212' : '+');
        indicator.setAttribute('aria-hidden', 'true'); button.appendChild(indicator);
        button.onclick = function () {
            expanded = expanded === article.id ? '' : article.id;
            var viewport = document.getElementById('help-viewport'), position = viewport.scrollTop;
            render();
            document.getElementById(button.id).focus();
            viewport.scrollTop = position;
            updateSettingsScrollbar('help');
        };
        heading.appendChild(button); card.appendChild(heading);
        if (prose.intro) card.appendChild(proseElement('p', 'help-summary', prose.intro));
        var body = element('div', 'help-article-body'); body.id = 'help-body-' + article.id;
        body.style.display = open ? 'block' : 'none';
        var steps = element('ol', 'help-steps');
        for (var i = 0; i < prose.steps.length; ++i) steps.appendChild(proseElement('li', '', prose.steps[i]));
        body.appendChild(steps);
        if (prose.note) body.appendChild(proseElement('p', 'help-note', prose.note));
        if (article.related && article.related.length) {
            var links = element('div', 'help-related');
            links.appendChild(element('span', '', LauncherI18n.text('help.related')));
            for (var r = 0; r < article.related.length; ++r) {
                (function (id) {
                    var target = find(id);
                    if (!target || (target.category === 'story' && !storyAcknowledged)) return;
                    var link = element('a', 'help-link', instruction(target, LauncherI18n.language()).title);
                    link.href = '#help-article-' + id;
                    link.onclick = function () { openArticle(id); return false; };
                    links.appendChild(link);
                }(article.related[r]));
            }
            body.appendChild(links);
        }
        card.appendChild(body); return card;
    }
    function find(id) {
        for (var i = 0; i < catalog.articles.length; ++i) if (catalog.articles[i].id === id) return catalog.articles[i];
        return null;
    }
    function render() {
        var viewport = document.getElementById('help-viewport');
        if (!viewport) return;
        endSettingsScroll('help');
        endSettingsScroll('helpSidebar');
        var articles = results(), searching = query.replace(/\s/g, '') !== '';
        var locked = category === 'story' && !storyAcknowledged;
        var titleKey = locked ? 'help.category.story' : searching ? 'help.searchResults' : 'help.category.' + category;
        document.getElementById('help-title').textContent = LauncherI18n.text(titleKey);
        viewport.setAttribute('aria-labelledby', 'help-title');
        document.getElementById('help-clear').style.display = searching ? 'inline-block' : 'none';
        document.getElementById('help-toolbar').style.display = locked ? 'none' : 'block';
        viewport.className = 'help-viewport' + (locked ? ' help-viewport-locked' : '');
        while (viewport.firstChild) viewport.removeChild(viewport.firstChild);
        if (locked) {
            var gate = element('div', 'help-story-gate');
            var message = element('p', 'help-story-message', LauncherI18n.text('help.storyWarning'));
            message.id = 'help-story-warning';
            gate.appendChild(message);
            var confirm = element('button', 'help-story-confirm', LauncherI18n.text('help.storyUnderstood'));
            confirm.type = 'button'; confirm.id = 'help-story-understood';
            confirm.setAttribute('aria-describedby', message.id);
            confirm.onclick = acknowledgeStory;
            gate.appendChild(confirm); viewport.appendChild(gate);
        } else if (!articles.length) {
            viewport.appendChild(element('p', 'help-empty', LauncherI18n.text('help.noResults')));
        } else {
            var fragment = document.createDocumentFragment();
            for (var i = 0; i < articles.length; ++i) fragment.appendChild(renderArticle(articles[i]));
            viewport.appendChild(fragment);
        }
        for (var c = 0; c < categories.length; ++c) {
            var tab = document.getElementById('help-tab-' + categories[c]);
            var selected = categories[c] === category;
            tab.setAttribute('aria-selected', selected ? 'true' : 'false');
            tab.tabIndex = selected ? 0 : -1;
        }
        updateSettingsScrollbar('help');
        updateSettingsScrollbar('helpSidebar');
    }
    function selectCategory(next) {
        if (categories.indexOf(next) < 0) return;
        var viewport = document.getElementById('help-viewport');
        if (!query) scrollPositions[category] = viewport.scrollTop;
        category = next; query = ''; document.getElementById('help-search').value = '';
        render(); viewport.scrollTop = scrollPositions[category] || 0;
        updateSettingsScrollbar('help');
    }
    function openArticle(id) {
        var article = find(id);
        if (!article || (article.category === 'story' && !storyAcknowledged)) return;
        expanded = id; selectCategory(article.category);
        document.getElementById('help-article-' + id).focus();
    }
    function search(value) {
        query = String(value).slice(0, 256);
        render(); document.getElementById('help-viewport').scrollTop = 0;
        updateSettingsScrollbar('help');
    }
    function clearSearch() {
        document.getElementById('help-search').value = ''; search('');
        document.getElementById('help-search').focus();
    }
    function acknowledgeStory() {
        if (category !== 'story' || storyAcknowledged) return;
        storyAcknowledged = true;
        selectCategory('story');
        document.getElementById('help-viewport').focus();
    }
    function categoryKey(event, current) {
        var at = categories.indexOf(current), next = at;
        if (event.keyCode === 38 || event.keyCode === 37) next = (at + categories.length - 1) % categories.length;
        else if (event.keyCode === 40 || event.keyCode === 39) next = (at + 1) % categories.length;
        else if (event.keyCode === 36) next = 0;
        else if (event.keyCode === 35) next = categories.length - 1;
        else return;
        event.preventDefault(); selectCategory(categories[next]);
        document.getElementById('help-tab-' + categories[next]).focus();
    }
    return { refresh: render, selectCategory: selectCategory, search: search, clearSearch: clearSearch,
        categoryKey: categoryKey };
}(launcherHelpCatalog));
