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
    cfg = get_config()["pricing"]
    base     = cfg["base_credits_per_player"] * players
    extra    = cfg["mode_extra"].get(mode, 0)
    discount = cfg["group_discount"].get(str(players), 0)
    credits  = max(1, base + extra - discount)
    return {
        "players":  players,
        "mode":     mode,
        "credits":  credits,
        "pesos":    credits * cfg["pesos_per_credit"],
    }
