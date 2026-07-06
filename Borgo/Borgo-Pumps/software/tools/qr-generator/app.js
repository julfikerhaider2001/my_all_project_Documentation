const form = document.querySelector("#device-form");
const qrcode = document.querySelector("#qrcode");
const payloadField = document.querySelector("#payload");
const copyButton = document.querySelector("#copy");
const downloadButton = document.querySelector("#download");
const regenerateButton = document.querySelector("#regenerate");
const claimCodeField = document.querySelector("#claimCode");

function randomClaimCode() {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("").toUpperCase();
}

function getPayload() {
  return {
    type: "borgo-farm-device",
    version: 1,
    name: document.querySelector("#name").value.trim(),
    model: document.querySelector("#model").value.trim(),
    deviceId: document.querySelector("#deviceId").value.trim(),
    claimCode: claimCodeField.value.trim().toUpperCase(),
  };
}

async function renderQr(event) {
  event?.preventDefault();
  if (!form.reportValidity()) return;
  const payload = getPayload();
  const raw = JSON.stringify(payload);
  payloadField.value = raw;
  qrcode.innerHTML = "";
  new QRCode(qrcode, {
    text: raw,
    width: 280,
    height: 280,
    colorDark: "#050607",
    colorLight: "#ffffff",
    correctLevel: QRCode.CorrectLevel.M,
  });
}

copyButton.addEventListener("click", async () => {
  try {
    await navigator.clipboard.writeText(payloadField.value);
  } catch {
    payloadField.select();
    document.execCommand("copy");
  }
  copyButton.textContent = "Copied";
  setTimeout(() => {
    copyButton.textContent = "Copy Payload";
  }, 1200);
});

downloadButton.addEventListener("click", () => {
  const canvas = qrcode.querySelector("canvas");
  const image = qrcode.querySelector("img");
  const link = document.createElement("a");
  link.download = `${document.querySelector("#deviceId").value.trim() || "borgo-device"}.png`;
  link.href = canvas ? canvas.toDataURL("image/png") : image?.src;
  if (!link.href) return;
  link.click();
});

regenerateButton.addEventListener("click", () => {
  claimCodeField.value = randomClaimCode();
  renderQr();
});

form.addEventListener("submit", renderQr);
claimCodeField.value = randomClaimCode();
renderQr();
