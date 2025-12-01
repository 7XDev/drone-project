# Help & Documentation Guide

Welcome to the Drones Documentation Help page! This guide will help you understand how this website works and how to use markdown to create content.

## How This Website Works

This documentation website uses a custom **Markdown Converter** system to dynamically render content. Here's how it works:

### Architecture Overview

1. **Content Structure** - All documentation pages are stored as `.md` (Markdown) files in the `assets/content/` directory
2. **Content Browser** - Manages the navigation structure defined in `content-structure.json`
3. **Markdown Converter** - Converts markdown syntax into styled HTML in real-time
4. **Dynamic Loading** - Pages are loaded on-demand when you click topics in the sidebar

### Key Components

* **Topic Selector** - The left sidebar that displays all available topics and categories
* **Display Window** - The main content area where markdown is rendered
* **Right Panel** - Shows a table of contents with all headings on the current page
* **Copy Button** - Allows you to copy the raw markdown content of the current page

## Standard Markdown Elements

### Headings

Use `#` symbols to create headings. More `#` symbols = smaller heading.

```
# Heading 1
## Heading 2
### Heading 3
#### Heading 4
##### Heading 5
###### Heading 6
```

### Text Formatting

* `**bold text**` - Creates **bold text**
* `*italic text*` - Creates *italic text*
* `~~strikethrough~~` - Creates ~~strikethrough~~
* `` `inline code` `` - Creates `inline code`

### Lists

**Unordered Lists:**
```
* Item 1
* Item 2
* Item 3
```

**Ordered Lists:**
```
1. First item
2. Second item
3. Third item
```

### Links and Images

**Links:**
```
[Link text](https://example.com)
```

**Images:**
```
![Alt text](path/to/image.jpg)
```

**Images with Dimensions:**
```
![Alt text](path/to/image.jpg)(300x200)
![Alt text](path/to/image.jpg)(300)
```

### Code Blocks

Use triple backticks for code blocks:

````
```
Your code here
Multiple lines supported
```
````

### Tables

Create tables using pipes `|` and hyphens `-`:

```
| Header 1 | Header 2 | Header 3 |
|----------|----------|----------|
| Cell 1   | Cell 2   | Cell 3   |
| Cell 4   | Cell 5   | Cell 6   |
```

## Custom Signature Blocks

This documentation system includes special custom signature blocks for highlighting important information.

### Positive Block (Success/Recommendation)

```
#+h Heading text
#+b Body text line 1
#+b Body text line 2
```

#ih Example:
#ib This renders as a green block with a checkmark icon, perfect for success messages or recommendations.

### Warning Block

```
#wh Warning Heading
#wb Warning body text
#wb Additional warning details
```

#ih Example:
#ib This renders as a yellow/orange block with a warning triangle icon.

### Negative Block (Danger/Error)

```
#-h Error or Danger
#-b Description of the error or danger
#-b Additional information
```

#ih Example:
#ib This renders as a red block with an X icon, used for errors or critical warnings.

### Info Block

```
#ih Information Heading
#ib Information body text
#ib More details about this information
```

#ih Example:
#ib This renders as a blue block with an info icon (like this one!).

### Button Block

```
#bh(unique-id) Button Text
#bb Description or details about this action
```

#ih Note:
#ib Button blocks are interactive elements with unique IDs for JavaScript functionality.

## Navigation Features

### Topic Navigation

* Click on any topic in the left sidebar to load its content
* Topics are organized into collapsible categories
* Click category names to expand/collapse their contents
* Selected topics are highlighted in blue

### Page Navigation

At the bottom of each page, you'll find navigation buttons:

* **Previous** - Navigate to the previous topic in sequence
* **Next** - Navigate to the next topic in sequence

### Table of Contents

The right panel displays all headings on the current page:

* Click any heading to jump directly to that section
* Headings are automatically extracted from the markdown

## Theme Switching

Use the **Light Mode** / **Dark Mode** button in the header to toggle between themes. Your preference is automatically saved and will persist across sessions.

## Copy Function

Click the **Copy** button in the right panel to copy the raw markdown content of the current page to your clipboard. This is useful for:

* Creating new pages with similar formatting
* Extracting content for external use
* Studying the markdown syntax

## Tips for Content Creators

### Best Practices

1. **Use descriptive headings** - They appear in the table of contents
2. **Combine signature blocks** - Mix different block types for emphasis
3. **Add images with dimensions** - Prevents layout shifts during loading
4. **Use tables sparingly** - They work best for structured data
5. **Test on both themes** - Ensure content looks good in light and dark mode

### File Organization

* Store all markdown files in `src/assets/content/`
* Update `content-structure.json` when adding new pages
* Use clear, descriptive filenames (kebab-case recommended)
* Keep related topics together in categories

### Performance Tips

* Optimize images before uploading
* Keep individual pages focused and not too long
* Use code blocks instead of images for code samples
* Minimize external link dependencies

## Troubleshooting

### Content Not Loading

* Check that the `.md` file exists in `assets/content/`
* Verify the path in `content-structure.json` is correct
* Check browser console for error messages

### Formatting Issues

* Ensure proper spacing after markdown symbols (`#`, `*`, etc.)
* Leave blank lines between different block types
* Close all code blocks with triple backticks
* End signature blocks before starting new content

### Dark Mode Issues

* Clear browser cache and reload
* Check localStorage is enabled in your browser
* Try toggling the theme button again

## Additional Resources

### File Locations

* **Content Files**: `src/assets/content/*.md`
* **Structure Config**: `src/assets/content/content-structure.json`
* **Scripts**: `src/scripts/`
* **Styles**: `src/styles/`

### Key Scripts

* `md-converter.js` - Markdown parsing and HTML conversion
* `content-browser.js` - Navigation structure management
* `script.js` - Main application logic and event handlers

#ih Questions or Issues?
#ib If you encounter any problems or have questions about using this documentation system, check the source code comments or create an issue in the project repository.
