const authScreen = document.querySelector("#authScreen");
const appScreen = document.querySelector("#appScreen");
const authForm = document.querySelector("#authForm");
const navItems = document.querySelectorAll(".nav-item");
const screens = document.querySelectorAll(".screen");
const toast = document.querySelector("#toast");
const activityList = document.querySelector("#activityList");

let toastTimer;

function showToast(message) {
  toast.textContent = message;
  toast.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => {
    toast.hidden = true;
  }, 2600);
}

function showScreen(id) {
  screens.forEach((screen) => screen.classList.toggle("active", screen.id === id));
  navItems.forEach((item) => item.classList.toggle("active", item.dataset.target === id));
}

function addActivity(title, detail) {
  const item = document.createElement("li");
  const strong = document.createElement("strong");
  const span = document.createElement("span");
  strong.textContent = title;
  span.textContent = detail;
  item.append(strong, span);
  activityList.prepend(item);
}

authForm.addEventListener("submit", (event) => {
  event.preventDefault();
  authScreen.hidden = true;
  appScreen.hidden = false;
  showToast("Signed in to Borgo Pumps");
});

navItems.forEach((item) => {
  item.addEventListener("click", () => showScreen(item.dataset.target));
});

document.querySelectorAll(".switch input").forEach((input) => {
  input.addEventListener("change", () => {
    const command = input.checked ? "ON" : "OFF";
    const device = input.dataset.device || "Pump";
    addActivity(device, `${command} command queued for signed SMS delivery`);
    showToast(`${device}: ${command} queued`);
  });
});

document.querySelectorAll(".bulk").forEach((button) => {
  button.addEventListener("click", () => {
    const command = button.dataset.command;
    addActivity(command === "ON" ? "Start All" : "Stop All", `${command} command queued for all online pumps`);
    showToast(`${command} command queued for all online pumps`);
  });
});

document.querySelector("#registerForm").addEventListener("submit", (event) => {
  event.preventDefault();
  showToast("Device registration submitted");
});
