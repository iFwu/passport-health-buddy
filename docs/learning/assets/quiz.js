"use strict";

document.querySelectorAll("[data-quiz]").forEach((group) => {
  const feedback = group.querySelector("[role=status]");
  group.addEventListener("change", (event) => {
    const choice = event.target;
    if (!(choice instanceof HTMLInputElement) || choice.type !== "radio") return;
    const correct = choice.value === group.dataset.answer;
    feedback.className = `feedback ${correct ? "correct" : "incorrect"}`;
    feedback.textContent = `${correct ? "本题回答正确。" : "再想一想。"}${choice.dataset.feedback}`;
  });
});

document.querySelector("[data-reset]")?.addEventListener("click", () => {
  document.querySelectorAll("[data-quiz] input").forEach((choice) => { choice.checked = false; });
  document.querySelectorAll("[data-quiz] [role=status]").forEach((feedback) => {
    feedback.className = "feedback";
    feedback.textContent = "选择后会立即显示反馈。";
  });
  document.querySelector("[data-quiz] input")?.focus();
});
