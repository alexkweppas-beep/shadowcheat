// Parses every R"JS( ... )JS" literal in the source tree.
//
// These scripts are only ever compiled by V8 inside the game, so a syntax error
// in one does not fail the build: the hook silently never installs and the
// feature just does nothing. That is expensive to diagnose in game and cheap to
// catch here.
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = path.join(__dirname, '..', 'src');

function* walk(dir) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name);
        if (entry.isDirectory())
            yield* walk(full);
        else if (/\.(cpp|hpp|h)$/.test(entry.name))
            yield full;
    }
}

let checked = 0;
let failed = 0;

for (const file of walk(root)) {
    const text = fs.readFileSync(file, 'utf8');
    const pattern = /R"JS\(([\s\S]*?)\)JS"/g;
    let match;
    while ((match = pattern.exec(text)) !== null) {
        const script = match[1];
        // Line number of the literal's start, so a failure points somewhere.
        const line = text.slice(0, match.index).split('\n').length;
        const where = `${path.relative(root, file)}:${line}`;
        try {
            new vm.Script(script);
            ++checked;
        } catch (error) {
            ++failed;
            console.error(`FAIL ${where}: ${error.message}`);
        }
    }
}

console.log(`embedded JS: ${checked} literal(s) parsed, ${failed} failed`);
if (failed)
    process.exit(1);
