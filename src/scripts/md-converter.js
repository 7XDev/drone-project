class MarkdownConverter {
    constructor() {
        this.headingCount = 0;
    }

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
        this.headingCount = 0;
        const lines = md.split('\n');
        const htmlLines = [];
        let inUnorderedList = false;
        let inOrderedList = false;
        let inCodeBlock = false;
        let inTable = false;
        let tableHeaders = [];

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
            } else if (this.isTableRow(line)) {
                // Handle table rows
                if (!inTable) {
                    // Close any open lists
                    if (inUnorderedList) {
                        htmlLines.push('</ul>');
                        inUnorderedList = false;
                    }
                    if (inOrderedList) {
                        htmlLines.push('</ol>');
                        inOrderedList = false;
                    }
                    
                    // Check if next line is separator to determine if this is a header
                    const nextLineIndex = lines.indexOf(line) + 1;
                    const nextLine = nextLineIndex < lines.length ? lines[nextLineIndex] : '';
                    
                    if (this.isTableSeparator(nextLine)) {
                        // This is a header row
                        htmlLines.push('<table class="markdown-table">');
                        htmlLines.push('<thead>');
                        htmlLines.push(this.parseTableRow(line, true));
                        htmlLines.push('</thead>');
                        htmlLines.push('<tbody>');
                        inTable = true;
                        tableHeaders = this.extractTableHeaders(line);
                    } else {
                        // This is a regular table without headers
                        htmlLines.push('<table class="markdown-table">');
                        htmlLines.push('<tbody>');
                        htmlLines.push(this.parseTableRow(line, false));
                        inTable = true;
                    }
                } else {
                    // Continue table rows
                    if (!this.isTableSeparator(line)) {
                        htmlLines.push(this.parseTableRow(line, false));
                    }
                }
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
                if (inTable) {
                    htmlLines.push('</tbody>');
                    htmlLines.push('</table>');
                    inTable = false;
                    tableHeaders = [];
                }
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
        if (inTable) {
            htmlLines.push('</tbody>');
            htmlLines.push('</table>');
        }

        return this.processSignatureBlocks(htmlLines.join('\n'));
    }

    // Detect and convert each line
    parseLine(line) {
        // Custom signatures
        if (/^#\+h\s/.test(line)) {
            const content = line.replace(/^#\+h\s/, '');
            return `SIGNATURE_START:positive:heading:${this.parseInline(content)}`;
        }
        if (/^#\+b\s/.test(line)) {
            const content = line.replace(/^#\+b\s/, '');
            return `SIGNATURE_START:positive:body:${this.parseInline(content)}`;
        }

        if (/^#wh\s/.test(line)) {
            const content = line.replace(/^#wh\s/, '');
            return `SIGNATURE_START:warning:heading:${this.parseInline(content)}`;
        }
        if (/^#wb\s/.test(line)) {
            const content = line.replace(/^#wb\s/, '');
            return `SIGNATURE_START:warning:body:${this.parseInline(content)}`;
        }

        if (/^#-h\s/.test(line)) {
            const content = line.replace(/^#-h\s/, '');
            return `SIGNATURE_START:negative:heading:${this.parseInline(content)}`;
        }
        if (/^#-b\s/.test(line)) {
            const content = line.replace(/^#-b\s/, '');
            return `SIGNATURE_START:negative:body:${this.parseInline(content)}`;
        }

        if (/^#ih\s/.test(line)) {
            const content = line.replace(/^#ih\s/, '');
            return `SIGNATURE_START:info:heading:${this.parseInline(content)}`;
        }
        if (/^#ib\s/.test(line)) {
            const content = line.replace(/^#ib\s/, '');
            return `SIGNATURE_START:info:body:${this.parseInline(content)}`;
        }

        if (/^#bh\(([^)]+)\)\s/.test(line)) {
            const match = line.match(/^#bh\(([^)]+)\)\s(.*)$/);
            const id = match[1];
            const content = match[2];
            return `SIGNATURE_START:button:heading:${this.parseInline(content)}:${id}`;
        }
        if (/^#bb\s/.test(line)) {
            const content = line.replace(/^#bb\s/, '');
            return `SIGNATURE_START:button:body:${this.parseInline(content)}`;
        }

        // Headings
        if (/^#{1,6}\s/.test(line)) {
            const level = line.match(/^#{1,6}/)[0].length;
            const content = line.replace(/^#{1,6}\s/, '');
            const headingId = this.headingCount++;
            return `<h${level} id="${headingId}" class="markdown-heading">${this.parseInline(content)}</h${level}>`;
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

        // End signature
        if (line.trim() === '#end') {
            return `<div class="markdown-end-container">
                        <div class="markdown-end-previous">
                            <p>Previous</p>
                        </div>
                        <div class="markdown-end-next">
                            <p>Next</p>
                        </div>
                    </div>`;
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
            .replace(/~~(.*?)~~/g, '<del class="markdown-strikethrough">$1</del>') // Strikethrough
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

    // Process and merge signature blocks
    processSignatureBlocks(html) {
        const lines = html.split('\n');
        const result = [];
        let currentSignature = null;

        for (const line of lines) {
            if (line.startsWith('SIGNATURE_START:')) {
                const parts = line.split(':');
                const type = parts[1];
                const part = parts[2];
                const content = parts[3];
                const id = parts[4]; // For button signatures with ID
                
                if (currentSignature && currentSignature.type === type) {
                    // Add to existing signature
                    if (part === 'heading') {
                        currentSignature.heading = content;
                        if (id) currentSignature.id = id;
                    } else if (part === 'body') {
                        currentSignature.bodies.push(content);
                    }
                } else {
                    // Finish previous signature if exists
                    if (currentSignature) {
                        result.push(this.generateSignatureHTML(currentSignature));
                    }
                    
                    // Start new signature
                    currentSignature = {
                        type: type,
                        heading: part === 'heading' ? content : null,
                        bodies: part === 'body' ? [content] : [],
                        id: id || null
                    };
                }
            } else {
                // Finish current signature if exists
                if (currentSignature) {
                    result.push(this.generateSignatureHTML(currentSignature));
                    currentSignature = null;
                }
                result.push(line);
            }
        }

        // Finish final signature if exists
        if (currentSignature) {
            result.push(this.generateSignatureHTML(currentSignature));
        }

        return result.join('\n');
    }

    // Generate HTML for a signature block
    generateSignatureHTML(signature) {
        if (signature.type === 'button') {
            // Special handling for button signatures
            let html = `<div class="markdown-button-container"${signature.id ? ` id="${signature.id}"` : ''}>`;
            
            if (signature.heading) {
                html += `<div class="markdown-button-heading">${signature.heading}</div>`;
            }
            
            for (const body of signature.bodies) {
                html += `<div class="markdown-button-body">${body}</div>`;
            }
            
            html += '</div>';
            return html;
        } else {
            // Standard signature handling for other types
            let html = `<div class="markdown-${signature.type}">`;
            
            if (signature.heading) {
                html += `<h4 class="markdown-signature-heading">${signature.heading}</h4>`;
            }
            
            for (const body of signature.bodies) {
                html += `<p class="markdown-signature-body">${body}</p>`;
            }
            
            html += '</div>';
            return html;
        }
    }

    // Check if line is a table row
    isTableRow(line) {
        return line.trim().includes('|') && line.trim().length > 0;
    }

    // Check if line is a table separator (|---|---|)
    isTableSeparator(line) {
        return /^\s*\|[\s\-\|:]+\|\s*$/.test(line);
    }

    // Extract table headers for reference
    extractTableHeaders(line) {
        return line.split('|').map(cell => cell.trim()).filter(cell => cell.length > 0);
    }

    // Parse a table row into HTML
    parseTableRow(line, isHeader) {
        const cells = line.split('|').map(cell => cell.trim()).filter(cell => cell.length > 0);
        const tag = isHeader ? 'th' : 'td';
        const className = isHeader ? 'markdown-table-header' : 'markdown-table-cell';
        
        let html = '<tr class="markdown-table-row">';
        for (const cell of cells) {
            html += `<${tag} class="${className}">${this.parseInline(cell)}</${tag}>`;
        }
        html += '</tr>';
        
        return html;
    }
}

export default MarkdownConverter;