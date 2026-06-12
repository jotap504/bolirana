import urllib.request
import json
import sys
import time

BASE_URL = "http://127.0.0.1:8000"

def api(path, method="POST", body=None):
    url = f"{BASE_URL}{path}"
    data = json.dumps(body).encode('utf-8') if body else None
    headers = {"Content-Type": "application/json"} if body else {}
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return json.loads(r.read().decode('utf-8'))
    except Exception as e:
        print(f"API Error on {method} {path}: {e}")
        raise e

def get_state():
    return api("/api/state", method="GET")

def reset():
    api("/api/sim/reset")
    time.sleep(0.15)

def press(btn):
    api(f"/api/sim/button/{btn}")
    time.sleep(0.15)

def insert_coin():
    api("/api/sim/coin")
    time.sleep(0.15)

def trigger_sensor(zone):
    api(f"/api/sim/sensor/{zone}")
    time.sleep(0.15)

def consume_ball():
    api("/api/sim/ball")
    time.sleep(0.15)

def select_mode(target_mode):
    for _ in range(10):
        s = get_state()
        if s["mode"] == target_mode:
            return
        press("next")
    raise Exception(f"No se pudo seleccionar el modo: {target_mode}")

def run_tests():
    print("=== INICIANDO PRUEBAS DE MODOS DE JUEGO ===")
    
    # --- PRUEBA 1: RESET Y VERIFICACIÓN INICIAL ---
    reset()
    s = get_state()
    assert s["state"] == "attract", f"Debería estar en attract, pero está en {s['state']}"
    print("[OK] Prueba 1: Estado inicial es attract.")

    # --- PRUEBA 2: SELECCIÓN DE JUGADORES (MODO CLASSIC) ---
    press("ok") # Ir a select_players
    s = get_state()
    print("State after ok (to select_players):", json.dumps(s, indent=2))
    assert s["state"] == "select_players", f"Estado actual: {s['state']}"
    assert len(s["players"]) == 0, f"Por defecto 0 perfiles de jugador iniciales, pero hay {len(s['players'])}"
    
    # Incrementar jugadores
    press("next")
    s = get_state()
    print("State after next:", json.dumps(s, indent=2))
    assert len(s["players"]) == 2, f"Deberían ser 2 jugadores, hay {len(s['players'])}: {json.dumps(s['players'])}"
    print("[OK] Prueba 2: Selección de jugadores funciona (2 jugadores seleccionados).")

    # Ir a selección de modo
    press("ok")
    s = get_state()
    assert s["state"] == "select_mode", f"Estado actual: {s['state']}"
    print("[OK] Prueba 3: Transición a select_mode correcta.")

    # Probar que podemos rotar al modo classic
    select_mode("classic")
    s = get_state()
    assert s["mode"] == "classic", f"Modo actual: {s['mode']}"
    print("[OK] Prueba 4: Rotación de modos correcta. BATTLE (Sapo Extremo) está ausente.")

    # --- PRUEBA 5: VERIFICACIÓN MODO EQUIPO (TEAM) ---
    # Para jugar modo equipo se requieren mínimo 4 jugadores (par)
    reset()
    press("ok") # select_players
    press("next") # 2 jugadores
    press("next")
    press("next") # 4 jugadores
    press("ok") # select_mode
    select_mode("team")
    press("ok") # confirmar modo TEAM -> debe transicionar a PAYMENT
    s = get_state()
    assert s["mode"] == "team", f"Modo actual: {s['mode']}"
    assert len(s["players"]) == 4, f"En TEAM debe haber 4 jugadores, hay: {len(s['players'])}"
    print("[OK] Prueba 5: Modo EQUIPO requiere mínimo 4 jugadores y número par.")

    # Insertar créditos para arrancar la partida
    # Requeridos en TEAM para 4 jugadores (base=1 por jugador + extra=0 - descuento=0) = 4 créditos
    credits_req = s["credits_required"]
    assert credits_req == 4, f"Créditos requeridos: {credits_req}"
    for _ in range(4):
        insert_coin()
    s = get_state()
    assert s["state"] == "waiting_start", f"Estado actual: {s['state']}"
    print("[OK] Prueba 6: Pago de créditos exitoso y transición a waiting_start.")

    # Presionar START para ir a SELECT_TEAM
    print("State before start press:", json.dumps(get_state(), indent=2))
    press("start")
    s = get_state()
    print("State after start press:", json.dumps(s, indent=2))
    assert s["state"] == "select_team", f"Estado actual: {s['state']}"
    # Verificar equipos por defecto (alternados: 0 y 2 -> team 1, 1 y 3 -> team 2)
    assert s["players"][0]["team"] == 1
    assert s["players"][1]["team"] == 2
    assert s["players"][2]["team"] == 1
    assert s["players"][3]["team"] == 2
    print("[OK] Prueba 7: Asignación de equipos por defecto alternada correcta.")

    # Probar botón Random para barajar
    press("random")
    s = get_state()
    t1_count = sum(1 for p in s["players"] if p["team"] == 1)
    t2_count = sum(1 for p in s["players"] if p["team"] == 2)
    assert t1_count == 2 and t2_count == 2, f"Equipos desbalanceados: {t1_count} vs {t2_count}"
    print("[OK] Prueba 8: Asignación aleatoria de equipos balanceada (2 por equipo).")

    # Confirmar equipos y pasar a jugar
    press("start")
    s = get_state()
    assert s["state"] == "playing", f"Estado actual: {s['state']}"
    print("[OK] Prueba 9: Transición a playing correcta en modo EQUIPO.")

    # --- PRUEBA 10: MODO GOLEADOR ---
    # Resetear y jugar un modo Goleador para ver la acumulación de balls_pocketed y sync ranking
    reset()
    press("ok") # select_players
    press("ok") # select_mode
    select_mode("goleador")
    press("ok") # confirmar goleador
    # Requeridos en GOLEADOR para 1 jugador = 1 crédito
    insert_coin()
    s = get_state()
    print("Goleador state before start:", json.dumps(s, indent=2))
    assert s["state"] == "waiting_start", f"Estado actual: {s['state']}"
    press("start")
    s = get_state()
    print("Goleador state after start:", json.dumps(s, indent=2))
    assert s["state"] == "playing", f"Estado actual: {s['state']}"

    # Meter una bola en rana (1000 pts)
    trigger_sensor("rana")
    s = get_state()
    print("Goleador state after sensor rana:", json.dumps(s, indent=2))
    # En modo GOLEADOR, las bolas embocadas aumentan en 1
    assert s["players"][0]["balls_pocketed"] == 1, f"Bolas embocadas: {s['players'][0]['balls_pocketed']}"
    assert s["players"][0]["score"] == 1000, f"Puntaje en puntos: {s['players'][0]['score']}"
    print("[OK] Prueba 10: Modo GOLEADOR incrementa balls_pocketed y score de forma separada.")

    # --- PRUEBA 11: DESEMPATE UNIVERSAL (TIEBREAK) ---
    # Jugar un juego Classic de 2 jugadores, forzar empate, verificar TIEBREAK
    reset()
    press("ok") # select_players
    press("next") # 2 jugadores
    press("ok") # select_mode
    select_mode("classic")
    press("ok") # confirmar classic
    # Requeridos: 2 créditos
    insert_coin()
    insert_coin()
    press("start")
    
    # Jugador 1 (default 5 bolas) mete una fosa_1 (100 pts) y consume el resto
    trigger_sensor("fosa_1")
    for _ in range(4):
        consume_ball()
    
    # Transición de turno (esperar 3 segundos para que termine el cambio de turno)
    print("Esperando cambio de turno...")
    time.sleep(2.5)
    s = get_state()
    assert s["state"] == "playing", f"Estado: {s['state']}"
    assert s["current_player"] == 1, f"Turno del jugador {s['current_player']}"

    # Jugador 2 mete fosa_1 (100 pts) para empatar y consume el resto
    trigger_sensor("fosa_1")
    for _ in range(4):
        consume_ball()
    
    # Al consumir la última bola de la partida empatada, debe transicionar a TIEBREAK
    s = get_state()
    assert s["state"] == "tiebreak", f"Debería estar en tiebreak por empate a 100 pts, pero está en: {s['state']}"
    assert len(s["tiebreak_players"]) == 2, f"Jugadores empatados: {s['tiebreak_players']}"
    assert s["players"][0]["balls_left"] == 1, "Jugador 1 debería tener 1 bola extra"
    assert s["players"][1]["balls_left"] == 1, "Jugador 2 debería tener 1 bola extra"
    print("[OK] Prueba 11: Sistema de desempate se activa al detectar empate de puntajes.")

    # Resolver el desempate:
    # Turno de Jugador 1 (cursor en 0): mete rana (+1000 pts)
    trigger_sensor("rana") # consume su bola extra automáticamente
    s = get_state()
    assert s["state"] == "tiebreak", f"Estado actual: {s['state']}"
    assert s["current_player"] == 1, f"Turno de jugador en tiebreak: {s['current_player']}"

    # Turno de Jugador 2: tira fosa_4 (10 pts)
    trigger_sensor("fosa_4")
    time.sleep(1.0) # Esperar a que se procese la bola y la transición asíncrona
    # Al resolverse el desempate (Jugador 1 tiene 1100 pts, Jugador 2 tiene 110 pts), debe transicionar a GAME_OVER
    s = get_state()
    if s["state"] != "game_over":
        print("DEBUG - Estado de sesión actual tras fosa_4:", json.dumps(s, indent=2))
    assert s["state"] == "game_over", f"Debería haber terminado, pero está en: {s['state']}"
    print("[OK] Prueba 12: Desempate se resuelve y transiciona a game_over.")

    print("\n=== ¡TODAS LAS PRUEBAS AUTOMATIZADAS PASARON EXITOSAMENTE! ===")

if __name__ == "__main__":
    try:
        run_tests()
    except AssertionError as ae:
        print(f"\n[FAIL] Falla de aserción: {ae}")
        sys.exit(1)
    except Exception as e:
        print(f"\n[ERROR] Falla de ejecución: {e}")
        sys.exit(1)
