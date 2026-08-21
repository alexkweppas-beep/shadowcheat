const db = require('better-sqlite3')('D:/антиграв/333333/444/676767/backend/shadow.db');
db.prepare("UPDATE subscriptions SET product_id='trigger_silent' WHERE user_id=1 AND product_id='trigger'").run();
console.log(db.prepare('SELECT * FROM subscriptions WHERE user_id=1').all());
