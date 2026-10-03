// sharing-web-files: list view (the page remembers the last view in localStorage).
(() => { if (!document.getElementById('items-container').classList.contains('list')) toggleViewMode(); return document.getElementById('items-container').className; })()
