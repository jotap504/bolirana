from fastapi import APIRouter, Request
from ..payments.mercadopago import create_qr_payment
from ..config import get_config

router = APIRouter(prefix="/api/payment")


@router.get("/qr/{credits}")
async def get_qr(credits: int, request: Request):
    """Genera QR de pago para N créditos."""
    data = await create_qr_payment(credits)
    return data


@router.post("/webhook")
async def mp_webhook(request: Request):
    """Webhook de MercadoPago — confirma pago y acredita."""
    body = await request.json()
    # En producción verificar firma y consultar la API de MP
    if body.get("type") == "payment" and body.get("data", {}).get("id"):
        cfg     = get_config()
        credits = 1   # TODO: mapear monto → créditos desde la referencia
        await request.app.state.engine.handle_qr_payment(credits, str(body["data"]["id"]))
    return {"ok": True}


@router.get("/cost")
async def get_cost(players: int = 1, mode: str = "classic"):
    cfg = get_config()
    pricing = cfg["pricing"]
    base     = pricing["base_credits_per_player"] * players
    extra    = pricing["mode_extra"].get(mode, 0)
    discount = pricing["group_discount"].get(str(players), 0)
    credits  = max(1, base + extra - discount)
    
    # Calcular descuentos por promociones activas
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

    pesos = int(credits * pricing["pesos_per_credit"] * discount_factor)
    return {
        "players":  players,
        "mode":     mode,
        "credits":  credits,
        "pesos":    pesos,
    }
