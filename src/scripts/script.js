import MarkdownConverter from './md-converter.js';
import ContentBrowser from './content-browser.js';

document.addEventListener("DOMContentLoaded", async (event) => {
    // Render test categories
    const browser = new ContentBrowser;
    await browser.fetchStructure('../../util/content-browser/content-structure.json');
    const browserContainer = document.querySelector(".topic-selector");
    browserContainer.innerHTML = browser.generateTopicsHTML();

    //Select/Unselect topic buttons
    const topicButtons = document.querySelectorAll('.topic-button');
    topicButtons.forEach(button => {
        button.addEventListener('click', () => {
            // Remove selected class from all buttons and add unselected
            topicButtons.forEach(btn => {
                btn.classList.remove('topic-selected');
                btn.classList.add('topic-unselected');
            });
            // Add selected class to clicked button and remove unselected
            button.classList.remove('topic-unselected');
            button.classList.add('topic-selected');
        });
    });
}); 