const WS = (() => {
  let sock = null;
  const listeners = {};

  function connect() {
    const protocol = location.protocol === "https:" ? "wss:" : "ws:";
    const url = `${protocol}//${location.host}/ws/display`;
    sock = new WebSocket(url);

    sock.onopen  = () => console.log("WS display conectado");
    sock.onclose = () => { console.warn("WS cerrado, reintentando..."); setTimeout(connect, 2000); };
    sock.onerror = (e) => console.error("WS error", e);

    sock.onmessage = ({ data }) => {
      try {
        const msg = JSON.parse(data);
        const type = msg.type;
        if (listeners[type]) listeners[type].forEach(fn => fn(msg));
        if (listeners["*"])   listeners["*"].forEach(fn => fn(msg));
      } catch(e) { console.error("WS parse error", e); }
    };
  }

  return {
    connect,
    on(type, fn) {
      if (!listeners[type]) listeners[type] = [];
      listeners[type].push(fn);
    },
    send(msg) {
      if (sock && sock.readyState === WebSocket.OPEN)
        sock.send(JSON.stringify(msg));
    },
    btn(id) { this.send({ type: "btn", id }); }
  };
})();
