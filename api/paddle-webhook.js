const crypto = require('crypto');
const { createLicenseKeyDocument, updateOrder } = require('../lib/firebaseAdmin');

// Vercel auto-parses JSON bodies by default — we need the exact raw bytes to
// verify Paddle's HMAC signature, so parsing is disabled for this function.
module.exports.config = { api: { bodyParser: false } };

function getRawBody(req) {
  return new Promise((resolve, reject) => {
    let data = '';
    req.on('data', (chunk) => (data += chunk));
    req.on('end', () => resolve(data));
    req.on('error', reject);
  });
}

function verifySignature(rawBody, header, secret) {
  if (!header) return false;
  const parts = Object.fromEntries(header.split(';').map((p) => p.split('=')));
  const { ts, h1 } = parts;
  if (!ts || !h1) return false;

  const expected = crypto
    .createHmac('sha256', secret)
    .update(`${ts}:${rawBody}`)
    .digest('hex');

  const a = Buffer.from(expected);
  const b = Buffer.from(h1);
  if (a.length !== b.length) return false;
  return crypto.timingSafeEqual(a, b);
}

module.exports = async (req, res) => {
  if (req.method !== 'POST') return res.status(405).end();

  const rawBody = await getRawBody(req);
  const secret = process.env.PADDLE_WEBHOOK_SECRET;
  const signatureHeader = req.headers['paddle-signature'];

  if (secret && !verifySignature(rawBody, signatureHeader, secret)) {
    console.error('Paddle webhook: invalid signature');
    return res.status(400).json({ error: 'invalid signature' });
  }

  let event;
  try {
    event = JSON.parse(rawBody);
  } catch {
    return res.status(400).json({ error: 'invalid json' });
  }

  if (event.event_type !== 'transaction.completed') {
    return res.status(200).json({ ok: true, ignored: true });
  }

  const orderId = event.data?.custom_data?.order_id;
  if (!orderId) return res.status(400).json({ error: 'missing order_id in custom_data' });

  try {
    const key = await createLicenseKeyDocument();
    await updateOrder(orderId, {
      status: { stringValue: 'paid' },
      key: { stringValue: key },
    });
    res.status(200).json({ ok: true });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'failed to process payment' });
  }
};
