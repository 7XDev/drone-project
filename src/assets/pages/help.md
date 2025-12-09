# Help & Documentation Guide

Welcome to the Drones Documentation Help page! This guide explains how the website works and how to use markdown to create content.

## How This Website Works

This documentation website uses a custom **Markdown Converter** system to dynamically render content.

### Architecture Overview
1. **Content Structure** - Documentation pages stored as `.md` files in `assets/content/`
2. **Content Browser** - Manages navigation structure from `content-structure.json`
3. **Markdown Converter** - Converts markdown syntax to styled HTML in real-time
4. **Dynamic Loading** - Pages load on-demand when you click topics

### Key Components
* **Topic Selector** - Left sidebar with all topics and categories
* **Display Window** - Main content area where markdown renders
* **Right Panel** - Table of contents with page headings
* **Copy Button** - Copies raw markdown content to clipboard

## Markdown Converter Documentation

This document provides comprehensive documentation for the custom Markdown converter implementation in `md-converter.js`. The converter supports standard Markdown syntax with advanced extended features for enhanced content presentation.

### Overview

The Markdown converter transforms Markdown text into semantically structured HTML with appropriate CSS classes for styling. It handles both standard Markdown elements and powerful custom extensions designed for professional documentation and content management. Find more information about regular markdown syntax [here]("https://www.markdownguide.org/basic-syntax/").

### Advanced Extended Features

#### Reworked Custom Signature System

The converter features a sophisticated custom signature system for creating structured, semantic content blocks. This system has been completely redesigned to support hierarchical content organization with automatic merging capabilities.

##### Core Signature Types

The system provides four distinct signature types, each with specific semantic meanings:

**Positive Signatures (`#+`)**
- **Purpose**: Success messages, confirmations, positive feedback
- **Visual Style**: Green color scheme with left border accent
- **Use Cases**: Completed tasks, successful operations, achievements

**Warning Signatures (`#w`)**
- **Purpose**: Cautions, important notices, experimental features
- **Visual Style**: Amber/yellow color scheme with left border accent
- **Use Cases**: Beta features, deprecation notices, important reminders

**Negative Signatures (`#-`)**
- **Purpose**: Errors, failures, critical issues
- **Visual Style**: Red color scheme with left border accent
- **Use Cases**: Error messages, failed operations, critical warnings

**Info Signatures (`#i`)**
- **Purpose**: General information, tips, explanations
- **Visual Style**: Blue color scheme with left border accent
- **Use Cases**: Documentation notes, helpful tips, general information

##### Hierarchical Content Structure

Each signature type supports two content components that can be combined for rich, structured information presentation:

**Heading Component (`h` modifier)**
- **Syntax**: `#+h`, `#wh`, `#-h`, `#ih`
- **Function**: Creates bold, prominent titles within signature blocks
- **Styling**: Larger font size with increased weight for visual hierarchy

**Body Component (`b` modifier)**
- **Syntax**: `#+b`, `#wb`, `#-b`, `#ib`
- **Function**: Provides detailed content and descriptions
- **Styling**: Regular weight with subtle opacity for clear content hierarchy

##### Intelligent Block Merging

The system features advanced automatic merging capabilities that combine consecutive signature elements of the same type into unified blocks:

**Merging Rules:**
- Consecutive signatures of the same type automatically merge into a single block
- Headings and bodies can appear in any order within a merged block
- Multiple body sections are supported within a single signature
- Different signature types or non-signature content breaks the merge sequence

**Example Input:**
```markdown
#+h Configuration Complete
#+b Your settings have been saved successfully.
#+b All services are now running with the new configuration.

#wh Important Notice
#wb This feature is currently in beta testing.

#-h Connection Failed
#-b Unable to establish connection to the database server.
#-b Please check your network settings and try again.
```

**Generated Structure:**
```html
<div class="markdown-positive">
    <h4 class="markdown-signature-heading">Configuration Complete</h4>
    <p class="markdown-signature-body">Your settings have been saved successfully.</p>
    <p class="markdown-signature-body">All services are now running with the new configuration.</p>
</div>

<div class="markdown-warning">
    <h4 class="markdown-signature-heading">Important Notice</h4>
    <p class="markdown-signature-body">This feature is currently in beta testing.</p>
</div>

<div class="markdown-negative">
    <h4 class="markdown-signature-heading">Connection Failed</h4>
    <p class="markdown-signature-body">Unable to establish connection to the database server.</p>
    <p class="markdown-signature-body">Please check your network settings and try again.</p>
</div>
```

##### Flexible Usage Patterns

The signature system supports various content organization patterns:

**Title-Only Blocks:**
```markdown
#+h Success!
```

**Content-Only Blocks:**
```markdown
#+b Operation completed successfully.
```

**Full Structured Blocks:**
```markdown
#+h Database Migration
#+b Migration completed successfully.
#+b All data has been preserved.
#+b New indexes have been created.
```

**Mixed Order Support:**
```markdown
#+b This body appears first.
#+h But This Heading Still Works
#+b And this body comes after.
```

##### Technical Implementation

The signature system uses a sophisticated two-pass processing approach:

1. **First Pass**: Signature elements are parsed and marked with temporary identifiers
2. **Second Pass**: Consecutive signatures are analyzed and merged into unified HTML structures
3. **Final Output**: Clean, semantic HTML with proper CSS classes for styling

This approach ensures optimal performance while maintaining flexibility and allowing for complex content structures.

##### Integration with Standard Markdown

Custom signatures fully support all standard Markdown formatting within their content:

```markdown
#+h **Important** Success!
#+b Configuration updated with *new settings* and `secure tokens`.
#+b For more information, visit [our documentation](./docs/config.md).
```

The signature system seamlessly integrates with the converter's other features, including links, images, code formatting, and all other Markdown elements.

### CSS Classes and Styling

The converter generates semantic HTML with comprehensive CSS classes for consistent styling:

#### Standard Element Classes
- `markdown-heading`: Applied to all heading elements (H1-H6)
- `markdown-paragraph`: Applied to paragraph elements
- `markdown-bold`: Applied to bold text elements
- `markdown-italic`: Applied to italic text elements
- `markdown-strikethrough`: Applied to strikethrough text elements
- `markdown-code`: Applied to inline code elements
- `markdown-code-block`: Applied to code block containers
- `markdown-link`: Applied to all link elements
- `markdown-button`: Applied to button-style link elements
- `markdown-image`: Applied to all image elements

#### Signature Block Classes
- `markdown-positive`: Applied to positive signature blocks
- `markdown-warning`: Applied to warning signature blocks
- `markdown-negative`: Applied to negative signature blocks
- `markdown-info`: Applied to info signature blocks
- `markdown-signature-heading`: Applied to signature heading elements
- `markdown-signature-body`: Applied to signature body elements

#### Table Classes
- `markdown-table`: Applied to table containers
- `markdown-table-header`: Applied to table header cells
- `markdown-table-cell`: Applied to table data cells
- `markdown-table-row`: Applied to table rows

### Usage

The converter is implemented as an ES6 module and can be imported and used as follows:

```javascript
import MarkdownConverter from './md-converter.js';

const converter = new MarkdownConverter();
const html = converter.convert(markdownText);
```

The converter provides both synchronous conversion and asynchronous file loading capabilities, making it suitable for both real-time editing and batch processing scenarios.

## Custom Signature Blocks

Special blocks for highlighting important information:

### Positive Block
```
#+h Heading text
#+b Body text
```
#ih Example:
#ib Green block with checkmark icon for success messages and recommendations.

### Warning Block
```
#wh Warning Heading
#wb Warning body text
```
#ih Example:
#ib Yellow/orange block with warning triangle icon.

### Negative Block
```
#-h Error or Danger
#-b Description text
```
#ih Example:
#ib Red block with X icon for errors and critical warnings.

### Info Block
```
#ih Information Heading
#ib Information body text
```
#ih Example:
#ib Blue block with info icon (like this one!).

### Button Block
```
#bh(unique-id) Button Text
#bb Description text
```
#ih Note:
#ib Interactive elements with unique IDs for JavaScript functionality.

## Navigation Features

### Topic Navigation
* Click topics in left sidebar to load content
* Topics organized into collapsible categories
* Click category names to expand/collapse
* Selected topics highlighted in blue

### Page Navigation
Bottom of each page has **Previous** and **Next** buttons to navigate through content sequentially.

### Table of Contents
Right panel shows all page headings - click any heading to jump to that section.

## Theme Switching

Toggle between **Light Mode** / **Dark Mode** using the header button. Your preference is saved automatically.

## Copy Function

**Copy** button in right panel copies raw markdown content - useful for creating similar pages or studying markdown syntax.

## Tips for Content Creators

### Best Practices
1. Use descriptive headings (appear in table of contents)
2. Combine signature blocks for emphasis
3. Add image dimensions to prevent layout shifts
4. Use tables for structured data only
5. Test content in both themes

### File Organization
* Store markdown in `src/assets/content/`
* Update `content-structure.json` for new pages
* Use descriptive filenames (kebab-case)
* Group related topics in categories

### Performance
* Optimize images before uploading
* Keep pages focused and concise
* Use code blocks for code (not images)
* Minimize external dependencies

## Troubleshooting

### Content Not Loading
* Verify `.md` file exists in `assets/content/`
* Check path in `content-structure.json`
* Review browser console for errors

### Formatting Issues
* Add spacing after markdown symbols (`#`, `*`, etc.)
* Leave blank lines between block types
* Close all code blocks with triple backticks
* End signature blocks before new content

### Dark Mode Issues
* Clear browser cache and reload
* Verify localStorage is enabled
* Toggle theme button again

## Additional Resources

### File Locations
* **Content**: `src/assets/content/*.md`
* **Config**: `src/assets/content/content-structure.json`
* **Scripts**: `src/scripts/`
* **Styles**: `src/styles/`

### Key Scripts
* `md-converter.js` - Markdown parsing and HTML conversion
* `content-browser.js` - Navigation structure management
* `script.js` - Main application logic and event handlers

#ih Questions or Issues?
#ib Check source code comments or create an issue in the project repository.
