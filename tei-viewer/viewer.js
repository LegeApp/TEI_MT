(function () {
  const fileInput = document.getElementById('fileInput');
  const folderInput = document.getElementById('folderInput');
  const dropzone = document.getElementById('dropzone');
  const content = document.getElementById('content');
  const template = document.getElementById('segmentTemplate');
  const metaTitle = document.getElementById('metaTitle');
  const metaFile = document.getElementById('metaFile');
  const metaCount = document.getElementById('metaCount');
  const toggleZh = document.getElementById('toggleZh');
  const toggleEn = document.getElementById('toggleEn');
  const fileList = document.getElementById('fileList');
  const indexCount = document.getElementById('indexCount');
  const filterInput = document.getElementById('filterInput');
  const statusLine = document.getElementById('statusLine');

  let hideZh = false;
  let hideEn = false;
  let currentSegments = [];
  let indexedFiles = [];
  let activeFileKey = '';

  function normalizeSpace(text) {
    return (text || '').replace(/\s+/g, ' ').trim();
  }

  function setStatus(text) {
    statusLine.textContent = text;
  }

  function localName(node) {
    return node ? (node.localName || node.nodeName || '').replace(/^.*:/, '') : '';
  }

  function shouldSkipSubtree(node) {
    const name = localName(node);
    return ['note', 'pb', 'lb', 'cb', 'fw', 'ref', 'anchor', 'milestone'].includes(name);
  }

  function isTranslatableNode(node) {
    const name = localName(node);
    return ['p', 'l', 'ab', 'head', 'seg'].includes(name);
  }

  function collectText(node, out) {
    if (!node) return;
    if (node.nodeType === Node.TEXT_NODE || node.nodeType === Node.CDATA_SECTION_NODE) {
      out.push(node.nodeValue || '');
      return;
    }
    if (node.nodeType !== Node.ELEMENT_NODE) {
      return;
    }
    if (shouldSkipSubtree(node)) {
      return;
    }
    for (const child of node.childNodes) {
      collectText(child, out);
    }
  }

  function extractNodeText(node) {
    const chunks = [];
    collectText(node, chunks);
    return normalizeSpace(chunks.join(' '));
  }

  function findTranslationNoteAfter(node) {
    let sib = node.nextSibling;
    while (sib) {
      if (sib.nodeType === Node.TEXT_NODE && normalizeSpace(sib.nodeValue) === '') {
        sib = sib.nextSibling;
        continue;
      }
      if (sib.nodeType !== Node.ELEMENT_NODE) {
        return '';
      }
      if (localName(sib) !== 'note') {
        return '';
      }
      const type = sib.getAttribute('type');
      const lang = sib.getAttribute('xml:lang') || sib.getAttribute('lang');
      if (type === 'translation' && lang === 'en') {
        return normalizeSpace(sib.textContent || '');
      }
      return '';
    }
    return '';
  }

  function collectSegments(body) {
    const segments = [];

    function walk(node) {
      if (!node || node.nodeType !== Node.ELEMENT_NODE) {
        return;
      }

      const name = localName(node);
      if (name === 'teiHeader') {
        return;
      }

      if (isTranslatableNode(node)) {
        const zh = extractNodeText(node);
        if (zh) {
          segments.push({
            id: node.getAttribute('xml:id') || node.getAttribute('id') || `seg-${segments.length + 1}`,
            zh,
            en: findTranslationNoteAfter(node)
          });
        }
        return;
      }

      for (const child of node.children) {
        walk(child);
      }
    }

    walk(body);
    return segments;
  }

  function findTitle(doc) {
    const titleNodes = doc.getElementsByTagNameNS('*', 'title');
    for (const n of titleNodes) {
      const lang = n.getAttribute('xml:lang') || n.getAttribute('lang') || '';
      if (lang.toLowerCase().startsWith('zh')) {
        const t = normalizeSpace(n.textContent || '');
        if (t) return t;
      }
    }
    for (const n of titleNodes) {
      const t = normalizeSpace(n.textContent || '');
      if (t) return t;
    }
    return 'Untitled TEI';
  }

  async function renderSegments() {
    content.innerHTML = '';
    document.body.classList.toggle('hidden-zh', hideZh);
    document.body.classList.toggle('hidden-en', hideEn);

    const batchSize = 80;
    for (let i = 0; i < currentSegments.length; i += batchSize) {
      const frag = document.createDocumentFragment();
      const end = Math.min(currentSegments.length, i + batchSize);
      for (let j = i; j < end; j += 1) {
        const seg = currentSegments[j];
        const node = template.content.cloneNode(true);
        node.querySelector('.seg-id').textContent = seg.id;
        node.querySelector('.zh-text').textContent = seg.zh || '(empty)';
        node.querySelector('.en-text').textContent = seg.en || '(no translation note found)';
        frag.appendChild(node);
      }
      content.appendChild(frag);
      setStatus(`Rendering segments ${end}/${currentSegments.length} ...`);
      await new Promise((resolve) => requestAnimationFrame(resolve));
    }
    setStatus(`Loaded ${currentSegments.length} segments.`);
  }

  async function parseXml(xmlText, fileLabel) {
    setStatus('Parsing XML ...');
    const parser = new DOMParser();
    const doc = parser.parseFromString(xmlText, 'application/xml');
    const err = doc.querySelector('parsererror');
    if (err) {
      throw new Error('XML parse error: ' + normalizeSpace(err.textContent || 'unknown error'));
    }

    const body = doc.getElementsByTagNameNS('*', 'body')[0];
    if (!body) {
      throw new Error('No <body> found in TEI XML.');
    }

    currentSegments = collectSegments(body);
    metaTitle.textContent = findTitle(doc);
    metaFile.textContent = fileLabel || '-';
    metaCount.textContent = String(currentSegments.length);
    await renderSegments();
  }

  async function openFile(file, fileKey) {
    setStatus(`Reading ${fileKey || file.name} ...`);
    const text = await file.text();
    await parseXml(text, fileKey || file.name);
    activeFileKey = fileKey || file.name;
    refreshFileList();
  }

  function refreshFileList() {
    const q = normalizeSpace(filterInput.value).toLowerCase();
    fileList.innerHTML = '';

    const shown = indexedFiles.filter((f) => !q || f.key.toLowerCase().includes(q));
    indexCount.textContent = `${shown.length} files`;

    for (const item of shown) {
      const btn = document.createElement('button');
      btn.className = 'file-item' + (item.key === activeFileKey ? ' active' : '');
      btn.textContent = item.key;
      btn.addEventListener('click', async () => {
        try {
          await openFile(item.file, item.key);
        } catch (err) {
          alert(String(err));
        }
      });
      fileList.appendChild(btn);
    }
  }

  function indexFolderFiles(fileListObj) {
    setStatus('Indexing folder ...');
    const arr = Array.from(fileListObj || [])
      .filter((f) => /\.xml$/i.test(f.name))
      .map((f) => ({
        file: f,
        key: f.webkitRelativePath || f.name
      }))
      .sort((a, b) => a.key.localeCompare(b.key));

    indexedFiles = arr;
    activeFileKey = '';
    refreshFileList();

    if (indexedFiles.length === 0) {
      setStatus('No .xml files found in selected folder.');
      currentSegments = [];
      metaTitle.textContent = '-';
      metaFile.textContent = '-';
      metaCount.textContent = '0';
      content.innerHTML = '';
      return;
    }

    setStatus(`Indexed ${indexedFiles.length} XML files. Loading first file ...`);
    if (indexedFiles.length > 0) {
      openFile(indexedFiles[0].file, indexedFiles[0].key).catch((err) => alert(String(err)));
    }
  }

  fileInput.addEventListener('change', async (e) => {
    const file = e.target.files && e.target.files[0];
    if (!file) return;
    try {
      indexedFiles = [{ file, key: file.name }];
      await openFile(file, file.name);
      refreshFileList();
      setStatus(`Loaded file ${file.name}.`);
    } catch (err) {
      setStatus(`Error: ${String(err)}`);
      alert(String(err));
    }
  });

  folderInput.addEventListener('change', (e) => {
    try {
      indexFolderFiles(e.target.files);
    } catch (err) {
      setStatus(`Error: ${String(err)}`);
      alert(String(err));
    }
  });

  dropzone.addEventListener('dragover', (e) => {
    e.preventDefault();
    dropzone.classList.add('drag');
  });

  dropzone.addEventListener('dragleave', () => {
    dropzone.classList.remove('drag');
  });

  dropzone.addEventListener('drop', async (e) => {
    e.preventDefault();
    dropzone.classList.remove('drag');

    const dt = e.dataTransfer;
    if (!dt) return;

    const files = dt.files;
    if (files && files.length > 1) {
      indexFolderFiles(files);
      return;
    }

    const file = files && files[0];
    if (!file) return;

    try {
      indexedFiles = [{ file, key: file.name }];
      await openFile(file, file.name);
      refreshFileList();
      setStatus(`Loaded file ${file.name}.`);
    } catch (err) {
      setStatus(`Error: ${String(err)}`);
      alert(String(err));
    }
  });

  filterInput.addEventListener('input', refreshFileList);

  toggleZh.addEventListener('click', () => {
    hideZh = !hideZh;
    toggleZh.textContent = hideZh ? 'Show Original' : 'Hide Original';
    renderSegments().catch((err) => {
      setStatus(`Error: ${String(err)}`);
    });
  });

  toggleEn.addEventListener('click', () => {
    hideEn = !hideEn;
    toggleEn.textContent = hideEn ? 'Show Translation' : 'Hide Translation';
    renderSegments().catch((err) => {
      setStatus(`Error: ${String(err)}`);
    });
  });
})();
