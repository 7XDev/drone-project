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
    const topicCategoryButtons = document.querySelectorAll('.topic-category-button');
    
    topicButtons.forEach(button => {
        button.addEventListener('click', () => {
            topicButtons.forEach(btn => {
                btn.classList.remove('topic-selected');
                btn.classList.add('topic-unselected');
            });

            topicCategoryButtons.forEach(btn => {
                btn.classList.remove('topic-category-button-selected');
                btn.classList.add('topic-category-button-unselected');
                btn.dataset.toggled = 'false';
            });

            button.classList.remove('topic-unselected');
            button.classList.add('topic-selected');
        });
    });

    topicCategoryButtons.forEach(button => {
        button.dataset.toggled = 'false';
        
        button.addEventListener('click', () => {
            const isToggled = button.dataset.toggled === 'true';
            
            topicButtons.forEach(btn => {
                btn.classList.remove('topic-selected');
                btn.classList.add('topic-unselected');
            });

            topicCategoryButtons.forEach(btn => {
                btn.classList.remove('topic-category-button-selected');
                btn.classList.add('topic-category-button-unselected');
                btn.dataset.toggled = 'false';
            });

            button.classList.remove('topic-category-button-unselected');
            button.classList.add('topic-category-button-selected');

            if (!isToggled) {
                button.dataset.toggled = 'true';
                onToggle(button);
            } else {
                button.dataset.toggled = 'false';
                onDeToggle(button);
            }
        });
    });

    function onToggle(button) {
        console.log('Category toggled ON:', button.textContent);
        const arrow = button.querySelector('.category-arrow');
        arrow.style.transition = 'transform 0.3s ease';
        arrow.style.transform = 'rotate(90deg)';
        arrow.innerHTML = '→';
    }

    function onDeToggle(button) {
        console.log('Category toggled OFF:', button.textContent);
        const arrow = button.querySelector('.category-arrow');
        arrow.style.transition = 'transform 0.3s ease';
        arrow.style.transform = 'rotate(0deg)';
        arrow.innerHTML = '→';
    }

    // Select first topic-button on load
    if (topicButtons.length > 0) {
        topicButtons[0].classList.remove('topic-unselected');
        topicButtons[0].classList.add('topic-selected');
    }
    
    //Add arrow to topic category buttons
    if (topicCategoryButtons.length > 0) {
        for(const button of topicCategoryButtons) {
            const arrow = document.createElement('span');
            arrow.classList.add('category-arrow');
            arrow.id = 'category-arrow';
            arrow.innerHTML = '→';
            button.appendChild(arrow);
        }
    }
}); 