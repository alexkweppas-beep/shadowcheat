const db=require('better-sqlite3')('shadow.db');
const r = db.prepare("SELECT sql FROM sqlite_master WHERE name='loader_sessions'").get();
console.log(r.sql);
