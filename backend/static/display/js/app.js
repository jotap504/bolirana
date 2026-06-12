/* Punto de entrada — conecta WS, despacha mensajes a Screens */

document.addEventListener("DOMContentLoaded", () => {
  let prevCredits = 0;
  let currentState = "";

  WS.connect();

  // ── Estado del servidor → pantallas ──────────────────────────────────────
  WS.on("state", (msg) => {
    const nextState = msg.state;

    // Detener fuegos artificiales si salimos de game_over
    if (currentState === "game_over" && nextState !== "game_over") {
      FX.stopVictoryFireworks();
    }

    // Tocar sonido de inicio si entramos a playing
    if (nextState === "playing" && currentState !== "playing") {
      AudioFX.playStart();
    }

    // Tocar sonido de fin y arrancar fuegos artificiales al terminar el juego
    if (nextState === "game_over" && currentState !== "game_over") {
      AudioFX.playGameOver();
      FX.startVictoryFireworks();
    }

    currentState = nextState;
    Screens.show(msg.state);

    const persistentCredsVal = document.getElementById("persistent-credits-val");
    if (persistentCredsVal && msg.credits !== undefined) {
      persistentCredsVal.textContent = msg.credits;
    }

    switch (msg.state) {
      case "select_players": Screens.updateSelectPlayers(msg); break;
      case "select_mode": Screens.updateSelectMode(msg); break;
      case "payment": Screens.updatePayment(msg); break;
      case "waiting_start": Screens.updateWaitingStart(msg); break;
      case "select_team": Screens.updateSelectTeam(msg); break;
      case "tiebreak": Screens.updateTiebreak(msg); break;
      case "playing": Screens.updateGame(msg); break;
      case "turn_change": Screens.updateTurnChange(msg); break;
      case "game_over": {
        let scores = msg.scores;
        if (!scores) {
          if (msg.mode === "goleador") {
            scores = [...(msg.players || [])].sort((a, b) => (b.balls_pocketed - a.balls_pocketed) || (b.score - a.score));
          } else {
            scores = [...(msg.players || [])].sort((a, b) => b.score - a.score);
          }
        }
        // Pasar team_scores para que updateGameOver pueda mostrar el equipo ganador
        Screens.updateGameOver({ scores, winner_index: msg.winner_index ?? (scores[0]?.index ?? 0), mode: msg.mode, team_scores: msg.team_scores });
        break;
      }
    }
  });

  // ── Eventos de juego ─────────────────────────────────────────────────────
  WS.on("score", (msg) => {
    const card = document.getElementById("score-card-" + msg.player_index) || document.getElementById("tiebreak-card-" + msg.player_index);
    const cx = card ? card.getBoundingClientRect().left + card.offsetWidth / 2 : window.innerWidth / 2;
    const cy = card ? card.getBoundingClientRect().top + card.offsetHeight / 2 : window.innerHeight / 2;
    Screens.animateScore(msg.player_index, msg.delta, msg.total, cx, cy);
  });

  WS.on("credits", (msg) => {
    Screens.updateCredits(msg.total, msg.required);

    // Tocar sonido coin si aumentaron las monedas
    if (msg.total > prevCredits) {
      AudioFX.playCoin();
      // Disparar destello dorado en el slot
      const slot = document.querySelector(".fichas-slot");
      if (slot) {
        const rect = slot.getBoundingClientRect();
        FX.goalBurst(rect.left + rect.width / 2, rect.top + rect.height / 2, 0xf5c000);
      }
    }
    prevCredits = msg.total;

    const gcred = document.getElementById("game-credits");
    if (gcred) gcred.textContent = msg.total;
    const pcred = document.getElementById("pause-credits");
    if (pcred) pcred.textContent = msg.total;

    const persistentCredsVal = document.getElementById("persistent-credits-val");
    if (persistentCredsVal) persistentCredsVal.textContent = msg.total;
  });

  WS.on("timer", (msg) => {
    const el = document.getElementById("timer-val");
    if (el) {
      el.textContent = msg.seconds_left;
      el.style.color = msg.seconds_left <= 10 ? "var(--red)" : "var(--red)";
    }
  });

  WS.on("proximity_alert", (msg) => {
    const overlay = document.getElementById("proximity-cheat-overlay");
    if (overlay) {
      if (msg.active) {
        overlay.style.display = "flex";
        if (typeof AudioFX !== 'undefined' && AudioFX.playAlarm) {
          AudioFX.playAlarm();
        }
      } else {
        overlay.style.display = "none";
      }
    }
  });

  WS.on("turn", (msg) => {
    const nameEl = document.getElementById("turn-name");
    if (nameEl) nameEl.textContent = msg.player_name;
    const ballsEl = document.getElementById("ball-icons");
    if (ballsEl) {
      const left = msg.balls_left || 0;
      const total = parseInt(document.querySelector("[data-balls-total]")?.dataset.ballsTotal || 5);
      ballsEl.textContent = "⚫".repeat(left) + "⚪".repeat(Math.max(0, total - left));
    }
    document.querySelectorAll(".score-card, .score-row").forEach((c, i) => {
      c.classList.toggle("current-turn", i === msg.current_player);
      // Restaurar estilos inline para permitir que la clase CSS vuelva a agrandarla
      c.style.transform = "";
      c.style.boxShadow = "";
    });
  });

  WS.on("game_over", (msg) => {
    Screens.show("game_over");
    Screens.updateGameOver(msg);
  });

  // ── Teclado de simulación (hasta tener hardware físico) ──────────────────
  // Esc → pantalla de inicio  |  Enter → confirmar  |  ↑↓ → navegar
  // 1 → insertar ficha        |  2 → pago QR simulado
  document.addEventListener("keydown", (e) => {
    const btnMap = {
      ArrowUp: "prev",
      ArrowDown: "next",
      ArrowLeft: "prev",
      ArrowRight: "next",
      Enter: "ok",
      " ": "start",
      p: "pause",
    };

    if (btnMap[e.key] !== undefined) {
      e.preventDefault();
      AudioFX.playTick();
      WS.btn(btnMap[e.key]);
      return;
    }

    switch (e.key) {
      case "Escape":
        e.preventDefault();
        AudioFX.playTick();
        fetch("/api/sim/reset", { method: "POST" });
        break;
      case "1":
        // El incremento de créditos reproducirá AudioFX.playCoin a través de WS
        fetch("/api/sim/coin", { method: "POST" });
        break;
      case "2":
        AudioFX.playTick();
        fetch("/api/sim/qr", { method: "POST" });
        break;
    }
  });

  // ── Botones físicos en pantalla ───────────────────────────────────────────
  const btnMap = {
    "players-prev": () => WS.btn("prev"),
    "players-next": () => WS.btn("next"),
    "players-ok": () => WS.btn("ok"),
    "payment-back": () => WS.btn("back"),
    "connect-skip": () => WS.btn("start"),
    "waiting-start": () => WS.btn("start"),
    "btn-pause": () => WS.btn("pause"),
    "pause-resume": () => WS.btn("pause"),
    "pause-end": () => WS.btn("back"),
    "go-again": () => WS.btn("start"),
    "go-attract": () => WS.btn("back"),
    "team-random": () => WS.btn("random"),
    "team-confirm": () => WS.btn("start"),
  };
  Object.entries(btnMap).forEach(([id, fn]) => {
    const el = document.getElementById(id);
    if (el) el.addEventListener("click", () => {
      AudioFX.playTick();
      fn();
    });
  });

  // click en modos
  document.querySelectorAll(".mode-item").forEach(el => {
    el.addEventListener("click", () => {
      AudioFX.playTick();
      const mode = el.dataset.mode;
      WS.btn("ok");
    });
  });

  // click en pantalla attract → iniciar
  document.getElementById("screen-attract")?.addEventListener("click", () => {
    AudioFX.playTick();
    WS.btn("ok");
  });

  // ── Skip timer en connect_phone ───────────────────────────────────────────
  let skipInterval = null;
  WS.on("state", (msg) => {
    if (msg.state === "connect_phone" || msg.state === "waiting_start") {
      const sessionId = msg.session_id || "";
      const localIp = msg.local_ip || "127.0.0.1";
      const cloudHost = msg.cloud_host || "";

      const host = cloudHost
        ? cloudHost
        : ((window.location.hostname === "localhost" || window.location.hostname === "127.0.0.1")
          ? `${localIp}:8000`
          : window.location.host);

      const protocol = cloudHost ? "https" : "http";
      const arcadeId = msg.arcade_id || "FUTSPO_01";
      const joinUrl = `${protocol}://${host}/player/index.html?arcade_id=${arcadeId}&session_id=${sessionId}`;

      const connectQr = document.getElementById("connect-qr");
      if (connectQr) {
        connectQr.src = `https://api.qrserver.com/v1/create-qr-code/?size=180x180&data=${encodeURIComponent(joinUrl)}`;
      }

      const connectUrlVal = document.getElementById("connect-url-val");
      if (connectUrlVal) {
        connectUrlVal.textContent = joinUrl;
      }

      const waitingStartQr = document.getElementById("waiting-start-qr");
      if (waitingStartQr) {
        waitingStartQr.src = `https://api.qrserver.com/v1/create-qr-code/?size=140x140&data=${encodeURIComponent(joinUrl)}`;
      }

      const waitingUrlVal = document.getElementById("waiting-url-val");
      if (waitingUrlVal) {
        waitingUrlVal.textContent = joinUrl;
      }
    }

    if (msg.state === "connect_phone") {
      let secs = 30;
      const countEl = document.getElementById("skip-countdown");
      clearInterval(skipInterval);
      skipInterval = setInterval(() => {
        secs--;
        if (countEl) countEl.textContent = secs;
        if (secs <= 0) { clearInterval(skipInterval); WS.btn("start"); }
      }, 1000);
    } else {
      clearInterval(skipInterval);
    }
  });
});
