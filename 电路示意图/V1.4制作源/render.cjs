const fs = require('fs');
const path = require('path');
const sharp = require('sharp');
const dir = path.dirname(__dirname);
(async () => {
  for (const name of fs.readdirSync(dir).filter(n => n.endsWith('_V1.4.svg'))) {
    const src = path.join(dir, name);
    await sharp(Buffer.from(fs.readFileSync(src, 'utf8')), {density: 120})
      .resize({width: 2200}).png().toFile(src.replace(/\.svg$/, '.png'));
    console.log(name);
  }
})().catch(error => { console.error(error); process.exit(1); });
