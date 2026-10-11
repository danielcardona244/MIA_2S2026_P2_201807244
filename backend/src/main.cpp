#include <iostream>
#include <string>

#include "api.h"
#include "scanner.h"
#include "shared.h"

using namespace std;

namespace {

void print_usage(const char* program) {
    cout << "Uso:" << endl;
    cout << "  " << program << "                 modo consola" << endl;
    cout << "  " << program << " --server [puerto] [host]  API REST" << endl;
    cout << "      puerto default: 8080" << endl;
    cout << "      host default: 127.0.0.1 (usar 0.0.0.0 en EC2)" << endl;
}

bool parse_port(const string& text, int& port) {
    if (text.empty()) return false;
    size_t used = 0;
    try {
        int value = stoi(text, &used);
        if (used != text.size() || value <= 0 || value > 65535) return false;
        port = value;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc >= 2) {
        string option = to_lower(argv[1]);

        if (option == "--server") {
            int port = 8080;
            if (argc >= 3 && !parse_port(argv[2], port)) {
                cerr << "[ERROR] puerto invalido: " << argv[2] << endl;
                return 1;
            }
            if (argc > 4) {
                print_usage(argv[0]);
                return 1;
            }
            const string host = (argc >= 4) ? argv[3] : "127.0.0.1";
            return run_api_server(port, host);
        }

        if (option == "--help" || option == "-h") {
            print_usage(argv[0]);
            return 0;
        }

        cerr << "[ERROR] opcion no reconocida: " << argv[1] << endl;
        print_usage(argv[0]);
        return 1;
    }

    cout << "ExtreamFS - Sistema de archivos EXT2/EXT3" << endl;
    cout << "Ingrese comandos (exit para salir):" << endl;
    cout << endl;

    string line;
    while (true) {
        cout << ">> ";
        if (!getline(cin, line)) {
            // fin de archivo
            break;
        }

        string trimmed = trim(line);
        if (to_lower(trimmed) == "exit") {
            cout << "Saliendo..." << endl;
            break;
        }

        process_line(line);
        cout << endl;
    }

    return 0;
}
