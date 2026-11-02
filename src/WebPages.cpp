
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
const SIGNING_SECRET_KEY_STORAGE = 'cachydrop-signing-secret-key';
const ENCRYPTION_PUBLIC_KEY_STORAGE = 'cachydrop-encryption-public-key';
const ENCRYPTION_SECRET_KEY_STORAGE = 'cachydrop-encryption-secret-key';
const ENCRYPTION_DESKTOP_PUBLIC_KEY_STORAGE = 'cachydrop-desktop-encryption-public-key';

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

async function getOrCreateEncryptionKeyPair(sodium) {
    const publicHex = localStorage.getItem(
        ENCRYPTION_PUBLIC_KEY_STORAGE
    );
    const secretHex = localStorage.getItem(
        ENCRYPTION_SECRET_KEY_STORAGE
    );

    if (publicHex && secretHex) {
        const publicKey = sodium.from_hex(publicHex);
        const privateKey = sodium.from_hex(secretHex);

        if (
            publicKey.length === sodium.crypto_box_PUBLICKEYBYTES &&
            privateKey.length === sodium.crypto_box_SECRETKEYBYTES
        ) {
            const derivedPublicKey = sodium.crypto_scalarmult_base(
                privateKey
            );

            if (sodium.memcmp(publicKey, derivedPublicKey)) {
                sodium.memzero(derivedPublicKey);
                return {publicKey, privateKey};
            }

            sodium.memzero(derivedPublicKey);
        }

        sodium.memzero(publicKey);
        sodium.memzero(privateKey);

        throw new Error(
            'Stored encryption keys are invalid. Clear this device’s CachyDrop site data and pair again.'
        );
    }

    if (publicHex || secretHex) {
        throw new Error(
            'The encryption key pair is incomplete. Clear this device’s CachyDrop site data and pair again.'
        );
    }

    const keys = sodium.crypto_box_keypair();

    try {
        localStorage.setItem(
            ENCRYPTION_PUBLIC_KEY_STORAGE,
            sodium.to_hex(keys.publicKey)
        );

        localStorage.setItem(
            ENCRYPTION_SECRET_KEY_STORAGE,
            sodium.to_hex(keys.privateKey)
        );

        return {
            publicKey: keys.publicKey.slice(),
            privateKey: keys.privateKey.slice()
        };
    } finally {
        sodium.memzero(keys.privateKey);
    }
}


function deriveSharedSecret(sodium, desktopPublicKey, browserPrivateKey) {
    if (
        desktopPublicKey.length !== sodium.crypto_box_PUBLICKEYBYTES ||
        browserPrivateKey.length !== sodium.crypto_box_SECRETKEYBYTES
    ) {
        throw new Error('Invalid encryption key length.');
    }

    return sodium.crypto_box_beforenm(
        desktopPublicKey,
        browserPrivateKey
    );
}








function deriveTransferKey(sodium, sharedSecret, context) {
    const prefix = sodium.from_string('CachyDrop transfer key v1:');
    const contextBytes = sodium.from_string(context);
    const input = new Uint8Array(prefix.length + contextBytes.length);

    input.set(prefix, 0);
    input.set(contextBytes, prefix.length);

    try {
        return sodium.crypto_generichash(
            sodium.crypto_generichash_BYTES,
            input,
            sharedSecret
        );
    } finally {
        sodium.memzero(prefix);
        sodium.memzero(contextBytes);
        sodium.memzero(input);
    }
}

function encryptFile(sodium, key, plaintext, associatedData) {
    const nonce = sodium.randombytes_buf(
        sodium.crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
    );
    const ad = sodium.from_string(associatedData);

    try {
        const ciphertext =
            sodium.crypto_aead_xchacha20poly1305_ietf_encrypt(
                new Uint8Array(plaintext),
                ad,
                null,
                nonce,
                key
            );

        const encrypted = new Uint8Array(
            nonce.length + ciphertext.length
        );

        encrypted.set(nonce, 0);
        encrypted.set(ciphertext, nonce.length);

        sodium.memzero(ciphertext);
        return encrypted;
    } finally {
        sodium.memzero(nonce);
        sodium.memzero(ad);
    }
}






async function getSigningContext() {
    const module = await import('/libsodium-bundle.mjs');
    const sodium = module.default;
    await sodium.ready;

    const secretHex = localStorage.getItem(
        SIGNING_SECRET_KEY_STORAGE
    );

    if (!secretHex) {
        throw new Error(
            'Signing key is missing. Please pair this device again.'
        );
    }

    const secretKey = sodium.from_hex(secretHex);

    if (secretKey.length !== sodium.crypto_sign_SECRETKEYBYTES) {
        sodium.memzero(secretKey);
        throw new Error('Invalid device signing key. Please pair again.');
    }

    return {sodium, secretKey};
}

function buildMultipartBody(files) {
   const boundary =
    '----CachyDrop-' +
    Array.from(
        crypto.getRandomValues(new Uint8Array(16)),
        byte => byte.toString(16).padStart(2, '0')
    ).join('');
    const encoder = new TextEncoder();
    const parts = [];

    for (const file of files) {
        const safeHeaderName = file.name
            .replace(/[\r\n"]/g, '_');

        parts.push(encoder.encode(
            `--${boundary}\r\n` +
            `Content-Disposition: form-data; name="files"; filename="${safeHeaderName}"\r\n` +
            'Content-Type: application/octet-stream\r\n' +
            '\r\n'
        ));

        parts.push(new Uint8Array(file.bytes));

        parts.push(encoder.encode('\r\n'));
    }

    parts.push(encoder.encode(`--${boundary}--\r\n`));

    const length = parts.reduce((sum, part) => sum + part.length, 0);
    const body = new Uint8Array(length);

    let offset = 0;

    for (const part of parts) {
        body.set(part, offset);
        offset += part.length;
    }

    return {
        boundary,
        body
    };
}


async function signedFetch(url, options = {}) {
    const {sodium, secretKey} = await getSigningContext();

    try {
        const method = (options.method || 'GET').toUpperCase();
        const requestUrl = new URL(url, window.location.href);

        if (requestUrl.origin !== window.location.origin) {
            throw new Error('Cross-origin requests are not permitted.');
        }

        let bodyBytes;

        if (typeof options.body === 'string') {
            bodyBytes = sodium.from_string(options.body);
        } else if (options.body instanceof Uint8Array) {
            bodyBytes = options.body;
        } else if (options.body instanceof ArrayBuffer) {
            bodyBytes = new Uint8Array(options.body);
        } else if (options.body == null) {
            bodyBytes = new Uint8Array(0);
        } else {
            throw new Error(
                'This request body type is not supported by signedFetch yet.'
            );
        }

        const timestamp = Math.floor(Date.now() / 1000).toString();
        const requestId = sodium.to_hex(sodium.randombytes_buf(16));
        const bodyHash = sodium.to_hex(
            sodium.crypto_generichash(
                sodium.crypto_generichash_BYTES,
                bodyBytes
            )
        );

        const canonicalRequest = [
            'CachyDrop-Request-v1',
            method,
            requestUrl.pathname + requestUrl.search,
            timestamp,
            requestId,
            bodyHash
        ].join('\n');

        const signature = sodium.crypto_sign_detached(
            sodium.from_string(canonicalRequest),
            secretKey
        );

        const headers = new Headers(options.headers || {});
        headers.set('X-CachyDrop-Timestamp', timestamp);
        headers.set('X-CachyDrop-Request-Id', requestId);
        headers.set(
            'X-CachyDrop-Signature',
            sodium.to_hex(signature)
        );

        return await fetch(requestUrl.href, {
            ...options,
            method,
            headers
        });
    } finally {
        sodium.memzero(secretKey);
    }
}

async function jsonFetch(url, options = {}) {
    const response = await signedFetch(url, options);

    let data;

    try {
        data = await response.json();
    } catch {
        throw new Error('The server returned an invalid response.');
    }

    if (!response.ok) {
        throw new Error(
            data.error || data.message || 'Request failed'
        );
    }

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
        status.textContent = 'Preparing cryptographic keys…';

        const module = await import('/libsodium-bundle.mjs');
        const sodium = module.default;
        await sodium.ready;

        const params = new URLSearchParams(window.location.search);
        const desktopPublicKeyHex = params.get('key');

        if (!desktopPublicKeyHex) {
            throw new Error(
                'Desktop public key is missing from the pairing URL.'
            );
        }

        
const desktopPublicKey = sodium.from_hex(desktopPublicKeyHex);

if (desktopPublicKey.length !== sodium.crypto_box_PUBLICKEYBYTES) {
    sodium.memzero(desktopPublicKey);
    throw new Error('Invalid desktop public key.');
}

localStorage.setItem(
    ENCRYPTION_DESKTOP_PUBLIC_KEY_STORAGE,
    desktopPublicKeyHex.toLowerCase()
);


        const browserKeys = await getOrCreateEncryptionKeyPair(sodium);

        try {
            const signingKeys = sodium.crypto_sign_keypair();

            try {
                if (
                    signingKeys.publicKey.length !==
                        sodium.crypto_sign_PUBLICKEYBYTES ||
                    signingKeys.privateKey.length !==
                        sodium.crypto_sign_SECRETKEYBYTES
                ) {
                    throw new Error('Invalid Ed25519 key pair.');
                }

                localStorage.setItem(
                    SIGNING_SECRET_KEY_STORAGE,
                    sodium.to_hex(signingKeys.privateKey)
                );

                status.textContent = 'Requesting permission to pair…';

                const request = await jsonFetch('/pair', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({
                        token: pairingToken,
                        device: /iPhone|iPad/.test(navigator.userAgent)
                            ? 'iPhone or iPad'
                            : /Android/.test(navigator.userAgent)
                                ? 'Android device'
                                : 'Mobile or web device',
                        publicKey: sodium.to_hex(browserKeys.publicKey),
                        signingPublicKey: sodium.to_hex(
                            signingKeys.publicKey
                        )
                    })
                });

                status.textContent =
                    'Approve this device in the CachyDrop desktop app.';

                for (let i = 0; i < 120; i++) {
                    await sleep(1000);

                    const state = await jsonFetch(
                        '/pair-status?id=' +
                            encodeURIComponent(request.id),
                        {cache: 'no-store'}
                    );

                    if (state.state === 'approved') {
                        session = state.session;

                        localStorage.setItem(
                            'cachydrop-session',
                            session
                        );

                        history.replaceState(null, '', '/');
                        form.hidden = false;
                        outgoing.hidden = false;
                        status.textContent = 'Device paired successfully.';
                        return;
                    }

                    if (state.state !== 'pending') {
                        throw new Error(
                            state.message || 'Pairing rejected or expired'
                        );
                    }
                }

                throw new Error(
                    'Pairing timed out. Scan the QR code again.'
                );
            } finally {
                sodium.memzero(signingKeys.privateKey);
            }
        } finally {
            sodium.memzero(browserKeys.privateKey);
            sodium.memzero(desktopPublicKey);
        }
    } catch (error) {
        status.textContent = 'Pairing failed: ' + error.message;
        console.error('CachyDrop pairing error:', error);
    }
}

pairDevice();

form.addEventListener('submit', async event => {
    event.preventDefault();

    const files = Array.from(fileInput.files);
    const totalSize = files.reduce((sum, file) => sum + file.size, 0);

    if (!files.length || !session) return;

    if (files.length > 20 || totalSize > 100 * 1024 * 1024) {
        status.textContent =
            "Select at most 20 files totaling no more than 100 MiB.";
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
                files: files.map(file => ({
                    filename: file.name,
                    size: file.size
                }))
            })
        });
        if (
    !Array.isArray(request.files) ||
    request.files.length !== files.length
) {
    throw new Error('Invalid approved file list from desktop.');
}

const approvedFiles = request.files.map((approved, index) => {
    if (
        typeof approved.filename !== 'string' ||
        !Number.isSafeInteger(approved.size) ||
        approved.size !== files[index].size
    ) {
        throw new Error('Approved file details do not match selection.');
    }

    return approved;
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

            if (state.state !== 'pending') {
                throw new Error(
                    state.message || 'Transfer rejected or expired'
                );
            }

            status.textContent =
                'Waiting for approval on your computer…';
        }

        if (!approved) {
            throw new Error('Approval timed out');
        }

        status.textContent = 'Approved. Uploading files…';

        const module = await import('/libsodium-bundle.mjs');
const sodium = module.default;
await sodium.ready;

const desktopPublicKeyHex = localStorage.getItem(
    ENCRYPTION_DESKTOP_PUBLIC_KEY_STORAGE
);
const browserSecretHex = localStorage.getItem(
    ENCRYPTION_SECRET_KEY_STORAGE
);

if (!desktopPublicKeyHex || !browserSecretHex) {
    throw new Error(
        'Encryption keys are missing. Please pair this browser again.'
    );
}

const desktopPublicKey = sodium.from_hex(desktopPublicKeyHex);
const browserPrivateKey = sodium.from_hex(browserSecretHex);

if (
    desktopPublicKey.length !== sodium.crypto_box_PUBLICKEYBYTES ||
    browserPrivateKey.length !== sodium.crypto_box_SECRETKEYBYTES
) {
    sodium.memzero(desktopPublicKey);
    sodium.memzero(browserPrivateKey);
    throw new Error('Invalid encryption keys. Please pair again.');
}

const sharedSecret = deriveSharedSecret(
    sodium,
    desktopPublicKey,
    browserPrivateKey
);

let transferKey;

try {
    transferKey = deriveTransferKey(
        sodium,
        sharedSecret,
        'upload-v1:' + request.id
    );
} finally {
    sodium.memzero(sharedSecret);
    sodium.memzero(desktopPublicKey);
    sodium.memzero(browserPrivateKey);
}

    let encryptedFiles;

try {
    encryptedFiles = await Promise.all(
        files.map(async (file, index) => {
            const plaintext = await file.arrayBuffer();
            const approvedFile = approvedFiles[index];

            const associatedData = [
                'CachyDrop-File-v1',
                'upload',
                request.id,
                index.toString(),
                approvedFile.filename,
                approvedFile.size.toString()
            ].join('\n');

            try {
                const encryptedBytes = encryptFile(
                    sodium,
                    transferKey,
                    plaintext,
                    associatedData
                );

                return {
                    name: approvedFile.filename,
                    bytes: encryptedBytes
                };
            } finally {
                sodium.memzero(new Uint8Array(plaintext));
            }
        })
    );
} finally {
    sodium.memzero(transferKey);
}

const multipart = buildMultipartBody(encryptedFiles);

        const response = await signedFetch(
    '/upload?id=' + encodeURIComponent(request.id) +
    '&session=' + encodeURIComponent(session),
    {
        method: 'POST',
        headers: {
            'Content-Type':
                'multipart/form-data; boundary=' + multipart.boundary
        },
        body: multipart.body
    }
);

if (!response.ok) {
    throw new Error(await response.text() || 'Upload failed');
}

progress.value = 100;

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
    selectAll.checked =
        boxes.length > 0 && checked.length === boxes.length;
    selectAll.indeterminate =
        checked.length > 0 && checked.length < boxes.length;
}

selectAll.addEventListener('change', () => {
    list.querySelectorAll('input[type="checkbox"][data-id]')
        .forEach(box => box.checked = selectAll.checked);

    updateSelection();
});

list.addEventListener('change', event => {
    if (event.target.matches('input[type="checkbox"][data-id]')) {
        updateSelection();
    }
});

async function decryptDownloadedFile(
    sodium,
    encryptedBytes,
    transferKey,
    fileId,
    filename,
    size
) {
    const associatedData = [
        'CachyDrop-File-v1',
        'download',
        fileId,
        '0',
        filename,
        size.toString()
    ].join('\n');

    const ad = sodium.from_string(associatedData);

    try {
        return sodium.crypto_aead_xchacha20poly1305_ietf_decrypt(
            null,
            encryptedBytes.subarray(
                sodium.crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
            ),
            ad,
            encryptedBytes.subarray(
                0,
                sodium.crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
            ),
            transferKey
        );
    } finally {
        sodium.memzero(ad);
    }
}

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

        if (!r.ok) {
            throw new Error(result.error || 'Could not list files');
        }

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
            checkbox.dataset.size = String(file.size);

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
            result.files.length +
            ' file(s) available. Select what to download.';

        updateSelection();
    } catch (error) {
        status.textContent = 'Error: ' + error.message;
    } finally {
        load.disabled = false;
    }
});


download.addEventListener('click', async () => {
    const selected = Array.from(
        list.querySelectorAll(
            'input[type="checkbox"][data-id]:checked'
        )
    );

    if (!selected.length || !session) return;

    download.disabled = true;

    let sodium;
    let desktopPublicKey;
    let browserPrivateKey;
    let sharedSecret;
    let transferKey;

    try {
        const module = await import('/libsodium-bundle.mjs');
        sodium = module.default;
        await sodium.ready;

        const desktopPublicHex = localStorage.getItem(
            'cachydrop-desktop-encryption-public-key'
        );
        const browserSecretHex = localStorage.getItem(
            'cachydrop-encryption-secret-key'
        );

        if (!desktopPublicHex || !browserSecretHex) {
            throw new Error(
                'Encryption keys are missing. Pair this browser again.'
            );
        }

        desktopPublicKey = sodium.from_hex(desktopPublicHex);
        browserPrivateKey = sodium.from_hex(browserSecretHex);

        if (
            desktopPublicKey.length !== sodium.crypto_box_PUBLICKEYBYTES ||
            browserPrivateKey.length !== sodium.crypto_box_SECRETKEYBYTES
        ) {
            throw new Error('Invalid encryption keys. Please pair again.');
        }

        sharedSecret = sodium.crypto_box_beforenm(
            desktopPublicKey,
            browserPrivateKey
        );

        for (const box of selected) {
            const fileId = box.dataset.id;
            const filename = box.dataset.filename || 'download';
            const size = Number(box.dataset.size);

            if (!Number.isSafeInteger(size) || size < 0) {
                throw new Error('Invalid file size.');
            }

            status.textContent = 'Downloading ' + filename + '…';

            const response = await fetch(
                '/download?id=' + encodeURIComponent(fileId),
                {
                    headers: {
                        'Authorization': 'Bearer ' + session
                    },
                    cache: 'no-store'
                }
            );

            if (!response.ok) {
                throw new Error('Could not download ' + filename);
            }

            const encryptedBytes = new Uint8Array(
                await response.arrayBuffer()
            );

            let plaintext;

            try {
                if (encryptedBytes.length !== size + 40) {
                    throw new Error(
                        'Encrypted file size is invalid: ' + filename
                    );
                }

                const prefix = sodium.from_string(
                    'CachyDrop transfer key v1:'
                );
                const context = sodium.from_string(
                    'download-v1:' + fileId
                );
                const keyInput = new Uint8Array(
                    prefix.length + context.length
                );

                try {
                    keyInput.set(prefix);
                    keyInput.set(context, prefix.length);

                    transferKey = sodium.crypto_generichash(
                        sodium.crypto_generichash_BYTES,
                        keyInput,
                        sharedSecret
                    );
                } finally {
                    sodium.memzero(prefix);
                    sodium.memzero(context);
                    sodium.memzero(keyInput);
                }

                try {
                    plaintext = await decryptDownloadedFile(
                        sodium,
                        encryptedBytes,
                        transferKey,
                        fileId,
                        filename,
                        size
                    );
                } catch {
                    throw new Error(
                        'Decryption or authentication failed for ' +
                        filename
                    );
                }

                if (plaintext.length !== size) {
                    throw new Error(
                        'Decrypted file size is invalid: ' + filename
                    );
                }

                const blob = new Blob([plaintext], {
                    type: 'application/octet-stream'
                });
                const objectUrl = URL.createObjectURL(blob);
                const link = document.createElement('a');

                link.href = objectUrl;
                link.download = filename;
                link.hidden = true;
                document.body.append(link);
                link.click();
                link.remove();

                setTimeout(() => URL.revokeObjectURL(objectUrl), 60000);
            } finally {
                if (transferKey) {
                    sodium.memzero(transferKey);
                    transferKey = undefined;
                }

                if (plaintext) {
                    sodium.memzero(plaintext);
                }

                sodium.memzero(encryptedBytes);
            }
        }

        status.textContent = 'Download(s) completed.';
    } catch (error) {
        status.textContent = 'Download failed: ' + error.message;
    } finally {
        if (sodium) {
            if (transferKey) sodium.memzero(transferKey);
            if (sharedSecret) sodium.memzero(sharedSecret);
            if (desktopPublicKey) sodium.memzero(desktopPublicKey);
            if (browserPrivateKey) sodium.memzero(browserPrivateKey);
        }

        download.disabled = false;
    }
});


if (session) {
    fetch('/session', {
        headers: {'Authorization': 'Bearer ' + session},
        cache: 'no-store'
    }).then(r => {
        if (!r.ok) {
            throw new Error('Device is not paired');
        }

        return r.json();
    }).then(data => {
        if (!data.valid) {
            throw new Error('Device is not paired');
        }

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
