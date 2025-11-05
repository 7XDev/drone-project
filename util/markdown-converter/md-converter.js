class MarkdownConverter {
    constructor() {}

    // Load markdown content from a given path
    async loadMarkdown(path) {
        const response = await fetch(path);
        if (!response.ok) {
            console.error(`Failed to load markdown file: ${response.statusText}`);
            return '';
        }
        return await response.text();
    }

    // Convert markdown text to HTML
    convert(md) {
        const lines = md.split('\n');
        const htmlLines = lines.map(line => this.parseLine(line));
        return htmlLines.join('\n');
    }

    // Detect and convert each line
    parseLine(line) {
        // Headings
        if (/^#{1,6}\s/.test(line)) {
            const level = line.match(/^#{1,6}/)[0].length;
            const content = line.replace(/^#{1,6}\s/, '');
            return `<h${level} class="markdown-heading">${this.parseInline(content)}</h${level}>`;
        }

        // Lists
        if (/^(\*|\-|\+)\s/.test(line)) {
            const content = line.replace(/^(\*|\-|\+)\s/, '');
            return `<li class="markdown-list-item">${this.parseInline(content)}</li>`;
        }

        // TODO Code blocks, blockquotes
        
        // Line breaks
        if (line.trim() === '') {
            return '<br>';
        }

        return `<p class="markdown-paragraph">${this.parseInline(line)}</p>`;
    }

    // Handle inline markdown elements
    parseInline(text) {
        return text
            .replace(/\*\*(.*?)\*\*/g, '<strong class="markdown-bold">$1</strong>') // Bold
            .replace(/\*(.*?)\*/g, '<em class="markdown-italic">$1</em>') // Italic
            .replace(/`(.*?)`/g, '<code class="markdown-code">$1</code>'); // Inline code
    }
}