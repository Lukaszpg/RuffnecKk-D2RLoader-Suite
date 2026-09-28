const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
// These pinned native layouts use trailing commas but no comments or comma-like
// string contents. All generated files are strict JSON.
const read = name => JSON.parse(fs.readFileSync(path.join(root, 'assets/layout-source', name), 'utf8')
  .replace(/,\s*([}\]])/g, '$1'));
const normal = read('vendorpanellayouthd.json');
const controller = read('controller-vendorpanellayouthd.json');
const insert = [
  { type: 'ImageWidget', name: 'vendor_refresh_frame', fields: {
    rect: { x: 0, y: 1210 }, filename: 'PANEL/Vendors/RuffnecKk/VendorRefreshFrame' } },
  { type: 'Widget', name: 'vendor_refresh_slot', fields: {
    rect: { x: 520, y: 1352, width: 116, height: 116 } } },
  { type: 'Widget', name: 'vendor_refresh_gold_anchor', fields: {
    rect: { x: 421, y: 1260, width: 313, height: 58 } } },
];
const offset = normal.children.findIndex(child => child.name === 'background_repair');
if (offset < 0) throw new Error('Expected both vendor backgrounds.');
normal.children.splice(offset + 1, 0, ...insert);
// The console layout inherits the keyboard/mouse file. Explicitly opt out of
// its custom markers so the plugin hides the insert and uses the existing fallback.
controller.children.push(...insert.slice(1).map(child => ({ ...child,
  fields: { rect: { x: 0, y: 0, width: 0, height: 0 } } })));
const output = path.join(root, 'assets/mod-data/data/global/ui/layouts');
fs.mkdirSync(path.join(output, 'controller'), { recursive: true });
fs.writeFileSync(path.join(output, 'vendorpanellayouthd.json'), JSON.stringify(normal, null, 4) + '\n');
fs.writeFileSync(path.join(output, 'controller/vendorpanellayouthd.json'), JSON.stringify(controller, null, 4) + '\n');
console.log('Generated keyboard/mouse panel layout and controller inheritance guard.');
