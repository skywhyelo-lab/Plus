const { readOrder } = require('../lib/firebaseAdmin');

module.exports = async (req, res) => {
  const orderId = req.query.order_id;
  if (!orderId) return res.status(400).json({ error: 'missing order_id' });

  try {
    const order = await readOrder(orderId);
    if (!order) return res.status(404).json({ status: 'not_found' });
    res.status(200).json({ status: order.status, key: order.key });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'failed to read order' });
  }
};
