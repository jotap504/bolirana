/**
 * FX — Motor de Videojuego WebGL para Bolirana Cyber Stadium (PixiJS v7).
 * Renderiza gráficos 3.5D acelerados por hardware en tiempo real: rejilla de movimiento,
 * focos volumétricos, anillos cibernéticos y emisores de partículas continuos.
 */
const FX = (() => {
  let app = null;
  let gridGraphics = null;
  let spotlightLeft = null;
  let spotlightRight = null;
  let qrRingGraphics = null;
  let logoRingGraphics = null;
  
  // Variables de control de animación
  let time = 0;
  let gridOffset = 0;
  let gridSpeed = 2; // Velocidad del scrolling del grid Z
  let isGameOver = false;

  function init() {
    const canvas = document.getElementById("fx-canvas");
    if (!canvas || !window.PIXI) return;

    // Detectar si el modo de bajo rendimiento (Low Spec) está activo
    if (localStorage.getItem("lowSpecMode") === "true") {
      canvas.style.display = "none";
      document.documentElement.classList.add("low-spec");
      console.log("Modo de bajo rendimiento activo: PixiJS deshabilitado.");
      return;
    }

    // Configurar aplicación PixiJS con WebGL habilitado y redimensionamiento automático
    app = new PIXI.Application({
      view: canvas,
      width: window.innerWidth,
      height: window.innerHeight,
      backgroundAlpha: 0,
      antialias: false, // Deshabilitar antialiasing para mejorar drásticamente los FPS en GPUs integradas
      resizeTo: window
    });

    // 1. Crear capa para la rejilla perspectiva 3.5D
    gridGraphics = new PIXI.Graphics();
    app.stage.addChild(gridGraphics);

    // 2. Crear focos volumétricos con gradientes aditivos
    _createSpotlights();

    // 3. Crear anillos de neón orbitales
    qrRingGraphics = new PIXI.Graphics();
    logoRingGraphics = new PIXI.Graphics();
    app.stage.addChild(qrRingGraphics);
    app.stage.addChild(logoRingGraphics);

    // 4. Iniciar emisor continuo de chispas ambientales
    _startAmbientDust();

    // 5. Registrar el ticker principal del juego (60 FPS)
    app.ticker.add(_gameLoop);
  }

  // Crea focos aditivos usando texturas generadas a partir de gradientes HTML5
  function _createSpotlights() {
    if (!app) return;

    // Generar textura de gradiente suave lineal
    const gradCanvas = document.createElement("canvas");
    gradCanvas.width = 128;
    gradCanvas.height = 512;
    const ctx = gradCanvas.getContext("2d");
    const grad = ctx.createLinearGradient(0, 0, 0, 512);
    grad.addColorStop(0, "rgba(0, 229, 255, 0.45)"); // Cian brillante arriba
    grad.addColorStop(0.3, "rgba(0, 150, 255, 0.2)"); // Azul medio
    grad.addColorStop(1, "rgba(0, 0, 0, 0)"); // Fuga a negro/transparente
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, 128, 512);

    const texture = PIXI.Texture.from(gradCanvas);

    // Foco izquierdo
    spotlightLeft = new PIXI.Sprite(texture);
    spotlightLeft.anchor.set(0.5, 0);
    spotlightLeft.blendMode = PIXI.BLEND_MODES.ADD;
    spotlightLeft.x = 80;
    spotlightLeft.y = 15;
    spotlightLeft.width = 160;
    spotlightLeft.height = app.screen.height * 1.2;
    app.stage.addChild(spotlightLeft);

    // Foco derecho
    spotlightRight = new PIXI.Sprite(texture);
    spotlightRight.anchor.set(0.5, 0);
    spotlightRight.blendMode = PIXI.BLEND_MODES.ADD;
    spotlightRight.x = app.screen.width - 80;
    spotlightRight.y = 15;
    spotlightRight.width = 160;
    spotlightRight.height = app.screen.height * 1.2;
    app.stage.addChild(spotlightRight);
  }

  const ambientParticles = [];
  let cachedQrRect = null;
  let cachedLogoRect = null;

  window.addEventListener("resize", () => {
    cachedQrRect = null;
    cachedLogoRect = null;
  });

  // Emisor persistente de chispas que flotan orgánicamente de fondo (Optimizado para Netbooks)
  function _startAmbientDust() {
    if (!app) return;
    const count = 12; // Reducido de 40 a 12 para optimizar rendimiento en hardware de bajos recursos
    for (let i = 0; i < count; i++) {
      const p = new PIXI.Graphics();
      const radius = 1.2 + Math.random() * 2.8;
      const color = Math.random() > 0.4 ? 0x00e5ff : 0x00ff88;
      
      p.beginFill(color, 0.25 + Math.random() * 0.45)
       .drawCircle(0, 0, radius)
       .endFill();
      
      p.x = Math.random() * app.screen.width;
      p.y = Math.random() * app.screen.height;
      app.stage.addChild(p);

      ambientParticles.push({
        graphics: p,
        speed: 0.25 + Math.random() * 0.65,
        drift: (Math.random() - 0.5) * 0.25
      });
    }

    // Un solo ticker global en vez de registrar uno por cada partícula, ahorrando CPU
    app.ticker.add((delta) => {
      for (let i = 0; i < ambientParticles.length; i++) {
        const item = ambientParticles[i];
        const p = item.graphics;
        p.y -= item.speed * delta;
        p.x += item.drift * delta;
        if (p.y < -10) {
          p.y = app.screen.height + 10;
          p.x = Math.random() * app.screen.width;
        }
      }
    });
  }

  // Bucle principal de actualización de gráficos (Game Loop)
  function _gameLoop(delta) {
    time += 0.015 * delta;

    // 1. Mover y dibujar la rejilla 3.5D en perspectiva Z (Deshabilitado a petición del usuario)
    // _renderPerspectiveGrid();

    // 2. Mover focos volumétricos con funciones trigonométricas cruzadas
    if (spotlightLeft && spotlightRight) {
      if (isGameOver) {
        // En fin de juego, giran de forma festiva y dorada
        spotlightLeft.tint = 0xf5c000;
        spotlightRight.tint = 0xf5c000;
        spotlightLeft.rotation = Math.sin(time * 1.8) * 0.6;
        spotlightRight.rotation = Math.cos(time * 1.8) * 0.6;
      } else {
        // Modulación cian/azul estándar
        spotlightLeft.tint = 0x00e5ff;
        spotlightRight.tint = 0x00ff88;
        spotlightLeft.rotation = Math.sin(time * 0.8) * 0.45;
        spotlightRight.rotation = Math.cos(time * 0.7) * 0.45;
      }
      
      // Ajustar posición si la pantalla cambia de tamaño
      spotlightRight.x = app.screen.width - 80;
    }

    // 3. Renderizar anillos de neón orbitales sobre elementos del DOM
    _renderNeonRings();
  }

  // Dibuja un grid en perspectiva 3.5D con scrolling constante hacia adelante
  function _renderPerspectiveGrid() {
    if (!gridGraphics) return;

    gridGraphics.clear();
    const w = app.screen.width;
    const h = app.screen.height;
    
    // Altura del horizonte cibernético (60% de la pantalla)
    const horizon = h * 0.58; 
    const gridHeight = h - horizon;

    // Dibujar línea de horizonte neón cian
    gridGraphics.lineStyle(3, 0x00e5ff, 0.65);
    gridGraphics.moveTo(0, horizon);
    gridGraphics.lineTo(w, horizon);

    // Ajustar velocidad e incremento del scroll
    gridOffset += gridSpeed;
    if (gridOffset >= 40) gridOffset = 0;

    // Dibujar líneas de perspectiva radiales (partiendo del centro del horizonte)
    const centerX = w / 2;
    const linesCount = 24;
    gridGraphics.lineStyle(1.5, 0x00e5ff, 0.22);
    
    for (let i = 0; i <= linesCount; i++) {
      const ratio = i / linesCount;
      // Posición final en la parte inferior de la pantalla (distribución abierta)
      const xEnd = w * (ratio * 1.8 - 0.4); 
      gridGraphics.moveTo(centerX, horizon);
      gridGraphics.lineTo(xEnd, h);
    }

    // Dibujar líneas horizontales con escala exponencial Z (más juntas cerca del horizonte)
    gridGraphics.lineStyle(1.5, 0x00e5ff, 0.35);
    let z = gridOffset;
    while (z < gridHeight) {
      // Función exponencial para simular profundidad Z en perspectiva
      const ratio = z / gridHeight;
      const y = horizon + Math.pow(ratio, 2.2) * gridHeight;
      
      if (y > horizon && y < h) {
        const opacity = Math.min(1.0, Math.pow(ratio, 1.8) * 0.7);
        gridGraphics.lineStyle(1.5, 0x00e5ff, opacity);
        gridGraphics.moveTo(0, y);
        gridGraphics.lineTo(w, y);
      }
      z += 30; // Distancia base de rejilla
    }
  }

  // Coloca y rota anillos cibernéticos de neón (Optimizado: evita recálculos de layout continuos)
  function _renderNeonRings() {
    // 1. Anillo alrededor del QR en la pantalla de pagos
    if (qrRingGraphics) {
      qrRingGraphics.clear();
      const paymentScreen = document.getElementById("screen-payment");
      
      if (paymentScreen && paymentScreen.classList.contains("active")) {
        if (!cachedQrRect) {
          const qrEl = document.getElementById("qr-canvas");
          if (qrEl) {
            cachedQrRect = qrEl.getBoundingClientRect(); // Solo se llama una vez en vez de 60 FPS
          }
        }
        if (cachedQrRect) {
          const cx = cachedQrRect.left + cachedQrRect.width / 2;
          const cy = cachedQrRect.top + cachedQrRect.height / 2;

          // Dibujar doble anillo rotativo
          qrRingGraphics.lineStyle(2.5, 0x00e5ff, 0.7);
          qrRingGraphics.drawCircle(cx, cy, 110 + Math.sin(time * 3) * 3);
          
          qrRingGraphics.lineStyle(1.5, 0xf5c000, 0.5);
          // Anillo interrumpido en arco
          const startAngle = time * 2;
          qrRingGraphics.arc(cx, cy, 118, startAngle, startAngle + Math.PI, false);
        }
      } else {
        cachedQrRect = null;
      }
    }

    // 2. Anillo alrededor de la cyber-rana en el Logo de inicio
    if (logoRingGraphics) {
      logoRingGraphics.clear();
      const attractScreen = document.getElementById("screen-attract");

      if (attractScreen && attractScreen.classList.contains("active")) {
        if (!cachedLogoRect) {
          const frogEl = document.querySelector(".logo-frog-svg");
          if (frogEl) {
            cachedLogoRect = frogEl.getBoundingClientRect(); // Solo se llama una vez en vez de 60 FPS
          }
        }
        if (cachedLogoRect) {
          const cx = cachedLogoRect.left + cachedLogoRect.width / 2;
          const cy = cachedLogoRect.top + cachedLogoRect.height / 2;

          // Dibujar órbita neón verde alrededor de la cabeza de la rana
          logoRingGraphics.lineStyle(2, 0x00ff88, 0.6);
          const startAngle = -time * 1.5;
          logoRingGraphics.arc(cx, cy, 60, startAngle, startAngle + Math.PI * 0.75, false);
          logoRingGraphics.arc(cx, cy, 60, startAngle + Math.PI, startAngle + Math.PI * 1.75, false);
        }
      } else {
        cachedLogoRect = null;
      }
    }
  }

  // Explota partículas neón físicas con rebotes y gravedad
  function goalBurst(x, y, color = 0x00e5ff) {
    if (!app) return;
    const cnt = 45;
    const gravity = 0.24;

    for (let i = 0; i < cnt; i++) {
      const g = new PIXI.Graphics();
      const radius = 3.5 + Math.random() * 6.5;
      
      g.beginFill(color)
       .drawCircle(0, 0, radius)
       .endFill();
      
      g.x = x; 
      g.y = y;
      app.stage.addChild(g);

      const angle = Math.random() * Math.PI * 2;
      const speed = 4 + Math.random() * 9.5;
      let vx = Math.cos(angle) * speed;
      let vy = Math.sin(angle) * speed - 2.5; // Empuje inicial hacia arriba
      let life = 1.0;
      const decay = 0.015 + Math.random() * 0.015;

      const tick = () => {
        vy += gravity; // Gravedad activa
        g.x += vx; 
        g.y += vy;
        
        life -= decay; 
        g.alpha = life;
        g.scale.x = life;
        g.scale.y = life;

        if (life <= 0) { 
          app.stage.removeChild(g); 
          app.ticker.remove(tick); 
        }
      };
      app.ticker.add(tick);
    }
  }

  // Sacudida de pantalla física (Screen Shake por hardware de traducción CSS)
  function shakeScreen(intensity = 10, duration = 220) {
    const appEl = document.getElementById("app");
    if (!appEl) return;

    const start = performance.now();
    const step = (now) => {
      const elapsed = now - start;
      if (elapsed < duration) {
        const remaining = 1 - (elapsed / duration);
        const x = (Math.random() - 0.5) * intensity * remaining;
        const y = (Math.random() - 0.5) * intensity * remaining;
        appEl.style.transform = `translate(${x}px, ${y}px)`;
        requestAnimationFrame(step);
      } else {
        appEl.style.transform = "";
      }
    };
    requestAnimationFrame(step);
  }

  // Destello aditivo en la pantalla entera
  function flashScreen(color = 0x00e5ff, alpha = 0.22) {
    if (!app) return;
    const g = new PIXI.Graphics();
    g.beginFill(color, alpha)
     .drawRect(0, 0, app.screen.width, app.screen.height)
     .endFill();
    
    app.stage.addChild(g);
    let life = 1.0;
    
    const tick = () => {
      life -= 0.06;
      g.alpha = life * alpha;
      if (life <= 0) { 
        app.stage.removeChild(g); 
        app.ticker.remove(tick); 
      }
    };
    app.ticker.add(tick);
  }

  // Popup de texto de puntos en 3D
  function scoreFloat(x, y, text, color = "#00e5ff") {
    const el = document.getElementById("score-popup");
    if (!el) return;
    
    el.textContent = "+" + text;
    el.style.left = x + "px"; 
    el.style.top = y + "px";
    el.style.color = color;
    
    el.classList.remove("show");
    void el.offsetWidth; // Disparar reflow para reiniciar la animación
    el.classList.add("show");
    
    setTimeout(() => el.classList.remove("show"), 900);
  }

  // Controladores de estado para cambiar la dinámica WebGL
  function setGameOverState(active) {
    isGameOver = active;
    gridSpeed = active ? 0.6 : 2; // El grid se frena o avanza
  }

  // Fuegos artificiales de victoria
  let fireworkInterval = null;
  function startVictoryFireworks() {
    if (!app) return;
    stopVictoryFireworks();
    setGameOverState(true);

    const colors = [0x00e5ff, 0x00ff88, 0xf5c000, 0xff2233, 0xdd44ff];
    fireworkInterval = setInterval(() => {
      const rx = 150 + Math.random() * (app.screen.width - 300);
      const ry = 120 + Math.random() * (app.screen.height - 300);
      const rc = colors[Math.floor(Math.random() * colors.length)];
      
      goalBurst(rx, ry, rc);
      flashScreen(rc, 0.08);
    }, 450);
  }

  function stopVictoryFireworks() {
    setGameOverState(false);
    if (fireworkInterval) {
      clearInterval(fireworkInterval);
      fireworkInterval = null;
    }
  }

  return { 
    init, 
    goalBurst, 
    shakeScreen, 
    flashScreen, 
    scoreFloat, 
    setGameOverState,
    startVictoryFireworks, 
    stopVictoryFireworks 
  };
})();

window.addEventListener("load", FX.init.bind(FX));

// Tecla de acceso rápido (Shift + L) para alternar el modo de bajos recursos (deshabilita PixiJS)
window.addEventListener("keydown", (e) => {
  if (e.key.toLowerCase() === "l" && e.shiftKey) {
    const isLow = localStorage.getItem("lowSpecMode") === "true";
    localStorage.setItem("lowSpecMode", isLow ? "false" : "true");
    alert(isLow 
      ? "🚀 ¡Modo ALTO RENDIMIENTO activado!\n\nSe restablecieron los efectos visuales avanzados (PixiJS).\nSe recargará la página."
      : "⚡ ¡Modo BAJO RENDIMIENTO activado!\n\nSe desactivaron los efectos pesados de fondo para máxima fluidez en tu Netbook.\nSe recargará la página."
    );
    window.location.reload();
  }
});
