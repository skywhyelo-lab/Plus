const jwt = require('./_jwt');
const { createLicenseKeyDocument, updateOrder } = require('../lib/firebaseAdmin');

// CryptoCloud/Trybit sends the postback as JSON or form-urlencoded depending
// on project settings — this project is configured for JSON.
module.exports = async (req, res) => {
  if (req.method !== 'POST') return res.status(405).end();

  const body = req.body || {};
  const { status, order_id: orderId, token } = body;

  const secret = process.env.CRYPTOCLOUD_SECRET;
  if (token && secret) {
    const valid = jwt.verifyHS256(token, secret);
    if (!valid) {
      console.error('CryptoCloud webhook: invalid token signature');
      return res.status(400).json({ error: 'invalid signature' });
    }
  }

  if (status !== 'success' && status !== 'paid') {
    // Not a paid-confirmation event (e.g. overpaid/partial/canceled) — ack and ignore.
    return res.status(200).json({ ok: true, ignored: true });
  }

  if (!orderId) return res.status(400).json({ error: 'missing order_id' });

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
