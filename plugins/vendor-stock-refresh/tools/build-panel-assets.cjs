// Prepare generated panel artwork and encode native SpA1 sprites. No runtime writes.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const sharp = require('sharp');
const root = path.resolve(__dirname, '..');
const artwork = path.join(root, 'assets/artwork');
const output = path.join(root, 'assets/mod-data/data/hd/global/ui/panel/vendors/ruffneckk');
const reference = process.env.VENDOR_PANEL_REFERENCE_ROOT;
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

async function nativePng(name, frameWidth) {
  const bytes = fs.readFileSync(path.join(reference, name + '.sprite'));
  const width = bytes.readUInt32LE(8), height = bytes.readUInt32LE(12);
  if (bytes.subarray(0, 4).toString() !== 'SpA1' || bytes.readUInt16LE(4) !== 31
      || bytes.length !== 40 + width * height * 4) throw new Error(`Unsupported sprite: ${name}`);
  let image = sharp(bytes.subarray(40), { raw: { width, height, channels: 4 } });
  if (frameWidth) image = image.extract({ left: 0, top: 0, width: frameWidth, height });
  return image.png().toBuffer();
}

async function main() {
  fs.mkdirSync(output, { recursive: true });
  // Use a complete footer so both horizontal rails belong to one continuous texture.
  // The only crop removes the generator's black letterboxing; no feathered insert.
  const png = await sharp(path.join(artwork, 'generated-footer-r2.png'))
    .extract({ left: 0, top: 70, width: 2170, height: 540 })
    .resize(1162, 297, { fit: 'fill' }).ensureAlpha().png().toBuffer();
  fs.writeFileSync(path.join(artwork, 'vendor-refresh-frame.png'), png);
  const entries = [];
  for (const [width, height, suffix] of [[1162, 297, ''], [581, 149, '.lowend']]) {
    const rgba = await sharp(png).resize(width, height, { fit: 'fill' }).ensureAlpha().raw().toBuffer();
    const header = Buffer.alloc(40);
    header.write('SpA1'); header.writeUInt16LE(31, 4); header.writeUInt16LE(width, 6);
    header.writeUInt32LE(width, 8); header.writeUInt32LE(height, 12); header.writeUInt32LE(1, 20);
    const data = Buffer.concat([header, rgba]);
    const filename = `vendorrefreshframe${suffix}.sprite`;
    fs.writeFileSync(path.join(output, filename), data);
    entries.push({ filename, width, height, bytes: data.length, sha256: hash(data) });
  }
  // Assemble a local preview from actual background/button pixels at layout positions.
  if (reference) {
  const refresh = await nativePng('vendors/gambling_refresh_button', 116);
  const coin = await nativePng('coins_icon', 57);
  for (const [name, repair] of [['vendorforge_bg', true], ['vendorshop_bg', false]]) {
    const background = await nativePng('vendors/' + name);
    const layers = [{ input: png, left: 0, top: 1210 },
      { input: refresh, left: 520, top: 1352 }, { input: coin, left: 427, top: 1259 }];
    if (repair) {
      layers.push({ input: await nativePng('vendors/repair_button', 116), left: 169, top: 1277 });
      layers.push({ input: await nativePng('vendors/repairall_button', 116), left: 877, top: 1277 });
    }
    // Encode the composite first: extraction before composite changes its coordinate space.
    const composed = await sharp(background).composite(layers).png().toBuffer();
    await sharp(composed).extract({ left: 0, top: 1210, width: 1162, height: 297 })
      .png().toFile(path.join(artwork, `${name}-preview.png`));
  }
  }
  const manifest = { generatedSourceSha256: hash(fs.readFileSync(path.join(artwork, 'generated-footer-r2.png'))),
    engine: 'built-in image_gen', assetRevision: 2, insert: { x: 0, y: 1210, width: 1162, height: 297 },
    goldAnchor: { x: 421, y: 1260, width: 313, height: 58 },
    refreshSlot: { x: 520, y: 1352, width: 116, height: 116 }, entries };
  fs.writeFileSync(path.join(root, 'assets/manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
  console.log(`Encoded ${entries.length} panel sprites; optional previews require VENDOR_PANEL_REFERENCE_ROOT.`);
}
main().catch(error => { console.error(error); process.exitCode = 1; });
