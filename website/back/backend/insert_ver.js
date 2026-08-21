const db = require('better-sqlite3')('shadow.db');
try {
    db.prepare("INSERT INTO loader_versions (version_id, version_token, label, revoked) VALUES ('111', 'token_111', 'Initial Release', 0)").run();
    console.log('Version 111 inserted successfully.');
} catch(e) {
    console.log('Error or already exists:', e.message);
}
