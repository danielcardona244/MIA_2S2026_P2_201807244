#include "api.h"
#include "scanner.h"
#include "shared.h"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

namespace {

constexpr size_t MAX_HEADER_BYTES = 64 * 1024;
constexpr size_t MAX_BODY_BYTES = 2 * 1024 * 1024;

struct HttpRequest {
    string method;
    string path;
    map<string, string> headers;
    string body;
};

string lower_copy(string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

string trim_http(const string& value) {
    size_t first = 0;
    while (first < value.size() &&
           (value[first] == ' ' || value[first] == '\t' ||
            value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }

    size_t last = value.size();
    while (last > first &&
           (value[last - 1] == ' ' || value[last - 1] == '\t' ||
            value[last - 1] == '\r' || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

bool recv_more(int client, string& buffer) {
    char chunk[4096];
    ssize_t received = recv(client, chunk, sizeof(chunk), 0);
    if (received <= 0) return false;
    buffer.append(chunk, static_cast<size_t>(received));
    return true;
}

bool parse_request(int client, HttpRequest& request, string& error) {
    string raw;
    size_t header_end = string::npos;

    while ((header_end = raw.find("\r\n\r\n")) == string::npos) {
        if (raw.size() > MAX_HEADER_BYTES) {
            error = "encabezados HTTP demasiado grandes";
            return false;
        }
        if (!recv_more(client, raw)) {
            error = "conexion cerrada antes de recibir encabezados completos";
            return false;
        }
    }

    const string header_text = raw.substr(0, header_end);
    const size_t body_start = header_end + 4;

    istringstream lines(header_text);
    string request_line;
    if (!getline(lines, request_line)) {
        error = "solicitud HTTP vacia";
        return false;
    }
    if (!request_line.empty() && request_line.back() == '\r') request_line.pop_back();

    {
        istringstream first(request_line);
        string version;
        if (!(first >> request.method >> request.path >> version)) {
            error = "linea de solicitud HTTP invalida";
            return false;
        }
        if (version.rfind("HTTP/", 0) != 0) {
            error = "version HTTP invalida";
            return false;
        }
    }

    string line;
    while (getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == string::npos) continue;

        string key = lower_copy(trim_http(line.substr(0, colon)));
        string value = trim_http(line.substr(colon + 1));
        request.headers[key] = value;
    }

    size_t content_length = 0;
    auto it = request.headers.find("content-length");
    if (it != request.headers.end()) {
        try {
            size_t used = 0;
            unsigned long parsed = stoul(it->second, &used);
            if (used != it->second.size()) throw invalid_argument("contenido extra");
            content_length = static_cast<size_t>(parsed);
        } catch (...) {
            error = "Content-Length invalido";
            return false;
        }
    }

    if (content_length > MAX_BODY_BYTES) {
        error = "cuerpo HTTP demasiado grande";
        return false;
    }

    while (raw.size() - body_start < content_length) {
        if (!recv_more(client, raw)) {
            error = "conexion cerrada antes de recibir el cuerpo completo";
            return false;
        }
        if (raw.size() - body_start > MAX_BODY_BYTES) {
            error = "cuerpo HTTP demasiado grande";
            return false;
        }
    }

    request.body = raw.substr(body_start, content_length);

    // Un query string no forma parte de la ruta para el enrutamiento.
    size_t q = request.path.find('?');
    if (q != string::npos) request.path = request.path.substr(0, q);

    return true;
}

string json_escape(const string& value) {
    string out;
    out.reserve(value.size() + 32);

    static const char* hex = "0123456789abcdef";
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[(c >> 4) & 0x0F];
                    out += hex[c & 0x0F];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

bool hex_value(char c, unsigned& value) {
    if (c >= '0' && c <= '9') {
        value = static_cast<unsigned>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        value = static_cast<unsigned>(10 + c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        value = static_cast<unsigned>(10 + c - 'A');
        return true;
    }
    return false;
}

void append_utf8(string& out, unsigned codepoint) {
    if (codepoint <= 0x7F) {
        out += static_cast<char>(codepoint);
    } else if (codepoint <= 0x7FF) {
        out += static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

bool parse_json_string_at(const string& input, size_t& pos, string& output, string& error) {
    if (pos >= input.size() || input[pos] != '"') {
        error = "se esperaba una cadena JSON";
        return false;
    }

    ++pos;
    output.clear();

    while (pos < input.size()) {
        char c = input[pos++];
        if (c == '"') return true;

        if (c != '\\') {
            output += c;
            continue;
        }

        if (pos >= input.size()) {
            error = "escape JSON incompleto";
            return false;
        }

        char escaped = input[pos++];
        switch (escaped) {
            case '"': output += '"'; break;
            case '\\': output += '\\'; break;
            case '/': output += '/'; break;
            case 'b': output += '\b'; break;
            case 'f': output += '\f'; break;
            case 'n': output += '\n'; break;
            case 'r': output += '\r'; break;
            case 't': output += '\t'; break;
            case 'u': {
                if (pos + 4 > input.size()) {
                    error = "escape unicode JSON incompleto";
                    return false;
                }
                unsigned codepoint = 0;
                for (int i = 0; i < 4; ++i) {
                    unsigned digit = 0;
                    if (!hex_value(input[pos + i], digit)) {
                        error = "escape unicode JSON invalido";
                        return false;
                    }
                    codepoint = (codepoint << 4) | digit;
                }
                pos += 4;
                append_utf8(output, codepoint);
                break;
            }
            default:
                error = "escape JSON no reconocido";
                return false;
        }
    }

    error = "cadena JSON sin cerrar";
    return false;
}

void skip_ws(const string& text, size_t& pos) {
    while (pos < text.size() &&
           (text[pos] == ' ' || text[pos] == '\t' ||
            text[pos] == '\r' || text[pos] == '\n')) {
        ++pos;
    }
}

bool extract_commands(const string& body, string& commands, string& error) {
    size_t pos = 0;
    skip_ws(body, pos);
    if (pos >= body.size() || body[pos] != '{') {
        error = "el cuerpo debe ser un objeto JSON";
        return false;
    }
    ++pos;

    while (pos < body.size()) {
        skip_ws(body, pos);
        if (pos < body.size() && body[pos] == '}') break;

        string key;
        if (!parse_json_string_at(body, pos, key, error)) return false;
        skip_ws(body, pos);

        if (pos >= body.size() || body[pos] != ':') {
            error = "falta ':' despues de una propiedad JSON";
            return false;
        }
        ++pos;
        skip_ws(body, pos);

        if (key == "commands") {
            if (!parse_json_string_at(body, pos, commands, error)) return false;
            return true;
        }

        // El endpoint solo necesita strings. Para propiedades desconocidas,
        // aceptar strings y descartarlas; otro tipo se rechaza con claridad.
        string ignored;
        if (!parse_json_string_at(body, pos, ignored, error)) {
            error = "la propiedad 'commands' debe ser una cadena JSON";
            return false;
        }

        skip_ws(body, pos);
        if (pos < body.size() && body[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < body.size() && body[pos] == '}') break;
    }

    error = "falta la propiedad JSON 'commands'";
    return false;
}

string capture_command_output(const string& commands) {
    ostringstream captured;
    streambuf* previous = cout.rdbuf(captured.rdbuf());
    set_api_execution(true);

    try {
        process_input(commands);
    } catch (const exception& e) {
        cout << "[ERROR] API: excepcion al ejecutar comandos: " << e.what() << '\n';
    } catch (...) {
        cout << "[ERROR] API: excepcion desconocida al ejecutar comandos\n";
    }

    set_api_execution(false);
    cout.rdbuf(previous);
    return captured.str();
}

string reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

bool send_all(int client, const string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t result = send(client, data.data() + sent, data.size() - sent, 0);
        if (result <= 0) return false;
        sent += static_cast<size_t>(result);
    }
    return true;
}

void send_response(int client,
                   int status,
                   const string& content_type,
                   const string& body,
                   const string& extra_headers = "") {
    ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << reason_phrase(status) << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type\r\n"
             << "Cache-Control: no-store\r\n"
             << "Connection: close\r\n";

    if (!extra_headers.empty()) response << extra_headers;
    response << "\r\n" << body;
    send_all(client, response.str());
}

void send_json_error(int client, int status, const string& message) {
    string body = "{\"ok\":false,\"error\":\"" + json_escape(message) + "\"}";
    send_response(client, status, "application/json; charset=utf-8", body);
}

void handle_client(int client) {
    HttpRequest request;
    string error;

    if (!parse_request(client, request, error)) {
        send_json_error(client, 400, error);
        return;
    }

    if (request.method == "OPTIONS") {
        send_response(client, 204, "text/plain; charset=utf-8", "");
        return;
    }

    if (request.path == "/api/health") {
        if (request.method != "GET") {
            send_json_error(client, 405, "metodo no permitido");
            return;
        }
        send_response(client,
                      200,
                      "application/json; charset=utf-8",
                      "{\"ok\":true,\"service\":\"ExtreamFS\",\"filesystem\":\"EXT2\"}");
        return;
    }

    if (request.path == "/api/execute") {
        if (request.method != "POST") {
            send_json_error(client, 405, "metodo no permitido");
            return;
        }

        string commands;
        if (!extract_commands(request.body, commands, error)) {
            send_json_error(client, 400, error);
            return;
        }

        string output = capture_command_output(commands);
        string body = "{\"ok\":true,\"output\":\"" + json_escape(output) + "\"}";
        send_response(client, 200, "application/json; charset=utf-8", body);
        return;
    }

    send_json_error(client, 404, "endpoint no encontrado");
}

} // namespace

int run_api_server(int port) {
    if (port <= 0 || port > 65535) {
        cerr << "[ERROR] API: puerto invalido" << endl;
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        cerr << "[ERROR] API: no se pudo crear el socket: " << strerror(errno) << endl;
        return 1;
    }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        cerr << "[ERROR] API: no se pudo usar 127.0.0.1:" << port
             << ": " << strerror(errno) << endl;
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        cerr << "[ERROR] API: no se pudo iniciar escucha: " << strerror(errno) << endl;
        close(server_fd);
        return 1;
    }

    cout << "ExtreamFS API REST" << endl;
    cout << "[OK] API: escuchando en http://127.0.0.1:" << port << endl;
    cout << "     GET  /api/health" << endl;
    cout << "     POST /api/execute" << endl;

    while (true) {
        sockaddr_in client_address{};
        socklen_t client_length = sizeof(client_address);

        int client = accept(server_fd,
                            reinterpret_cast<sockaddr*>(&client_address),
                            &client_length);
        if (client < 0) {
            if (errno == EINTR) continue;
            cerr << "[ERROR] API: accept fallo: " << strerror(errno) << endl;
            close(server_fd);
            return 1;
        }

        handle_client(client);
        close(client);
    }
}
