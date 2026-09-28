const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '..');
const parse = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/,\s*([}\]])/g, '$1'));
const layoutRoot = path.join(root, 'assets/mod-data/data/global/ui/layouts');
const original = parse(path.join(root, 'assets/layout-source/vendorpanellayouthd.json'));
const edited = parse(path.join(layoutRoot, 'vendorpanellayouthd.json'));
const added = node => node.name.startsWith('vendor_refresh_');
assert.deepEqual({ ...edited, children: edited.children.filter(node => !added(node)) }, original,
  'Preserve every pre-existing widget, property, native action, sound, and tooltip');
assert.equal(new Set(edited.children.map(node => node.name)).size, edited.children.length);
const index = name => edited.children.findIndex(node => node.name === name);
assert(index('vendor_refresh_frame') > index('background_repair'));
assert(index('vendor_refresh_frame') < index('button_refresh'));
assert(index('vendor_refresh_frame') < index('gold_amount'));
const manifest = parse(path.join(root, 'assets/manifest.json'));
assert.deepEqual(edited.children[index('vendor_refresh_slot')].fields.rect, manifest.refreshSlot);
assert.deepEqual(edited.children[index('vendor_refresh_gold_anchor')].fields.rect, manifest.goldAnchor);
const { insert, refreshSlot: slot, goldAnchor: gold } = manifest;
for (const rect of [slot, gold]) {
  assert(rect.x >= insert.x && rect.y >= insert.y);
  assert(rect.x + rect.width <= insert.x + insert.width);
  assert(rect.y + rect.height <= insert.y + insert.height);
}
assert(slot.y > gold.y + gold.height);
const consoleOriginal = parse(path.join(root, 'assets/layout-source/controller-vendorpanellayouthd.json'));
const consoleEdited = parse(path.join(layoutRoot, 'controller/vendorpanellayouthd.json'));
assert.deepEqual({ ...consoleEdited, children: consoleEdited.children.filter(node => !added(node)) }, consoleOriginal);
for (const marker of consoleEdited.children.filter(added)) {
  assert.equal(marker.fields.rect.width, 0);
  assert.equal(marker.fields.rect.height, 0);
}
for (const entry of manifest.entries) {
  const bytes = fs.readFileSync(path.join(root, 'assets/mod-data/data/hd/global/ui/panel/vendors/ruffneckk', entry.filename));
  assert.equal(bytes.subarray(0, 4).toString(), 'SpA1');
  assert.equal(bytes.readUInt16LE(4), 31);
  assert.equal(bytes.readUInt16LE(6), entry.width);
  assert.equal(bytes.readUInt32LE(8), entry.width);
  assert.equal(bytes.readUInt32LE(12), entry.height);
  assert.equal(bytes.readUInt32LE(20), 1);
  assert.equal(bytes.length, 40 + entry.width * entry.height * 4);
  assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'), entry.sha256);
}
console.log('PASS: native widget preservation, layering, slot bounds, controller inheritance, HD/lowend sprite headers and hashes.');
