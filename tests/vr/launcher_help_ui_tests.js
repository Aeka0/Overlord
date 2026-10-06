// Focused checks for offline help: compiled data, search, spoiler privacy and reader state.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '../..');
const resources = path.join(root, 'src/client/resources/launcher');
const catalog = JSON.parse(fs.readFileSync(path.join(resources, 'help-content.json'), 'utf8'));
const localeIds = ['en', 'zh-CN', 'zh-TW', 'ru', 'fr', 'de', 'es', 'ja', 'ko'];
const locales = Object.fromEntries(localeIds.map(id =>
    [id, JSON.parse(fs.readFileSync(path.join(resources, 'locales', id + '.json'), 'utf8'))]));
const ids = new Set(catalog.articles.map(article => article.id));
assert.equal(ids.size, catalog.articles.length);
for (const article of catalog.articles) {
    assert.match(article.id, /^[a-z0-9-]+$/);
    if (article.recipe) assert.ok(catalog.recipes[article.recipe]);
    for (const id of article.related || []) assert.ok(ids.has(id), article.id + ' -> ' + id);
    for (const source of article.sources) assert.ok(fs.existsSync(path.join(root, source)), source);
    for (const language of localeIds) {
        const text = article.text[language];
        assert.ok(text.title && text.intro);
        assert.ok(text.steps.length || article.recipe);
        for (const line of [text.title, text.intro, ...text.steps, text.note || '']) {
            assert.equal(line.includes('\uFFFD'), false, article.id);
            assert.equal(/<\/?(?:script|a|p|li)\b/i.test(line), false, article.id);
        }
    }
}
for (const language of localeIds) {
    for (const recipe of Object.values(catalog.recipes)) {
        assert.equal(recipe[language].steps.length, recipe.en.steps.length);
        assert.ok(recipe[language].steps.every(step => step && !step.includes('\uFFFD')));
    }
    for (const key of Object.keys(locales.en).filter(key => key.startsWith('help.') || key === 'nav.help')) {
        assert.equal(typeof locales[language][key], 'string', key);
    }
}
const html = fs.readFileSync(path.join(root, 'src/client/resources/main.html'), 'utf8');
const resourceScript = fs.readFileSync(path.join(root, 'src/client/resource.rc'), 'utf8');
for (const filename of ['help.js', 'help.css', 'help-content.json']) assert.ok(resourceScript.includes('launcher/' + filename));
assert.ok(html.includes('<!-- LAUNCHER_HELP_STYLES -->'));

class Node {
    constructor(tag = 'div') { this.tag = tag; this.children = []; this.attributes = {}; this.style = {}; this.value = ''; this.scrollTop = 0; this._text = ''; }
    get firstChild() { return this.children[0]; }
    get textContent() { return this._text + this.children.map(child => child.textContent).join(''); }
    set textContent(value) { this._text = String(value); this.children = []; }
    appendChild(node) { if (node.tag === 'fragment') this.children.push(...node.children); else this.children.push(node); return node; }
    removeChild(node) { this.children.splice(this.children.indexOf(node), 1); }
    setAttribute(key, value) { this.attributes[key] = String(value); }
    focus() { document.activeElement = this; }
}
const nodes = {};
for (const id of ['help-page', 'help-toolbar', 'help-viewport', 'help-title', 'help-clear', 'help-search',
    ...['movement', 'world', 'equipment', 'weapons', 'story'].map(id => 'help-tab-' + id)]) {
    assert.ok(html.includes('id="' + id + '"')); nodes[id] = new Node(); nodes[id].id = id;
}
const walk = node => [node, ...node.children.flatMap(walk)];
const document = {
    createElement: tag => new Node(tag), createDocumentFragment: () => new Node('fragment'),
    createTextNode: text => { const node = new Node('text'); node.textContent = text; return node; },
    getElementById: id => nodes[id] || walk(nodes['help-viewport']).find(node => node.id === id)
};
let language = 'zh-CN';
const scrollbar = { top: 0 };
const context = vm.createContext({ document, launcherHelpCatalog: catalog,
    endSettingsScroll() {},
    updateSettingsScrollbar(area) { if (area === 'help') scrollbar.top = nodes['help-viewport'].scrollTop; },
    LauncherI18n: { language: () => language, text: (key, values = {}) =>
        locales[language][key].replace(/\{(\w+)\}/g, (_, token) => values[token]) } });
vm.runInContext(fs.readFileSync(path.join(resources, 'help.js'), 'utf8'), context);
const help = context.LauncherHelp, viewport = nodes['help-viewport'];
const titles = () => viewport.children.filter(node => node.tag === 'section').map(node => node.children[0].textContent);
help.refresh();
assert.ok(titles().some(title => title.includes('先认识手柄')));
const instructions = walk(document.getElementById('help-body-controls')).filter(node => node.tag === 'li');
assert.equal(instructions[0].textContent, catalog.articles.find(article => article.id === 'controls').text['zh-CN'].steps[0]);
assert.ok(walk(instructions[0]).some(node => node.className === 'help-keyword' && node.textContent === 'Grip'));
assert.equal(document.getElementById('help-article-controls').attributes['aria-expanded'], 'true');
document.getElementById('help-article-controls').onclick();
assert.equal(document.getElementById('help-article-controls').attributes['aria-expanded'], 'false');
help.selectCategory('story');
assert.equal(titles().length, 0);
assert.match(viewport.textContent, /剧透/);
assert.equal(nodes['help-toolbar'].style.display, 'none');
assert.equal(walk(viewport).filter(node => node.tag === 'button').length, 1);
assert.equal(document.getElementById('help-story-understood').textContent, '了解');
assert.equal(html.includes('id="help-spoilers"'), false);
help.selectCategory('equipment');
help.selectCategory('story');
assert.ok(document.getElementById('help-story-understood'), 'leaving without acknowledgement preserves the gate');
help.search('Shepherd');
assert.equal(titles().length, 0, 'hidden story prose must not leak into search results');
document.getElementById('help-story-understood').onclick();
assert.equal(nodes['help-toolbar'].style.display, 'block');
assert.equal(titles().length, catalog.articles.filter(article => article.category === 'story').length);
help.selectCategory('equipment');
help.selectCategory('story');
assert.equal(document.getElementById('help-story-understood'), undefined, 'acknowledge once per launcher session');
help.search('Shepherd');
assert.ok(titles().some(title => title.includes('Endgame')));
help.search('javelin');
assert.equal(titles().length, 1);
assert.match(titles()[0], /Javelin/);
help.search('beretta');
assert.equal(titles().length, 1, 'weapon aliases work across languages');
help.search('<script>alert(1)</script>');
assert.equal(titles().length, 0);
help.search('x'.repeat(100000));
assert.equal(titles().length, 0);
help.selectCategory('weapons');
assert.equal(nodes['help-search'].value, '');
assert.equal(titles().length, catalog.articles.filter(article => article.category === 'weapons').length);
document.getElementById('help-article-m9').onclick();
assert.equal(document.getElementById('help-body-m9').style.display, 'block');
language = 'en'; help.refresh();
assert.equal(document.getElementById('help-body-m9').style.display, 'block');
assert.match(document.getElementById('help-body-m9').textContent, /Press B/);
assert.equal(nodes['help-title'].textContent, 'Weapons');
const related = walk(document.getElementById('help-body-m9')).find(node => node.tag === 'a' && node.href === '#help-article-reload');
assert.ok(related, 'related navigation uses a real hyperlink');
assert.equal(related.onclick(), false);
assert.equal(document.getElementById('help-body-reload').style.display, 'block');
assert.equal(document.activeElement.id, 'help-article-reload');
assert.equal(html.includes('help-theme'), false);
assert.equal(html.includes('help-result-count'), false);
assert.equal(html.includes('data-reader-theme'), false);
let prevented = false;
help.categoryKey({ keyCode: 36, preventDefault() { prevented = true; } }, 'weapons');
assert.ok(prevented);
assert.equal(nodes['help-tab-movement'].attributes['aria-selected'], 'true');
assert.equal(document.activeElement.id, 'help-tab-movement');
for (const locale of localeIds) {
    language = locale;
    help.search(''); help.selectCategory('movement'); help.refresh();
    assert.ok(titles().some(title => title.includes(catalog.articles.find(a => a.id === 'controls').text[locale].title)), locale);
}
language = 'fr';
help.search('piolets');
assert.ok(titles().some(title => title.includes('Cliffhanger')), 'translated help is searchable across every locale');
language = 'ko'; help.refresh();
assert.ok(titles().some(title => title.includes('얼음도끼')), 'search is independent of the displayed language');
language = 'en';
help.selectCategory('movement');
viewport.scrollTop = 160;
document.getElementById('help-article-controls').onclick();
assert.equal(viewport.scrollTop, 160, 'accordion updates preserve the reader position');
assert.equal(scrollbar.top, 160, 'the scrollbar follows the restored reader position');
help.selectCategory('weapons'); help.selectCategory('movement');
assert.equal(scrollbar.top, 160, 'returning to a category restores its scrollbar position');
help.search('no-such-help-entry');
assert.equal(viewport.scrollTop, 0);
assert.equal(scrollbar.top, 0, 'empty search resets the scrollbar with the reader');
// A new launcher document resets acknowledgement without a storage API.
vm.runInContext(fs.readFileSync(path.join(resources, 'help.js'), 'utf8'), context);
context.LauncherHelp.selectCategory('story');
assert.equal(titles().length, 0);
assert.equal(document.getElementById('help-story-understood').textContent, 'Understood');
console.log('Launcher player help tests passed');
