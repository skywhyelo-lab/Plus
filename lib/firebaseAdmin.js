// Firestore REST helper, mirrors KeyGenerator/main.cpp (source/repos/Pulse/KeyGenerator).
// Auth: Firebase anonymous-session refresh token bound to a fixed admin UID,
// which is the only UID allowed by firestore.rules to write `keys`/`orders`.

const PROJECT_ID = 'pulse-50386';
const API_KEY = 'AIzaSyD5SJOd8xIKBXnf47nuCPzPLMzPBXwdckg'; // same hardcoded key as the desktop app/keygen
const KEY_ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789'; // no 0/O, 1/I

let cachedIdToken = null;
let cachedExpiry = 0;

async function getAdminIdToken() {
  if (cachedIdToken && Date.now() < cachedExpiry) return cachedIdToken;

  const refreshToken = process.env.FIREBASE_ADMIN_REFRESH_TOKEN;
  if (!refreshToken) throw new Error('FIREBASE_ADMIN_REFRESH_TOKEN is not set');

  const res = await fetch(`https://securetoken.googleapis.com/v1/token?key=${API_KEY}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ grant_type: 'refresh_token', refresh_token: refreshToken }),
  });
  if (!res.ok) throw new Error(`Firebase token refresh failed: ${res.status} ${await res.text()}`);
  const data = await res.json();

  cachedIdToken = data.id_token;
  cachedExpiry = Date.now() + (Number(data.expires_in) - 60) * 1000; // refresh 60s early
  return cachedIdToken;
}

function generateLicenseKey() {
  // Same format as KeyGenerator's GenerateKey(): XXXX-XXXX-XXXX-XXXX from a crypto RNG.
  const crypto = require('crypto');
  const raw = crypto.randomBytes(16);
  let key = '';
  for (let i = 0; i < 16; i++) {
    if (i > 0 && i % 4 === 0) key += '-';
    key += KEY_ALPHABET[raw[i] % KEY_ALPHABET.length];
  }
  return key;
}

async function createLicenseKeyDocument() {
  const idToken = await getAdminIdToken();
  const key = generateLicenseKey();
  const url = `https://firestore.googleapis.com/v1/projects/${PROJECT_ID}/databases/(default)/documents/keys?documentId=${encodeURIComponent(key)}`;
  const res = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${idToken}` },
    body: JSON.stringify({ fields: { hwid: { nullValue: null }, activatedAt: { nullValue: null } } }),
  });
  if (!res.ok) throw new Error(`Firestore key create failed: ${res.status} ${await res.text()}`);
  return key;
}

// orders/{orderId} — link between a CryptoCloud order_id and the license key
// generated for it, so success.html can look the key up after redirect.
async function createOrder(orderId, fields) {
  const idToken = await getAdminIdToken();
  const url = `https://firestore.googleapis.com/v1/projects/${PROJECT_ID}/databases/(default)/documents/orders?documentId=${encodeURIComponent(orderId)}`;
  const res = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${idToken}` },
    body: JSON.stringify({ fields }),
  });
  if (!res.ok) throw new Error(`Firestore order create failed: ${res.status} ${await res.text()}`);
}

// Called by the webhook once payment is confirmed — patches the existing
// order doc (created as "pending" by create-invoice) rather than creating a new one.
async function updateOrder(orderId, fields) {
  const idToken = await getAdminIdToken();
  const fieldPaths = Object.keys(fields).map((k) => `updateMask.fieldPaths=${k}`).join('&');
  const url = `https://firestore.googleapis.com/v1/projects/${PROJECT_ID}/databases/(default)/documents/orders/${encodeURIComponent(orderId)}?${fieldPaths}`;
  const res = await fetch(url, {
    method: 'PATCH',
    headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${idToken}` },
    body: JSON.stringify({ fields }),
  });
  if (!res.ok) throw new Error(`Firestore order update failed: ${res.status} ${await res.text()}`);
}

async function readOrder(orderId) {
  const idToken = await getAdminIdToken();
  const url = `https://firestore.googleapis.com/v1/projects/${PROJECT_ID}/databases/(default)/documents/orders/${encodeURIComponent(orderId)}`;
  const res = await fetch(url, { headers: { Authorization: `Bearer ${idToken}` } });
  if (res.status === 404) return null;
  if (!res.ok) throw new Error(`Firestore order read failed: ${res.status} ${await res.text()}`);
  const doc = await res.json();
  const fields = doc.fields || {};
  return {
    status: fields.status?.stringValue ?? null,
    key: fields.key?.stringValue ?? null,
  };
}

module.exports = { createLicenseKeyDocument, createOrder, updateOrder, readOrder };
