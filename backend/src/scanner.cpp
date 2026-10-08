#include "scanner.h"
#include "shared.h"
#include "disk.h"
#include "mount.h"
#include "filesystem.h"
#include "users.h"
#include "reports.h"

#include <iostream>
#include <sstream>
#include <set>
#include <cctype>

using namespace std;

// Parametros permitidos por los comandos implementados hasta Sprint 5.
static const map<string, set<string>> allowed_params = {
    {"mkdisk",  {"size", "fit", "unit", "path"}},
    {"rmdisk",  {"path"}},
    {"fdisk",   {"size", "unit", "path", "type", "fit", "name", "delete", "add"}},
    {"mount",   {"path", "name"}},
    {"mounted", {}},
    {"unmount", {"id"}},
    {"mkfs",    {"id", "type"}},
    {"login",   {"user", "pass", "id"}},
    {"logout",  {}},
    {"mkgrp",   {"name"}},
    {"rmgrp",   {"name"}},
    {"mkusr",   {"user", "pass", "grp"}},
    {"rmusr",   {"user"}},
    {"chgrp",   {"user", "grp"}},
    {"mkfile",  {"path", "r", "size", "cont"}},
    {"mkdir",   {"path", "p"}},
    {"chmod",   {"path", "ugo", "r"}},
    {"chown",   {"path", "user", "r"}},
    {"rep",     {"name", "path", "id", "path_file_ls"}},
};

static const map<string, set<string>> flag_params = {
    {"mkfile", {"r"}},
    {"mkdir",  {"p"}},
    {"chmod",  {"r"}},
    {"chown",  {"r"}},
};

// separar tokens por espacios, respetando comillas dobles
vector<string> tokenize(const string& input) {
    vector<string> tokens;
    string current;
    bool in_quotes = false;

    for (int i = 0; i < (int)input.size(); i++) {
        char c = input[i];
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (isspace(static_cast<unsigned char>(c)) && !in_quotes) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

pair<string, string> split_param(const string& token) {
    string t = token;
    if (!t.empty() && t[0] == '-') t = t.substr(1);

    size_t eq = t.find('=');
    if (eq == string::npos) return {to_lower(t), ""};

    string key = to_lower(t.substr(0, eq));
    string value = t.substr(eq + 1);
    return {key, value};
}

void process_line(const string& line) {
    string trimmed = trim(line);
    if (trimmed.empty()) return;

    if (trimmed[0] == '#') {
        cout << trimmed << endl;
        return;
    }

    int quote_count = 0;
    for (char c : trimmed) {
        if (c == '"') quote_count++;
    }
    if (quote_count % 2 != 0) {
        cout << "[ERROR] comillas dobles sin cerrar" << endl;
        return;
    }

    vector<string> tokens = tokenize(trimmed);
    if (tokens.empty()) return;

    string cmd = to_lower(tokens[0]);

    map<string, string> params;
    string fdisk_mode; // primera operacion size/add/delete encontrada en FDISK
    for (int i = 1; i < (int)tokens.size(); i++) {
        if (tokens[i].empty() || tokens[i][0] != '-') {
            cout << "[ERROR] " << to_upper(cmd)
                 << ": parametro debe iniciar con '-': " << tokens[i] << endl;
            return;
        }

        auto [key, value] = split_param(tokens[i]);
        if (key.empty()) {
            cout << "[ERROR] " << to_upper(cmd)
                 << ": parametro mal formado: " << tokens[i] << endl;
            return;
        }

        bool has_eq = (tokens[i].find('=') != string::npos);
        if (has_eq && value.empty()) {
            cout << "[ERROR] " << to_upper(cmd)
                 << ": parametro sin valor: " << tokens[i] << endl;
            return;
        }

        if (params.find(key) != params.end()) {
            cout << "[ERROR] " << to_upper(cmd)
                 << ": parametro -" << key << " repetido" << endl;
            return;
        }

        if (cmd == "fdisk" && fdisk_mode.empty() &&
            (key == "size" || key == "add" || key == "delete")) {
            fdisk_mode = key;
        }
        params[key] = value;
    }

    if (cmd == "cat") {
        for (auto& [key, val] : params) {
            bool valid_key = key.rfind("file", 0) == 0 && key.size() > 4;
            int number = 0;
            if (!valid_key || !strict_stoi(key.substr(4), number) || number <= 0) {
                cout << "[ERROR] CAT: parametro no reconocido -" << key << endl;
                return;
            }
            if (val.empty()) {
                cout << "[ERROR] CAT: parametro -" << key << " requiere un valor" << endl;
                return;
            }
        }
    } else if (allowed_params.count(cmd)) {
        const auto& allowed = allowed_params.at(cmd);
        const set<string> empty_flags = flag_params.count(cmd) ? flag_params.at(cmd) : set<string>{};
        for (auto& [key, val] : params) {
            if (allowed.find(key) == allowed.end()) {
                cout << "[ERROR] " << to_upper(cmd)
                     << ": parametro no reconocido -" << key << endl;
                return;
            }
            bool is_flag = empty_flags.find(key) != empty_flags.end();
            if (is_flag && !val.empty()) {
                cout << "[ERROR] " << to_upper(cmd)
                     << ": parametro -" << key << " no recibe valor" << endl;
                return;
            }
            if (!is_flag && val.empty()) {
                cout << "[ERROR] " << to_upper(cmd)
                     << ": parametro -" << key << " requiere un valor" << endl;
                return;
            }
        }
    }

    if (cmd == "fdisk" && !fdisk_mode.empty()) {
        params["__mode"] = fdisk_mode;
    }

    if (cmd == "mkdisk") {
        cmd_mkdisk(params);
    } else if (cmd == "rmdisk") {
        cmd_rmdisk(params);
    } else if (cmd == "fdisk") {
        cmd_fdisk(params);
    } else if (cmd == "mount") {
        cmd_mount(params);
    } else if (cmd == "mounted") {
        cmd_mounted();
    } else if (cmd == "unmount") {
        cmd_unmount(params);
    } else if (cmd == "mkfs") {
        cmd_mkfs(params);
    } else if (cmd == "login") {
        cmd_login(params);
    } else if (cmd == "logout") {
        cmd_logout();
    } else if (cmd == "mkgrp") {
        cmd_mkgrp(params);
    } else if (cmd == "rmgrp") {
        cmd_rmgrp(params);
    } else if (cmd == "mkusr") {
        cmd_mkusr(params);
    } else if (cmd == "rmusr") {
        cmd_rmusr(params);
    } else if (cmd == "chgrp") {
        cmd_chgrp(params);
    } else if (cmd == "mkfile") {
        cmd_mkfile(params);
    } else if (cmd == "mkdir") {
        cmd_mkdir(params);
    } else if (cmd == "cat") {
        cmd_cat(params);
    } else if (cmd == "chmod") {
        cmd_chmod(params);
    } else if (cmd == "chown") {
        cmd_chown(params);
    } else if (cmd == "rep") {
        cmd_rep(params);
    } else if (cmd == "exit") {
        // se maneja en main
    } else {
        cout << "[ERROR] comando no reconocido: " << cmd << endl;
    }
}

void process_input(const string& text) {
    istringstream stream(text);
    string line;
    while (getline(stream, line)) process_line(line);
}

