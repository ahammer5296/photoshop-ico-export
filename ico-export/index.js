const { app, core, constants } = require("photoshop");
const storage = require("uxp").storage;
const { buildIco } = require("./ico.js");

const fs = storage.localFileSystem;
const statusEl = document.getElementById("status");
const setStatus = (t) => (statusEl.textContent = t);

function selectedSizes() {
  return [...document.querySelectorAll("#sizes sp-checkbox")]
    .filter((c) => c.checked)
    .map((c) => parseInt(c.value, 10));
}

// Renders a square copy of the active document at `size` px and returns its PNG bytes.
async function renderPng(source, size, tempFolder) {
  const dup = await source.duplicate("__ico_tmp__");
  try {
    if (dup.mode !== constants.DocumentMode.RGB) {
      await dup.changeMode(constants.ChangeMode.RGB);
    }
    if (dup.bitsPerChannel !== constants.BitsPerChannelType.EIGHT) {
      dup.bitsPerChannel = constants.BitsPerChannelType.EIGHT;
    }
    // Pad non-square images to a square canvas (centered), then scale.
    const side = Math.max(dup.width, dup.height);
    if (dup.width !== dup.height) {
      await dup.resizeCanvas(side, side, constants.AnchorPosition.MIDDLECENTER);
    }
    await dup.resizeImage(size, size, undefined, constants.ResampleMethod.AUTOMATIC);

    const file = await tempFolder.createFile(`ico_${size}.png`, { overwrite: true });
    await dup.saveAs.png(file, { compression: 6, interlaced: false }, true);
    return await file.read({ format: storage.formats.binary });
  } finally {
    await dup.closeWithoutSaving();
  }
}

async function exportIco() {
  const doc = app.activeDocument;
  if (!doc) return setStatus("Нет открытого документа.");

  const sizes = selectedSizes();
  if (!sizes.length) return setStatus("Выберите хотя бы один размер.");

  const baseName = (doc.title || "icon").replace(/\.[^.]+$/, "");
  const target = await fs.getFileForSaving(`${baseName}.ico`, { types: ["ico"] });
  if (!target) return setStatus("Отменено.");

  try {
    setStatus("Экспорт…");
    const tempFolder = await fs.getTemporaryFolder();
    const images = [];

    await core.executeAsModal(
      async () => {
        for (const size of sizes) {
          images.push({ size, data: await renderPng(doc, size, tempFolder) });
        }
      },
      { commandName: "Export ICO" }
    );

    await target.write(buildIco(images), { format: storage.formats.binary });
    setStatus(`Готово: ${target.name}\nРазмеры: ${sizes.join(", ")}`);
  } catch (e) {
    setStatus(`Ошибка: ${e.message || e}`);
  }
}

document.getElementById("export").addEventListener("click", exportIco);
document.getElementById("reload").addEventListener("click", () => location.reload());
