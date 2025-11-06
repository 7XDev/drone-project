import MarkdownConverter from './md-converter.js';
import ContentBrowser from './content-browser.js';

document.addEventListener("DOMContentLoaded", async (event) => {
    // Render test categories
    const browser = new ContentBrowser;
    await browser.fetchStructure('../../util/content-browser/content-structure.json');
    const browserContainer = document.querySelector(".topic-selector");
    browserContainer.innerHTML = browser.generateTopicsHTML();
}); 