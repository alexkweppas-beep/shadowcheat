// Запусти на сервере: node add_trigger_sub.js <username> <days>
// Пример: node add_trigger_sub.js fraud444 30
const db = require('better-sqlite3')('./shadow.db');

const username = process.argv[2];
const days = parseInt(process.argv[3]) || 30;

if (!username) {
    console.log('Usage: node add_trigger_sub.js <username> <days>');
    process.exit(1);
}

const user = db.prepare('SELECT * FROM users WHERE username=?').get(username);
if (!user) {
    console.log('User not found:', username);
    process.exit(1);
}

// Добавляем trigger подписку
const existing = db.prepare("SELECT * FROM subscriptions WHERE user_id=? AND product_id='trigger'").get(user.id);
if (existing) {
    db.prepare("UPDATE subscriptions SET days_left=days_left+? WHERE id=?").run(days, existing.id);
    console.log(`Updated trigger sub for ${username}: +${days} days (now ${existing.days_left + days})`);
} else {
    db.prepare("INSERT INTO subscriptions (user_id, product_id, days_left) VALUES (?,?,?)").run(user.id, 'trigger', days);
    console.log(`Added trigger sub for ${username}: ${days} days`);
}

// Показываем все подписки пользователя
const subs = db.prepare('SELECT product_id, days_left FROM subscriptions WHERE user_id=? AND days_left > 0').all(user.id);
console.log('All active subs:', JSON.stringify(subs));
