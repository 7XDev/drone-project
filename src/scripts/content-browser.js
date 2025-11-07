class ContentBrowser {
    contentStructure;

    constructor() {
        this.contentStructure = [];
    }

    async fetchStructure(path) {
        const res = await fetch(path);
        if (!res.ok) {
            console.error(`Failed to fetch content structure: ${res.statusText}`);
            return null;
        }
        this.contentStructure = await res.json();
    }

    generateTopicsHTML(structure = this.contentStructure) {
        let html = '';

        const traverse = (items) => {
            items.forEach(item => {
                if (item.type === 'page') {
                    html += `<p class="topic-unselected topic-button" id="topic-button-${item.name}">${item.name}</p>`;
                } else if (item.type === 'category' && !item.collapsed) {
                    if (item.path) {
                        html += `<p class="topic-category-button-unselected topic-category-button" id="topic-category-topic-${item.name}">${item.name}</p>`
                    } else {
                        html += `<p class="topic-category" id="topic-category-${item.name}">${item.name}</p>`;
                    }
                    
                    if (item.children) {
                        traverse(item.children);
                    }
                }
            });
        };

        traverse(structure);
        return html;
    }

    changeCategoryVisibility(item, container) {
        if (item.type === 'category') {
            item.collapsed = !item.collapsed;
        }

        if (container) {
            container.innerHTML = this.generateTopicsHTML();
        }
    }

    async getStructure() {
        return this.generateTopicsHTML();
    }
}

export default ContentBrowser;