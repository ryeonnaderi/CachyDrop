
#include "WebPages.h"

namespace WebPages
{

QByteArray htmlPage()
{
    return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CachyDrop</title>
<style>
body{font:16px system-ui;max-width:540px;margin:8vh auto;padding:16px}
main{border:1px solid #8885;border-radius:20px;padding:24px}
input,button{box-sizing:border-box;width:100%;margin-top:14px;padding:13px}
button{cursor:pointer}
progress{width:100%;margin-top:12px}
#status{overflow-wrap:anywhere}
</style>
</head>
<body>
<main>
<h1>CachyDrop</h1>
<p id="status" role="status" aria-live="polite">Connecting to CachyDrop…</p>

<form id="form" hidden>
<label for="file">Files to send (up to 20 files, 100 MiB total)</label>
<input id="file" type="file" multiple required>
<button id="send">Request batch approval</button>
</form>

<p><a id="outgoing" href="/outgoing-page" hidden>
Download files from this computer</a></p>
<progress id="progress" max="100" value="0"></progress>
</main>
<script>
const form = document.querySelector('#form');
const fileInput = document.querySelector('#file');
const send = document.querySelector('#send');
const status = document.querySelector('#status');
const progress = document.querySelector('#progress');
const outgoing = document.querySelector('#outgoing');

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const params = new URLSearchParams(location.search);
const pairingToken = params.get('pair');
let session = localStorage.getItem('cachydrop-session');

async function jsonFetch(url, options) {
    const r = await fetch(url, options);
    const data = await r.json();
    if (!r.ok) throw new Error(data.error || data.message || 'Request failed');
    return data;
}

async function pairDevice() {
    if (session) {
        const result = await jsonFetch('/session', {
            headers: {'Authorization': 'Bearer ' + session},
            cache: 'no-store'
        }).catch(() => null);

        if (result && result.valid) {
            form.hidden = false;
            outgoing.hidden = false;
            status.textContent = 'Connected to CachyDrop.';
            return;
        }

        localStorage.removeItem('cachydrop-session');
        session = null;
    }

    if (!pairingToken) {
        status.textContent =
            'Scan the QR code shown in the CachyDrop desktop app to pair.';
        return;
    }

    try {
        status.textContent = 'Requesting permission to pair…';

        const request = await jsonFetch('/pair', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({
                token: pairingToken,
                device: /iPhone|iPad/.test(navigator.userAgent)
                    ? 'iPhone or iPad'
                    : /Android/.test(navigator.userAgent)
                        ? 'Android device' : 'Mobile or web device'
            })
        });

        status.textContent =
            'Approve this device in the CachyDrop desktop app.';

        for (let i = 0; i < 120; i++) {
            await sleep(1000);

            const state = await jsonFetch(
                '/pair-status?id=' + encodeURIComponent(request.id),
                {cache: 'no-store'}
            );

            if (state.state === 'approved') {
                session = state.session;
                localStorage.setItem('cachydrop-session', session);
                history.replaceState(null, '', '/');
                form.hidden = false;
                outgoing.hidden = false;
                status.textContent = 'Device paired successfully.';
                return;
            }

            if (state.state !== 'pending')
                throw new Error(state.message || 'Pairing rejected or expired');
        }

        throw new Error('Pairing timed out. Scan the QR code again.');
    } catch (error) {
        status.textContent = 'Pairing failed: ' + error.message;
    }
}

form.addEventListener('submit', async event => {
    event.preventDefault();

    const files = Array.from(fileInput.files);
    const totalSize = files.reduce((sum, file) => sum + file.size, 0);
    if (!files.length || !session) return;
    if (files.length > 20 || totalSize > 100 * 1024 * 1024) {
        status.textContent = "Select at most 20 files totaling no more than 100 MiB.";
        return;
    }

    send.disabled = true;
    progress.value = 0;

    try {
        status.textContent = 'Requesting desktop approval…';

        const request = await jsonFetch('/request', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({
                session,
                files: files.map(file => ({ filename: file.name, size: file.size }))
            })
        });

        let approved = false;

        for (let i = 0; i < 120; i++) {
            await sleep(1000);

            const state = await jsonFetch(
                '/status?id=' + encodeURIComponent(request.id) +
                '&session=' + encodeURIComponent(session),
                {cache: 'no-store'}
            );

            if (state.state === 'approved') {
                approved = true;
                break;
            }

            if (state.state !== 'pending')
                throw new Error(state.message || 'Transfer rejected or expired');

            status.textContent = 'Waiting for approval on your computer…';
        }

        if (!approved) throw new Error('Approval timed out');

        status.textContent = 'Approved. Uploading files…';

        const data = new FormData();
        for (const file of files) data.append('files', file, file.name);

        await new Promise((resolve, reject) => {
            const xhr = new XMLHttpRequest();

            xhr.open('POST',
                '/upload?id=' + encodeURIComponent(request.id) +
                '&session=' + encodeURIComponent(session));

            xhr.upload.addEventListener('progress', event => {
                if (event.lengthComputable)
                    progress.value =
                        Math.round(event.loaded / event.total * 100);
            });

            xhr.addEventListener('load', () => {
                if (xhr.status >= 200 && xhr.status < 300) resolve();
                else reject(new Error(xhr.responseText || 'Upload failed'));
            });

            xhr.addEventListener('error', () =>
                reject(new Error('Network connection failed')));

            xhr.send(data);
        });

        status.textContent = files.length === 1
            ? 'File received successfully!'
            : `${files.length} files received successfully!`;
        progress.value = 100;
        fileInput.value = '';
    } catch (error) {
        status.textContent = 'Transfer failed: ' + error.message;
    } finally {
        send.disabled = false;
    }
});

pairDevice();
</script>
</body>
</html>)HTML";
}

QByteArray outgoingHtmlPage()
{
    return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CachyDrop — Get files</title>
<style>
body{font:16px system-ui;max-width:540px;margin:8vh auto;padding:16px}
main{border:1px solid #8885;border-radius:20px;padding:24px}
button{box-sizing:border-box;width:100%;margin-top:14px;padding:13px}
li{list-style:none;margin:14px 0;padding:12px;border:1px solid #8885;border-radius:10px}
.file-row{display:flex;align-items:flex-start;gap:12px}
.file-row input{width:auto;margin-top:5px;flex-shrink:0}
.file-name{overflow-wrap:anywhere}
.file-size{font-size:.9em;opacity:.75}
#status{overflow-wrap:anywhere}
a{overflow-wrap:anywhere}
</style>
</head>
<body>
<main>
<h1>Get files from CachyDrop</h1>
<p id="status">Checking device pairing…</p>
<button id="load" hidden>Show available files</button>
<div id="selection" hidden>
<label>
<input type="checkbox" id="selectAll">
Select all files
</label>
<ul id="files"></ul>
<button id="download" disabled>Download selected</button>
</div>
<p><a href="/">Back to sending files to the computer</a></p>
</main>
<script>
const load = document.querySelector('#load');
const status = document.querySelector('#status');
const list = document.querySelector('#files');
const selection = document.querySelector('#selection');
const selectAll = document.querySelector('#selectAll');
const download = document.querySelector('#download');
const session = localStorage.getItem('cachydrop-session');

function updateSelection() {
    const boxes = Array.from(
        list.querySelectorAll('input[type="checkbox"][data-id]')
    );
    const checked = boxes.filter(box => box.checked);
    download.disabled = checked.length === 0;
    selectAll.checked = boxes.length > 0 && checked.length === boxes.length;
    selectAll.indeterminate =
        checked.length > 0 && checked.length < boxes.length;
}

selectAll.addEventListener('change', () => {
    list.querySelectorAll('input[type="checkbox"][data-id]')
        .forEach(box => box.checked = selectAll.checked);
    updateSelection();
});

list.addEventListener('change', event => {
    if (event.target.matches('input[type="checkbox"][data-id]'))
        updateSelection();
});

load.addEventListener('click', async () => {
    load.disabled = true;
    list.replaceChildren();
    selection.hidden = true;
    selectAll.checked = false;
    selectAll.indeterminate = false;
    status.textContent = 'Loading available files…';

    try {
        const r = await fetch('/outgoing', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            cache: 'no-store',
            body: JSON.stringify({session})
        });

        const result = await r.json();
        if (!r.ok)
            throw new Error(result.error || 'Could not list files');

        if (!result.files.length) {
            status.textContent =
                'No shared files yet. Select files in the desktop app first.';
            return;
        }

        for (const file of result.files) {
            const item = document.createElement('li');
            const row = document.createElement('label');
            const checkbox = document.createElement('input');
            const details = document.createElement('div');
            const name = document.createElement('div');
            const size = document.createElement('div');

            row.className = 'file-row';
            checkbox.type = 'checkbox';
            checkbox.dataset.id = file.id;
            checkbox.dataset.filename = file.filename;

            name.className = 'file-name';
            name.textContent = file.filename;
            size.className = 'file-size';
            size.textContent =
                (file.size / (1024 * 1024)).toFixed(2) + ' MiB';

            details.append(name, size);
            row.append(checkbox, details);
            item.append(row);
            list.append(item);
        }

        selection.hidden = false;
        status.textContent =
            result.files.length + ' file(s) available. Select what to download.';
        updateSelection();
    } catch (error) {
        status.textContent = 'Error: ' + error.message;
    } finally {
        load.disabled = false;
    }
});

download.addEventListener('click', () => {
    const selected = Array.from(
        list.querySelectorAll('input[type="checkbox"][data-id]:checked')
    );

    if (!selected.length)
        return;

    status.textContent =
        'Starting ' + selected.length +
        ' download(s). Your browser may ask permission for multiple downloads.';

    // Each selected file is downloaded separately.
    // A browser may require permission for multiple automatic downloads.
    for (const box of selected) {
        const link = document.createElement('a');
        link.href = '/download?id=' + encodeURIComponent(box.dataset.id) +
            '&session=' + encodeURIComponent(session);
        link.download = box.dataset.filename || '';
        link.hidden = true;
        document.body.append(link);
        link.click();
        link.remove();
    }
});

if (session) {
    fetch('/session', {
        headers: {'Authorization': 'Bearer ' + session},
        cache: 'no-store'
    }).then(r => {
        if (!r.ok)
            throw new Error('Device is not paired');
        return r.json();
    }).then(data => {
        if (!data.valid)
            throw new Error('Device is not paired');
        status.textContent = 'Connected to CachyDrop.';
        load.hidden = false;
    }).catch(error => {
        status.textContent =
            error.message + '. Return to the main page and pair again.';
    });
} else {
    status.textContent =
        'No paired device. Scan the desktop QR code first.';
}
</script>
</body>
</html>)HTML";
}

} // namespace WebPages
