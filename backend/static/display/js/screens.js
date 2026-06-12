/* Gestión de pantallas — muestra/oculta según el estado del servidor */
const PLAYER_COLORS = ["#ff4455","#44aaff","#44ee66","#ffaa00","#ff44ff","#00ffdd"];
const MODE_NAMES    = { classic:"CLÁSICO", timed:"CONTRARRELOJ", goleador:"GOLEADOR", team:"EQUIPO" };

const Screens = (() => {
  let _current = "";
  let _lastSessionId = ""; // Rastrear el session_id para forzar re-render al iniciar nueva partida

  function show(name) {
    if (_current === name) return;
    
    // Si pasamos a "turn_change", no queremos ocultar los scores de la pantalla "playing"
    let screenName = name;
    if (name === "turn_change") {
      screenName = "playing";
    }
    
    document.querySelectorAll(".screen").forEach(el => el.classList.remove("active"));
    const el = document.getElementById("screen-" + screenName);
    if (el) el.classList.add("active");
    _current = name;
  }

  // ── Construye celdas hexagonales de jugadores ───────────────────────
  function _buildHexGrid(gridId, activeCount) {
    const grid = document.getElementById(gridId);
    if (!grid) return;
    grid.innerHTML = "";
    for (let i = 0; i < 6; i++) {
      const d = document.createElement("div");
      const isActive  = i < activeCount;
      const isCurrent = i === activeCount - 1;
      d.className = "player-hex-cell" + (isActive ? " active" : "") + (isCurrent ? " current" : "");
      d.textContent = i + 1;
      grid.appendChild(d);
    }
  }

  // ── Actualiza cada pantalla con datos del servidor ──────────────────

  function updateSelectPlayers(data) {
    const count = data.players?.length || 1;

    // Número grande entre flechas
    const countEl = document.getElementById("player-count-num");
    if (countEl) countEl.textContent = count;

    // Hex grid del panel izquierdo
    _buildHexGrid("player-hex-grid-a", count);

    // Costo
    const req = data.credits_required || 1;
    const costEl = document.getElementById("cost-preview-val");
    if (costEl) costEl.textContent =
      `${req} ficha${req !== 1 ? "s" : ""} · $${req * 200}`;
  }

  function updateSelectMode(data) {
    const count = data.players?.length || 1;

    // Hex grid del panel izquierdo (bloqueado) en select_mode
    _buildHexGrid("player-hex-grid-b", count);

    // Texto debajo del hex bloqueado
    const locked = document.getElementById("mode-player-count");
    if (locked) locked.textContent = `${count} JUGADOR${count !== 1 ? "ES" : ""}`;

    // Resaltar modo seleccionado (solo en #mode-list del panel derecho)
    const modes = ["classic", "timed", "goleador", "team"];
    modes.forEach(m => {
      const el = document.querySelector(`#mode-list [data-mode="${m}"]`);
      if (el) el.classList.toggle("selected", m === data.mode);
    });

    // Deshabilitar visualmente el modo TEAM si el total es impar o menor a 4
    const isTeamDisabled = (count % 2 !== 0 || count < 4);
    const teamEl = document.querySelector(`#mode-list [data-mode="team"]`);
    if (teamEl) {
      if (isTeamDisabled) {
        teamEl.style.opacity = "0.35";
        const extraLabel = teamEl.querySelector(".mode-extra");
        if (extraLabel) {
          extraLabel.textContent = "NRO PAR (MIN 4)";
          extraLabel.style.color = "#ff4455";
          extraLabel.style.fontWeight = "bold";
        }
      } else {
        teamEl.style.opacity = "1";
        const extraLabel = teamEl.querySelector(".mode-extra");
        if (extraLabel) {
          extraLabel.textContent = "base";
          extraLabel.style.color = "";
          extraLabel.style.fontWeight = "";
        }
      }
    }

    // Costo
    const req = data.credits_required || 1;
    const costEl = document.getElementById("mode-cost-val");
    if (costEl) costEl.textContent =
      `${req} ficha${req !== 1 ? "s" : ""} · $${req * 200}`;
  }

  async function updatePayment(data) {
    const modeNames = { classic:"Clásico", timed:"Contrarreloj", goleador:"Goleador", team:"Equipo" };
    const players = data.players?.length || 1;
    const req = data.credits_required || 1;

    const payPlayers = document.getElementById("pay-players");
    if (payPlayers) payPlayers.textContent = players;
    const payMode = document.getElementById("pay-mode");
    if (payMode) payMode.textContent = modeNames[data.mode] || data.mode;
    const payTotal = document.getElementById("pay-total");
    if (payTotal) payTotal.textContent = `$${req * 200}`;

    document.getElementById("credits-needed").textContent = req;
    updateCredits(data.credits || 0, req);

    // Cargar QR de pago dinámico
    try {
      const res = await fetch(`/api/payment/qr/${req}`);
      const qrData = await res.json();
      const canvas = document.getElementById("qr-canvas");
      if (canvas && qrData.qr_data) {
        canvas.src = `https://api.qrserver.com/v1/create-qr-code/?size=200x200&data=${encodeURIComponent(qrData.qr_data)}`;
      }
    } catch (e) {
      console.warn("Fallo al obtener QR de MercadoPago:", e);
    }
  }

  function updateCredits(current, required) {
    const loadedEl = document.getElementById("credits-loaded");
    if (loadedEl) loadedEl.textContent = current;
    const pct = Math.min(100, (current / (required || 1)) * 100);
    const fill = document.getElementById("progress-fill");
    if (fill) fill.style.width = pct + "%";
  }

  function updateWaitingStart(data) {
    const wrap = document.getElementById("waiting-players");
    if (!wrap) return;
    wrap.innerHTML = "";
    (data.players || []).forEach((p, i) => {
      const d = document.createElement("div");
      // Añadir clases para animación y borde cian/oro futurista
      d.className = "waiting-player-card";
      d.style.display = "flex";
      d.style.alignItems = "center";
      d.style.gap = "16px";
      d.style.padding = "12px 20px";
      d.style.background = "rgba(0, 16, 44, 0.75)";
      d.style.border = `2.5px solid ${PLAYER_COLORS[i] || "var(--cyan)"}`;
      d.style.borderRadius = "16px";
      d.style.boxShadow = `0 4px 15px rgba(0, 0, 0, 0.4), inset 0 0 10px ${PLAYER_COLORS[i]}40`;
      d.style.position = "relative";
      d.style.overflow = "hidden";
      d.style.transition = "all 0.3s ease";

      const defaultAvatar = `https://api.dicebear.com/7.x/pixel-art/svg?seed=Player${i + 1}`;
      const avatarUrl = p.avatar || defaultAvatar;

      // Generar remera de fútbol SVG dinámica
      const primary = p.jersey_primary_color || "#ffffff";
      const secondary = p.jersey_secondary_color || "#00ffcc";
      const pattern = p.jersey_pattern || "plain";

      let patternHtml = "";
      if (pattern === "vertical") {
        patternHtml = `
          <g clip-path="url(#body-clip-${i})">
            <rect x="34" y="18" width="8" height="62" fill="${secondary}" />
            <rect x="58" y="18" width="8" height="62" fill="${secondary}" />
          </g>`;
      } else if (pattern === "horizontal") {
        patternHtml = `
          <g clip-path="url(#body-clip-${i})">
            <rect x="27" y="42" width="46" height="16" fill="${secondary}" />
          </g>`;
      } else if (pattern === "diagonal") {
        patternHtml = `
          <g clip-path="url(#body-clip-${i})">
            <polygon points="27,18 38,18 73,66 73,78" fill="${secondary}" />
          </g>`;
      }

      const jerseySvg = `
        <svg width="45" height="45" viewBox="0 0 100 100" style="filter: drop-shadow(0 2px 5px rgba(0,0,0,0.35)); flex-shrink: 0;">
          <defs>
            <clipPath id="body-clip-${i}">
              <path d="M 27 18 L 73 18 L 73 80 L 27 80 Z" />
            </clipPath>
          </defs>
          <path d="M 12 30 L 27 15 L 35 23 L 23 43 Z" fill="${primary}" stroke="#07070f" stroke-width="1.5" />
          <path d="M 88 30 L 73 15 L 65 23 L 77 43 Z" fill="${primary}" stroke="#07070f" stroke-width="1.5" />
          <path d="M 12 30 L 15 27 L 19 32 L 16 35 Z" fill="${secondary}" />
          <path d="M 88 30 L 85 27 L 81 32 L 84 35 Z" fill="${secondary}" />
          <path d="M 27 18 L 73 18 L 73 80 L 27 80 Z" fill="${primary}" stroke="#07070f" stroke-width="1.5" />
          ${patternHtml}
          <polygon points="37,18 50,32 63,18" fill="${secondary}" stroke="#07070f" stroke-width="1" />
          <line x1="50" y1="18" x2="50" y2="28" stroke="#07070f" stroke-width="1.5" />
        </svg>
      `;

      d.innerHTML = `
        <div class="wp-avatar-container" style="width: 54px; height: 54px; border-radius: 50%; border: 2px solid ${PLAYER_COLORS[i]}; overflow: hidden; background: #061026; display: flex; align-items: center; justify-content: center; flex-shrink: 0; box-shadow: 0 0 10px ${PLAYER_COLORS[i]}50;">
          <img src="${avatarUrl}" style="width: 100%; height: 100%; object-fit: cover;">
        </div>
        <div style="flex: 1; min-width: 0; display: flex; flex-direction: column; align-items: flex-start; gap: 2px;">
          <div class="wp-name" style="color: #ffffff; font-family: 'Orbitron', sans-serif; font-size: 16px; font-weight: bold; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; width: 100%; text-shadow: 0 0 8px rgba(255,255,255,0.25);">${p.name || "Jugador " + (i + 1)}</div>
          <div class="wp-phone" style="font-size: 11px; color: ${PLAYER_COLORS[i]}; font-family: 'Orbitron', sans-serif; font-weight: bold; letter-spacing: 0.5px; display: flex; align-items: center; gap: 4px;">
            ${p.connected ? "<span>📱 CONECTADO</span>" : "<span style='opacity: 0.5;'>🎮 CPU</span>"}
          </div>
        </div>
        <div style="display: flex; flex-direction: column; align-items: center; justify-content: center;">
          ${jerseySvg}
          <span style="font-size: 8px; color: var(--label); font-weight: bold; margin-top: 3px; font-family: 'Orbitron', sans-serif; text-transform: uppercase;">${p.club || 'Libre'}</span>
        </div>
      `;
      wrap.appendChild(d);
    });
  }

  function updateSelectTeam(data) {
    const teamAContainer = document.getElementById("team-a-players");
    const teamBContainer = document.getElementById("team-b-players");
    if (!teamAContainer || !teamBContainer) return;

    teamAContainer.innerHTML = "";
    teamBContainer.innerHTML = "";

    const players = data.players || [];
    const cursor = data.select_team_cursor ?? 0;

    players.forEach((p, i) => {
      const d = document.createElement("div");
      const isCurrent = i === cursor;
      
      d.className = "waiting-player-card" + (isCurrent ? " current-turn" : "");
      d.style.display = "flex";
      d.style.alignItems = "center";
      d.style.gap = "12px";
      d.style.padding = "8px 16px";
      d.style.background = "rgba(0, 16, 44, 0.75)";
      d.style.border = isCurrent ? `2.5px solid var(--gold)` : `1.5px solid ${PLAYER_COLORS[i] || "var(--cyan)"}`;
      d.style.borderRadius = "12px";
      d.style.boxShadow = isCurrent ? `0 0 15px var(--gold)` : "";
      d.style.position = "relative";
      d.style.width = "100%";

      const defaultAvatar = `https://api.dicebear.com/7.x/pixel-art/svg?seed=Player${i + 1}`;
      const avatarUrl = p.avatar || defaultAvatar;

      const curIndicator = isCurrent ? `<div style="color: var(--gold); font-weight: 900; font-size: 16px; margin-right: 4px; animation: pulseArrow 0.8s infinite alternate;">▶</div>` : "";

      d.innerHTML = `
        ${curIndicator}
        <div class="wp-avatar-container" style="width: 40px; height: 40px; border-radius: 50%; border: 2px solid ${PLAYER_COLORS[i]}; overflow: hidden; background: #061026; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">
          <img src="${avatarUrl}" style="width: 100%; height: 100%; object-fit: cover;">
        </div>
        <div style="flex: 1; min-width: 0;">
          <div class="wp-name" style="color: #ffffff; font-family: 'Orbitron', sans-serif; font-size: 14px; font-weight: bold; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;">${p.name || "Jugador " + (i + 1)}</div>
          <div style="font-size: 10px; color: var(--label); font-family: 'Orbitron', sans-serif; text-transform: uppercase;">${p.club || 'Libre'}</div>
        </div>
      `;

      if (p.team === 2) {
        teamBContainer.appendChild(d);
      } else {
        teamAContainer.appendChild(d);
      }
    });

    // Validar si los equipos están balanceados
    const t1Count = players.filter(p => p.team === 1).length;
    const t2Count = players.filter(p => p.team === 2).length;
    const warning = document.getElementById("team-balance-warning");
    const confirmBtn = document.getElementById("team-confirm");
    
    if (t1Count !== t2Count) {
      if (warning) {
        warning.style.display = "block";
        warning.textContent = `⚠️ EQUIPOS DESBALANCEADOS (Rojo: ${t1Count} vs Azul: ${t2Count}). ¡Deben tener la misma cantidad de jugadores!`;
      }
      if (confirmBtn) {
        confirmBtn.style.opacity = "0.4";
        confirmBtn.style.cursor = "not-allowed";
        confirmBtn.textContent = "ESPERANDO BALANCEO";
      }
    } else {
      if (warning) warning.style.display = "none";
      if (confirmBtn) {
        confirmBtn.style.opacity = "1";
        confirmBtn.style.cursor = "pointer";
        confirmBtn.textContent = "JUGAR START";
      }
    }
  }

  function updateTiebreak(data) {
    const wrap = document.getElementById("tiebreak-tied-list");
    if (!wrap) return;

    const players = data.players || [];
    const tiedIndices = data.tiebreak_players || [];
    const cursor = data.tiebreak_cursor ?? 0;

    // Si cambia el número de jugadores empatados, reconstruimos el DOM
    if (wrap.children.length !== tiedIndices.length) {
      wrap.innerHTML = "";
      tiedIndices.forEach((idx, cIdx) => {
        const p = players[idx];
        if (!p) return;

        const d = document.createElement("div");
        const isCurrent = cIdx === cursor;

        d.className = "waiting-player-card" + (isCurrent ? " current-turn" : "");
        d.id = "tiebreak-card-" + idx; // ID para la animación
        d.style.display = "flex";
        d.style.flexDirection = "column";
        d.style.alignItems = "center";
        d.style.gap = "8px";
        d.style.padding = "16px 24px";
        d.style.background = "rgba(0, 16, 44, 0.85)";
        d.style.border = isCurrent ? `3px solid var(--gold)` : `2px solid ${PLAYER_COLORS[idx] || "var(--cyan)"}`;
        d.style.borderRadius = "16px";
        d.style.boxShadow = isCurrent ? `0 0 25px var(--gold)` : `0 4px 15px rgba(0,0,0,0.5)`;
        d.style.position = "relative";
        d.style.minWidth = "180px";
        d.style.transition = "transform 0.25s ease, box-shadow 0.25s ease"; // Suavizar la animación

        const defaultAvatar = `https://api.dicebear.com/7.x/pixel-art/svg?seed=Player${idx + 1}`;
        const avatarUrl = p.avatar || defaultAvatar;

        d.innerHTML = `
          <div class="tiebreak-crown-wrap" style="position: absolute; top: -18px; font-size: 24px; display: ${isCurrent ? 'block' : 'none'}; animation: frog-bounce 1s infinite;">👑</div>
          <div class="wp-avatar-container" style="width: 64px; height: 64px; border-radius: 50%; border: 2.5px solid ${PLAYER_COLORS[idx]}; overflow: hidden; background: #061026; display: flex; align-items: center; justify-content: center; box-shadow: 0 0 10px ${PLAYER_COLORS[idx]}50;">
            <img src="${avatarUrl}" style="width: 100%; height: 100%; object-fit: cover;">
          </div>
          <div style="text-align: center; margin-top: 4px;">
            <div class="wp-name" style="color: #ffffff; font-family: 'Orbitron', sans-serif; font-size: 15px; font-weight: bold;">${p.name || "Jugador " + (idx + 1)}</div>
            <div id="tiebreak-score-val-${idx}" style="font-size: 18px; color: var(--gold); font-family: monospace; font-weight: 900; margin-top: 8px; transition: transform 0.15s ease;">
              ${data.mode === 'goleador' ? p.balls_pocketed + ' BOLAS' : p.score + ' PTS'}
            </div>
            <div class="tiebreak-status-label" style="font-size: 11px; color: var(--label); font-family: 'Orbitron', sans-serif; margin-top: 4px; font-weight: bold;">
              ${isCurrent ? "🟡 TU TURNO" : "⏳ ESPERANDO"}
            </div>
          </div>
        `;
        wrap.appendChild(d);
      });
    } else {
      // Si el número es igual, actualizamos en caliente para conservar las animaciones activas
      tiedIndices.forEach((idx, cIdx) => {
        const p = players[idx];
        if (!p) return;
        const d = document.getElementById("tiebreak-card-" + idx);
        if (d) {
          const isCurrent = cIdx === cursor;
          d.className = "waiting-player-card" + (isCurrent ? " current-turn" : "");
          d.style.border = isCurrent ? `3px solid var(--gold)` : `2px solid ${PLAYER_COLORS[idx] || "var(--cyan)"}`;
          d.style.boxShadow = isCurrent ? `0 0 25px var(--gold)` : `0 4px 15px rgba(0,0,0,0.5)`;
          
          const crown = d.querySelector(".tiebreak-crown-wrap");
          if (crown) {
            crown.style.display = isCurrent ? "block" : "none";
          }
          
          const val = document.getElementById("tiebreak-score-val-" + idx);
          if (val) {
            const expectedText = data.mode === 'goleador' ? p.balls_pocketed + ' BOLAS' : p.score + ' PTS';
            if (val.textContent.trim() !== expectedText) {
              val.textContent = expectedText;
            }
          }
          
          const statusLabel = d.querySelector(".tiebreak-status-label");
          if (statusLabel) {
            statusLabel.textContent = isCurrent ? "🟡 TU TURNO" : "⏳ ESPERANDO";
          }
        }
      });
    }
  }

  function updateGame(data) {
    document.getElementById("game-mode-badge").textContent = MODE_NAMES[data.mode] || data.mode;
    document.getElementById("game-credits").textContent    = data.credits || 0;

    // Actualizar header de equipos
    const teamScoresHeader = document.getElementById("team-scores-header");
    if (teamScoresHeader) {
      if (data.mode === "team") {
        teamScoresHeader.style.display = "flex";
        const teamAScore = document.getElementById("team-a-score-val");
        const teamBScore = document.getElementById("team-b-score-val");
        if (teamAScore) teamAScore.textContent = data.team_scores ? (data.team_scores[1] || 0) : 0;
        if (teamBScore) teamBScore.textContent = data.team_scores ? (data.team_scores[2] || 0) : 0;
      } else {
        teamScoresHeader.style.display = "none";
      }
    }

    const wrap = document.getElementById("scores-wrap");
    if (!wrap) return;

    const players = data.players || [];

    // Calcular posiciones de juego en tiempo real (Rankings de Puesto)
    const rankedPlayers = [...players]
      .map((p, idx) => ({ ...p, originalIndex: idx }))
      .sort((a, b) => {
        if (data.mode === "goleador") {
          return (b.balls_pocketed - a.balls_pocketed) || (b.score - a.score);
        }
        return b.score - a.score;
      });

    const playerRanks = [];
    rankedPlayers.forEach((p, rankIndex) => {
      playerRanks[p.originalIndex] = rankIndex + 1;
    });

    // Re-crear celdas si cambia el número de jugadores O si es una nueva sesión
    const sessionChanged = data.session_id && data.session_id !== _lastSessionId;
    if (sessionChanged) _lastSessionId = data.session_id;
    if (wrap.children.length !== players.length || sessionChanged) {
      wrap.innerHTML = "";
      wrap.classList.toggle("many-players", players.length >= 4); // Activa rejilla de 2 columnas si son 4+ jugadores
      players.forEach((p, i) => {
        const card = document.createElement("div");
        card.className = `score-row player-${i}`;
        card.id = "score-card-" + i;
        
        const rank = playerRanks[i] || 1;
        const defaultAvatar = `https://api.dicebear.com/7.x/pixel-art/svg?seed=Player${i + 1}`;
        const avatarUrl = p.avatar || defaultAvatar;

        // Generar remera de fútbol SVG dinámica para pantalla de juego
        const primary = p.jersey_primary_color || "#ffffff";
        const secondary = p.jersey_secondary_color || "#00ffcc";
        const pattern = p.jersey_pattern || "plain";

        let patternHtml = "";
        if (pattern === "vertical") {
          patternHtml = `
            <g clip-path="url(#game-body-clip-${i})">
              <rect x="34" y="18" width="8" height="62" fill="${secondary}" />
              <rect x="58" y="18" width="8" height="62" fill="${secondary}" />
            </g>`;
        } else if (pattern === "horizontal") {
          patternHtml = `
            <g clip-path="url(#game-body-clip-${i})">
              <rect x="27" y="42" width="46" height="16" fill="${secondary}" />
            </g>`;
        } else if (pattern === "diagonal") {
          patternHtml = `
            <g clip-path="url(#game-body-clip-${i})">
              <polygon points="27,18 38,18 73,66 73,78" fill="${secondary}" />
            </g>`;
        }

        const jerseySvg = `
          <svg width="32" height="32" viewBox="0 0 100 100" style="filter: drop-shadow(0 1px 3px rgba(0,0,0,0.3)); flex-shrink: 0;" id="player-jersey-svg-${i}">
            <defs>
              <clipPath id="game-body-clip-${i}">
                <path d="M 27 18 L 73 18 L 73 80 L 27 80 Z" />
              </clipPath>
            </defs>
            <path class="jersey-sleeve-l" d="M 12 30 L 27 15 L 35 23 L 23 43 Z" fill="${primary}" stroke="#07070f" stroke-width="1.5" />
            <path class="jersey-sleeve-r" d="M 88 30 L 73 15 L 65 23 L 77 43 Z" fill="${primary}" stroke="#07070f" stroke-width="1.5" />
            <path d="M 12 30 L 15 27 L 19 32 L 16 35 Z" fill="${secondary}" />
            <path d="M 88 30 L 85 27 L 81 32 L 84 35 Z" fill="${secondary}" />
            <path class="jersey-body" d="M 27 18 L 73 18 L 73 80 L 27 80 Z" fill="${primary}" stroke="#07070f" stroke-width="1.5" />
            ${patternHtml}
            <polygon class="jersey-collar" points="37,18 50,32 63,18" fill="${secondary}" stroke="#07070f" stroke-width="1" />
            <line x1="50" y1="18" x2="50" y2="28" stroke="#07070f" stroke-width="1.5" />
          </svg>
        `;

        card.innerHTML = `
          <div class="turn-arrow-indicator" id="turn-arrow-${i}">▶</div>
          <div class="player-avatar-container">
            <img class="player-avatar-img" id="player-avatar-${i}" src="${avatarUrl}">
          </div>
          ${jerseySvg}
          <div class="player-name-val" id="player-name-${i}">${p.name || "JUGADOR " + (i + 1)}</div>
          <div class="player-score-val" id="score-val-${i}">${data.mode === "goleador" ? p.balls_pocketed + " BOLAS" : p.score}</div>
          <div class="player-rank-badge" id="rank-badge-${i}">#${rank}</div>`;
        wrap.appendChild(card);
      });
    }

    // Actualizar estados, puntajes y clases en caliente para conservar las transiciones del navegador
    players.forEach((p, i) => {
      const card = document.getElementById("score-card-" + i);
      if (card) {
        card.classList.toggle("current-turn", i === data.current_player);
        
        const nameEl = document.getElementById("player-name-" + i);
        if (nameEl) nameEl.textContent = p.name || "JUGADOR " + (i + 1);

        const val = document.getElementById("score-val-" + i);
        if (val) val.textContent = data.mode === "goleador" ? p.balls_pocketed + " BOLAS" : p.score;

        const rankEl = document.getElementById("rank-badge-" + i);
        if (rankEl) rankEl.textContent = "#" + (playerRanks[i] || 1);

        const avatarEl = document.getElementById("player-avatar-" + i);
        if (avatarEl) {
          const defaultAvatar = `https://api.dicebear.com/7.x/pixel-art/svg?seed=Player${i + 1}`;
          const currentUrl = p.avatar || defaultAvatar;
          if (avatarEl.getAttribute("src") !== currentUrl) {
            avatarEl.setAttribute("src", currentUrl);
          }
        }

        // Hot-update jersey SVG colors/patterns
        const jerseySvgEl = document.getElementById("player-jersey-svg-" + i);
        if (jerseySvgEl) {
          const primary = p.jersey_primary_color || "#ffffff";
          const secondary = p.jersey_secondary_color || "#00ffcc";
          const pattern = p.jersey_pattern || "plain";

          // Update fill colors
          const sleeveL = jerseySvgEl.querySelector('.jersey-sleeve-l');
          const sleeveR = jerseySvgEl.querySelector('.jersey-sleeve-r');
          const body = jerseySvgEl.querySelector('.jersey-body');
          const collar = jerseySvgEl.querySelector('.jersey-collar');

          if (sleeveL) sleeveL.setAttribute("fill", primary);
          if (sleeveR) sleeveR.setAttribute("fill", primary);
          if (body) body.setAttribute("fill", primary);
          if (collar) collar.setAttribute("fill", secondary);

          // Update pattern SVG elements
          const patternV = jerseySvgEl.querySelector('g[clip-path*="body-clip"]');
          if (patternV) {
            // Remove old dynamic group if pattern changed
            patternV.remove();
          }

          // Let's inject the correct pattern group
          let patternHtml = "";
          if (pattern === "vertical") {
            patternHtml = `
              <g clip-path="url(#game-body-clip-${i})">
                <rect x="34" y="18" width="8" height="62" fill="${secondary}" />
                <rect x="58" y="18" width="8" height="62" fill="${secondary}" />
              </g>`;
          } else if (pattern === "horizontal") {
            patternHtml = `
              <g clip-path="url(#game-body-clip-${i})">
                <rect x="27" y="42" width="46" height="16" fill="${secondary}" />
              </g>`;
          } else if (pattern === "diagonal") {
            patternHtml = `
              <g clip-path="url(#game-body-clip-${i})">
                <polygon points="27,18 38,18 73,66 73,78" fill="${secondary}" />
              </g>`;
          }

          if (patternHtml) {
            const tempDiv = document.createElement("div");
            tempDiv.innerHTML = `<svg>${patternHtml}</svg>`;
            const patternGroup = tempDiv.querySelector("g");
            if (patternGroup) {
              const bodyPath = jerseySvgEl.querySelector('.jersey-body');
              if (bodyPath) {
                bodyPath.insertAdjacentElement("afterend", patternGroup);
              }
            }
          }
        }
      }
    });

    const cp = players[data.current_player];
    if (cp) {
      const turnName = document.getElementById("turn-name");
      if (turnName) turnName.textContent = cp.name || `Jugador ${data.current_player + 1}`;
      const balls = cp.balls_left || 0;
      const total = data.balls_per_player || 5;
      const ballIcons = document.getElementById("ball-icons");
      if (ballIcons) ballIcons.textContent = "⚫".repeat(balls) + "⚪".repeat(Math.max(0, total - balls));
    }

    if (data.mode === "timed") {
      document.getElementById("game-timer").style.display = "block";
      document.getElementById("timer-val").textContent = data.time_left || 0;
    }
  }

  const PIXI_COLORS = [0xff4455, 0x44aaff, 0x00ff88, 0xffaa00, 0xdd44ff, 0x00ffee];

  function animateScore(playerIndex, delta, total, zoneX, zoneY) {
    const val = document.getElementById("score-val-" + playerIndex);
    if (val) {
      val.textContent = total;
      val.classList.add("bump");
      setTimeout(() => val.classList.remove("bump"), 200);
    }
    
    // Animación de puntaje en la pantalla de Desempate (Tiebreak)
    const tiebreakVal = document.getElementById("tiebreak-score-val-" + playerIndex);
    if (tiebreakVal) {
      const isGoleador = document.getElementById("game-mode-badge")?.textContent === "GOLEADOR";
      tiebreakVal.textContent = isGoleador ? total + " BOLAS" : total + " PTS";
      tiebreakVal.classList.add("bump");
      setTimeout(() => tiebreakVal.classList.remove("bump"), 200);
    }

    const card = document.getElementById("score-card-" + playerIndex);
    if (card) {
      card.style.transform = "scale(1.2) translateY(5px)";
      setTimeout(() => card.style.transform = "", 250);
    }

    // Animación de tarjeta en Desempate (Tiebreak)
    const tiebreakCard = document.getElementById("tiebreak-card-" + playerIndex);
    if (tiebreakCard) {
      tiebreakCard.style.transform = "scale(1.18)";
      tiebreakCard.style.boxShadow = "0 0 35px var(--gold)";
      setTimeout(() => {
        tiebreakCard.style.transform = "";
        tiebreakCard.style.boxShadow = "";
      }, 250);
    }
    
    const colorHex = PIXI_COLORS[playerIndex] || 0x00e5ff;
    const colorStr = PLAYER_COLORS[playerIndex] || "#00e5ff";
    
    // Disparar FX avanzados
    FX.scoreFloat(zoneX || window.innerWidth / 2, zoneY || window.innerHeight / 2, delta, colorStr);
    FX.goalBurst(zoneX || window.innerWidth / 2, zoneY || window.innerHeight / 2, colorHex);
    FX.flashScreen(colorHex, delta >= 1000 ? 0.25 : 0.12);
    FX.shakeScreen(delta >= 1000 ? 14 : 7, 220);
    
    // Disparar Audio sintetizado
    AudioFX.playPoint(delta);
  }

  function updateGameOver(data) {
    const scores = data.scores || [];
    const winnerAnnounce = document.getElementById("winner-announce");
    const wrap = document.getElementById("final-scores");
    if (!wrap) return;
    wrap.innerHTML = "";

    if (data.mode === "team") {
      const teamScores = data.team_scores || { 1: 0, 2: 0 };
      const scoreA = teamScores[1] || 0;
      const scoreB = teamScores[2] || 0;
      
      let winnerText = "";
      let winnerColor = "var(--text)";
      if (scoreA > scoreB) {
        winnerText = `🏆 GANADOR: EQUIPO ROJO (${scoreA} vs ${scoreB})`;
        winnerColor = "var(--p0)";
      } else if (scoreB > scoreA) {
        winnerText = `🏆 GANADOR: EQUIPO AZUL (${scoreB} vs ${scoreA})`;
        winnerColor = "var(--p1)";
      } else {
        winnerText = `🤝 EMPATE DE EQUIPOS (${scoreA} - ${scoreB})`;
        winnerColor = "var(--gold)";
      }

      if (winnerAnnounce) {
        winnerAnnounce.textContent = winnerText;
        winnerAnnounce.style.color = winnerColor;
        winnerAnnounce.style.textShadow = `0 0 15px ${winnerColor}`;
      }

      // Añadir fila de resultados de equipo
      const teamRow = document.createElement("div");
      teamRow.style.display = "flex";
      teamRow.style.gap = "20px";
      teamRow.style.width = "100%";
      teamRow.style.justifyContent = "center";
      teamRow.style.marginBottom = "24px";
      
      const cardA = document.createElement("div");
      cardA.className = "final-score-card" + (scoreA > scoreB ? " winner" : "");
      cardA.style.border = `3px solid var(--p0)`;
      cardA.style.flex = "1";
      cardA.style.boxShadow = scoreA > scoreB ? `0 0 20px rgba(255, 68, 85, 0.4)` : "";
      cardA.innerHTML = `
        <div style="font-size: 11px; color: var(--muted); font-family: 'Orbitron', sans-serif;">EQUIPO ROJO</div>
        <div style="font-size: 28px; font-weight: 900; color: var(--p0);">${scoreA} PTS</div>
      `;

      const cardB = document.createElement("div");
      cardB.className = "final-score-card" + (scoreB > scoreA ? " winner" : "");
      cardB.style.border = `3px solid var(--p1)`;
      cardB.style.flex = "1";
      cardB.style.boxShadow = scoreB > scoreA ? `0 0 20px rgba(68, 170, 255, 0.4)` : "";
      cardB.innerHTML = `
        <div style="font-size: 11px; color: var(--muted); font-family: 'Orbitron', sans-serif;">EQUIPO AZUL</div>
        <div style="font-size: 28px; font-weight: 900; color: var(--p1);">${scoreB} PTS</div>
      `;

      teamRow.appendChild(cardA);
      teamRow.appendChild(cardB);
      wrap.appendChild(teamRow);

      // Listar jugadores individuales con su color de equipo
      scores.forEach((s, rank) => {
        const d = document.createElement("div");
        d.className = "final-score-card";
        const isRed = s.team === 1;
        const teamColor = isRed ? "var(--p0)" : "var(--p1)";
        const teamLabel = isRed ? "ROJO" : "AZUL";
        
        d.innerHTML = `
          <div class="final-rank" style="color: ${teamColor};">●</div>
          <div class="final-name" style="text-align: left;">
            ${s.name} 
            <span style="font-size: 10px; font-family: 'Orbitron', sans-serif; color: ${teamColor}; margin-left: 8px; font-weight: bold; border: 1.5px solid ${teamColor}; padding: 1px 6px; border-radius: 4px;">${teamLabel}</span>
          </div>
          <div class="final-pts" style="color: ${teamColor};">${s.score} PTS</div>`;
        wrap.appendChild(d);
      });

    } else {
      // Modo individual estándar
      if (winnerAnnounce) {
        winnerAnnounce.textContent = scores[0] ? `🏆 GANADOR: ${scores[0].name}` : "";
        winnerAnnounce.style.color = "var(--gold)";
        winnerAnnounce.style.textShadow = "0 0 15px rgba(245, 192, 0, 0.6)";
      }
      const medals = ["🥇","🥈","🥉","4️⃣","5️⃣","6️⃣"];
      scores.forEach((s, rank) => {
        const d = document.createElement("div");
        d.className = "final-score-card" + (rank === 0 ? " winner" : "");
        const finalPts = data.mode === "goleador" ? s.balls_pocketed + " BOLAS" : s.score + " PTS";
        d.innerHTML = `<div class="final-rank">${medals[rank] || ""}</div>
          <div class="final-name">${s.name}</div>
          <div class="final-pts">${finalPts}</div>`;
        wrap.appendChild(d);
      });
    }
  }

  function updateTurnChange(data) {
    const el = document.getElementById("tc-name");
    if (el) el.textContent = data.player_name || "";
  }

  return {
    show, updateSelectPlayers, updateSelectMode, updatePayment, updateCredits,
    updateWaitingStart, updateSelectTeam, updateTiebreak, updateGame, animateScore, updateGameOver, updateTurnChange
  };
})();
