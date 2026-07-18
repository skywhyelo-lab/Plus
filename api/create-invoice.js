const { createOrder } = require('../lib/firebaseAdmin');
const crypto = require('crypto');

const PRICE_USD = 5; // ~200 UAH per the pricing decision

module.exports = async (req, res) => {
  if (req.method !== 'POST') return res.status(405).json({ error: 'Method not allowed' });

  const shopId = process.env.CRYPTOCLOUD_SHOP_ID;
  const apiKey = process.env.CRYPTOCLOUD_API_KEY;
  if (!shopId || !apiKey) return res.status(500).json({ error: 'Payments are not configured yet' });

  const orderId = crypto.randomUUID();

  try {
    await createOrder(orderId, { status: { stringValue: 'pending' }, key: { nullValue: null } });

    // Trybit/CryptoCloud doesn't take success/fail URLs per invoice — those are
    // fixed in the project's dashboard settings (set to /success.html there).
    // The client stores orderId in localStorage before redirecting so success.html
    // knows which order to poll for.
    const invoiceRes = await fetch('https://api.trybit.com/v2/invoice/create', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', Authorization: `Token ${apiKey}` },
      body: JSON.stringify({
        shop_id: shopId,
        amount: PRICE_USD,
        order_id: orderId,
      }),
    });
    const invoiceData = await invoiceRes.json();
    if (!invoiceRes.ok || invoiceData.status !== 'success') {
      throw new Error(`CryptoCloud invoice create failed: ${JSON.stringify(invoiceData)}`);
    }

    res.status(200).json({ pay_url: invoiceData.result.link, order_id: orderId });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Could not create invoice' });
  }
};
