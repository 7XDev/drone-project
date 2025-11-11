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

async function GetMarkdownHeaders(path) {
    const headings = [];
    let id = 0;
    const markdown = await converter.loadMarkdown(path);
    const lines = markdown.split('\n');

    for(const line of lines) {
        const match = line.match(/^(#{1,6})\s+(.+)$/);

        if(match) {
            const text = match[2].trim();
            headings.push({ text, id });
            id++;
        }
    }

    return headings;
}

async function GenerateHtmlRightHeader(headings) {
    let html = '<ul class="right-panel-list">';
    headings.forEach(heading => {
        html += `<li class="right-bar-items"><a href="#${heading.id}">${heading.text}</a></li>`;
    });
    html += '</ul>';
    return html;
}

/**
 * Recursively find a category by name in the content structure
 * @param {Array} structure - The content structure to search
 * @param {string} name - The name of the category to find
 * @returns {Object|null} - The category item or null if not found
 */
function findCategoryByName(structure, name) {
    for (const item of structure) {
        if (item.type === 'category' && item.name === name) {
            return item;
        }
        if (item.children) {
            const found = findCategoryByName(item.children, name);
            if (found) return found;
        }
    }
    return null;
}

/**
 * Generate HTML for a category's children
 * @param {Object} categoryItem - The category item whose children to generate HTML for
 * @returns {string} - HTML string for the children
 */
function generateCategoryChildrenHTML(categoryItem) {
    let html = '';
    
    const traverse = (items, parentIsCategoryButton = false) => {
        items.forEach(item => {
            if (item.type === 'page') {
                // Generate HTML for individual pages
                const categoryClass = parentIsCategoryButton ? 'topic-under-category' : '';
                html += `<p class="topic-unselected topic-button ${categoryClass}" id="topic-button-${item.name}" data-path="${item.path}">${item.name}</p>`;
            } else if (item.type === 'category') {
                // Check if this category is a category button (has path property)
                const isCategoryButton = !!item.path;
                
                // Generate HTML for categories that are not collapsed
                if (item.path) {
                    html += `<p class="topic-category-button-unselected topic-category-button" id="topic-category-topic-${item.name}">${item.name}</p>`
                } else if (!item.collapsed) {
                    html += `<p class="topic-category" id="topic-category-${item.name}">${item.name}</p>`;
                }

                // Recursively generate HTML for child items
                if (item.children && !item.collapsed) {
                    traverse(item.children, isCategoryButton);
                }
            }
        });
    };

    if (categoryItem.children && !categoryItem.collapsed) {
        traverse(categoryItem.children, !!categoryItem.path);
    }
    
    return html;
}

/**
 * Remove all DOM elements that belong to a specific category's children
 * @param {HTMLElement} categoryButton - The category button element
 * @param {Object} categoryItem - The category item from the data structure
 */
function removeCategoryChildren(categoryButton, categoryItem) {
    // Get all the IDs of elements that should be removed
    const elementsToRemove = [];
    
    const collectElementIds = (items) => {
        items.forEach(item => {
            if (item.type === 'page') {
                elementsToRemove.push(`topic-button-${item.name}`);
            } else if (item.type === 'category') {
                if (item.path) {
                    elementsToRemove.push(`topic-category-topic-${item.name}`);
                } else {
                    elementsToRemove.push(`topic-category-${item.name}`);
                }
                if (item.children) {
                    collectElementIds(item.children);
                }
            }
        });
    };
    
    if (categoryItem.children) {
        collectElementIds(categoryItem.children);
    }
    
    // Remove all collected elements from the DOM
    elementsToRemove.forEach(id => {
        const element = document.getElementById(id);
        if (element) {
            element.remove();
        }
    });
}

/**
 * Set up event listeners for newly added elements without affecting existing ones
 */
function setupEventListenersForNewElements() {
    // Get only the buttons that don't already have event listeners
    const newTopicButtons = document.querySelectorAll('.topic-button:not([data-has-listener])');
    const newCategoryButtons = document.querySelectorAll('.topic-category-button:not([data-has-listener])');

    // Set up event listeners for new category buttons
    newCategoryButtons.forEach(button => {
        button.setAttribute('data-has-listener', 'true');
        
        // Add arrow if it doesn't exist
        if (!button.querySelector('.category-arrow')) {
            const arrow = document.createElement('span');
            arrow.classList.add('category-arrow');
            arrow.innerHTML = '<img src="assets/img/arrow.svg" class="category-arrow" alt="arrow" width="15" height="15">';
            button.appendChild(arrow);
        }
        
        // Initialize state
        button.dataset.toggled = 'false';
        
        button.addEventListener('click', () => {
            const isToggled = button.dataset.toggled === 'true';
            const isSameButton = currentlySelectedCategory === button;
            
            // Deselect previously selected topic button (O(1))
            if (currentlySelectedTopic) {
                currentlySelectedTopic.classList.remove('topic-selected');
                currentlySelectedTopic.classList.add('topic-unselected');
                currentlySelectedTopic = null;
            }

            if (isSameButton) {
                if (isToggled) {
                    button.classList.remove('topic-category-button-selected');
                    button.classList.add('topic-category-button-unselected');
                    button.dataset.toggled = 'false';
                    currentlySelectedCategory = null;
                    onDeToggle(button);
                } else {
                    button.dataset.toggled = 'true';
                    onToggle(button);
                }
            } else {
                if (currentlySelectedCategory) {
                    currentlySelectedCategory.classList.remove('topic-category-button-selected');
                    currentlySelectedCategory.classList.add('topic-category-button-unselected');
                    currentlySelectedCategory.dataset.toggled = 'false';
                }

                button.classList.remove('topic-category-button-unselected');
                button.classList.add('topic-category-button-selected');
                button.dataset.toggled = 'true';
                currentlySelectedCategory = button;
                onToggle(button);
            }
        });
    });

    // Set up event listeners for new topic buttons
    newTopicButtons.forEach(button => {
        button.setAttribute('data-has-listener', 'true');
        
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

            button.classList.remove('topic-unselected');
            button.classList.add('topic-selected');
            currentlySelectedTopic = button;

            const md = await converter.loadMarkdown('assets/' + button.dataset.path);
            const html = converter.convert(md);
            preview.innerHTML = html;
        });
    });
}

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
        button.setAttribute('data-has-listener', 'true');
        console.log();
        // Load the current state from content browser and apply it
        const categoryItem = findCategoryByName(browser.contentStructure, button.textContent);

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
                
                // Set arrow rotation for expanded state without animation on initial load
                const arrow = button.querySelector('.category-arrow');
                if (arrow) {
                    arrow.style.transform = 'rotate(90deg)';
                    arrow.style.transition = 'none'; // No animation on initial load
                    // Re-enable animation after a brief delay
                    setTimeout(() => {
                        arrow.style.transition = 'transform 0.3s ease';
                    }, 10);
               }
            } else {
                button.classList.remove('topic-category-button-selected');
                button.classList.add('topic-category-button-unselected');
                
                // Set arrow rotation for collapsed state without animation on initial load
                const arrow = button.querySelector('.category-arrow');
                if (arrow) {
                    arrow.style.transform = 'rotate(0deg)';
                    arrow.style.transition = 'none'; // No animation on initial load
                    // Re-enable animation after a brief delay
                    setTimeout(() => {
                        arrow.style.transition = 'transform 0.3s ease';
                    }, 10);
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
        button.setAttribute('data-has-listener', 'true');
        
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

            const rightPanelHeader = document.getElementById("right-panel-header");
            const headings = await GetMarkdownHeaders('assets/' + button.dataset.path);
            rightPanelHeader.innerHTML = await GenerateHtmlRightHeader(headings);
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

        const rightPanelHeader = document.getElementById("right-panel-header");
        const headings = await GetMarkdownHeaders('assets/' + topicButtons[0].dataset.path);
        rightPanelHeader.innerHTML = await GenerateHtmlRightHeader(headings);
    }
}

/**
 * Handle category expansion - rotates arrow to indicate expanded state
 * @param {HTMLElement} button - The category button that was toggled
 */
function onToggle(button) {
    console.log('Category toggled ON:', button.textContent); // DEBUG_STATEMENT
    const arrow = button.querySelector('.category-arrow');
    if (arrow) {
        arrow.style.transform = 'rotate(90deg)'; // Rotate arrow to point down
        arrow.style.transition = 'transform 0.3s ease';
    }

    // Toggle category visibility in data structure
    const categoryItem = findCategoryByName(browser.contentStructure, button.textContent);
    if (categoryItem) {
        categoryItem.collapsed = !categoryItem.collapsed; // Toggle the collapsed state
        
        // Generate HTML for just this category's children
        const childrenHTML = generateCategoryChildrenHTML(categoryItem);
        
        // Insert the children HTML after this button
        const nextSibling = button.nextElementSibling;
        const tempDiv = document.createElement('div');
        tempDiv.innerHTML = childrenHTML;
        
        // Insert all children after the button
        while (tempDiv.firstChild) {
            if (nextSibling) {
                browserContainer.insertBefore(tempDiv.firstChild, nextSibling);
            } else {
                browserContainer.appendChild(tempDiv.firstChild);
            }
        }
        
        // Set up event listeners for the newly added elements
        setupEventListenersForNewElements();
    }
}

/**
 * Handle category collapse - rotates arrow back to indicate collapsed state
 * @param {HTMLElement} button - The category button that was toggled
 */
function onDeToggle(button) {
    console.log('Category toggled OFF:', button.textContent); // DEBUG_STATEMENT
    const arrow = button.querySelector('.category-arrow');
    if (arrow) {
        console.log("Found arrow");
        arrow.style.transform = 'rotate(0deg)'; // Rotate arrow back to point right
        arrow.style.transition = 'transform 0.3s ease';
    }

    // Toggle category visibility in data structure
    const categoryItem = findCategoryByName(browser.contentStructure, button.textContent);
    if (categoryItem) {
        categoryItem.collapsed = !categoryItem.collapsed; // Toggle the collapsed state
        
        // Remove all children elements that belong to this category
        removeCategoryChildren(button, categoryItem);
    }
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