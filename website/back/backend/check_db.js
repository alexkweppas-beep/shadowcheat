const db = require('better-sqlite3')('./shadow.db');

const users = db.prepare('SELECT id, username, hwid FROM users LIMIT 10').all();
console.log('Users:', JSON.stringify(users, null, 2));

const subs = db.prepare("SELECT * FROM subscriptions WHERE product_id LIKE '%trigger%'").all();
console.log('Trigger subs:', JSON.stringify(subs, null, 2));

const allSubs = db.prepare('SELECT user_id, product_id, days_left FROM subscriptions WHERE days_left > 0 LIMIT 20').all();
console.log('Active subs:', JSON.stringify(allSubs, null, 2));
