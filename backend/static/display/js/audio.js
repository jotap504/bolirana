/**
 * AudioFX — Sintetizador retro de 8-bits usando Web Audio API nativo.
 * Produce efectos de sonido arcade perfectos con 0 bytes de peso de archivo.
 */
const AudioFX = (() => {
  let ctx = null;
  let ranaAudio = null;
  let dibuAudio = null;
  let _volumeMultiplier = 0.8; // Volumen por defecto (80%)

  function init() {
    if (ctx) {
      if (ctx.state === "suspended") {
        ctx.resume().then(() => {
          console.log("AudioContext reanudado por interacción del usuario.");
        }).catch(err => {
          console.warn("Error al intentar reanudar el AudioContext:", err);
        });
      }
      return;
    }
    try {
      const AudioContextClass = window.AudioContext || window.webkitAudioContext;
      ctx = new AudioContextClass();
      console.log("AudioContext de Bolirana inicializado correctamente.");
      
      // Pre-cargar audio personalizado de Rana (con cache-buster para evitar cachear versiones viejas)
      ranaAudio = new Audio('/audios/rana.mp3?v=' + Date.now());
      ranaAudio.load();

      // Pre-cargar audio personalizado de Sapo/Dibu (con cache-buster)
      dibuAudio = new Audio('/audios/dibu.mp3?v=' + Date.now());
      dibuAudio.load();
    } catch (e) {
      console.warn("Web Audio API no está soportado en este navegador.", e);
    }
  }

  function _resume() {
    init();
    if (ctx && ctx.state === "suspended") {
      ctx.resume();
    }
  }

  // Crea un oscilador temporal con envolvente de volumen para retro sfx
  function _playTone(freqs, duration, type = "square", volume = 0.1, sweepFreq = null) {
    _resume();
    if (!ctx) return;

    const osc = ctx.createOscillator();
    const gain = ctx.createGain();

    osc.type = type;
    osc.connect(gain);
    gain.connect(ctx.destination);

    const now = ctx.currentTime;
    
    // Configuración de frecuencia
    if (Array.isArray(freqs)) {
      // Arpegios o notas múltiples secuenciales
      let stepTime = duration / freqs.length;
      freqs.forEach((f, idx) => {
        osc.frequency.setValueAtTime(f, now + idx * stepTime);
      });
    } else {
      osc.frequency.setValueAtTime(freqs, now);
      if (sweepFreq) {
        // Barrido de frecuencia retro (e.g., de alta a baja para láseres/explosiones)
        osc.frequency.exponentialRampToValueAtTime(sweepFreq, now + duration);
      }
    }

    // Configuración de envolvente de volumen optimizada (Retro arcade ADSR: más sostenido y claro)
    gain.gain.setValueAtTime(volume * _volumeMultiplier, now);
    // Sostener el volumen al máximo durante el 40% de la duración, luego decaer linealmente
    gain.gain.setValueAtTime(volume * _volumeMultiplier, now + duration * 0.4);
    gain.gain.linearRampToValueAtTime(0.0001, now + duration);

    osc.start(now);
    osc.stop(now + duration);
  }

  // Sonido de ficha ingresada (Láser ascendente brillante de doble tono)
  function playCoin() {
    _resume();
    _playTone(150, 0.18, "sawtooth", 0.14, 1200);
    setTimeout(() => {
      _playTone(300, 0.28, "square", 0.10, 1800);
    }, 80);
  }

  // Clic de menú o navegación
  function playTick() {
    _playTone(800, 0.05, "triangle", 0.18, 300);
  }

  // Inicio de partida (Fanfarria ascendente clásica 8-bits)
  function playStart() {
    const now = ctx ? ctx.currentTime : 0;
    _playTone([261.63, 329.63, 392.00, 523.25], 0.45, "square", 0.18);
    setTimeout(() => {
      _playTone(523.25, 0.35, "square", 0.18, 1046.50);
    }, 250);
  }

  function playRana() {
    _resume();
    if (ranaAudio) {
      ranaAudio.volume = _volumeMultiplier;
      ranaAudio.currentTime = 0;
      ranaAudio.play().catch(err => {
        console.warn("Fallo al reproducir audio personalizado de Rana:", err);
        // Fallback sintetizado
        _playTone(100, 0.8, "sawtooth", 0.30, 1800);
        setTimeout(() => {
          _playTone(800, 0.6, "square", 0.20, 100);
        }, 150);
      });
    } else {
      _playTone(100, 0.8, "sawtooth", 0.30, 1800);
      setTimeout(() => {
        _playTone(800, 0.6, "square", 0.20, 100);
      }, 150);
    }
  }

  function playSapo() {
    _resume();
    if (dibuAudio) {
      dibuAudio.volume = _volumeMultiplier;
      dibuAudio.currentTime = 0;
      dibuAudio.play().catch(err => {
        console.warn("Fallo al reproducir audio personalizado de Dibu (Sapo):", err);
        // Fallback sintetizado
        _playTone(120, 0.8, "sawtooth", 0.30, 2000);
        setTimeout(() => {
          _playTone(600, 0.6, "square", 0.20, 150);
        }, 150);
      });
    } else {
      _playTone(120, 0.8, "sawtooth", 0.30, 2000);
      setTimeout(() => {
        _playTone(600, 0.6, "square", 0.20, 150);
      }, 150);
    }
  }

  // Al anotar puntos (Barrido que escala en tono y fuerza según la cantidad de puntos)
  function playPoint(val) {
    _resume();
    const points = parseInt(val) || 0;
    if (points <= 0) return;

    if (points >= 1000) {
      // Súper puntos: Fallback sintetizado de alta puntuación (Largo y potente)
      _playTone(100, 0.7, "sawtooth", 0.30, 1800);
      setTimeout(() => {
        _playTone(800, 0.5, "square", 0.20, 100);
      }, 150);
    } else if (points >= 100) {
      // Puntuación intermedia: Láser brillante y más largo
      const baseFreq = 380 + (points / 2);
      _playTone(baseFreq, 0.45, "sawtooth", 0.26, baseFreq * 2.8);
    } else {
      // Puntuaciones chicas: Sonido doble más sostenido
      _playTone(600, 0.22, "square", 0.24, 1200);
      setTimeout(() => {
        _playTone(750, 0.26, "square", 0.18, 1500);
      }, 90);
    }
  }

  // Fin de la partida (Melodía melancólica retro descendente)
  function playGameOver() {
    _resume();
    _playTone([523.25, 493.88, 440.00, 392.00, 349.23, 329.63, 261.63], 0.8, "triangle", 0.18);
    setTimeout(() => {
      _playTone(196.00, 0.6, "sawtooth", 0.15, 60);
    }, 600);
  }

  // Alarma de proximidad (Sirena oscilante de emergencia)
  function playAlarm() {
    _resume();
    _playTone(200, 0.35, "sawtooth", 0.2, 800);
    setTimeout(() => {
      _playTone(800, 0.35, "sawtooth", 0.2, 200);
    }, 180);
  }

  // Sonido de transición entre pantallas principales (doble tono ascendente futurista)
  function playScreenTransition() {
    _resume();
    _playTone(180, 0.15, "triangle", 0.08, 900);
    setTimeout(() => {
      _playTone(450, 0.2, "sine", 0.06, 1200);
    }, 60);
  }

  // Sonido de transición de diapositivas en attract mode (un tick suave y amortiguado)
  function playSlideTransition() {
    _resume();
    _playTone(120, 0.08, "triangle", 0.04, 240);
  }

  function setVolume(pct) {
    _volumeMultiplier = Math.max(0, Math.min(100, parseInt(pct) ?? 80)) / 100;
    console.log("Volumen del juego ajustado a: " + (_volumeMultiplier * 100) + "%");
    if (ranaAudio) {
      ranaAudio.volume = _volumeMultiplier;
    }
    if (dibuAudio) {
      dibuAudio.volume = _volumeMultiplier;
    }
  }

  function getVolumeMultiplier() {
    return _volumeMultiplier;
  }

  return { 
    init, 
    playCoin, 
    playTick, 
    playStart, 
    playPoint, 
    playGameOver, 
    playAlarm, 
    playRana, 
    playSapo,
    playScreenTransition, 
    playSlideTransition,
    setVolume,
    getVolumeMultiplier
  };
})();

// Auto-inicializar cuando el usuario interactúa para saltar la política de reproducción
["click", "keydown", "touchstart"].forEach(evt => {
  window.addEventListener(evt, () => AudioFX.init(), { once: true });
});
