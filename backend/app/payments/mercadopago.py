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

    # Calcular monto aplicando descuentos por promociones activas
    from ..game.promotions import get_active_promotions
    active_promos = get_active_promotions(cfg)
    discount_factor = 1.0
    for promo in active_promos:
        if "discount_pct" in promo and promo["discount_pct"] > 0:
            discount_factor *= (1.0 - promo["discount_pct"])
        if "credits_multiplier" in promo and promo["credits_multiplier"] > 1:
            discount_factor *= (1.0 / promo["credits_multiplier"])
        elif promo.get("id") == "happy_hour":
            discount_factor *= 0.5

    amount = int(credits * cfg["pricing"]["pesos_per_credit"] * discount_factor)

    if not mp_cfg.get("enabled") or not mp_cfg.get("access_token"):
        return {"mock": True, "qr_data": f"MOCK-QR-{credits}cr-{uuid.uuid4().hex[:8]}",
                "credits": credits, "amount": amount}

    from .crypto import decrypt_data
    decrypted_token = decrypt_data(mp_cfg["access_token"])
    if not decrypted_token:
        log.warning("No se pudo desencriptar el token de MercadoPago. Iniciando en modo MOCK.")
        return {"mock": True, "qr_data": f"MOCK-QR-{credits}cr-{uuid.uuid4().hex[:8]}",
                "credits": credits, "amount": amount}

    import mercadopago
    sdk = mercadopago.SDK(decrypted_token)
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
