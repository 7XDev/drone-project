class ContentBrowser {
    contentStructure;

    constructor() {
        this.contentStructure = contentStructure;
    }

    async fetchStructure(path) {
        const res = await fetch(path);
        if (!res.ok) {
            console.error(`Failed to fetch content structure: ${res.statusText}`);
            return null;
        }
        this.contentStructure = await res.json();
    }

    async getStructure() {
        return '';
    }
}