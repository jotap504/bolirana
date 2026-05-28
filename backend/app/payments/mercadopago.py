"""
Integración básica con MercadoPago.
Genera un QR de pago y verifica el estado.
"""
import logging, uuid
from ..config import get_config

log = logging.getLogger(__name__)


async def create_qr_payment(credits: int) -> dict:
    """Crea una preferencia de pago y devuelve la URL del QR."""
    cfg = get_config()
    mp_cfg = cfg["mercadopago"]

    if not mp_cfg.get("enabled") or not mp_cfg.get("access_token"):
        return {"mock": True, "qr_data": f"MOCK-QR-{credits}cr-{uuid.uuid4().hex[:8]}",
                "credits": credits, "amount": credits * cfg["pricing"]["pesos_per_credit"]}

    import mercadopago
    sdk = mercadopago.SDK(mp_cfg["access_token"])
    amount = credits * cfg["pricing"]["pesos_per_credit"]
    preference = {
        "items": [{"title": f"Bolirana - {credits} créditos",
                   "quantity": 1, "unit_price": float(amount)}],
        "external_reference": str(uuid.uuid4()),
        "notification_url": mp_cfg.get("notification_url", ""),
    }
    result = sdk.preference().create(preference)
    if result["status"] == 201:
        return {"qr_data": result["response"]["init_point"],
                "credits": credits, "amount": amount,
                "reference": result["response"]["external_reference"]}
    log.error("MP error: %s", result)
    raise Exception("Error creando preferencia MercadoPago")
