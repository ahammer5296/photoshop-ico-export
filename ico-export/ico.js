// Builds an .ico container from PNG-encoded images (PNG-in-ICO, supported since Windows Vista).
// images: [{ size: 16..256, data: ArrayBuffer | Uint8Array }]
function buildIco(images) {
  const sorted = [...images].sort((a, b) => a.size - b.size);
  const count = sorted.length;
  const blobs = sorted.map((i) => (i.data instanceof Uint8Array ? i.data : new Uint8Array(i.data)));
  const headerLen = 6 + 16 * count;
  const total = headerLen + blobs.reduce((s, b) => s + b.length, 0);

  const out = new Uint8Array(total);
  const view = new DataView(out.buffer);
  view.setUint16(0, 0, true); // reserved
  view.setUint16(2, 1, true); // type: icon
  view.setUint16(4, count, true);

  let offset = headerLen;
  sorted.forEach((img, i) => {
    const e = 6 + 16 * i;
    const dim = img.size >= 256 ? 0 : img.size; // 0 means 256
    view.setUint8(e, dim);
    view.setUint8(e + 1, dim);
    view.setUint8(e + 2, 0); // palette colors
    view.setUint8(e + 3, 0);
    view.setUint16(e + 4, 1, true); // planes
    view.setUint16(e + 6, 32, true); // bpp
    view.setUint32(e + 8, blobs[i].length, true);
    view.setUint32(e + 12, offset, true);
    out.set(blobs[i], offset);
    offset += blobs[i].length;
  });
  return out.buffer;
}

module.exports = { buildIco };
