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
        let inCodeBlock = false;

        for (const line of lines) {
            if (/^```/.test(line)) {
                if (inCodeBlock) {
                    htmlLines.push('</pre>');
                    inCodeBlock = false;
                } else {
                    htmlLines.push('<pre class="markdown-code-block">');
                    inCodeBlock = true;
                }
            } else if (inCodeBlock) {
                htmlLines.push(this.escapeHtml(line));
            } else if (/^(\*|\-|\+)\s/.test(line)) {
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
        if (inCodeBlock) {
            htmlLines.push('</pre>');
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

        // Line breaks
        if (line.trim() === '') {
            return '<br>';
        }

        return `<p class="markdown-paragraph">${this.parseInline(line)}</p>`;
    }

    // Handle inline markdown elements
    parseInline(text) {
        return text
            .replace(/!\[([^\]]*)\]\(([^)]+)\)(\(([^)]+)\))?/g, (match, alt, src, _, dimensions) => {
                let imgTag = `<img src="${src}" alt="${alt}" class="markdown-image"`;
                
                if (dimensions) {
                    // Parse dimensions like "300x200" or "300" (width only)
                    const dimMatch = dimensions.match(/^(\d+)(?:x(\d+))?$/);
                    if (dimMatch) {
                        const width = dimMatch[1];
                        const height = dimMatch[2];
                        imgTag += ` width="${width}"`;
                        if (height) {
                            imgTag += ` height="${height}"`;
                        }
                    }
                }
                
                return imgTag + ' />';
            }) // Images with optional dimensions
            .replace(/\[\[([^\]]+)\]\(([^)]+)\)\]/g, '<span id="$2" class="markdown-button">$1</span>') // Button links with ID
            .replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank" class="markdown-link">$1</a>') // Links
            .replace(/\*\*(.*?)\*\*/g, '<strong class="markdown-bold">$1</strong>') // Bold
            .replace(/\*(.*?)\*/g, '<em class="markdown-italic">$1</em>') // Italic
            .replace(/`(.*?)`/g, '<code class="markdown-code">$1</code>'); // Inline code
    }

    // Escape HTML for code blocks
    escapeHtml(text) {
        return text
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#039;');
    }
}

export default MarkdownConverter;