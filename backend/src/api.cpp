#include "api.h"
#include "disk.h"
#include "mount.h"
#include "scanner.h"
#include "shared.h"
#include "filesystem.h"
#include "users.h"

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

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

bool extract_string_property(const string& body,
                             const string& wanted_key,
                             string& value,
                             string& error) {
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

        string parsed;
        if (!parse_json_string_at(body, pos, parsed, error)) {
            error = "la propiedad '" + wanted_key + "' debe ser una cadena JSON";
            return false;
        }
        if (key == wanted_key) {
            value = parsed;
            if (value.empty()) {
                error = "la propiedad '" + wanted_key + "' no puede estar vacia";
                return false;
            }
            return true;
        }

        skip_ws(body, pos);
        if (pos < body.size() && body[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < body.size() && body[pos] == '}') break;
    }

    error = "falta la propiedad JSON '" + wanted_key + "'";
    return false;
}

bool extract_commands(const string& body, string& commands, string& error) {
    return extract_string_property(body, "commands", commands, error);
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

string capture_action_output(const function<void()>& action) {
    ostringstream captured;
    streambuf* previous = cout.rdbuf(captured.rdbuf());
    try {
        action();
    } catch (const exception& e) {
        cout << "[ERROR] API: excepcion: " << e.what() << '\n';
    } catch (...) {
        cout << "[ERROR] API: excepcion desconocida\n";
    }
    cout.rdbuf(previous);
    return captured.str();
}

string format_time_text(time_t value) {
    if (value <= 0) return "";
    tm tm_value{};
#ifdef _WIN32
    localtime_s(&tm_value, &value);
#else
    localtime_r(&value, &tm_value);
#endif
    char buffer[32] = {0};
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_value);
    return buffer;
}

string disk_fit_text(char fit) {
    switch (fit) {
        case 'B': return "BF";
        case 'W': return "WF";
        default: return "FF";
    }
}

string partition_type_text(char type) {
    switch (type) {
        case 'E': return "extended";
        case 'L': return "logical";
        default: return "primary";
    }
}

int mounted_count_for_path(const string& path) {
    int count = 0;
    for (const MountEntry& entry : mounted_list) {
        if (entry.path == path) count++;
    }
    return count;
}

vector<string> current_disk_paths() {
    vector<string> paths = get_known_disks();
    for (const MountEntry& entry : mounted_list) {
        if (find(paths.begin(), paths.end(), entry.path) == paths.end()) {
            paths.push_back(entry.path);
        }
    }
    sort(paths.begin(), paths.end());
    return paths;
}

string mounted_id_for_partition(const string& path, const string& name) {
    for (const MountEntry& entry : mounted_list) {
        if (entry.path == path && compare_ci(entry.name, name)) return entry.id;
    }
    return "";
}

void append_partition_json(ostringstream& json,
                           const string& path,
                           const string& name,
                           const string& type,
                           char fit,
                           int start,
                           int size,
                           bool mounted,
                           const string& id,
                           bool& first) {
    if (!first) json << ',';
    first = false;
    json << "{\"name\":\"" << json_escape(name)
         << "\",\"type\":\"" << json_escape(type)
         << "\",\"fit\":\"" << json_escape(disk_fit_text(fit))
         << "\",\"start\":" << start
         << ",\"size\":" << size
         << ",\"mounted\":" << (mounted ? "true" : "false")
         << ",\"id\":\"" << json_escape(id)
         << "\",\"diskPath\":\"" << json_escape(path) << "\"}";
}

void append_logical_partitions_json(ostringstream& json,
                                    const string& path,
                                    const Partition& ext,
                                    bool& first) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return;

    const int ext_end = ext.part_start + ext.part_s;
    int pos = ext.part_start;
    set<int> visited;
    for (int guard = 0; guard < 500; ++guard) {
        if (pos < ext.part_start || pos + static_cast<int>(sizeof(EBR)) > ext_end) break;
        if (visited.count(pos)) break;
        visited.insert(pos);

        EBR ebr{};
        if (fseek(file, pos, SEEK_SET) != 0 || fread(&ebr, sizeof(EBR), 1, file) != 1) break;

        if (ebr.part_s > 0) {
            string name = read_fixed(ebr.part_name, 16);
            string id = mounted_id_for_partition(path, name);
            append_partition_json(json, path, name, "logical", ebr.part_fit,
                                  ebr.part_start, ebr.part_s, !id.empty(), id, first);
        }
        if (ebr.part_next == -1) break;
        pos = ebr.part_next;
    }
    fclose(file);
}

string session_json() {
    ostringstream json;
    json << "{\"ok\":true,\"active\":" << (current_session.active ? "true" : "false");
    if (current_session.active) {
        json << ",\"user\":\"" << json_escape(current_session.user)
             << "\",\"group\":\"" << json_escape(current_session.group)
             << "\",\"id\":\"" << json_escape(current_session.id)
             << "\",\"uid\":" << current_session.uid
             << ",\"gid\":" << current_session.gid;
    }
    json << '}';
    return json.str();
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
                      "{\"ok\":true,\"service\":\"ExtreamFS\",\"filesystem\":\"EXT2/EXT3\"}");
        return;
    }

    if (request.path == "/api/session") {
        if (request.method != "GET") {
            send_json_error(client, 405, "metodo no permitido");
            return;
        }
        send_response(client, 200, "application/json; charset=utf-8", session_json());
        return;
    }

    if (request.path == "/api/login") {
        if (request.method != "POST") {
            send_json_error(client, 405, "metodo no permitido");
            return;
        }
        if (current_session.active) {
            send_json_error(client, 400, "ya existe una sesion activa; cierre sesion antes de iniciar otra");
            return;
        }

        string user, password, id;
        if (!extract_string_property(request.body, "user", user, error) ||
            !extract_string_property(request.body, "password", password, error) ||
            !extract_string_property(request.body, "id", id, error)) {
            send_json_error(client, 400, error);
            return;
        }

        map<string, string> params = {{"user", user}, {"pass", password}, {"id", id}};
        string output = capture_action_output([&]() { cmd_login(params); });
        if (!current_session.active) {
            send_json_error(client, 400, trim_http(output).empty() ? "no se pudo iniciar sesion" : trim_http(output));
            return;
        }

        send_response(client, 200, "application/json; charset=utf-8", session_json());
        return;
    }

    if (request.path == "/api/logout") {
        if (request.method != "POST") {
            send_json_error(client, 405, "metodo no permitido");
            return;
        }
        if (!current_session.active) {
            send_json_error(client, 400, "no hay una sesion activa");
            return;
        }
        capture_action_output([]() { cmd_logout(); });
        send_response(client, 200, "application/json; charset=utf-8", session_json());
        return;
    }

    if (request.path == "/api/disks") {
        if (request.method != "GET") {
            send_json_error(client, 405, "metodo no permitido");
            return;
        }

        vector<string> paths = current_disk_paths();
        ostringstream json;
        json << "{\"ok\":true,\"disks\":[";
        bool first = true;
        for (const string& path : paths) {
            FILE* check = fopen(path.c_str(), "rb");
            if (!check) continue;
            fclose(check);

            MBR mbr = read_mbr(path);
            if (mbr.mbr_tamano <= 0) continue;

            int partition_count = 0;
            for (const Partition& part : mbr.mbr_partitions) {
                if (part.part_s > 0 && part.part_start >= 0) partition_count++;
            }

            if (!first) json << ',';
            first = false;
            string filename = fs::path(path).filename().string();
            json << "{\"name\":\"" << json_escape(filename)
                 << "\",\"path\":\"" << json_escape(path)
                 << "\",\"size\":" << mbr.mbr_tamano
                 << ",\"fit\":\"" << json_escape(disk_fit_text(mbr.dsk_fit))
                 << "\",\"created\":\"" << json_escape(format_time_text(mbr.mbr_fecha_creacion))
                 << "\",\"signature\":" << mbr.mbr_dsk_signature
                 << ",\"partitionCount\":" << partition_count
                 << ",\"mountedCount\":" << mounted_count_for_path(path) << "}";
        }
        json << "]}";
        send_response(client, 200, "application/json; charset=utf-8", json.str());
        return;
    }

    if (request.path == "/api/partitions") {
        if (request.method != "POST") {
            send_json_error(client, 405, "metodo no permitido");
            return;
        }

        string path;
        if (!extract_string_property(request.body, "path", path, error)) {
            send_json_error(client, 400, error);
            return;
        }
        FILE* check = fopen(path.c_str(), "rb");
        if (!check) {
            send_json_error(client, 400, "el disco indicado no existe");
            return;
        }
        fclose(check);

        MBR mbr = read_mbr(path);
        if (mbr.mbr_tamano <= 0) {
            send_json_error(client, 400, "el archivo no contiene un MBR valido");
            return;
        }

        ostringstream json;
        json << "{\"ok\":true,\"path\":\"" << json_escape(path) << "\",\"partitions\":[";
        bool first = true;
        for (const Partition& part : mbr.mbr_partitions) {
            if (part.part_s <= 0 || part.part_start < 0) continue;
            string name = read_fixed(part.part_name, 16);
            string id = mounted_id_for_partition(path, name);
            append_partition_json(json, path, name, partition_type_text(part.part_type),
                                  part.part_fit, part.part_start, part.part_s,
                                  !id.empty(), id, first);
            if (part.part_type == 'E') append_logical_partitions_json(json, path, part, first);
        }
        json << "]}";
        send_response(client, 200, "application/json; charset=utf-8", json.str());
        return;
    }

    if (request.path == "/api/fs/list") {
        if (request.method != "POST") {
            send_json_error(client, 405, "metodo no permitido");
            return;
        }
        string id, path;
        if (!extract_string_property(request.body, "id", id, error) ||
            !extract_string_property(request.body, "path", path, error)) {
            send_json_error(client, 400, error);
            return;
        }

        vector<FsViewEntry> entries;
        if (!fs_list_directory_view(id, path, entries, error)) {
            send_json_error(client, 400, error);
            return;
        }

        ostringstream json;
        json << "{\"ok\":true,\"id\":\"" << json_escape(id)
             << "\",\"path\":\"" << json_escape(path) << "\",\"entries\":[";
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i) json << ',';
            const FsViewEntry& entry = entries[i];
            json << "{\"name\":\"" << json_escape(entry.name)
                 << "\",\"path\":\"" << json_escape(entry.path)
                 << "\",\"type\":\"" << json_escape(entry.type)
                 << "\",\"inode\":" << entry.inode
                 << ",\"size\":" << entry.size
                 << ",\"permissions\":\"" << json_escape(entry.permissions)
                 << "\",\"uid\":" << entry.uid
                 << ",\"gid\":" << entry.gid
                 << ",\"modified\":\"" << json_escape(entry.modified)
                 << "\",\"readable\":" << (entry.readable ? "true" : "false") << "}";
        }
        json << "]}";
        send_response(client, 200, "application/json; charset=utf-8", json.str());
        return;
    }

    if (request.path == "/api/fs/file") {
        if (request.method != "POST") {
            send_json_error(client, 405, "metodo no permitido");
            return;
        }
        string id, path;
        if (!extract_string_property(request.body, "id", id, error) ||
            !extract_string_property(request.body, "path", path, error)) {
            send_json_error(client, 400, error);
            return;
        }

        FsFileView view;
        if (!fs_read_file_view(id, path, view, error)) {
            send_json_error(client, 400, error);
            return;
        }

        ostringstream json;
        json << "{\"ok\":true,\"file\":{\"name\":\"" << json_escape(view.name)
             << "\",\"path\":\"" << json_escape(view.path)
             << "\",\"inode\":" << view.inode
             << ",\"size\":" << view.size
             << ",\"permissions\":\"" << json_escape(view.permissions)
             << "\",\"uid\":" << view.uid
             << ",\"gid\":" << view.gid
             << ",\"modified\":\"" << json_escape(view.modified)
             << "\",\"content\":\"" << json_escape(view.content) << "\"}}";
        send_response(client, 200, "application/json; charset=utf-8", json.str());
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

    if (request.path == "/api/journaling") {
        if (request.method != "POST") {
            send_json_error(client, 405, "metodo no permitido");
            return;
        }

        string id;
        if (!extract_string_property(request.body, "id", id, error)) {
            send_json_error(client, 400, error);
            return;
        }

        vector<JournalViewEntry> entries;
        if (!fs_read_journal_entries(id, entries, error)) {
            send_json_error(client, 400, error);
            return;
        }

        ostringstream json;
        json << "{\"ok\":true,\"id\":\"" << json_escape(id)
             << "\",\"entries\":[";
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i > 0) json << ',';
            const JournalViewEntry& entry = entries[i];
            json << "{\"count\":" << entry.count
                 << ",\"operation\":\"" << json_escape(entry.operation)
                 << "\",\"path\":\"" << json_escape(entry.path)
                 << "\",\"content\":\"" << json_escape(entry.content)
                 << "\",\"date\":\"" << json_escape(entry.date) << "\"}";
        }
        json << "]}";
        send_response(client, 200, "application/json; charset=utf-8", json.str());
        return;
    }

    send_json_error(client, 404, "endpoint no encontrado");
}

} // namespace

int run_api_server(int port, const string& bind_address) {
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
    address.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, bind_address.c_str(), &address.sin_addr) != 1) {
        cerr << "[ERROR] API: direccion IPv4 invalida: " << bind_address << endl;
        close(server_fd);
        return 1;
    }

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        cerr << "[ERROR] API: no se pudo usar " << bind_address << ':' << port
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
    cout << "[OK] API: escuchando en http://" << bind_address << ':' << port << endl;
    if (bind_address == "0.0.0.0") {
        cout << "     modo red: accesible mediante la IP/DNS del servidor si el firewall lo permite" << endl;
    }
    cout << "     GET  /api/health" << endl;
    cout << "     GET  /api/session" << endl;
    cout << "     POST /api/login" << endl;
    cout << "     POST /api/logout" << endl;
    cout << "     GET  /api/disks" << endl;
    cout << "     POST /api/partitions" << endl;
    cout << "     POST /api/fs/list" << endl;
    cout << "     POST /api/fs/file" << endl;
    cout << "     POST /api/execute" << endl;
    cout << "     POST /api/journaling" << endl;

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
