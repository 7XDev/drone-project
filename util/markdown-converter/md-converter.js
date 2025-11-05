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
        const htmlLines = [];
        let inUnorderedList = false;
        let inOrderedList = false;

        for (const line of lines) {
            if (/^(\*|\-|\+)\s/.test(line)) {
                if (!inUnorderedList) {
                    htmlLines.push('<ul class="markdown-list">');
                    inUnorderedList = true;
                }

                if (inOrderedList) {
                    htmlLines.push('</ol>');
                    inOrderedList = false;
                }
                htmlLines.push(this.parseLine(line));
            } else if (/^\d+\.\s/.test(line)) {
                if (!inOrderedList) {
                    htmlLines.push('<ol class="markdown-ordered-list-spaced">');
                    inOrderedList = true;
                }
                if (inUnorderedList) {
                    htmlLines.push('</ul>'); 
                    inUnorderedList = false;
                }
                htmlLines.push(this.parseLine(line));
            } else {
                if (inUnorderedList) {
                    htmlLines.push('</ul>');
                    inUnorderedList = false;
                }
                if (inOrderedList) {
                    htmlLines.push('</ol>');
                    inOrderedList = false;
                }
                htmlLines.push(this.parseLine(line));
            }
        }

        if (inUnorderedList) {
            htmlLines.push('</ul>');
        }
        if (inOrderedList) {
            htmlLines.push('</ol>');
        }

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

        // Unordered Lists
        if (/^(\*|\-|\+)\s/.test(line)) {
            const content = line.replace(/^(\*|\-|\+)\s/, '');
            return `<li class="markdown-list-item">${this.parseInline(content)}</li>`;
        }

        // Ordered Lists
        if (/^\d+\.\s/.test(line)) {
            const content = line.replace(/^\d+\.\s/, '');
            return `<li class="markdown-ordered-list-item">${this.parseInline(content)}</li>`;
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