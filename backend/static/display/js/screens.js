/* Gestión de pantallas — muestra/oculta según el estado del servidor */
const PLAYER_COLORS = ["#ff4455","#44aaff","#44ee66","#ffaa00","#ff44ff","#00ffdd"];
const MODE_NAMES    = { classic:"CLÁSICO", timed:"CONTRARRELOJ", goleador:"GOLEADOR", team:"EQUIPO" };

const Screens = (() => {
  let _current = "";
  let _lastSessionId = ""; // Rastrear el session_id para forzar re-render al iniciar nueva partida
  let _ballsPerPlayer = 5;  // Bolas configuradas por jugador en la partida actual

  // Variables para Carrusel de Atracción y Rankings
  let _supabaseClient = null;
  let _supabaseConfigured = false;
  let _arcadeId = "FUTSPO_01";
  let _machineZone = "Buenos Aires";
  let _machineLat = -34.6037;
  let _machineLon = -58.3816;
  let _attractIndex = 0;
  let _attractInterval = null;

  const MOCK_LOCAL = [
    { name: "EL FACA", score: 4800, zone: "Local" },
    { name: "COCO", score: 4200, zone: "Local" },
    { name: "TITI", score: 3900, zone: "Local" },
    { name: "Lucho_C", score: 3500, zone: "Local" },
    { name: "Jona", score: 3000, zone: "Local" },
    { name: "Sapo_Cyber", score: 2800, zone: "Local" },
    { name: "Gaby_PRO", score: 2600, zone: "Local" },
    { name: "Futsapo_1", score: 2400, zone: "Local" },
    { name: "ArcadeArg", score: 2200, zone: "Local" },
    { name: "MessiFan", score: 2100, zone: "Local" },
    { name: "Boli_CABA", score: 2000, zone: "Local" },
    { name: "PrensaStart", score: 1800, zone: "Local" },
    { name: "Nico", score: 1600, zone: "Local" },
    { name: "Tito", score: 1500, zone: "Local" },
    { name: "Rana_Goleadora", score: 1200, zone: "Local" }
  ];

  const MOCK_ZONAL = [
    { name: "Seba_B", score: 5200, zone: "Buenos Aires" },
    { name: "RanaMaster", score: 4900, zone: "Buenos Aires" },
    { name: "Gaby", score: 4700, zone: "Santa Fe" },
    { name: "SapoCyber", score: 4500, zone: "Córdoba" },
    { name: "Mariano", score: 4300, zone: "Buenos Aires" },
    { name: "Dibu_Fan", score: 4100, zone: "Mar del Plata" },
    { name: "BoliRosario", score: 3800, zone: "Santa Fe" },
    { name: "CordobaPro", score: 3600, zone: "Córdoba" },
    { name: "MendozaCyber", score: 3400, zone: "Mendoza" },
    { name: "SapoLaPlata", score: 3200, zone: "La Plata" },
    { name: "TucumanBoli", score: 3000, zone: "Tucumán" },
    { name: "NeuquenSapo", score: 2800, zone: "Neuquén" },
    { name: "ChacoPRO", score: 2600, zone: "Chaco" },
    { name: "SaltaBoli", score: 2400, zone: "Salta" },
    { name: "Entrerriano", score: 2200, zone: "Entre Ríos" }
  ];

  const MOCK_GLOBAL = [
    { name: "Leo_10", score: 152000, zone: "Rosario, ARG" },
    { name: "Diego_10", score: 148000, zone: "Lanús, ARG" },
    { name: "CyberSapo", score: 139000, zone: "Bogotá, COL" },
    { name: "Futsapero", score: 125000, zone: "Medellín, COL" },
    { name: "Bolirana_PRO", score: 110000, zone: "Quito, ECU" },
    { name: "SapoStuttgart", score: 98000, zone: "Stuttgart, GER" },
    { name: "BoliMiami", score: 92000, zone: "Miami, USA" },
    { name: "RanaMadrid", score: 85000, zone: "Madrid, ESP" },
    { name: "SapoSantiago", score: 79000, zone: "Santiago, CHI" },
    { name: "RanaBrasil", score: 74000, zone: "São Paulo, BRA" },
    { name: "BoliMontevideo", score: 68000, zone: "Montevideo, URU" },
    { name: "LimaCyber", score: 62000, zone: "Lima, PER" },
    { name: "SapoBolivia", score: 58000, zone: "La Paz, BOL" },
    { name: "MexicoFutsap", score: 54000, zone: "CDMX, MEX" },
    { name: "BoliParis", score: 50000, zone: "Paris, FRA" }
  ];

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

  async function updateSelectPlayers(data) {
    const count = data.players?.length || 1;

    // Número grande entre flechas
    const countEl = document.getElementById("player-count-num");
    if (countEl) countEl.textContent = count;

    // Hex grid del panel izquierdo
    _buildHexGrid("player-hex-grid-a", count);

    // Costo
    const costEl = document.getElementById("cost-preview-val");
    if (costEl) {
      if (data.free_play) {
        costEl.textContent = "MODO JUEGO LIBRE";
      } else {
        const req = data.credits_required || 1;
        try {
          const res = await fetch(`/api/payment/cost?players=${count}&mode=${data.mode || 'classic'}`);
          const costData = await res.json();
          costEl.textContent = `${req} ficha${req !== 1 ? "s" : ""} · $${costData.pesos}`;
        } catch (e) {
          costEl.textContent = `${req} ficha${req !== 1 ? "s" : ""} · $${req * 200}`;
        }
      }
    }
  }

  async function updateSelectMode(data) {
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
    const costEl = document.getElementById("mode-cost-val");
    if (costEl) {
      if (data.free_play) {
        costEl.textContent = "MODO JUEGO LIBRE";
      } else {
        const req = data.credits_required || 1;
        try {
          const res = await fetch(`/api/payment/cost?players=${count}&mode=${data.mode || 'classic'}`);
          const costData = await res.json();
          costEl.textContent = `${req} ficha${req !== 1 ? "s" : ""} · $${costData.pesos}`;
        } catch (e) {
          costEl.textContent = `${req} ficha${req !== 1 ? "s" : ""} · $${req * 200}`;
        }
      }
    }
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
    if (payTotal) {
      try {
        const res = await fetch(`/api/payment/cost?players=${players}&mode=${data.mode}`);
        const costData = await res.json();
        payTotal.textContent = `$${costData.pesos}`;
      } catch (e) {
        payTotal.textContent = `$${req * 200}`;
      }
    }

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

    const playersList = data.players || [];
    const isMany = playersList.length >= 4;
    const cardPadding = isMany ? "20px 16px" : "36px 20px";
    const avatarSize = isMany ? "72px" : "96px";
    const jerseySize = isMany ? "46px" : "60px";
    const nameFontSize = isMany ? "30px" : "38px";
    const connectedFontSize = isMany ? "13px" : "15px";
    const gapSize = isMany ? "14px" : "20px";

    playersList.forEach((p, i) => {
      const d = document.createElement("div");
      // Añadir clases para animación y borde cian/oro futurista
      d.className = "waiting-player-card";
      d.style.display = "flex";
      d.style.alignItems = "center";
      d.style.gap = gapSize;
      d.style.padding = cardPadding;
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
        <svg width="${jerseySize}" height="${jerseySize}" viewBox="0 0 100 100" style="filter: drop-shadow(0 2px 4px rgba(0,0,0,0.3)); flex-shrink: 0;">
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
        <div class="wp-avatar-container" style="width: ${avatarSize}; height: ${avatarSize}; border-radius: 50%; border: 2px solid ${PLAYER_COLORS[i]}; overflow: hidden; background: #061026; display: flex; align-items: center; justify-content: center; flex-shrink: 0; box-shadow: 0 0 10px ${PLAYER_COLORS[i]}50;">
          <img src="${avatarUrl}" style="width: 100%; height: 100%; object-fit: cover;">
        </div>
        <div style="flex: 1; min-width: 0; display: flex; flex-direction: column; align-items: flex-start; gap: 4px;">
          <div class="wp-name" style="color: #ffffff; font-family: 'Orbitron', sans-serif; font-size: ${nameFontSize}; font-weight: bold; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; width: 100%; text-shadow: 0 0 8px rgba(255,255,255,0.3);">${p.name || "Jugador " + (i + 1)}</div>
          ${p.connected ? `<div style="font-size: ${connectedFontSize}; color: ${PLAYER_COLORS[i]}; font-family: 'Orbitron', sans-serif; font-weight: bold; letter-spacing: 0.5px;">📱 CONECTADO</div>` : ""}
        </div>
        <div style="display: flex; flex-direction: column; align-items: center; justify-content: center;">
          ${jerseySvg}
          <span style="font-size: 9px; color: var(--label); font-weight: bold; margin-top: 2px; font-family: 'Orbitron', sans-serif; text-transform: uppercase;">${p.club || 'Libre'}</span>
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
      d.style.padding = "10px 16px";
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
        <div class="wp-avatar-container" style="width: 50px; height: 50px; border-radius: 50%; border: 2px solid ${PLAYER_COLORS[i]}; overflow: hidden; background: #061026; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">
          <img src="${avatarUrl}" style="width: 100%; height: 100%; object-fit: cover;">
        </div>
        <div style="flex: 1; min-width: 0;">
          <div class="wp-name" style="color: #ffffff; font-family: 'Orbitron', sans-serif; font-size: 16px; font-weight: bold; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;">${p.name || "Jugador " + (i + 1)}</div>
          <div style="font-size: 11px; color: var(--label); font-family: 'Orbitron', sans-serif; text-transform: uppercase;">${p.club || 'Libre'}</div>
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
        d.style.gap = "12px";
        d.style.padding = "24px 36px";
        d.style.background = "rgba(0, 16, 44, 0.85)";
        d.style.border = isCurrent ? `4px solid var(--gold)` : `3.5px solid ${PLAYER_COLORS[idx] || "var(--cyan)"}`;
        d.style.borderRadius = "20px";
        d.style.boxShadow = isCurrent ? `0 0 35px var(--gold)` : `0 4px 15px rgba(0,0,0,0.5)`;
        d.style.position = "relative";
        d.style.minWidth = "280px";
        d.style.transition = "transform 0.25s ease, box-shadow 0.25s ease"; // Suavizar la animación

        const defaultAvatar = `https://api.dicebear.com/7.x/pixel-art/svg?seed=Player${idx + 1}`;
        const avatarUrl = p.avatar || defaultAvatar;

        d.innerHTML = `
          <div class="tiebreak-crown-wrap" style="position: absolute; top: -30px; font-size: 40px; display: ${isCurrent ? 'block' : 'none'}; animation: frog-bounce 1s infinite;">👑</div>
          <div class="wp-avatar-container" style="width: 110px; height: 110px; border-radius: 50%; border: 4px solid ${PLAYER_COLORS[idx]}; overflow: hidden; background: #061026; display: flex; align-items: center; justify-content: center; box-shadow: 0 0 15px ${PLAYER_COLORS[idx]}60;">
            <img src="${avatarUrl}" style="width: 100%; height: 100%; object-fit: cover;">
          </div>
          <div style="text-align: center; margin-top: 8px; display: flex; flex-direction: column; gap: 6px;">
            <div class="wp-name" style="color: #ffffff; font-family: 'Orbitron', sans-serif; font-size: 26px; font-weight: bold;">${p.name || "Jugador " + (idx + 1)}</div>
            <div id="tiebreak-score-val-${idx}" style="font-size: 32px; color: var(--gold); font-family: monospace; font-weight: 900; margin-top: 8px; transition: transform 0.15s ease;">
              ${data.mode === 'goleador' ? p.balls_pocketed : p.score + ' PTS'}
            </div>
            <div class="tiebreak-status-label" style="font-size: 18px; color: var(--label); font-family: 'Orbitron', sans-serif; margin-top: 4px; font-weight: bold;">
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
            const expectedText = data.mode === 'goleador' ? String(p.balls_pocketed) : p.score + ' PTS';
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

        const jerseySize = players.length >= 4 ? 42 : 70;
        const jerseySvg = `
          <svg width="${jerseySize}" height="${jerseySize}" viewBox="0 0 100 100" style="filter: drop-shadow(0 2px 5px rgba(0,0,0,0.35)); flex-shrink: 0;" id="player-jersey-svg-${i}">
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
          <div class="player-avatar-wrapper">
            <div class="turn-arrow-indicator" id="turn-arrow-${i}">▼</div>
            <div class="player-avatar-container">
              <img class="player-avatar-img" id="player-avatar-${i}" src="${avatarUrl}">
            </div>
          </div>
          <div class="player-info-container" style="display: flex; flex-direction: column; flex: 1; min-width: 0; align-items: flex-start; gap: 8px;">
            <div class="player-name-val" id="player-name-${i}" style="margin: 0; text-align: left; width: 100%;">${p.name || "JUGADOR " + (i + 1)}</div>
            <div class="player-balls-row" id="player-balls-${i}" style="display: flex; gap: 6px;"></div>
          </div>
          <div class="player-score-val" id="score-val-${i}">${data.mode === "goleador" ? p.balls_pocketed : p.score}</div>
          <div class="player-rank-col">
            <div class="player-rank-badge" id="rank-badge-${i}">#${rank}</div>
            ${jerseySvg}
          </div>`;
        wrap.appendChild(card);
      });
    }

    _ballsPerPlayer = data.balls_per_player || 5;

    // Actualizar estados, puntajes y clases en caliente para conservar las transiciones del navegador
    players.forEach((p, i) => {
      const card = document.getElementById("score-card-" + i);
      if (card) {
        card.classList.toggle("current-turn", i === data.current_player);
        
        const nameEl = document.getElementById("player-name-" + i);
        if (nameEl) nameEl.textContent = p.name || "JUGADOR " + (i + 1);

        const val = document.getElementById("score-val-" + i);
        if (val) val.textContent = data.mode === "goleador" ? p.balls_pocketed : p.score;

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

        // Renderizar mini-bolas tipo penal de fútbol
        renderPlayerBalls(i, p.shots || [], _ballsPerPlayer);

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
      
      // Renderizar penalty balls principal (del jugador de turno)
      renderPenaltyBalls(cp.shots || [], _ballsPerPlayer);

      // Mostrar popup de última bola
      const lastBallPopup = document.getElementById("last-ball-popup");
      if (lastBallPopup) {
        if (cp.balls_left === 1) {
          lastBallPopup.style.display = "block";
        } else {
          lastBallPopup.style.display = "none";
        }
      }
    }

    if (data.mode === "timed") {
      document.getElementById("game-timer").style.display = "block";
      document.getElementById("timer-val").textContent = data.time_left || 0;
    }
  }

  function renderPenaltyBalls(shots, total) {
    const ballIcons = document.getElementById("ball-icons");
    if (!ballIcons) return;
    ballIcons.innerHTML = "";
    for (let j = 0; j < total; j++) {
      const ballSpan = document.createElement("span");
      if (j < shots.length) {
        const points = shots[j];
        if (points > 0) {
          ballSpan.className = "penalty-ball hit";
          ballSpan.innerHTML = "⚽";
        } else {
          ballSpan.className = "penalty-ball missed";
          ballSpan.innerHTML = "⚽";
        }
      } else {
        ballSpan.className = "penalty-ball pending";
      }
      ballIcons.appendChild(ballSpan);
    }
  }

  function renderPlayerBalls(playerIndex, shots, total) {
    const container = document.getElementById(`player-balls-${playerIndex}`);
    if (!container) return;
    container.innerHTML = "";
    for (let j = 0; j < total; j++) {
      const ballSpan = document.createElement("span");
      if (j < shots.length) {
        const points = shots[j];
        if (points > 0) {
          ballSpan.className = "penalty-ball mini hit";
          ballSpan.innerHTML = "⚽";
        } else {
          ballSpan.className = "penalty-ball mini missed";
          ballSpan.innerHTML = "⚽";
        }
      } else {
        ballSpan.className = "penalty-ball mini pending";
      }
      container.appendChild(ballSpan);
    }
  }

  const PIXI_COLORS = [0xff4455, 0x44aaff, 0x00ff88, 0xffaa00, 0xdd44ff, 0x00ffee];

  function animateScore(playerIndex, delta, total, zoneX, zoneY, zoneId) {
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
    
    // Disparar Audio sintetizado (solo si no es gol en la rana ni en el sapo, para que suenen sus respectivos mp3)
    if (zoneId !== "rana" && zoneId !== "sapo") {
      AudioFX.playPoint(delta);
    }
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
        const finalPts = data.mode === "goleador" ? s.balls_pocketed : s.score + " PTS";
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

  // ── Funciones Privadas del Carrusel de Atracción y Rankings ──

  async function _initSupabase() {
    if (_supabaseClient) return;
    try {
      const res = await fetch("/api/supabase-config");
      const cfg = await res.json();
      if (cfg.enabled && window.supabase && cfg.url && cfg.anon_key) {
        _supabaseClient = window.supabase.createClient(cfg.url, cfg.anon_key);
        _supabaseConfigured = true;
      }
    } catch (e) {
      console.warn("Error al inicializar Supabase en display client:", e);
    }
  }

  async function _fetchMachineInfo() {
    try {
      const res = await fetch("/api/machine/info");
      const info = await res.json();
      _arcadeId = info.arcade_id || "FUTSPO_01";
      _machineZone = info.zone || "Buenos Aires";
      _machineLat = info.latitude ?? -34.6037;
      _machineLon = info.longitude ?? -58.3816;
    } catch (e) {
      console.warn("Error al obtener información de la máquina:", e);
    }
  }

  function _createRankingRowHtml(pos, name, zone, score, avatarUrl) {
    const isTop5 = pos <= 5;
    const isPodium = pos <= 3;
    const rowClass = `ranking-row-item ${isTop5 ? 'top-5-row' : ''} ${isPodium ? 'rank-' + pos : ''}`;
    const defaultAvatar = `https://api.dicebear.com/7.x/pixel-art/svg?seed=${encodeURIComponent(name)}`;
    const avatar = avatarUrl || defaultAvatar;

    return `
      <div class="${rowClass}">
        <div class="ranking-row-rank">${pos}</div>
        <div class="ranking-row-player-info">
          <div class="ranking-row-avatar">
            <img class="ranking-row-avatar-img" src="${avatar}">
          </div>
          <span class="ranking-row-name">${name}</span>
        </div>
        <div class="ranking-row-zone">${zone || 'Local'}</div>
        <div class="ranking-row-score">${score}</div>
      </div>
    `;
  }

  function _renderRankings(containerLeftId, containerRightId, data) {
    const leftEl = document.getElementById(containerLeftId);
    const rightEl = document.getElementById(containerRightId);
    if (!leftEl || !rightEl) return;

    leftEl.innerHTML = "";
    rightEl.innerHTML = "";

    // Puestos 1-8 (Columna Izquierda)
    for (let i = 0; i < 8; i++) {
      if (data[i]) {
        leftEl.innerHTML += _createRankingRowHtml(i + 1, data[i].name, data[i].zone, data[i].score, data[i].avatar_url);
      }
    }

    // Puestos 9-15 (Columna Derecha)
    for (let i = 8; i < 15; i++) {
      if (data[i]) {
        rightEl.innerHTML += _createRankingRowHtml(i + 1, data[i].name, data[i].zone, data[i].score, data[i].avatar_url);
      }
    }
  }

  function _mergeWithMockData(fetched, mock) {
    const merged = [...fetched];
    for (let i = merged.length; i < 15; i++) {
      if (mock[i]) {
        merged.push(mock[i]);
      }
    }
    return merged.slice(0, 15);
  }

  async function _fetchSupabaseRankings() {
    await _initSupabase();
    await _fetchMachineInfo();

    let localData = [];
    let zonalData = [];
    let globalData = [];

    const zonalTitle = document.getElementById("zonal-title-text");
    if (zonalTitle && _machineZone) {
      zonalTitle.textContent = `TOP 15 ${_machineZone.toUpperCase()}`;
    }

    if (_supabaseClient) {
      // 1. Rankings Locales
      try {
        const { data, error } = await _supabaseClient
          .from("rankings_view")
          .select("player_name, score, zone, avatar_url")
          .eq("arcade_id", _arcadeId)
          .order("score", { ascending: false })
          .limit(15);
        if (!error && data) {
          localData = data.map(d => ({ name: d.player_name, score: d.score, zone: d.zone, avatar_url: d.avatar_url }));
        }
      } catch (e) {
        console.warn("Error al cargar rankings locales de Supabase:", e);
      }

      // 2. Rankings Provinciales (Zonales)
      try {
        const { data, error } = await _supabaseClient
          .from("rankings_view")
          .select("player_name, score, zone, avatar_url")
          .eq("zone", _machineZone)
          .order("score", { ascending: false })
          .limit(15);
        if (!error && data && data.length > 0) {
          zonalData = data.map(d => ({ name: d.player_name, score: d.score, zone: d.zone, avatar_url: d.avatar_url }));
        } else {
          // Backup RPC
          const { data: rpcData, error: rpcError } = await _supabaseClient.rpc("get_zonal_rankings", {
            machine_lat: _machineLat,
            machine_lon: _machineLon,
            radius_km: 100.0
          });
          if (!rpcError && rpcData) {
            zonalData = rpcData.map(d => ({ name: d.player_name, score: d.score, zone: d.zone, avatar_url: d.avatar_url }));
          }
        }
      } catch (e) {
        console.warn("Error al cargar rankings zonales de Supabase:", e);
      }

      // 3. Rankings Globales
      try {
        const { data, error } = await _supabaseClient
          .from("profiles")
          .select("name, total_score, zone, avatar_url")
          .order("total_score", { ascending: false })
          .limit(15);
        if (!error && data) {
          globalData = data.map(d => ({ name: d.name, score: d.total_score, zone: d.zone, avatar_url: d.avatar_url }));
        }
      } catch (e) {
        console.warn("Error al cargar rankings globales de Supabase:", e);
      }
    }

    // Unificar con Mock Data
    const finalLocal = _mergeWithMockData(localData, MOCK_LOCAL);
    const finalZonal = _mergeWithMockData(zonalData, MOCK_ZONAL);
    const finalGlobal = _mergeWithMockData(globalData, MOCK_GLOBAL);

    // Renderizar
    _renderRankings("attract-local-rows-left", "attract-local-rows-right", finalLocal);
    _renderRankings("attract-zonal-rows-left", "attract-zonal-rows-right", finalZonal);
    _renderRankings("attract-global-rows-left", "attract-global-rows-right", finalGlobal);
  }

  async function _renderPromotions() {
    const container = document.getElementById("attract-promos-container");
    if (!container) return;

    let promotions = [];
    try {
      const res = await fetch("/api/config/promotions");
      promotions = await res.json();
    } catch (e) {
      console.warn("Error al cargar promociones de la API:", e);
    }

    // Fallback si la API no devuelve nada
    if (!promotions || promotions.length === 0) {
      promotions = [
        {
          id: "happy_hour",
          title: "HAPPY HOUR FUTSAPO",
          desc: "2x1 en créditos de juego de lunes a jueves de 18:00 a 20:00 hs.",
          badge: "2x1 CRÉDITOS",
          icon: "⚡",
          days: [1, 2, 3, 4],
          start_hour: 18,
          end_hour: 20
        },
        {
          id: "crazy_wednesday",
          title: "MIÉRCOLES LOCOS",
          desc: "Todos los miércoles, obtené un 25% de descuento en la carga de fichas.",
          badge: "25% OFF FICHAS",
          icon: "🎉",
          days: [3],
          start_hour: 0,
          end_hour: 24
        },
        {
          id: "late_night",
          title: "TRASNOCHE CYBER",
          desc: "Viernes y sábados de 23:00 a 02:00 hs, jugá 3 partidas por sólo $500.",
          badge: "DESCUENTO DE NOCHE",
          icon: "🌙",
          days: [5, 6, 0],
          start_hour: 23,
          end_hour: 2
        },
        {
          id: "weekend_champions",
          title: "FIN DE SEMANA",
          desc: "Sábados y domingos sumás el doble para el Ranking Provincial y Global.",
          badge: "DOBLE PUNTOS RANKING",
          icon: "🏆",
          days: [6, 0],
          start_hour: 0,
          end_hour: 24
        }
      ];
    }

    let activePromos = [];
    try {
      const res = await fetch("/api/promotions/active");
      activePromos = await res.json();
    } catch (e) {
      console.warn("Error al cargar promociones activas de la API (usando local fallback):", e);
      const now = new Date();
      const day = now.getDay();
      const hour = now.getHours();
      activePromos = promotions.filter(p => {
        if (p.days && p.days.includes(day)) {
          const sh = p.start_hour ?? 0;
          const eh = p.end_hour ?? 24;
          if (sh < eh) {
            return (hour >= sh && hour < eh);
          } else {
            return (hour >= sh || hour < eh);
          }
        }
        return false;
      });
    }

    const activePromoIds = new Set(activePromos.map(p => p.id));

    container.innerHTML = "";
    promotions.forEach(promo => {
      const isActive = activePromoIds.has(promo.id);
      const activeClass = isActive ? "active" : "";
      const activeLabel = isActive ? '<div class="promo-active-label">VIGENTE AHORA</div>' : "";
      
      container.innerHTML += `
        <div class="promo-card ${activeClass}">
          ${activeLabel}
          <div class="promo-card-icon">${promo.icon || '🎉'}</div>
          <div class="promo-card-title">${promo.title}</div>
          <div class="promo-card-desc">${promo.desc}</div>
          <div class="promo-card-badge">${promo.badge}</div>
        </div>
      `;
    });

    const splashBanner = document.getElementById("splash-active-promo");
    if (splashBanner) {
      if (activePromos.length > 0) {
        const text = activePromos.map(p => `${p.icon || '🔥'} PROMO ACTIVA: ${p.title} (${p.badge})`).join(" | ");
        splashBanner.innerHTML = text;
        splashBanner.style.display = "block";
      } else {
        splashBanner.style.display = "none";
      }
    }
  }

  function startAttractCycle() {
    stopAttractCycle();

    _attractIndex = 0;
    const slides = document.querySelectorAll("#screen-attract .attract-slide");
    slides.forEach((slide, idx) => slide.classList.toggle("active", idx === 0));

    // Cargar datos
    _fetchSupabaseRankings();
    _renderPromotions();

    // Rotar diapositivas cada 10 segundos y refrescar promociones para vigencia exacta
    _attractInterval = setInterval(() => {
      const activeSlides = document.querySelectorAll("#screen-attract .attract-slide");
      if (activeSlides.length === 0) return;

      _attractIndex = (_attractIndex + 1) % activeSlides.length;
      activeSlides.forEach((slide, idx) => {
        slide.classList.toggle("active", idx === _attractIndex);
      });

      // Refrescar vigencia exacta de las promociones en cada ciclo
      _renderPromotions();

      // Sonido sutil de transición de slide
      if (typeof AudioFX !== 'undefined' && AudioFX.playSlideTransition) {
        AudioFX.playSlideTransition();
      }
    }, 10000);
  }

  function stopAttractCycle() {
    if (_attractInterval) {
      clearInterval(_attractInterval);
      _attractInterval = null;
    }
    _attractIndex = 0;
    const slides = document.querySelectorAll("#screen-attract .attract-slide");
    slides.forEach((slide, idx) => slide.classList.toggle("active", idx === 0));
  }

  return {
    show, updateSelectPlayers, updateSelectMode, updatePayment, updateCredits,
    updateWaitingStart, updateSelectTeam, updateTiebreak, updateGame, animateScore, updateGameOver, updateTurnChange,
    renderPenaltyBalls, startAttractCycle, stopAttractCycle
  };
})();
