// Class to manage the content structure and its rendering
class ContentBrowser {
    contentStructure; // Holds the hierarchical structure of topics and categories

    // Constructor initializes an empty content structure
    constructor() {
        this.contentStructure = [];
    }

    /**
     * Fetches the content structure from a JSON file at the given path
     * @param {string} path - Path to the JSON file containing the content structure
     */
    async fetchStructure(path) {
        const res = await fetch(path);
        if (!res.ok) {
            console.error(`Failed to fetch content structure: ${res.statusText}`);
            return null;
        }
        this.contentStructure = await res.json(); // Parse and store the content structure
    }

    /**
     * Generates HTML for the topics and categories based on the content structure
     * @param {Array} structure - Optional parameter to provide a custom structure; defaults to the main content structure
     * @returns {string} - HTML string representing the topics and categories
     */
    generateTopicsHTML(structure = this.contentStructure) {
        let html = '';

        // Recursive function to traverse the content structure and generate HTML
        const traverse = (items, parentIsCategoryButton = false) => {
            items.forEach(item => {
                if (item.type === 'page') {
                    // Generate HTML for individual pages
                    // Add special class only if direct parent is a category button (has path)
                    const categoryClass = parentIsCategoryButton ? 'topic-under-category' : '';
                    html += `<p class="topic-unselected topic-button ${categoryClass}" id="topic-button-${item.name}" data-path="${item.path}">${item.name}</p>`;
                } else if (item.type === 'category') {
                    // Check if this category is a category button (has path property)
                    const isCategoryButton = !!item.path;
                    
                    // Generate HTML for categories that are not collapsed
                    if (item.path) {
                        html += `<p class="topic-category-button-collapsed topic-category-button" id="topic-category-topic-${item.name}">${item.name}</p>`
                    } else if (!item.collapsed) {
                        html += `<p class="topic-category" id="topic-category-${item.name}">${item.name}</p>`;
                    }

                    // Recursively generate HTML for child items
                    // Pass true only if THIS category is a category button
                    if (item.children && !item.collapsed) {
                        traverse(item.children, isCategoryButton);
                    }
                }
            });
        };

        traverse(structure); // Start traversal from the root structure
        return html;
    }

    /**
     * Toggles the visibility of a category (collapsed/expanded) and updates the container
     * @param {Object} item - The category item to toggle
     * @param {HTMLElement} container - The HTML container to update with the new structure
     */
    changeCategoryVisibility(item, container) {
        if (item.type === 'category') {
            item.collapsed = !item.collapsed; // Toggle the collapsed state
        }

        if (container) {
            // Regenerate and update the HTML in the container
            container.innerHTML = this.generateTopicsHTML();
        }
    }

    /**
     * Returns the generated HTML for the current content structure
     * @returns {string} - HTML string representing the topics and categories
     */
    async getStructure() {
        return this.generateTopicsHTML();
    }

    /**
     * Flattens the hierarchical content structure into a flat array of items with their paths
     * @param {Array} structure - Optional parameter to provide a custom structure; defaults to the main content structure
     * @returns {Array} - Flat array of items with their paths
     */
    flattenStructure(structure = this.contentStructure) {
        const flatStructure = [];

        // Recursive function to traverse the structure and flatten it
        const traverse = (items, parentPath = '') => {
            items.forEach(item => {
                const currentPath = parentPath ? `${parentPath}/${item.name}` : item.name;

                if (item.path) {
                    flatStructure.push(item.path); 
                }

                if (item.type === 'category' && item.children) {
                    // Recursively traverse categories
                    traverse(item.children, currentPath);
                }
            });
        };

        traverse(structure); // Start traversal from the root structure
        return flatStructure;
    }
}

export default ContentBrowser;