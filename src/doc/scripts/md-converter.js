class MarkdownConverter {
	headingCount;
	flatStructure;
	contentStructure;
	currentPageIndex;
	codeBlocks;

	constructor(flatStructure, contentStructure = []) {
		this.headingCount = 0;
		this.currentPageIndex = 0;
		this.flatStructure = flatStructure;
		this.contentStructure = contentStructure;
		this.codeBlocks = [];
	}

	// Load markdown content from a given path
	async loadMarkdown(path) {
		const response = await fetch(path);
		if (!response.ok) {
			console.error(`Failed to load markdown file: ${response.statusText}`);
			return "";
		}
		return await response.text();
	}

	// Convert markdown text to HTML
	convert(md, path) {
		this.currentPageIndex = this.flatStructure.indexOf(path);

		// Set up values for different conversion conditions
		this.headingCount = 0;
		this.codeBlocks = [];
		const lines = md.split("\n");
		const htmlLines = [];
		let inUnorderedList = false;
		let inOrderedList = false;
		let inCodeBlock = false;
		let inTable = false;
		let inCalculation = false;
		let calculationContent = [];
		let tableHeaders = [];
		let inFormattedCode = false;
		let currentFormattedCodeLang = "";
		let formattedCodeContent = [];
		let formattedCodePlainText = [];

		for (const line of lines) {
			if (/#codefs\(([^)]+)\)/.test(line)) {
				inFormattedCode = true;
				formattedCodeContent = [];
				formattedCodePlainText = [];
				const match = line.match(/#codefs\(([^)]+)\)/);
				currentFormattedCodeLang = match[1];
			} else if (/#codefe/.test(line)) {
				let lineNumbers = [];
				for (let i = 0; i < formattedCodeContent.length; i++) {
					const newLineNumber = `<span class="markdown-formatted-code-line-number">${i + 1}</span>`;
					lineNumbers.push(newLineNumber);
				}

				const codeBlockIndex = this.codeBlocks.length;
				this.codeBlocks.push(formattedCodePlainText.join("\n"));

				htmlLines.push(`<div class="markdown-formatted-code" data-code-index="${codeBlockIndex}">
                                    <div class="markdown-formatted-code-header">
                                        <p class="code-lang">${currentFormattedCodeLang}</p>
                                        <button class="code-copy-button" onclick="copyCodeBlock(this)">
                                            <img class="icon" src="assets/img/copy.svg" alt="copy-icon">
                                            <span class="code-copy-text">Copy</span>
                                        </button>
                                    </div>
                                    <div class="markdown-formatted-code-content">
                                        <div class="markdown-formatted-code-line-numbers">${lineNumbers.join("<br>")}</div>
                                        <div class="markdown-formatted-code-content-lines">${this.colorFormatCode(formattedCodeContent).join("")}</div>
                                    </div>
                                </div>`);
				inFormattedCode = false;
			} else if (inFormattedCode) {
				// Only push raw line, do not escape here
				formattedCodeContent.push(line);
				formattedCodePlainText.push(line);
			} else if (/^#cal/.test(line)) {
				if (inCalculation) {
					htmlLines.push(`<div class="markdown-calculation">${calculationContent.join("<br>")}</div>`);
				}
				inCalculation = !inCalculation;
				calculationContent = [];
			} else if (inCalculation) {
				calculationContent.push(this.escapeHtml(line));
			} else if (/^```/.test(line)) {
				if (inCodeBlock) {
					htmlLines.push("</pre>");
				} else {
					htmlLines.push('<pre class="markdown-code-block">');
				}
				inCodeBlock = !inCodeBlock;
			} else if (inCodeBlock) {
				htmlLines.push(this.escapeHtml(line));
			} else if (this.isTableRow(line)) {
				// Handle table rows
				if (!inTable) {
					// Close any open lists
					if (inUnorderedList) {
						htmlLines.push("</ul>");
						inUnorderedList = false;
					}
					if (inOrderedList) {
						htmlLines.push("</ol>");
						inOrderedList = false;
					}

					// Check if next line is separator to determine if this is a header
					const nextLineIndex = lines.indexOf(line) + 1;
					const nextLine = nextLineIndex < lines.length ? lines[nextLineIndex] : "";

					if (this.isTableSeparator(nextLine)) {
						// This is a header row
						htmlLines.push('<table class="markdown-table">');
						htmlLines.push("<thead>");
						htmlLines.push(this.parseTableRow(line, true));
						htmlLines.push("</thead>");
						htmlLines.push("<tbody>");
						inTable = true;
						tableHeaders = this.extractTableHeaders(line);
					} else {
						// This is a regular table without headers
						htmlLines.push('<table class="markdown-table">');
						htmlLines.push("<tbody>");
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
					htmlLines.push("</ol>");
					inOrderedList = false;
				}
				htmlLines.push(this.parseLine(line));
			} else if (/^\d+\.\s/.test(line)) {
				if (!inOrderedList) {
					htmlLines.push('<ol class="markdown-ordered-list-spaced">');
					inOrderedList = true;
				}
				if (inUnorderedList) {
					htmlLines.push("</ul>");
					inUnorderedList = false;
				}
				htmlLines.push(this.parseLine(line));
			} else {
				if (inTable) {
					htmlLines.push("</tbody>");
					htmlLines.push("</table>");
					inTable = false;
					tableHeaders = [];
				}
				if (inUnorderedList) {
					htmlLines.push("</ul>");
					inUnorderedList = false;
				}
				if (inOrderedList) {
					htmlLines.push("</ol>");
					inOrderedList = false;
				}
				htmlLines.push(this.parseLine(line));
			}
		}

		if (inUnorderedList) {
			htmlLines.push("</ul>");
		}
		if (inOrderedList) {
			htmlLines.push("</ol>");
		}
		if (inCodeBlock) {
			htmlLines.push("</pre>");
		}
		if (inTable) {
			htmlLines.push("</tbody>");
			htmlLines.push("</table>");
		}
		if (inCalculation) {
			// Close any unclosed calculation block
			htmlLines.push(`<div class="markdown-calculation">${calculationContent.join("<br>")}</div>`);
		}

		return this.processSignatureBlocks(htmlLines.join("\n"));
	}

	// color format lines
	colorFormatCode(codeLines = []) {
		let returnLines = [];

		const escapeHtml = (str) => {
			return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#039;");
		};

		const keywords = [
			"auto",
			"break",
			"case",
			"char",
			"const",
			"continue",
			"default",
			"do",
			"double",
			"else",
			"enum",
			"extern",
			"float",
			"for",
			"goto",
			"if",
			"int",
			"long",
			"register",
			"return",
			"short",
			"signed",
			"sizeof",
			"static",
			"struct",
			"switch",
			"typedef",
			"union",
			"unsigned",
			"void",
			"volatile",
			"while",
			"asm",
			"bool",
			"catch",
			"class",
			"const_cast",
			"delete",
			"dynamic_cast",
			"explicit",
			"export",
			"false",
			"friend",
			"inline",
			"mutable",
			"namespace",
			"new",
			"operator",
			"private",
			"protected",
			"public",
			"reinterpret_cast",
			"static_cast",
			"string",
			"template",
			"this",
			"throw",
			"true",
			"try",
			"typeid",
			"typename",
			"using",
			"virtual",
			"wchar_t",
		];

		const tokenRegex = /\/\/.*?$|\/\*.*?\*\/|"[^"]*"|'[^']*'|#\w+|\b\d+(?:\.\d+)?\b|\b\w+(?=\.)|(?<=\.)\w+\b|[{}()[\];,.]|[-+\/*=%.&|!<>?:]+|\b\w+\b|\s+/g;

		codeLines.forEach((line) => {
			let newline = [];
			let lastIndex = 0;
			let previousToken = "";
			tokenRegex.lastIndex = 0;

			if (line == "") {
				newline.push("<br>");
			} else {
				while (true) {
					const match = tokenRegex.exec(line);
					if (!match) break;

					if (match.index > lastIndex) {
						const unmatched = line.substring(lastIndex, match.index);
						newline.push(escapeHtml(unmatched));
					}

					const token = match[0];
					let colored = false;

					if (token.startsWith("//") || token.startsWith("/*")) {
						newline.push(`<span class="markdown-code-comment">${escapeHtml(token)}</span>`);
						colored = true;
					} else if (token.startsWith("#")) {
						newline.push(`<span class="markdown-code-hash">${escapeHtml(token)}</span>`);
						colored = true;
					} else if (token.startsWith('"') || token.startsWith("'")) {
						newline.push(`<span class="markdown-code-qmark">${escapeHtml(token)}</span>`);
						colored = true;
					} else if (/^\d+(?:\.\d+)?$/.test(token)) {
						newline.push(`<span class="markdown-code-number">${escapeHtml(token)}</span>`);
						colored = true;
					} else if (/^\w+$/.test(token) && previousToken === ".") {
						newline.push(`<span class="markdown-code-property">${escapeHtml(token)}</span>`);
						colored = true;
					} else if (/^\w+$/.test(token) && line.charAt(match.index + token.length) === ".") {
						newline.push(`<span class="markdown-code-object">${escapeHtml(token)}</span>`);
						colored = true;
					} else if (keywords.includes(token)) {
						newline.push(`<span class="markdown-code-keyword">${escapeHtml(token)}</span>`);
						colored = true;
					} else if (/^[\[\]]$/.test(token)) {
						newline.push(`<span class="markdown-code-square-brackets">${escapeHtml(token)}</span>`);
						colored = true;
					} else if (/^[()]$/.test(token)) {
						newline.push(`<span class="markdown-code-brackets">${escapeHtml(token)}</span>`);
						colored = true;
					} else if (/^[{}]$/.test(token)) {
						newline.push(`<span class="markdown-code-curly-brackets">${escapeHtml(token)}</span>`);
						colored = true;
					} else if (token === ".") {
						newline.push(`<span class="markdown-code-dot">${escapeHtml(token)}</span>`);
						colored = true;
					} else if (/^<.*>$/.test(token)) {
						newline.push(token);
						colored = true;
					}

					if (!colored) {
						if (/^&[a-zA-Z]+;/.test(token)) {
							newline.push(`<span>${token}</span>`);
						} else {
							newline.push(`<span>${escapeHtml(token)}</span>`);
						}
					}

					previousToken = token.trim();
					lastIndex = match.index + token.length;
				}
			}

			if (lastIndex < line.length) {
				const unmatched = line.substring(lastIndex);
				newline.push(escapeHtml(unmatched));
			}

			const lineHtml = newline.join("");
			returnLines.push(`<p style="white-space: pre;">${lineHtml}</p>`);
		});

		return returnLines;
	}

	// Detect and convert each line
	parseLine(line) {
		// Custom signatures
		if (/^#\+h\s/.test(line)) {
			const content = line.replace(/^#\+h\s/, "");
			return `SIGNATURE_START:positive:heading:${this.parseInline(content)}`;
		}
		if (/^#\+b\s/.test(line)) {
			const content = line.replace(/^#\+b\s/, "");
			return `SIGNATURE_START:positive:body:${this.parseInline(content)}`;
		}

		if (/^#wh\s/.test(line)) {
			const content = line.replace(/^#wh\s/, "");
			return `SIGNATURE_START:warning:heading:${this.parseInline(content)}`;
		}
		if (/^#wb\s/.test(line)) {
			const content = line.replace(/^#wb\s/, "");
			return `SIGNATURE_START:warning:body:${this.parseInline(content)}`;
		}

		if (/^#-h\s/.test(line)) {
			const content = line.replace(/^#-h\s/, "");
			return `SIGNATURE_START:negative:heading:${this.parseInline(content)}`;
		}
		if (/^#-b\s/.test(line)) {
			const content = line.replace(/^#-b\s/, "");
			return `SIGNATURE_START:negative:body:${this.parseInline(content)}`;
		}

		if (/^#ih\s/.test(line)) {
			const content = line.replace(/^#ih\s/, "");
			return `SIGNATURE_START:info:heading:${this.parseInline(content)}`;
		}
		if (/^#ib\s/.test(line)) {
			const content = line.replace(/^#ib\s/, "");
			return `SIGNATURE_START:info:body:${this.parseInline(content)}`;
		}

		if (/^#bh\(([^)]+)\)\s/.test(line)) {
			const match = line.match(/^#bh\(([^)]+)\)\s(.*)$/);
			const id = match[1];
			const content = match[2];
			return `SIGNATURE_START:button:heading:${this.parseInline(content)}:${id}`;
		}
		if (/^#bb\s/.test(line)) {
			const content = line.replace(/^#bb\s/, "");
			return `SIGNATURE_START:button:body:${this.parseInline(content)}`;
		}

		// Headings
		if (/^#{1,6}\s/.test(line)) {
			const level = line.match(/^#{1,6}/)[0].length;
			const content = line.replace(/^#{1,6}\s/, "");
			const headingId = this.headingCount++;
			return `<h${level} id="${headingId}" class="markdown-heading">${this.parseInline(content)}</h${level}>`;
		}

		// Unordered Lists
		if (/^(\*|\-|\+)\s/.test(line)) {
			const content = line.replace(/^(\*|\-|\+)\s/, "");
			return `<li class="markdown-list-item">${this.parseInline(content)}</li>`;
		}

		// Ordered Lists
		if (/^\d+\.\s/.test(line)) {
			const content = line.replace(/^\d+\.\s/, "");
			return `<li class="markdown-ordered-list-item">${this.parseInline(content)}</li>`;
		}

		// Line breaks
		if (line.trim() === "") {
			return "<br>";
		}

		// End signature
		if (line.trim() === "#end") {
			let item = `<div class="markdown-end-container">`;

			// If previous item exists
			if (this.currentPageIndex !== 0) {
				const previousPath = this.flatStructure[this.currentPageIndex - 1];
				const previousName = this.getPageName(previousPath);
				item += `<div class="markdown-end-next" data-path="${previousPath}">
                            <img class="markdown-end-icon-previous" src="assets/img/arrow.svg" alt="next-icon"></img>
                            <div class="markdown-end-content-p">
                                <p class="markdown-end-label">Previous</p>
                                <p class="markdown-end-title">${previousName}</p>
                            </div>
                         </div>`;
			}

			// If next item exists
			if (this.currentPageIndex !== this.flatStructure.length - 1) {
				const nextPath = this.flatStructure[this.currentPageIndex + 1];
				const nextName = this.getPageName(nextPath);
				item += `<div class="markdown-end-next" data-path="${nextPath}">
                            <div class="markdown-end-content-n">
                                <p class="markdown-end-label">Next</p>
                                <p class="markdown-end-title">${nextName}</p>
                            </div>
                            <img class="markdown-end-icon" src="assets/img/arrow.svg" alt="next-icon"></img>
                         </div>`;
			}

			return item;
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

				return imgTag + " />";
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
		return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#039;");
	}

	// Process and merge signature blocks
	processSignatureBlocks(html) {
		const lines = html.split("\n");
		const result = [];
		let currentSignature = null;

		for (const line of lines) {
			if (line.startsWith("SIGNATURE_START:")) {
				const parts = line.split(":");
				const type = parts[1];
				const part = parts[2];
				const content = parts[3];
				const id = parts[4]; // For button signatures with ID

				if (currentSignature && currentSignature.type === type) {
					// Add to existing signature
					if (part === "heading") {
						currentSignature.heading = content;
						if (id) currentSignature.id = id;
					} else if (part === "body") {
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
						heading: part === "heading" ? content : null,
						bodies: part === "body" ? [content] : [],
						id: id || null,
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

		return result.join("\n");
	}

	// Generate HTML for a signature block
	generateSignatureHTML(signature) {
		if (signature.type === "button") {
			// Special handling for button signatures
			let html = `<div class="markdown-button-container"${signature.id ? ` id="${signature.id}"` : ""}>`;

			if (signature.heading) {
				html += `<div class="markdown-button-heading">${signature.heading}</div>`;
			}

			for (const body of signature.bodies) {
				html += `<div class="markdown-button-body">${body}</div>`;
			}

			html += "</div>";
			return html;
		} else {
			// Standard signature handling for other types
			let html = `<div class="markdown-${signature.type}">`;

			if (signature.heading) {
				html += `<div class="markdown-signature-header">`;
				if (signature.type === "positive") {
					html += `<img class="icon" src="assets/img/circle-check.svg" alt="positive-icon"></img>`;
				} else if (signature.type === "warning") {
					html += `<img class="icon" src="assets/img/triangle-exclamation.svg" alt="warning-icon"></img>`;
				} else if (signature.type === "negative") {
					html += `<img class="icon" src="assets/img/cancel.svg" alt="negative-icon"></img>`;
				} else if (signature.type === "info") {
					html += `<img class="icon" src="assets/img/circle-info.svg" alt="info-icon"></img>`;
				}
				html += `<h1 class="markdown-signature-heading">${signature.heading}</h1>`;
				html += `</div>`;
			}

			for (const body of signature.bodies) {
				html += `<p class="markdown-signature-body">${body}</p>`;
			}

			html += "</div>";
			return html;
		}
	}

	// Get page name from path by searching content structure
	getPageName(path) {
		const findName = (items) => {
			for (const item of items) {
				if (item.path === path) {
					return item.name;
				}
				if (item.children) {
					const found = findName(item.children);
					if (found) return found;
				}
			}
			return null;
		};
		return findName(this.contentStructure) || "Unknown Page";
	}

	// Check if line is a table row
	isTableRow(line) {
		return line.trim().includes("|") && line.trim().length > 0;
	}

	// Check if line is a table separator (|---|---|)
	isTableSeparator(line) {
		return /^\s*\|[\s\-\|:]+\|\s*$/.test(line);
	}

	// Extract table headers for reference
	extractTableHeaders(line) {
		return line
			.split("|")
			.map((cell) => cell.trim())
			.filter((cell) => cell.length > 0);
	}

	// Parse a table row into HTML
	parseTableRow(line, isHeader) {
		const cells = line
			.split("|")
			.map((cell) => cell.trim())
			.filter((cell) => cell.length > 0);
		const tag = isHeader ? "th" : "td";
		const className = isHeader ? "markdown-table-header" : "markdown-table-cell";

		let html = '<tr class="markdown-table-row">';
		for (const cell of cells) {
			html += `<${tag} class="${className}">${this.parseInline(cell)}</${tag}>`;
		}
		html += "</tr>";

		return html;
	}
}

export default MarkdownConverter;
