# Markdown Converter Documentation

This document provides comprehensive documentation for the custom Markdown converter implementation in `md-converter.js`. The converter supports standard Markdown syntax with additional extended features for enhanced content presentation.

## Overview

The Markdown converter transforms Markdown text into semantically structured HTML with appropriate CSS classes for styling. It handles both standard Markdown elements and custom extensions for improved functionality.

## Standard Markdown Support

The converter supports all standard Markdown elements including:

- **Headings** (H1-H6): `# Heading` through `###### Heading`
- **Paragraphs**: Regular text blocks
- **Text Formatting**: 
  - Bold text: `**bold**` or `__bold__`
  - Italic text: `*italic*` or `_italic_`
  - Inline code: `` `code` ``
- **Links**: `[Link Text](URL)`
- **Lists**:
  - Unordered lists: `*`, `-`, or `+` prefixes
  - Ordered lists: `1.`, `2.`, etc.
- **Code Blocks**: Fenced with triple backticks (```)

## Extended Features

### Links

The converter supports standard Markdown link syntax for creating clickable hyperlinks.

#### Link Syntax

```markdown
[Link Text](URL)
```

- **Link Text**: The visible text that will be displayed and clickable
- **URL**: The destination URL (can be relative or absolute)

**Examples:**
```markdown
[Visit GitHub](https://github.com)
[Documentation](../docs/readme.md)
[Contact Us](mailto:contact@example.com)
[Internal Section](#section-name)
```

#### Generated HTML Output

```html
<a href="https://example.com" class="markdown-link">Link Text</a>
```

### Button Links with Custom IDs

The converter supports custom button-style links with unique identifiers for JavaScript-controlled interactions.

#### Button Link Syntax

```markdown
[[Button Text](id)]
```

- **Button Text**: The visible text displayed on the button
- **ID**: A unique identifier for the button element

**Examples:**
```markdown
[[Download PDF](download-btn)]
[[Sign Up Now](signup-button)]
[[Learn More](info-btn)]
```

#### Generated HTML Output

```html
<span id="download-btn" class="markdown-button">Download PDF</span>
```

#### JavaScript Integration

Since the button generates only an ID and class without href attributes, you need to handle clicks in JavaScript:

```javascript
// Example click handler setup
document.getElementById('download-btn').addEventListener('click', () => {
    // Your custom logic here
    window.open('./documents/manual.pdf', '_blank');
});

document.getElementById('signup-button').addEventListener('click', () => {
    // Custom signup logic
    handleSignup();
});
```

**Use Cases:**
- Call-to-action buttons with custom styling and behavior
- Elements requiring complex JavaScript interactions
- Buttons that need preprocessing before navigation
- Custom analytics tracking before redirect
- Modal dialogs or interactive elements

### Custom Signature Blocks

The converter supports custom signature blocks for semantic content highlighting with distinct visual styling.

#### Signature Block Syntax

**Positive Block (Green):**
```markdown
#+ This is a positive message
```

**Warning Block (Yellow):**
```markdown
#w This is a warning message
```

**Negative Block (Red):**
```markdown
#- This is a negative or error message
```

#### Usage Examples

```markdown
#+ Great job! Your configuration is working perfectly.
#w Please note that this feature is experimental.
#- Error: Unable to connect to the database.
```

#### Generated HTML Output

```html
<!-- Positive block -->
<p class="markdown-positive">Great job! Your configuration is working perfectly.</p>

<!-- Warning block -->
<p class="markdown-warning">Please note that this feature is experimental.</p>

<!-- Negative block -->
<p class="markdown-negative">Error: Unable to connect to the database.</p>
```

#### Styling Guidelines

These blocks are designed for semantic styling:

- **`.markdown-positive`**: Typically styled with green background/border for success messages, confirmations, or positive feedback
- **`.markdown-warning`**: Typically styled with yellow/orange background/border for warnings, cautions, or important notes
- **`.markdown-negative`**: Typically styled with red background/border for errors, failures, or critical issues

```

### Enhanced Image Rendering

The converter provides enhanced image embedding capabilities beyond standard Markdown syntax.

#### Basic Image Syntax

```markdown
![Alt Text](image-source)
```

- **Alt Text**: Descriptive text for accessibility and fallback display
- **Image Source**: Can be either a remote URL or local file path

**Examples:**
```markdown
![Company Logo](https://example.com/logo.png)
![Local Photo](../assets/photos/sunset.jpg)
```

#### Dimensional Control

Images support optional dimensional specifications for precise layout control:

**Width Only (Maintains Aspect Ratio):**
```markdown
![Alt Text](image-source)(width)
```

**Width and Height:**
```markdown
![Alt Text](image-source)(width×height)
```

**Examples:**
```markdown
![Profile Picture](avatar.jpg)(150)
![Banner Image](banner.png)(800×200)
![Thumbnail](thumb.jpg)(100×100)
```

#### Generated HTML Output

The converter generates semantic HTML with appropriate CSS classes:

```html
<!-- Basic image -->
<img src="image.jpg" alt="Description" class="markdown-image" />

<!-- With width only -->
<img src="image.jpg" alt="Description" class="markdown-image" width="300" />

<!-- With width and height -->
<img src="image.jpg" alt="Description" class="markdown-image" width="300" height="200" />
```

## CSS Classes

All generated HTML elements include CSS classes for consistent styling:

- `markdown-heading`: Applied to all heading elements (H1-H6)
- `markdown-paragraph`: Applied to paragraph elements
- `markdown-bold`: Applied to bold text elements
- `markdown-italic`: Applied to italic text elements
- `markdown-code`: Applied to inline code elements
- `markdown-code-block`: Applied to code block containers
- `markdown-list`: Applied to unordered list containers
- `markdown-list-item`: Applied to unordered list items
- `markdown-ordered-list-spaced`: Applied to ordered list containers
- `markdown-ordered-list-item`: Applied to ordered list items
- `markdown-link`: Applied to all link elements
- `markdown-button`: Applied to button-style link elements
- `markdown-positive`: Applied to positive signature blocks (`#+`)
- `markdown-warning`: Applied to warning signature blocks (`#w`)
- `markdown-negative`: Applied to negative signature blocks (`#-`)
- `markdown-image`: Applied to all image elements

## Usage

The converter is implemented as an ES6 module and can be imported and used as follows:

```javascript
import MarkdownConverter from './md-converter.js';

const converter = new MarkdownConverter();
const html = converter.convert(markdownText);
```