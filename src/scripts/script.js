// Import necessary modules for content browsing and markdown conversion
import MarkdownConverter from './md-converter.js';
import ContentBrowser from './content-browser.js';

// Global variable to store current markdown content
let currentMarkdownContent = ""; // for copy button

// Global content browser instance
let browser = new ContentBrowser;

// Global markdown converter instance
let converter = new MarkdownConverter;
const preview = document.getElementById('markdown-container');

let browserContainer;

// Track currently selected elements for O(1) deselection
let currentlySelectedTopic = null;
let currentlySelectedCategory = null;

// Assign mode switch variable to the button for improved onclick event
window.lightDarkModeToggle = lightDarkModeToggle;

// Make function available globally for onclick handler
window.copyButtonTrigger = copyButtonTrigger;

/**
 * Gets all the markdown headings to display in the right panel
 * @param {Path} path 
 * @returns 
 */
async function getMarkdownHeaders(path) {
    let headings = [];
    let id = 0;
    let markdown = await converter.loadMarkdown(path);
    
    for (let line of markdown.split('\n')) {
        // Trim line to remove any trailing whitespace including \r on Windows
        line = line.trim();
        let match = line.match(/^(#{1,6})\s+(.+)$/);    // Look for heading in line

        if (match) {
            // Add match to heading array
            const text = match[2].trim();
            headings.push({ text, id });
            id++;
        }
    }

    return headings;
}

/**
 * Toggle between light mode and dark mode
 */
function lightDarkModeToggle() {
    const html = document.documentElement;
    const toggleButton = document.getElementById("lightDarkToggle");

    html.classList.toggle("dark-mode"); // Change page appearance

    // Toggle mode switch button content
    if (html.classList.contains("dark-mode")) {
        toggleButton.textContent = "Light Mode";
        localStorage.setItem("theme", "dark");
    } else {
        toggleButton.textContent = "Dark Mode";
        localStorage.setItem("theme", "light");
    }
}

/**
 * Generate the header list with the buttons to skip to certain header in the right hand side bar
 * @param {HTMLHeadElement[]} headings Array containing the headers 
 * @returns The generated html
 */
function generateHtmlRightHeader(headings) {
    let html = '<ul class="right-bar-list">';
    headings.forEach(heading => {
        html += `<li class="right-bar-items"><a href="#${heading.id}">${heading.text}</a></li>`;
    });
    html += '</ul>';
    return html;
}

/**
 * Set up event listeners for right panel navigation links to scroll within the display window
 * @param {HTMLElement} rightPanelHeader - The right panel header element containing the links
 */
function setupRightPanelListeners(rightPanelHeader) {
    const links = rightPanelHeader.querySelectorAll('a');
    links.forEach(link => {
        link.addEventListener('click', (e) => {
            e.preventDefault();
            const targetId = link.getAttribute('href').substring(1);
            const targetElement = document.getElementById(targetId);
            if (targetElement) {
                const container = document.querySelector('.display-window');
                const rect = targetElement.getBoundingClientRect();
                const containerRect = container.getBoundingClientRect();
                const scrollTop = container.scrollTop + (rect.top - containerRect.top);
                container.scrollTo({ top: scrollTop, behavior: 'smooth' });
            }
        });
    });
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
                
                // Generate HTML for all categories - let DOM state management handle visibility
                if (item.path) {
                    html += `<p class="topic-category-button-unselected topic-category-button" id="topic-category-topic-${item.name}">${item.name}</p>`
                } else {
                    html += `<p class="topic-category" id="topic-category-${item.name}">${item.name}</p>`;
                }

                // Always include children in HTML generation - DOM will control visibility
                if (item.children) {
                    traverse(item.children, isCategoryButton);
                }
            }
        });
    };

    if (categoryItem.children) {
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
        
        button.addEventListener('click', async () => {
            const isToggled = button.dataset.toggled === 'true';
            
            // Deselect previously selected topic button (O(1))
            if (currentlySelectedTopic) {
                currentlySelectedTopic.classList.remove('topic-selected');
                currentlySelectedTopic.classList.add('topic-unselected');
                currentlySelectedTopic = null;
            }

            // Toggle this category independently
            if (isToggled) {
                // Collapse this category
                button.classList.remove('topic-category-button-expanded');
                button.classList.add('topic-category-button-collapsed');
                button.dataset.toggled = 'false';
                if (currentlySelectedCategory === button) {
                    currentlySelectedCategory.classList.remove('topic-category-button-selected');
                    currentlySelectedCategory = null;
                }
                onDeToggle(button);
            } else {
                // Expand this category
                button.classList.remove('topic-category-button-collapsed');
                button.classList.add('topic-category-button-expanded');
                button.dataset.toggled = 'true';
                await onToggle(button);
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

            // Clear the currently selected category for content display tracking
            if (currentlySelectedCategory) {
                currentlySelectedCategory.classList.remove('topic-category-button-selected');
                currentlySelectedCategory = null;
            }

            button.classList.remove('topic-unselected');
            button.classList.add('topic-selected');
            currentlySelectedTopic = button;

            const md = await converter.loadMarkdown('assets/' + button.dataset.path);
            currentMarkdownContent = md; // for copy button
            const html = converter.convert(md);
            preview.innerHTML = html;

            const rightPanelHeader = document.getElementById("right-panel-header");
            const headings = await getMarkdownHeaders('assets/' + button.dataset.path);
            rightPanelHeader.innerHTML = await generateHtmlRightHeader(headings);
            setupRightPanelListeners(rightPanelHeader);
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
            
            // Check if children exist in DOM to determine actual UI state
            let hasChildrenInDOM = false;
            if (categoryItem.children && categoryItem.children.length > 0) {
                const firstChild = categoryItem.children[0];
                const firstChildId = firstChild.type === 'page' 
                    ? `topic-button-${firstChild.name}` 
                    : (firstChild.path ? `topic-category-topic-${firstChild.name}` : `topic-category-${firstChild.name}`);
                hasChildrenInDOM = document.getElementById(firstChildId) !== null;
            }
            
            // Set button state based on whether children are visible in DOM
            button.dataset.toggled = hasChildrenInDOM.toString();
            
            // Apply visual state based on DOM state (not data structure)
            if (hasChildrenInDOM) {
                button.classList.remove('topic-category-button-collapsed');
                button.classList.add('topic-category-button-expanded');
                
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
                button.classList.remove('topic-category-button-expanded');
                button.classList.add('topic-category-button-collapsed');
                
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
        
        button.addEventListener('click', async () => {
            // Check current toggle state before making changes
            const isToggled = button.dataset.toggled === 'true';
            
            // Deselect previously selected topic button (O(1))
            if (currentlySelectedTopic) {
                currentlySelectedTopic.classList.remove('topic-selected');
                currentlySelectedTopic.classList.add('topic-unselected');
                currentlySelectedTopic = null;
            }

            // Toggle this category independently
            if (isToggled) {
                // Collapse this category
                button.classList.remove('topic-category-button-expanded');
                button.classList.add('topic-category-button-collapsed');
                button.dataset.toggled = 'false';
                if (currentlySelectedCategory === button) {
                    currentlySelectedCategory.classList.remove('topic-category-button-selected');
                    currentlySelectedCategory = null;
                }
                onDeToggle(button);
            } else {
                // Expand this category
                button.classList.remove('topic-category-button-collapsed');
                button.classList.add('topic-category-button-expanded');
                button.dataset.toggled = 'true';
                await onToggle(button);
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

            // Clear the currently selected category for content display tracking
            if (currentlySelectedCategory) {
                currentlySelectedCategory.classList.remove('topic-category-button-selected');
                currentlySelectedCategory = null;
            }

            // Select the clicked topic button
            button.classList.remove('topic-unselected');
            button.classList.add('topic-selected');
            currentlySelectedTopic = button;

            const md = await converter.loadMarkdown('assets/' + button.dataset.path);
            currentMarkdownContent = md; // for copy button
            const html = converter.convert(md);
            preview.innerHTML = html;

            const rightPanelHeader = document.getElementById("right-panel-header");
            const headings = await getMarkdownHeaders('assets/' + button.dataset.path);
            rightPanelHeader.innerHTML = await generateHtmlRightHeader(headings);
            setupRightPanelListeners(rightPanelHeader);
        });
    });

    // Select first topic-button on load if none selected and no category is expanded
    if (topicButtons.length > 0 && !currentlySelectedTopic && !currentlySelectedCategory) {
        topicButtons[0].classList.remove('topic-unselected');
        topicButtons[0].classList.add('topic-selected');
        currentlySelectedTopic = topicButtons[0];

        const md = await converter.loadMarkdown('assets/' + topicButtons[0].dataset.path);
        currentMarkdownContent = md; // for copy button
        const html = converter.convert(md);
        preview.innerHTML = html;

        const rightPanelHeader = document.getElementById("right-panel-header");
        const headings = await getMarkdownHeaders('assets/' + topicButtons[0].dataset.path);
        rightPanelHeader.innerHTML = await generateHtmlRightHeader(headings);
        setupRightPanelListeners(rightPanelHeader);
    }
}

/**
 * Handle category expansion - rotates arrow to indicate expanded state
 * @param {HTMLElement} button - The category button that was toggled
 */
async function onToggle(button) {
    console.log('Category toggled ON:', button.textContent); // DEBUG_STATEMENT
    const arrow = button.querySelector('.category-arrow');
    if (arrow) {
        arrow.style.transform = 'rotate(90deg)'; // Rotate arrow to point down
        arrow.style.transition = 'transform 0.3s ease';
    }

    // Find category but don't modify its collapsed state
    const categoryItem = findCategoryByName(browser.contentStructure, button.textContent);
    if (categoryItem) {
        // If this category has a path, load its markdown content and mark as selected
        if (categoryItem.path) {
            // Clear previous category selection
            if (currentlySelectedCategory && currentlySelectedCategory !== button) {
                currentlySelectedCategory.classList.remove('topic-category-button-selected');
            }
            
            // Mark this category as selected for content display
            button.classList.add('topic-category-button-selected');
            currentlySelectedCategory = button;
            
            const md = await converter.loadMarkdown('assets/' + categoryItem.path);
            currentMarkdownContent = md; // for copy button
            const html = converter.convert(md);
            preview.innerHTML = html;

            const rightPanelHeader = document.getElementById("right-panel-header");
            const headings = await getMarkdownHeaders('assets/' + categoryItem.path);
            rightPanelHeader.innerHTML = await generateHtmlRightHeader(headings);
            setupRightPanelListeners(rightPanelHeader);
        }
        
        // Check if children already exist in the DOM to prevent duplicates
        let hasExistingChildren = false;
        if (categoryItem.children && categoryItem.children.length > 0) {
            const firstChild = categoryItem.children[0];
            const firstChildId = firstChild.type === 'page' 
                ? `topic-button-${firstChild.name}` 
                : (firstChild.path ? `topic-category-topic-${firstChild.name}` : `topic-category-${firstChild.name}`);
            hasExistingChildren = document.getElementById(firstChildId) !== null;
        }
        
        // Only insert children if they don't already exist
        if (!hasExistingChildren) {
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

    // Find category but don't modify its collapsed state - only manage DOM
    const categoryItem = findCategoryByName(browser.contentStructure, button.textContent);
    if (categoryItem) {
        // Remove all children elements that belong to this category from DOM only
        removeCategoryChildren(button, categoryItem);
    }
}

/* 
Trigger the copy button to copy the current page content into the clipboard
**/
async function copyButtonTrigger() {
    try {
        await navigator.clipboard.writeText(currentMarkdownContent);
    } catch (err) {
        console.error('Failed to copy text: ', err);
    }
};

// Refreshes MarkDown-Container and Topic-Container
async function refresh() {    
    // Populate container with the generated topics
    browserContainer.innerHTML = browser.generateTopicsHTML();
    // Re-setup event listeners after DOM regeneration
    setupEventListeners();
}

function selectInitialLoadedTopic(path) {
    converter.loadMarkdown(path).then(md => {
        currentMarkdownContent = md; // for copy button
        const html = converter.convert(md);
        preview.innerHTML = html;
    });

    const rightPanelHeader = document.getElementById("right-panel-header");
    getMarkdownHeaders(path).then(headings => {
        const html = generateHtmlRightHeader(headings);
        rightPanelHeader.innerHTML = html;
        setupRightPanelListeners(rightPanelHeader);
    });

    const dataPath = path.replace('assets/', '');
    const targetButton = document.querySelector(`[data-path="${dataPath}"]`);
    targetButton.classList.remove('topic-unselected');
    targetButton.classList.add('topic-selected');
    currentlySelectedTopic = targetButton;
}

document.addEventListener("DOMContentLoaded", async (event) => {
    
    // Get reference to the topic browser container
    browserContainer = document.querySelector(".topic-selector");

    // Fetch and load topic structure
    await browser.fetchStructure('assets/content/content-structure.json'); // DEBUG_DATA

    await refresh();
    
    selectInitialLoadedTopic('assets/content/introduction.md');

    const savedTheme = localStorage.getItem("theme");
    if (savedTheme === "dark") {
        const toggleButton = document.getElementById("lightDarkToggle");
        if (toggleButton) {
            toggleButton.textContent = "Light Mode";
        }
    }   
}); 