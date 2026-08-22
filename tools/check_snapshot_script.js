// Extracts the embedded RAGE snapshot script out of RagePlayerNames.cpp and
// runs it through the JS parser. The script is only ever parsed by V8 inside the
// game, so a typo in it fails silently as "no names, no visibility" rather than
// as a build error.
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const source = path.join(
    __dirname, '..', 'src', 'Features', 'RagePlayerNames.cpp');
const lines = fs.readFileSync(source, 'utf8').split(/\r?\n/);

const begin = lines.findIndex(l => l.includes('const rows=[],seen={};'));
if (begin < 0)
    throw new Error('format string start not found');
const end = lines.findIndex(
    (l, i) => i > begin && l.trim() === 'adminKey,');
if (end < 0)
    throw new Error('format string end not found');

let literal = '';
for (const line of lines.slice(begin, end)) {
    const trimmed = line.trim();
    if (trimmed.startsWith('//'))
        continue;
    // The final literal carries the comma that starts the argument list.
    const match = trimmed.match(/^"((?:[^"\\]|\\.)*)",?$/);
    if (!match)
        throw new Error(`unexpected line in format string: ${trimmed}`);
    literal += match[1];
}

const unescaped = literal.replace(
    /\\(u[0-9a-fA-F]{4}|x[0-9a-fA-F]{2}|.)/g,
    (all, esc) => {
        switch (esc[0]) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '0': return '\0';
        case '"': return '"';
        case "'": return "'";
        case '\\': return '\\';
        case 'u': return String.fromCharCode(parseInt(esc.slice(1), 16));
        case 'x': return String.fromCharCode(parseInt(esc.slice(1), 16));
        // Regex escapes such as \d pass straight through to JS.
        default: return all;
        }
    });

const script = unescaped
    .replace(/%%/g, '%')
    .replace(/%s/g, 'adminLevel')
    .replace(/%d/g, '1');

// snprintf writes into a fixed 16 KB buffer and reports failure by truncating.
const budget = 16384;
const bytes = Buffer.byteLength(script, 'utf8');

// The script body contains top-level `return`, so it is parsed as a function.
new vm.Script(`(function(){${script}})`);

console.log(`snapshot script OK: ${bytes} bytes of ${budget} ` +
    `(${(100 * bytes / budget).toFixed(1)}%)`);
if (bytes >= budget)
    throw new Error('script no longer fits in g_SnapshotScript');
