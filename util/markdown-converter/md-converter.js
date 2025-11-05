async function loadMarkdown(path) {
    const response = await fetch(path);
    if (!response.ok) {
        console.error(`Failed to load markdown file: ${response.statusText}`);
        return '';
    }
    return await response.text();
}