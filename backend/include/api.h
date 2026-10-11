#ifndef API_H
#define API_H

#include <string>

// Inicia el servidor REST de ExtreamFS.
// Por seguridad el modo local escucha en 127.0.0.1. Para AWS/EC2 se puede
// exponer en todas las interfaces con:
//   extreamfs --server 8080 0.0.0.0
// El servidor atiende los endpoints /api/* y habilita CORS para el frontend.
int run_api_server(int port = 8080, const std::string& bind_address = "127.0.0.1");

#endif
