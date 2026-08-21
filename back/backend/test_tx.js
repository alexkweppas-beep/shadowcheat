const db=require('better-sqlite3')(':memory:');
db.exec('CREATE TABLE users(id INTEGER PRIMARY KEY); CREATE TABLE activation_keys(id INTEGER PRIMARY KEY, key_str TEXT, product_id TEXT, days INTEGER, used INTEGER, used_by INTEGER, used_at TEXT); CREATE TABLE subscriptions(id INTEGER PRIMARY KEY, user_id INTEGER, product_id TEXT, days_left INTEGER);');
db.prepare('INSERT INTO users(id) VALUES(1)').run();
db.prepare('INSERT INTO activation_keys(id, key_str, product_id, days, used) VALUES(1, \'test\', \'prod\', 30, 0)').run();
const k = db.prepare('SELECT * FROM activation_keys').get();
const tx = db.transaction(() => {
    db.prepare('UPDATE activation_keys SET used=1, used_by=?, used_at=datetime("now") WHERE id=?').run(1, k.id);
    const pidLower = (k.product_id || '').toLowerCase();
    const existing = db.prepare('SELECT * FROM subscriptions WHERE user_id=? AND product_id COLLATE NOCASE=?').get(1, pidLower);
    if (existing) {
        db.prepare('UPDATE subscriptions SET days_left=days_left+? WHERE id=?').run(k.days, existing.id);
    } else {
        db.prepare('INSERT INTO subscriptions (user_id, product_id, days_left) VALUES (?,?,?)').run(1, pidLower, k.days);
    }
});
try {
    tx();
    console.log('TX OK');
} catch(e) {
    console.log('TX ERROR:', e.message);
}
