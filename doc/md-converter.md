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
- **Lists**:
  - Unordered lists: `*`, `-`, or `+` prefixes
  - Ordered lists: `1.`, `2.`, etc.
- **Code Blocks**: Fenced with triple backticks (```)

## Extended Features

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
- `markdown-image`: Applied to all image elements

## Usage

The converter is implemented as an ES6 module and can be imported and used as follows:

```javascript
import MarkdownConverter from './md-converter.js';

const converter = new MarkdownConverter();
const html = converter.convert(markdownText);
```