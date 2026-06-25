from datetime import datetime
import logging

log = logging.getLogger(__name__)

def get_active_promotions(cfg: dict) -> list:
    """Retorna la lista de promociones activas actualmente según la hora local de la máquina."""
    promotions = cfg.get("promotions", [])
    active = []
    
    now = datetime.now()
    # strftime("%w") retorna '0' para domingo, '1' para lunes, ..., '6' para sábado.
    day_js = int(now.strftime("%w"))
    hour = now.hour
    
    for promo in promotions:
        days = promo.get("days", [])
        if day_js in days:
            sh = promo.get("start_hour", 0)
            eh = promo.get("end_hour", 24)
            if sh < eh:
                if sh <= hour < eh:
                    active.append(promo)
            else:
                # Cruce de medianoche (ej. de 23:00 a 02:00)
                if hour >= sh or hour < eh:
                    active.append(promo)
    return active
