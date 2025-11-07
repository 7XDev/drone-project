// Import necessary modules for content browsing and markdown conversion
import MarkdownConverter from './md-converter.js';
import ContentBrowser from './content-browser.js';

// Global variable to track the currently opened page path
let currentPagePath = "";

// Global content browser instance
let browser = new ContentBrowser;

// Global markdown converter instance
let converter = new MarkdownConverter;

let browserContainer;

document.addEventListener("DOMContentLoaded", async (event) => {
    // Get references to all topic buttons and category buttons in the sidebar
    browserContainer = document.querySelector(".topic-selector");
    const topicButtons = document.querySelectorAll('.topic-button');
    const topicCategoryButtons = document.querySelectorAll('.topic-category-button'); 

    await refresh();
    
    /**
     * Set up click handlers for individual topic buttons
     * When a topic is clicked, it becomes selected and all others become unselected
     */
    topicButtons.forEach(button => {
        button.addEventListener('click', () => {
            // Deselect all topic buttons
            topicButtons.forEach(btn => {
                btn.classList.remove('topic-selected');
                btn.classList.add('topic-unselected');
            });

            // Deselect all category buttons and reset their toggle state
            topicCategoryButtons.forEach(btn => {
                btn.classList.remove('topic-category-button-selected');
                btn.classList.add('topic-category-button-unselected');
                btn.dataset.toggled = 'false';
            });

            // Select the clicked topic button
            button.classList.remove('topic-unselected');
            button.classList.add('topic-selected');
        });
    });

    /**
     * Set up click handlers for topic category buttons
     * Category buttons can be toggled on/off and show expanded/collapsed states
     */
    topicCategoryButtons.forEach(button => {
        // Initialize all category buttons as not toggled
        button.dataset.toggled = 'false';
        
        button.addEventListener('click', () => {
            // Check current toggle state before making changes
            const isToggled = button.dataset.toggled === 'true';
            
            // Deselect all individual topic buttons when a category is clicked
            topicButtons.forEach(btn => {
                btn.classList.remove('topic-selected');
                btn.classList.add('topic-unselected');
            });

            // Reset all category buttons to unselected state
            topicCategoryButtons.forEach(btn => {
                btn.classList.remove('topic-category-button-selected');
                btn.classList.add('topic-category-button-unselected');
                btn.dataset.toggled = 'false';
            });

            // Select the clicked category button
            button.classList.remove('topic-category-button-unselected');
            button.classList.add('topic-category-button-selected');

            // Toggle the category expanded/collapsed state
            if (!isToggled) {
                // Expand the category
                button.dataset.toggled = 'true';
                onToggle(button);
            } else {
                // Collapse the category
                button.dataset.toggled = 'false';
                onDeToggle(button);
            }
        });
    });

    /**
     * Handle category expansion - rotates arrow to indicate expanded state
     * @param {HTMLElement} button - The category button that was toggled
     */
    function onToggle(button) {
        console.log('Category toggled ON:', button.textContent); // DEBUG_STATEMENT
        const arrow = button.querySelector('.category-arrow');
        arrow.style.transition = 'transform 0.3s ease';
        arrow.style.transform = 'rotate(90deg)'; // Rotate arrow to point down
    }

    /**
     * Handle category collapse - rotates arrow back to indicate collapsed state
     * @param {HTMLElement} button - The category button that was toggled
     */
    function onDeToggle(button) {
        console.log('Category toggled OFF:', button.textContent); // DEBUG_STATEMENT
        const arrow = button.querySelector('.category-arrow');
        arrow.style.transition = 'transform 0.3s ease';
        arrow.style.transform = 'rotate(0deg)'; // Rotate arrow back to point right


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
            arrow.innerHTML = '<img src="assets/img/arrow.svg" class="category-arrow" alt="arrow" width="15" height="15">';
            button.appendChild(arrow);
        }
    }
}); 

// Refreshes MarkDown-Container and Topic-Container
async function refresh() {
    // Fetch and load topic structure
    await browser.fetchStructure('../../util/content-browser/content-structure.json'); // DEBUG_DATA
    
    // Populate container with the generated topics
    browserContainer.innerHTML = browser.generateTopicsHTML();
}