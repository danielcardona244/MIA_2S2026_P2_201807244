#include "users.h"

#include "filesystem.h"
#include "shared.h"

#include <iostream>
#include <sstream>
#include <vector>

using namespace std;

SessionInfo current_session;

namespace {

struct Record {
    int id = -1;
    char type = '?';
    vector<string> fields;
};

vector<string> split_csv(const string& line) {
    vector<string> fields;
    string current;
    stringstream ss(line);
    while (getline(ss, current, ',')) fields.push_back(trim(current));
    return fields;
}

vector<Record> parse_records(const string& content) {
    vector<Record> records;
    string line;
    stringstream ss(content);
    while (getline(ss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        vector<string> fields = split_csv(line);
        if (fields.size() < 3) continue;

        int id = -1;
        if (!strict_stoi(fields[0], id)) continue;
        if (fields[1].size() != 1) continue;

        Record r;
        r.id = id;
        r.type = fields[1][0];
        r.fields = fields;
        records.push_back(r);
    }
    return records;
}

string serialize_records(const vector<Record>& records) {
    string out;
    for (const Record& r : records) {
        for (size_t i = 0; i < r.fields.size(); ++i) {
            if (i) out += ',';
            out += r.fields[i];
        }
        out += '\n';
    }
    return out;
}

bool require_session(const string& command) {
    if (!current_session.active) {
        cout << "[ERROR] " << command << ": necesita iniciar sesion" << endl;
        return false;
    }
    return true;
}

bool require_root(const string& command) {
    if (!require_session(command)) return false;
    if (current_session.uid != 1 || current_session.user != "root") {
        cout << "[ERROR] " << command << ": solo el usuario root puede ejecutar este comando" << endl;
        return false;
    }
    return true;
}

bool load_records(vector<Record>& records, string& error) {
    string content;
    if (!fs_read_users_file(current_session.id, content, error)) return false;
    records = parse_records(content);
    return true;
}

bool save_records(const vector<Record>& records, string& error) {
    return fs_write_users_file(current_session.id, serialize_records(records), error);
}

int find_active_group(const vector<Record>& records, const string& name) {
    for (size_t i = 0; i < records.size(); ++i) {
        const Record& r = records[i];
        if (r.id > 0 && r.type == 'G' && r.fields.size() >= 3 && r.fields[2] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int find_active_user(const vector<Record>& records, const string& user) {
    for (size_t i = 0; i < records.size(); ++i) {
        const Record& r = records[i];
        if (r.id > 0 && r.type == 'U' && r.fields.size() >= 5 && r.fields[3] == user) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int next_id(const vector<Record>& records, char type) {
    int max_id = 0;
    int count = 0;
    for (const Record& r : records) {
        if (r.type == type) {
            count++;
            if (r.id > max_id) max_id = r.id;
        }
    }
    return max(max_id, count) + 1;
}

bool validate_len(const string& command, const string& field, const string& value, size_t max_len) {
    if (value.empty()) {
        cout << "[ERROR] " << command << ": -" << field << " no puede estar vacio" << endl;
        return false;
    }
    if (value.size() > max_len) {
        cout << "[ERROR] " << command << ": -" << field << " admite maximo " << max_len
             << " caracteres" << endl;
        return false;
    }
    if (value.find(',') != string::npos || value.find('\n') != string::npos ||
        value.find('\r') != string::npos) {
        cout << "[ERROR] " << command << ": -" << field
             << " no puede contener comas ni saltos de linea" << endl;
        return false;
    }
    return true;
}

} // namespace

void cmd_login(map<string, string>& params) {
    if (current_session.active) {
        cout << "[ERROR] LOGIN: ya existe una sesion activa, ejecute LOGOUT primero" << endl;
        return;
    }

    for (const string key : {"user", "pass", "id"}) {
        if (params.find(key) == params.end()) {
            cout << "[ERROR] LOGIN: falta parametro -" << key << endl;
            return;
        }
    }

    const string user = params["user"];
    const string pass = params["pass"];
    const string id = params["id"];
    if (user.empty() || pass.empty() || id.empty()) {
        cout << "[ERROR] LOGIN: user, pass e id no pueden estar vacios" << endl;
        return;
    }

    string content;
    string error;
    if (!fs_read_users_file(id, content, error)) {
        cout << "[ERROR] LOGIN: " << error << endl;
        return;
    }

    vector<Record> records = parse_records(content);
    int user_idx = find_active_user(records, user);
    if (user_idx < 0) {
        cout << "[ERROR] LOGIN: el usuario no existe" << endl;
        return;
    }

    const Record& u = records[user_idx];
    if (u.fields[4] != pass) {
        cout << "[ERROR] LOGIN: autenticacion fallida" << endl;
        return;
    }

    int gid = 0;
    int group_idx = find_active_group(records, u.fields[2]);
    if (group_idx >= 0) gid = records[group_idx].id;

    current_session.active = true;
    current_session.uid = u.id;
    current_session.gid = gid;
    current_session.user = u.fields[3];
    current_session.group = u.fields[2];
    current_session.id = id;

    cout << "[OK] LOGIN: sesion iniciada como " << current_session.user
         << " en " << current_session.id << endl;
}

void cmd_logout() {
    if (!current_session.active) {
        cout << "[ERROR] LOGOUT: no hay una sesion activa" << endl;
        return;
    }

    string user = current_session.user;
    current_session = SessionInfo();
    cout << "[OK] LOGOUT: sesion de " << user << " finalizada" << endl;
}

void cmd_mkgrp(map<string, string>& params) {
    if (!require_root("MKGRP")) return;
    if (params.find("name") == params.end()) {
        cout << "[ERROR] MKGRP: falta parametro -name" << endl;
        return;
    }

    string name = params["name"];
    if (!validate_len("MKGRP", "name", name, 10)) return;

    vector<Record> records;
    string error;
    if (!load_records(records, error)) {
        cout << "[ERROR] MKGRP: " << error << endl;
        return;
    }
    if (find_active_group(records, name) >= 0) {
        cout << "[ERROR] MKGRP: el grupo ya existe" << endl;
        return;
    }

    int id = next_id(records, 'G');
    Record r;
    r.id = id;
    r.type = 'G';
    r.fields = {to_string(id), "G", name};
    records.push_back(r);

    if (!save_records(records, error)) {
        cout << "[ERROR] MKGRP: " << error << endl;
        return;
    }
    cout << "[OK] MKGRP: grupo " << name << " creado con id=" << id << endl;
    string journal_error;
    if (!fs_append_journal(current_session.id, "mkgrp", "/users.txt", name, journal_error))
        cout << "[WARN] MKGRP: " << journal_error << endl;
}

void cmd_rmgrp(map<string, string>& params) {
    if (!require_root("RMGRP")) return;
    if (params.find("name") == params.end()) {
        cout << "[ERROR] RMGRP: falta parametro -name" << endl;
        return;
    }

    string name = params["name"];
    if (name == "root") {
        cout << "[ERROR] RMGRP: no se puede eliminar el grupo root" << endl;
        return;
    }

    vector<Record> records;
    string error;
    if (!load_records(records, error)) {
        cout << "[ERROR] RMGRP: " << error << endl;
        return;
    }

    int idx = find_active_group(records, name);
    if (idx < 0) {
        cout << "[ERROR] RMGRP: el grupo no existe" << endl;
        return;
    }
    records[idx].id = 0;
    records[idx].fields[0] = "0";

    if (!save_records(records, error)) {
        cout << "[ERROR] RMGRP: " << error << endl;
        return;
    }
    cout << "[OK] RMGRP: grupo " << name << " eliminado" << endl;
    string journal_error;
    if (!fs_append_journal(current_session.id, "rmgrp", "/users.txt", name, journal_error))
        cout << "[WARN] RMGRP: " << journal_error << endl;
}

void cmd_mkusr(map<string, string>& params) {
    if (!require_root("MKUSR")) return;
    for (const string key : {"user", "pass", "grp"}) {
        if (params.find(key) == params.end()) {
            cout << "[ERROR] MKUSR: falta parametro -" << key << endl;
            return;
        }
    }

    string user = params["user"];
    string pass = params["pass"];
    string grp = params["grp"];
    if (!validate_len("MKUSR", "user", user, 10) ||
        !validate_len("MKUSR", "pass", pass, 10) ||
        !validate_len("MKUSR", "grp", grp, 10)) {
        return;
    }

    vector<Record> records;
    string error;
    if (!load_records(records, error)) {
        cout << "[ERROR] MKUSR: " << error << endl;
        return;
    }
    if (find_active_user(records, user) >= 0) {
        cout << "[ERROR] MKUSR: el usuario ya existe" << endl;
        return;
    }
    if (find_active_group(records, grp) < 0) {
        cout << "[ERROR] MKUSR: el grupo no existe o esta eliminado" << endl;
        return;
    }

    int id = next_id(records, 'U');
    Record r;
    r.id = id;
    r.type = 'U';
    r.fields = {to_string(id), "U", grp, user, pass};
    records.push_back(r);

    if (!save_records(records, error)) {
        cout << "[ERROR] MKUSR: " << error << endl;
        return;
    }
    cout << "[OK] MKUSR: usuario " << user << " creado con id=" << id << endl;
    string journal_error;
    if (!fs_append_journal(current_session.id, "mkusr", "/users.txt",
                           "user=" + user + ",grp=" + grp, journal_error))
        cout << "[WARN] MKUSR: " << journal_error << endl;
}

void cmd_rmusr(map<string, string>& params) {
    if (!require_root("RMUSR")) return;
    if (params.find("user") == params.end()) {
        cout << "[ERROR] RMUSR: falta parametro -user" << endl;
        return;
    }

    string user = params["user"];
    if (user == "root") {
        cout << "[ERROR] RMUSR: no se puede eliminar el usuario root" << endl;
        return;
    }

    vector<Record> records;
    string error;
    if (!load_records(records, error)) {
        cout << "[ERROR] RMUSR: " << error << endl;
        return;
    }

    int idx = find_active_user(records, user);
    if (idx < 0) {
        cout << "[ERROR] RMUSR: el usuario no existe" << endl;
        return;
    }
    records[idx].id = 0;
    records[idx].fields[0] = "0";

    if (!save_records(records, error)) {
        cout << "[ERROR] RMUSR: " << error << endl;
        return;
    }
    cout << "[OK] RMUSR: usuario " << user << " eliminado" << endl;
    string journal_error;
    if (!fs_append_journal(current_session.id, "rmusr", "/users.txt", user, journal_error))
        cout << "[WARN] RMUSR: " << journal_error << endl;
}

void cmd_chgrp(map<string, string>& params) {
    if (!require_root("CHGRP")) return;
    for (const string key : {"user", "grp"}) {
        if (params.find(key) == params.end()) {
            cout << "[ERROR] CHGRP: falta parametro -" << key << endl;
            return;
        }
    }

    string user = params["user"];
    string grp = params["grp"];
    if (!validate_len("CHGRP", "user", user, 10) ||
        !validate_len("CHGRP", "grp", grp, 10)) {
        return;
    }

    vector<Record> records;
    string error;
    if (!load_records(records, error)) {
        cout << "[ERROR] CHGRP: " << error << endl;
        return;
    }

    int user_idx = find_active_user(records, user);
    if (user_idx < 0) {
        cout << "[ERROR] CHGRP: el usuario no existe" << endl;
        return;
    }
    if (find_active_group(records, grp) < 0) {
        cout << "[ERROR] CHGRP: el grupo no existe o esta eliminado" << endl;
        return;
    }

    records[user_idx].fields[2] = grp;
    if (!save_records(records, error)) {
        cout << "[ERROR] CHGRP: " << error << endl;
        return;
    }
    cout << "[OK] CHGRP: usuario " << user << " movido al grupo " << grp << endl;
    string journal_error;
    if (!fs_append_journal(current_session.id, "chgrp", "/users.txt",
                           "user=" + user + ",grp=" + grp, journal_error))
        cout << "[WARN] CHGRP: " << journal_error << endl;
}
