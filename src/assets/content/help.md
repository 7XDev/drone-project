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

## Standard Markdown Elements

### Headings
Use `#` symbols to create headings (more `#` = smaller heading):
```
# Heading 1
## Heading 2
### Heading 3
```

### Text Formatting
* `**bold text**` - **bold text**
* `*italic text*` - *italic text*
* `~~strikethrough~~` - ~~strikethrough~~
* `` `inline code` `` - `inline code`

### Lists
**Unordered:**
```
* Item 1
* Item 2
```
**Ordered:**
```
1. First item
2. Second item
```

### Links and Images
**Links:** `[Link text](https://example.com)`

**Images:** `![Alt text](path/to/image.jpg)`

**Images with Dimensions:** `![Alt text](image.jpg)(300x200)` or `![Alt text](image.jpg)(300)`

### Code Blocks
Use triple backticks:
````
```
Your code here
```
````

### Tables
```
| Header 1 | Header 2 |
|----------|----------|
| Cell 1   | Cell 2   |
```

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
