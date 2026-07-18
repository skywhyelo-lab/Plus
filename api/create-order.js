const { createOrder } = require('../lib/firebaseAdmin');
const crypto = require('crypto');

// Used by the Paddle (card) checkout flow — Paddle.js handles the checkout
// itself client-side, we only need a pending order doc to attach as custom_data
// so the webhook can later write the license key to the right place.
module.exports = async (req, res) => {
  if (req.method !== 'POST') return res.status(405).json({ error: 'Method not allowed' });

  const orderId = crypto.randomUUID();
  try {
    await createOrder(orderId, { status: { stringValue: 'pending' }, key: { nullValue: null } });
    res.status(200).json({ order_id: orderId });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Could not create order' });
  }
};
