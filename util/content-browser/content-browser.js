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
        console.log(this.contentStructure);
    }

    generateTopicsHTML(structure = this.contentStructure) {
        let html = '';

        const traverse = (items) => {
            items.forEach(item => {
                if (item.type === 'page') {
                    html += `<p class="topic-unselected hyper-button">${item.name}</p>`;
                } else if (item.type === 'category' && item.children) {
                    traverse(item.children);
                }
            });
        };

        traverse(structure);
        return html;
    }

    async getStructure() {
        return this.generateTopicsHTML();
    }
}

// Showcase purposes
export default ContentBrowser;