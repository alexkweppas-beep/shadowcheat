const db = require('better-sqlite3')('shadow.db');
db.prepare("INSERT INTO subscriptions (user_id, product_id, days_left) VALUES (1, 'trigger', 715)").run();
console.log('trigger sub added');
console.log(db.prepare('SELECT * FROM subscriptions').all());
