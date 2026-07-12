const button = document.querySelector('.menu-button');
const nav = document.querySelector('#site-nav');
if (button && nav) button.addEventListener('click', () => {
  const open = button.getAttribute('aria-expanded') === 'true';
  button.setAttribute('aria-expanded', String(!open));
  nav.classList.toggle('open', !open);
});
