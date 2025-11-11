// Import necessary modules for content browsing and markdown conversion
import MarkdownConverter from './md-converter.js';
import ContentBrowser from './content-browser.js';

// Global variable to track the currently opened page path
let currentPagePath = "";

// Global content browser instance
let browser = new ContentBrowser;

// Global markdown converter instance
let converter = new MarkdownConverter;
const preview = document.getElementById('markdown-container');

let browserContainer;

// Track currently selected elements for O(1) deselection
let currentlySelectedTopic = null;
let currentlySelectedCategory = null;

/**
 * Set up click handlers for all buttons - called after each refresh
 */
async function setupEventListeners() {
    // Get fresh references to all topic buttons and category buttons
    const topicButtons = document.querySelectorAll('.topic-button');
    const topicCategoryButtons = document.querySelectorAll('.topic-category-button');

    /**
     * Set up click handlers for topic category buttons
     */
    topicCategoryButtons.forEach(button => {
        console.log();
        // Load the current state from content browser and apply it
        const categoryItem = browser.contentStructure.find(item => 
            item.type === 'category' && item.name === button.textContent
        );

        if (!button.querySelector('.category-arrow')) {
            const arrow = document.createElement('span');
            arrow.classList.add('category-arrow');
            arrow.innerHTML = '<img src="assets/img/arrow.svg" class="category-arrow" alt="arrow" width="15" height="15">';
            button.appendChild(arrow);
        }
        
        if (categoryItem) {
            const isExpanded = !categoryItem.collapsed;
            button.dataset.toggled = isExpanded.toString();
            
            // Apply visual state based on actual data
            if (isExpanded) {
                button.classList.remove('topic-category-button-unselected');
                button.classList.add('topic-category-button-selected');
                currentlySelectedCategory = button;
                
                // Set arrow rotation for expanded state
                const arrow = button.querySelector('.category-arrow');
                if (arrow) {
                    arrow.style.transform = 'rotate(90deg)';
                    arrow.style.transition = 'transform 0.3s ease';
               }
            } else {
                button.classList.remove('topic-category-button-selected');
                button.classList.add('topic-category-button-unselected');
                
                // Set arrow rotation for collapsed state
                const arrow = button.querySelector('.category-arrow');
                if (arrow) {
                    arrow.style.transform = 'rotate(0deg)';
                    arrow.style.transition = 'transform 0.3s ease';
                }
            }
        } else {
            // Fallback: Initialize as not toggled if category not found
            button.dataset.toggled = 'false';
        }
        
        button.addEventListener('click', () => {
            // Check current toggle state before making changes
            const isToggled = button.dataset.toggled === 'true';
            const isSameButton = currentlySelectedCategory === button;
            
            // Deselect previously selected topic button (O(1))
            if (currentlySelectedTopic) {
                currentlySelectedTopic.classList.remove('topic-selected');
                currentlySelectedTopic.classList.add('topic-unselected');
                currentlySelectedTopic = null;
            }

            // If clicking the same category button that's already selected
            if (isSameButton) {
                if (isToggled) {
                    // Collapse and deselect
                    button.classList.remove('topic-category-button-selected');
                    button.classList.add('topic-category-button-unselected');
                    button.dataset.toggled = 'false';
                    currentlySelectedCategory = null;
                    onDeToggle(button);
                } else {
                    // Expand (keep selected)
                    button.dataset.toggled = 'true';
                    onToggle(button);
                }
            } else {
                // Clicking a different category button
                if (currentlySelectedCategory) {
                    currentlySelectedCategory.classList.remove('topic-category-button-selected');
                    currentlySelectedCategory.classList.add('topic-category-button-unselected');
                    currentlySelectedCategory.dataset.toggled = 'false';
                }

                // Select the clicked category button
                button.classList.remove('topic-category-button-unselected');
                button.classList.add('topic-category-button-selected');
                button.dataset.toggled = 'true';
                currentlySelectedCategory = button;
                onToggle(button);
            }
        });
    });

    /**
     * Set up click handlers for individual topic buttons after category buttons
     */
    topicButtons.forEach(button => {
        button.addEventListener('click', async () => {
            if (currentlySelectedTopic) {
                currentlySelectedTopic.classList.remove('topic-selected');
                currentlySelectedTopic.classList.add('topic-unselected');
            }

            if (currentlySelectedCategory) {
                currentlySelectedCategory.classList.remove('topic-category-button-selected');
                currentlySelectedCategory.classList.add('topic-category-button-unselected');
                currentlySelectedCategory.dataset.toggled = 'false';
                currentlySelectedCategory = null;
            }

            // Select the clicked topic button
            button.classList.remove('topic-unselected');
            button.classList.add('topic-selected');
            currentlySelectedTopic = button;

            const md = await converter.loadMarkdown('assets/' + button.dataset.path);
            const html = converter.convert(md);
            preview.innerHTML = html;
        });
    });

    // Select first topic-button on load if none selected and no category is expanded
    if (topicButtons.length > 0 && !currentlySelectedTopic && !currentlySelectedCategory) {
        topicButtons[0].classList.remove('topic-unselected');
        topicButtons[0].classList.add('topic-selected');
        currentlySelectedTopic = topicButtons[0];

        const md = await converter.loadMarkdown('assets/' + topicButtons[0].dataset.path);
        const html = converter.convert(md);
        preview.innerHTML = html;
    }
}

/**
 * Handle category expansion - rotates arrow to indicate expanded state
 * @param {HTMLElement} button - The category button that was toggled
 */
function onToggle(button) {
    console.log('Category toggled ON:', button.textContent); // DEBUG_STATEMENT
    // const arrow = button.querySelector('.category-arrow');
    // if (arrow) {
    //     arrow.style.transform = 'rotate(90deg)'; // Rotate arrow to point down
    //     arrow.style.transition = 'transform 0.3s ease';
    // }

    // Toggle category visibility
    const categoryItem = browser.contentStructure.find(item => item.name === button.textContent);
    if (categoryItem) {
        browser.changeCategoryVisibility(categoryItem, browserContainer);
    }
    refresh();
}

/**
 * Handle category collapse - rotates arrow back to indicate collapsed state
 * @param {HTMLElement} button - The category button that was toggled
 */
function onDeToggle(button) {
    console.log('Category toggled OFF:', button.textContent); // DEBUG_STATEMENT
    // const arrow = button.querySelector('.category-arrow');
    // if (arrow) {
    //     console.log("Found arrow");
    //     arrow.style.transform = 'rotate(0deg)'; // Rotate arrow back to point right
    //     arrow.style.transition = 'transform 0.3s ease';
    // }

    // Toggle category visibility
    const categoryItem = browser.contentStructure.find(item => item.name === button.textContent);
    if (categoryItem) {
        browser.changeCategoryVisibility(categoryItem, browserContainer);
    }
    refresh();
}

document.addEventListener("DOMContentLoaded", async (event) => {
    // Get reference to the topic browser container
    browserContainer = document.querySelector(".topic-selector");

    // Fetch and load topic structure
    await browser.fetchStructure('assets/content/content-structure.json'); // DEBUG_DATA
    await refresh();
}); 

// Refreshes MarkDown-Container and Topic-Container
async function refresh() {    
    // Populate container with the generated topics
    browserContainer.innerHTML = browser.generateTopicsHTML();
    // Re-setup event listeners after DOM regeneration
    setupEventListeners();
}