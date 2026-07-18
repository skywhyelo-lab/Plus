const crypto = require('crypto');

function base64UrlDecode(str) {
  str = str.replace(/-/g, '+').replace(/_/g, '/');
  while (str.length % 4) str += '=';
  return Buffer.from(str, 'base64');
}

// Verifies a JWT's HS256 signature (used for CryptoCloud/Trybit postback `token`).
// No claim validation beyond signature — the caller checks status/order_id itself.
function verifyHS256(token, secret) {
  const parts = token.split('.');
  if (parts.length !== 3) return false;
  const [headerB64, payloadB64, sigB64] = parts;

  const expectedSig = crypto
    .createHmac('sha256', secret)
    .update(`${headerB64}.${payloadB64}`)
    .digest();
  const actualSig = base64UrlDecode(sigB64);

  if (expectedSig.length !== actualSig.length) return false;
  return crypto.timingSafeEqual(expectedSig, actualSig);
}

module.exports = { verifyHS256 };
