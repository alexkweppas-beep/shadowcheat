const fs = require('fs');
const b1 = fs.readFileSync('C:/Users/Vlad/.gemini/antigravity-ide/brain/a8226f99-a7ff-4e5b-bacc-232f01a25393/media__1782417648239.png').toString('base64');
const b2 = fs.readFileSync('C:/Users/Vlad/.gemini/antigravity-ide/brain/a8226f99-a7ff-4e5b-bacc-232f01a25393/media__1782417648248.png').toString('base64');

fs.mkdirSync('C:/Users/Vlad/.gemini/antigravity-ide/brain/a8226f99-a7ff-4e5b-bacc-232f01a25393/scratch', {recursive: true});
fs.writeFileSync('C:/Users/Vlad/.gemini/antigravity-ide/brain/a8226f99-a7ff-4e5b-bacc-232f01a25393/scratch/b1.txt', b1);
fs.writeFileSync('C:/Users/Vlad/.gemini/antigravity-ide/brain/a8226f99-a7ff-4e5b-bacc-232f01a25393/scratch/b2.txt', b2);

console.log("Done");
