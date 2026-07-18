const button = document.querySelector('.menu-button');
const nav = document.querySelector('#site-nav');
const closeMenu = () => {
  if (!button || !nav) return;
  button.setAttribute('aria-expanded', 'false');
  nav.classList.remove('open');
};

if (button && nav) {
  button.addEventListener('click', () => {
    const open = button.getAttribute('aria-expanded') === 'true';
    button.setAttribute('aria-expanded', String(!open));
    nav.classList.toggle('open', !open);
  });
  nav.addEventListener('click', event => {
    if (event.target.closest('a')) closeMenu();
  });
  document.addEventListener('click', event => {
    if (!nav.contains(event.target) && !button.contains(event.target)) closeMenu();
  });
  window.addEventListener('resize', () => {
    if (window.innerWidth > 800) closeMenu();
  });
}

// Every meaningful content image can be enlarged without changing the Markdown
// used by the page. The same interaction works with touch, mouse and keyboard.
const zoomableImages = [...document.querySelectorAll('main img:not([data-no-zoom])')];
if (zoomableImages.length) {
  const lightbox = document.createElement('div');
  lightbox.className = 'image-lightbox';
  lightbox.hidden = true;
  lightbox.innerHTML = `
    <div class="image-lightbox__bar">
      <p class="image-lightbox__caption" aria-live="polite"></p>
      <button class="image-lightbox__close" type="button" aria-label="Close enlarged image">Close</button>
    </div>
    <div class="image-lightbox__viewport">
      <img class="image-lightbox__image" alt="">
    </div>`;
  document.body.appendChild(lightbox);

  const fullImage = lightbox.querySelector('.image-lightbox__image');
  const caption = lightbox.querySelector('.image-lightbox__caption');
  const closeButton = lightbox.querySelector('.image-lightbox__close');
  let opener = null;

  const closeLightbox = () => {
    if (lightbox.hidden) return;
    lightbox.hidden = true;
    document.body.classList.remove('lightbox-open');
    fullImage.removeAttribute('src');
    opener?.focus();
  };
  const openLightbox = image => {
    opener = image;
    fullImage.src = image.currentSrc || image.src;
    fullImage.alt = image.alt || '';
    const figureCaption = image.closest('figure')?.querySelector('figcaption')?.textContent.trim();
    caption.textContent = figureCaption || image.alt || 'Enlarged image';
    lightbox.hidden = false;
    document.body.classList.add('lightbox-open');
    closeButton.focus();
  };

  zoomableImages.forEach(image => {
    image.classList.add('zoomable-image');
    image.tabIndex = 0;
    image.setAttribute('role', 'button');
    image.setAttribute('aria-label', `${image.alt || 'Image'}. Open full-size view.`);
    image.addEventListener('click', () => openLightbox(image));
    image.addEventListener('keydown', event => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        openLightbox(image);
      }
    });
  });
  closeButton.addEventListener('click', closeLightbox);
  lightbox.addEventListener('click', event => {
    if (event.target === lightbox || event.target.classList.contains('image-lightbox__viewport')) closeLightbox();
  });
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape') {
      closeLightbox();
      closeMenu();
    }
  });
}
