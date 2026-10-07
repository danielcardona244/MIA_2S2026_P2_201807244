#ifndef API_H
#define API_H

// Inicia el servidor REST local de ExtreamFS.
// El servidor atiende:
//   GET  /api/health
//   POST /api/execute   body JSON: {"commands":"..."}
//   OPTIONS             soporte CORS para el frontend
int run_api_server(int port = 8080);

#endif
