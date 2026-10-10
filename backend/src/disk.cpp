#include "disk.h"
#include "shared.h"

#include <iostream>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <climits>
#include <algorithm>
#include <vector>
#include <random>
#include <filesystem>
#ifdef __unix__
#include <unistd.h>
#endif

using namespace std;
namespace fs = std::filesystem;

namespace {
vector<string> known_disks;
}

void register_disk_path(const string& path) {
    if (path.empty()) return;
    if (find(known_disks.begin(), known_disks.end(), path) == known_disks.end()) {
        known_disks.push_back(path);
    }
}

void unregister_disk_path(const string& path) {
    known_disks.erase(remove(known_disks.begin(), known_disks.end(), path), known_disks.end());
}

vector<string> get_known_disks() {
    vector<string> existing;
    for (const string& path : known_disks) {
        FILE* file = fopen(path.c_str(), "rb");
        if (file == NULL) continue;
        fclose(file);
        existing.push_back(path);
    }
    known_disks = existing;
    return known_disks;
}

// ---- utilidades de lectura/escritura del MBR ----

MBR read_mbr(const string& path) {
    MBR mbr;
    memset(&mbr, 0, sizeof(MBR));
    FILE* file = fopen(path.c_str(), "rb");
    if (file != NULL) {
        fread(&mbr, sizeof(MBR), 1, file);
        fclose(file);
    }
    return mbr;
}

void write_mbr(const string& path, const MBR& mbr) {
    FILE* file = fopen(path.c_str(), "r+b");
    if (file != NULL) {
        fseek(file, 0, SEEK_SET);
        fwrite(&mbr, sizeof(MBR), 1, file);
        fclose(file);
    }
}

// crear directorios intermedios usando std::filesystem
static bool create_parent_dirs(const string& filepath) {
    fs::path p(filepath);
    fs::path parent = p.parent_path();
    if (parent.empty()) {
        // ruta relativa sin directorio, no hay nada que crear
        return true;
    }
    try {
        fs::create_directories(parent);
        return true;
    } catch (const fs::filesystem_error& e) {
        return false;
    }
}

// ---- MKDISK ----
void cmd_mkdisk(map<string, string>& params) {
    // obligatorios
    if (params.find("size") == params.end()) {
        cout << "[ERROR] MKDISK: falta parametro -size" << endl;
        return;
    }
    if (params.find("path") == params.end()) {
        cout << "[ERROR] MKDISK: falta parametro -path" << endl;
        return;
    }

    string path = params["path"];
    if (path.empty()) {
        cout << "[ERROR] MKDISK: path no puede estar vacio" << endl;
        return;
    }

    // validar extension .mia
    if (!ends_with_mia(path)) {
        cout << "[ERROR] MKDISK: el archivo debe tener extension .mia" << endl;
        return;
    }

    // conversion numerica estricta
    int size = 0;
    if (!strict_stoi(params["size"], size)) {
        cout << "[ERROR] MKDISK: size no es un numero valido" << endl;
        return;
    }
    if (size <= 0) {
        cout << "[ERROR] MKDISK: size debe ser mayor a 0" << endl;
        return;
    }

    // unidad: K o M, default M
    string unit = "M";
    if (params.find("unit") != params.end()) {
        unit = to_upper(params["unit"]);
    }
    if (unit != "K" && unit != "M") {
        cout << "[ERROR] MKDISK: unidad no valida, usar K o M" << endl;
        return;
    }

    // fit: BF, FF, WF, default FF
    string fit = "FF";
    if (params.find("fit") != params.end()) {
        fit = to_upper(params["fit"]);
    }
    if (fit != "BF" && fit != "FF" && fit != "WF") {
        cout << "[ERROR] MKDISK: ajuste no valido, usar BF, FF o WF" << endl;
        return;
    }

    // calcular tamano en bytes con verificacion de overflow
    long long total = (long long)size;
    if (unit == "M") {
        total *= 1024LL * 1024LL;
    } else if (unit == "K") {
        total *= 1024LL;
    }
    if (total > INT_MAX) {
        cout << "[ERROR] MKDISK: tamano excede el limite permitido" << endl;
        return;
    }
    int total_bytes = (int)total;

    // crear carpetas intermedias
    if (!create_parent_dirs(path)) {
        cout << "[ERROR] MKDISK: no se pudieron crear los directorios para " << path << endl;
        return;
    }

    // verificar que no exista
    FILE* check = fopen(path.c_str(), "rb");
    if (check != NULL) {
        fclose(check);
        cout << "[ERROR] MKDISK: el disco ya existe en " << path << endl;
        return;
    }

    // crear archivo binario lleno de ceros
    FILE* file = fopen(path.c_str(), "wb");
    if (file == NULL) {
        cout << "[ERROR] MKDISK: no se pudo crear el archivo " << path << endl;
        return;
    }

    // escribir ceros con buffer de 1024
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    int written = 0;
    while (written < total_bytes) {
        int to_write = min(1024, total_bytes - written);
        size_t w = fwrite(buffer, 1, to_write, file);
        if ((int)w != to_write) {
            cout << "[ERROR] MKDISK: error de escritura al crear disco" << endl;
            fclose(file);
            remove(path.c_str());
            return;
        }
        written += to_write;
    }

    // preparar MBR
    MBR mbr;
    memset(&mbr, 0, sizeof(MBR));
    mbr.mbr_tamano = total_bytes;
    mbr.mbr_fecha_creacion = time(NULL);
    // generar signature unico con random moderno
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int> dist(1, INT_MAX);
    mbr.mbr_dsk_signature = dist(gen);
    mbr.dsk_fit = fit[0]; // B, F o W

    // inicializar particiones vacias
    for (int i = 0; i < 4; i++) {
        mbr.mbr_partitions[i] = Partition();
    }

    // escribir MBR al inicio
    fseek(file, 0, SEEK_SET);
    fwrite(&mbr, sizeof(MBR), 1, file);
    fclose(file);

    register_disk_path(path);

    cout << "[OK] MKDISK: disco creado en " << path
         << " (" << total_bytes << " bytes)" << endl;
}

// ---- RMDISK ----
void cmd_rmdisk(map<string, string>& params) {
    if (params.find("path") == params.end()) {
        cout << "[ERROR] RMDISK: falta parametro -path" << endl;
        return;
    }

    string path = params["path"];
    if (path.empty()) {
        cout << "[ERROR] RMDISK: path no puede estar vacio" << endl;
        return;
    }

    if (!ends_with_mia(path)) {
        cout << "[ERROR] RMDISK: el archivo debe tener extension .mia" << endl;
        return;
    }

    FILE* check = fopen(path.c_str(), "rb");
    if (check == NULL) {
        cout << "[ERROR] RMDISK: el disco no existe en " << path << endl;
        return;
    }
    fclose(check);

    if (remove(path.c_str()) == 0) {
        unregister_disk_path(path);
        cout << "[OK] RMDISK: disco eliminado " << path << endl;
    } else {
        cout << "[ERROR] RMDISK: no se pudo eliminar " << path << endl;
    }
}

// ---- FDISK ----

// estructura auxiliar para manejar espacios libres
struct FreeSpace {
    int start;
    int size;
};

struct LogicalRef {
    bool found = false;
    int ext_idx = -1;
    int pos = -1;
    int prev_pos = -1;
    EBR ebr{};
};

static bool file_exists(const string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f == NULL) return false;
    fclose(f);
    return true;
}

static bool zero_region(const string& path, int start, int size) {
    if (start < 0 || size < 0) return false;
    FILE* f = fopen(path.c_str(), "r+b");
    if (f == NULL) return false;

    if (fseek(f, start, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    char zeros[4096];
    memset(zeros, 0, sizeof(zeros));
    int remaining = size;
    while (remaining > 0) {
        int chunk = min(remaining, (int)sizeof(zeros));
        if ((int)fwrite(zeros, 1, chunk, f) != chunk) {
            fclose(f);
            return false;
        }
        remaining -= chunk;
    }

    fclose(f);
    return true;
}

static int find_mbr_partition(const MBR& mbr, const string& name) {
    for (int i = 0; i < 4; ++i) {
        const Partition& p = mbr.mbr_partitions[i];
        if (p.part_s <= 0 || p.part_start == -1) continue;
        if (compare_ci(read_fixed(p.part_name, 16), name)) return i;
    }
    return -1;
}

static LogicalRef find_logical_partition(const string& path,
                                         const MBR& mbr,
                                         const string& name) {
    LogicalRef result;

    for (int i = 0; i < 4; ++i) {
        const Partition& ext = mbr.mbr_partitions[i];
        if (ext.part_type != 'E' || ext.part_s <= 0 || ext.part_start < 0) continue;

        FILE* f = fopen(path.c_str(), "rb");
        if (f == NULL) return result;

        const int ext_end = ext.part_start + ext.part_s;
        int pos = ext.part_start;
        int prev = -1;
        int iterations = 0;

        while (pos != -1 && iterations++ < 500) {
            if (pos < ext.part_start || pos + (int)sizeof(EBR) > ext_end) break;

            EBR ebr{};
            fseek(f, pos, SEEK_SET);
            if (fread(&ebr, sizeof(EBR), 1, f) != 1) break;

            if (ebr.part_s > 0 && compare_ci(read_fixed(ebr.part_name, 16), name)) {
                result.found = true;
                result.ext_idx = i;
                result.pos = pos;
                result.prev_pos = prev;
                result.ebr = ebr;
                fclose(f);
                return result;
            }

            prev = pos;
            pos = ebr.part_next;
        }

        fclose(f);
        break; // solo puede existir una extendida
    }

    return result;
}

// encontrar espacios libres en el disco (entre particiones P/E)
static vector<FreeSpace> get_free_spaces(const MBR& mbr) {
    vector<FreeSpace> spaces;
    vector<pair<int, int>> used; // start, size

    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_s > 0 && mbr.mbr_partitions[i].part_start != -1) {
            used.push_back({mbr.mbr_partitions[i].part_start, mbr.mbr_partitions[i].part_s});
        }
    }
    sort(used.begin(), used.end());

    int cursor = (int)sizeof(MBR);
    for (auto& p : used) {
        if (p.first > cursor) spaces.push_back({cursor, p.first - cursor});
        cursor = max(cursor, p.first + p.second);
    }

    if (cursor < mbr.mbr_tamano) spaces.push_back({cursor, mbr.mbr_tamano - cursor});
    return spaces;
}

// aplicar ajuste segun tipo (FF, BF, WF)
static int apply_fit(vector<FreeSpace>& spaces, int needed, char fit) {
    int best_idx = -1;

    if (fit == 'F') {
        for (int i = 0; i < (int)spaces.size(); i++) {
            if (spaces[i].size >= needed) {
                best_idx = i;
                break;
            }
        }
    } else if (fit == 'B') {
        int min_size = -1;
        for (int i = 0; i < (int)spaces.size(); i++) {
            if (spaces[i].size >= needed && (min_size == -1 || spaces[i].size < min_size)) {
                min_size = spaces[i].size;
                best_idx = i;
            }
        }
    } else {
        int max_size = -1;
        for (int i = 0; i < (int)spaces.size(); i++) {
            if (spaces[i].size >= needed && spaces[i].size > max_size) {
                max_size = spaces[i].size;
                best_idx = i;
            }
        }
    }

    return best_idx == -1 ? -1 : spaces[best_idx].start;
}

// encontrar espacios libres dentro de la particion extendida
static vector<FreeSpace> get_free_spaces_ext(const string& path, const Partition& ext) {
    vector<FreeSpace> spaces;
    vector<pair<int, int>> used; // posicion del EBR, sizeof(EBR) + datos

    FILE* file = fopen(path.c_str(), "rb");
    if (file == NULL) return spaces;

    int ext_end = ext.part_start + ext.part_s;
    int pos = ext.part_start;
    int iterations = 0;

    while (pos != -1) {
        if (iterations++ > 500) break;
        if (pos < ext.part_start || pos + (int)sizeof(EBR) > ext_end) break;

        EBR ebr{};
        fseek(file, pos, SEEK_SET);
        if (fread(&ebr, sizeof(EBR), 1, file) != 1) break;

        if (ebr.part_s > 0) {
            used.push_back({pos, (int)sizeof(EBR) + ebr.part_s});
        }
        pos = ebr.part_next;
    }
    fclose(file);

    sort(used.begin(), used.end());

    int cursor = ext.part_start;
    for (auto& u : used) {
        if (u.first > cursor) spaces.push_back({cursor, u.first - cursor});
        cursor = max(cursor, u.first + u.second);
    }

    if (cursor < ext_end) spaces.push_back({cursor, ext_end - cursor});
    return spaces;
}

static bool parse_unit_multiplier(const map<string, string>& params,
                                  const string& command,
                                  long long& multiplier) {
    string unit = "K";
    auto it = params.find("unit");
    if (it != params.end()) unit = to_upper(it->second);

    if (unit == "B") multiplier = 1LL;
    else if (unit == "K") multiplier = 1024LL;
    else if (unit == "M") multiplier = 1024LL * 1024LL;
    else {
        cout << "[ERROR] " << command << ": unidad no valida, usar B, K o M" << endl;
        return false;
    }
    return true;
}

static bool confirm_partition_delete(const string& name, const string& mode) {
    cout << "[CONFIRM] FDISK: eliminar particion " << name
         << " con modo " << mode << ".";

    // Las solicitudes REST no pueden responder por stdin. La interfaz web realiza
    // su confirmacion antes de enviar el comando al backend.
    if (is_api_execution()) {
        cout << " Confirmacion recibida desde la interfaz web." << endl;
        return true;
    }

#ifdef __unix__
    if (isatty(STDIN_FILENO)) {
        cout << " Confirmar [S/N]: " << flush;
        string answer;
        if (!getline(cin, answer)) {
            cout << endl << "[INFO] FDISK: eliminacion cancelada" << endl;
            return false;
        }
        answer = to_lower(trim(answer));
        if (answer != "s" && answer != "si" && answer != "y" && answer != "yes") {
            cout << "[INFO] FDISK: eliminacion cancelada" << endl;
            return false;
        }
        return true;
    }
#endif

    // Scripts .smia y pruebas automatizadas ya expresan la intencion de borrar
    // mediante -delete; no se debe bloquear esperando una entrada adicional.
    cout << " Confirmacion automatica en ejecucion no interactiva." << endl;
    return true;
}

static bool extended_contents_fit_after_shrink(const string& path,
                                               const Partition& ext,
                                               int new_size) {
    if (new_size < (int)sizeof(EBR)) return false;

    FILE* f = fopen(path.c_str(), "rb");
    if (f == NULL) return false;

    int old_end = ext.part_start + ext.part_s;
    int new_end = ext.part_start + new_size;
    int pos = ext.part_start;
    int iterations = 0;

    while (pos != -1 && iterations++ < 500) {
        if (pos < ext.part_start || pos + (int)sizeof(EBR) > old_end) {
            fclose(f);
            return false;
        }

        EBR ebr{};
        fseek(f, pos, SEEK_SET);
        if (fread(&ebr, sizeof(EBR), 1, f) != 1) {
            fclose(f);
            return false;
        }

        if (ebr.part_s > 0 && pos + (int)sizeof(EBR) + ebr.part_s > new_end) {
            fclose(f);
            return false;
        }
        pos = ebr.part_next;
    }

    fclose(f);
    return true;
}

static void fdisk_delete(map<string, string>& params,
                         const string& path,
                         const string& name,
                         MBR& mbr) {
    string mode = to_upper(params["delete"]);
    if (mode != "FAST" && mode != "FULL") {
        cout << "[ERROR] FDISK: -delete debe ser fast o full" << endl;
        return;
    }

    int idx = find_mbr_partition(mbr, name);
    LogicalRef logical;
    if (idx == -1) logical = find_logical_partition(path, mbr, name);

    if (idx == -1 && !logical.found) {
        cout << "[ERROR] FDISK: no existe una particion con nombre " << name << endl;
        return;
    }

    if (!confirm_partition_delete(name, mode)) return;

    if (idx != -1) {
        Partition victim = mbr.mbr_partitions[idx];

        if (mode == "FULL" && !zero_region(path, victim.part_start, victim.part_s)) {
            cout << "[ERROR] FDISK: no se pudo limpiar el espacio de la particion" << endl;
            return;
        }

        mbr.mbr_partitions[idx] = Partition();
        write_mbr(path, mbr);

        cout << "[OK] FDISK: particion " << name
             << " eliminada (" << mode << ")" << endl;
        return;
    }

    const Partition& ext = mbr.mbr_partitions[logical.ext_idx];
    const int total_region = (int)sizeof(EBR) + logical.ebr.part_s;

    FILE* f = fopen(path.c_str(), "r+b");
    if (f == NULL) {
        cout << "[ERROR] FDISK: no se pudo abrir el disco" << endl;
        return;
    }

    if (logical.pos == ext.part_start) {
        // El primer EBR funciona tambien como cabecera de la cadena. Al borrar
        // la primera logica se deja un placeholder que conserva part_next.
        if (mode == "FULL") {
            fclose(f);
            if (!zero_region(path, logical.pos, total_region)) {
                cout << "[ERROR] FDISK: no se pudo limpiar la particion logica" << endl;
                return;
            }
            f = fopen(path.c_str(), "r+b");
            if (f == NULL) {
                cout << "[ERROR] FDISK: no se pudo reabrir el disco" << endl;
                return;
            }
        }

        EBR placeholder{};
        placeholder.part_mount = '0';
        placeholder.part_fit = ext.part_fit;
        placeholder.part_start = ext.part_start;
        placeholder.part_s = 0;
        placeholder.part_next = logical.ebr.part_next;
        memset(placeholder.part_name, 0, sizeof(placeholder.part_name));
        fseek(f, ext.part_start, SEEK_SET);
        fwrite(&placeholder, sizeof(EBR), 1, f);
    } else {
        EBR prev{};
        fseek(f, logical.prev_pos, SEEK_SET);
        if (fread(&prev, sizeof(EBR), 1, f) != 1) {
            fclose(f);
            cout << "[ERROR] FDISK: no se pudo actualizar la cadena EBR" << endl;
            return;
        }
        prev.part_next = logical.ebr.part_next;
        fseek(f, logical.prev_pos, SEEK_SET);
        fwrite(&prev, sizeof(EBR), 1, f);
        fclose(f);
        f = NULL;

        if (mode == "FULL") {
            if (!zero_region(path, logical.pos, total_region)) {
                cout << "[ERROR] FDISK: no se pudo limpiar la particion logica" << endl;
                return;
            }
        } else {
            f = fopen(path.c_str(), "r+b");
            if (f == NULL) {
                cout << "[ERROR] FDISK: no se pudo reabrir el disco" << endl;
                return;
            }
            EBR empty{};
            fseek(f, logical.pos, SEEK_SET);
            fwrite(&empty, sizeof(EBR), 1, f);
        }
    }

    if (f != NULL) fclose(f);
    cout << "[OK] FDISK: particion logica " << name
         << " eliminada (" << mode << ")" << endl;
}

static void fdisk_add(map<string, string>& params,
                      const string& path,
                      const string& name,
                      MBR& mbr) {
    int units = 0;
    if (!strict_stoi(params["add"], units)) {
        cout << "[ERROR] FDISK: add no es un numero valido" << endl;
        return;
    }
    if (units == 0) {
        cout << "[ERROR] FDISK: add debe ser distinto de 0" << endl;
        return;
    }

    long long multiplier = 0;
    if (!parse_unit_multiplier(params, "FDISK", multiplier)) return;

    long long delta = (long long)units * multiplier;
    long long magnitude = delta < 0 ? -delta : delta;
    if (magnitude > INT_MAX) {
        cout << "[ERROR] FDISK: valor de add excede el limite permitido" << endl;
        return;
    }

    int idx = find_mbr_partition(mbr, name);
    LogicalRef logical;
    if (idx == -1) logical = find_logical_partition(path, mbr, name);

    if (idx == -1 && !logical.found) {
        cout << "[ERROR] FDISK: no existe una particion con nombre " << name << endl;
        return;
    }

    if (idx != -1) {
        Partition& part = mbr.mbr_partitions[idx];
        long long new_size_ll = (long long)part.part_s + delta;

        if (delta < 0) {
            if (new_size_ll <= 0) {
                cout << "[ERROR] FDISK: al quitar espacio la particion debe conservar tamano positivo" << endl;
                return;
            }
            if (part.part_type == 'E' &&
                !extended_contents_fit_after_shrink(path, part, (int)new_size_ll)) {
                cout << "[ERROR] FDISK: no se puede reducir la extendida porque cortaria particiones logicas" << endl;
                return;
            }
        } else {
            if (new_size_ll > INT_MAX) {
                cout << "[ERROR] FDISK: nuevo tamano excede el limite permitido" << endl;
                return;
            }

            int current_end = part.part_start + part.part_s;
            int boundary = mbr.mbr_tamano;
            for (int i = 0; i < 4; ++i) {
                if (i == idx) continue;
                const Partition& other = mbr.mbr_partitions[i];
                if (other.part_s <= 0 || other.part_start < 0) continue;
                if (other.part_start >= current_end) boundary = min(boundary, other.part_start);
            }

            long long available = (long long)boundary - current_end;
            if (delta > available) {
                cout << "[ERROR] FDISK: no hay espacio libre suficiente inmediatamente despues de la particion" << endl;
                return;
            }
        }

        part.part_s = (int)new_size_ll;
        write_mbr(path, mbr);
        cout << "[OK] FDISK: particion " << name
             << " redimensionada a " << part.part_s << " bytes" << endl;
        return;
    }

    Partition ext = mbr.mbr_partitions[logical.ext_idx];
    long long new_size_ll = (long long)logical.ebr.part_s + delta;
    if (new_size_ll <= 0 || new_size_ll > INT_MAX) {
        cout << "[ERROR] FDISK: al redimensionar la logica debe conservar tamano positivo" << endl;
        return;
    }

    if (delta > 0) {
        int current_end = logical.pos + (int)sizeof(EBR) + logical.ebr.part_s;
        int boundary = ext.part_start + ext.part_s;

        FILE* f = fopen(path.c_str(), "rb");
        if (f == NULL) {
            cout << "[ERROR] FDISK: no se pudo abrir el disco" << endl;
            return;
        }

        int pos = ext.part_start;
        int iterations = 0;
        while (pos != -1 && iterations++ < 500) {
            if (pos < ext.part_start || pos + (int)sizeof(EBR) > ext.part_start + ext.part_s) break;
            EBR ebr{};
            fseek(f, pos, SEEK_SET);
            if (fread(&ebr, sizeof(EBR), 1, f) != 1) break;
            if (ebr.part_s > 0 && pos != logical.pos && pos >= current_end) {
                boundary = min(boundary, pos);
            }
            pos = ebr.part_next;
        }
        fclose(f);

        long long available = (long long)boundary - current_end;
        if (delta > available) {
            cout << "[ERROR] FDISK: no hay espacio libre suficiente inmediatamente despues de la particion logica" << endl;
            return;
        }
    }

    logical.ebr.part_s = (int)new_size_ll;
    FILE* f = fopen(path.c_str(), "r+b");
    if (f == NULL) {
        cout << "[ERROR] FDISK: no se pudo abrir el disco" << endl;
        return;
    }
    fseek(f, logical.pos, SEEK_SET);
    fwrite(&logical.ebr, sizeof(EBR), 1, f);
    fclose(f);

    cout << "[OK] FDISK: particion logica " << name
         << " redimensionada a " << logical.ebr.part_s << " bytes" << endl;
}

static void fdisk_create(map<string, string>& params,
                         const string& path,
                         const string& name,
                         MBR& mbr) {
    if (params.find("size") == params.end()) {
        cout << "[ERROR] FDISK: falta parametro -size" << endl;
        return;
    }

    int size = 0;
    if (!strict_stoi(params["size"], size)) {
        cout << "[ERROR] FDISK: size no es un numero valido" << endl;
        return;
    }
    if (size <= 0) {
        cout << "[ERROR] FDISK: size debe ser mayor a 0" << endl;
        return;
    }

    long long multiplier = 0;
    if (!parse_unit_multiplier(params, "FDISK", multiplier)) return;

    string type = "P";
    if (params.find("type") != params.end()) type = to_upper(params["type"]);
    if (type != "P" && type != "E" && type != "L") {
        cout << "[ERROR] FDISK: tipo no valido, usar P, E o L" << endl;
        return;
    }

    string fit = "WF";
    if (params.find("fit") != params.end()) fit = to_upper(params["fit"]);
    if (fit != "BF" && fit != "FF" && fit != "WF") {
        cout << "[ERROR] FDISK: ajuste no valido, usar BF, FF o WF" << endl;
        return;
    }

    long long part_ll = (long long)size * multiplier;
    if (part_ll <= 0 || part_ll > INT_MAX) {
        cout << "[ERROR] FDISK: tamano de particion excede el limite" << endl;
        return;
    }
    int part_size = (int)part_ll;

    // nombre unico en MBR
    if (find_mbr_partition(mbr, name) != -1) {
        cout << "[ERROR] FDISK: ya existe una particion con el nombre " << name << endl;
        return;
    }
    if (find_logical_partition(path, mbr, name).found) {
        cout << "[ERROR] FDISK: ya existe una particion logica con nombre " << name << endl;
        return;
    }

    if (type == "P" || type == "E") {
        int count = 0;
        bool has_extended = false;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_s > 0) {
                count++;
                if (mbr.mbr_partitions[i].part_type == 'E') has_extended = true;
            }
        }

        if (count >= 4) {
            cout << "[ERROR] FDISK: ya hay 4 particiones, no se puede crear otra" << endl;
            return;
        }
        if (type == "E" && has_extended) {
            cout << "[ERROR] FDISK: ya existe una particion extendida en este disco" << endl;
            return;
        }
        if (type == "E" && part_size < (int)sizeof(EBR)) {
            cout << "[ERROR] FDISK: la extendida es muy pequena, no cabe ni un EBR" << endl;
            return;
        }

        vector<FreeSpace> spaces = get_free_spaces(mbr);
        int start = apply_fit(spaces, part_size, mbr.dsk_fit);
        if (start == -1) {
            cout << "[ERROR] FDISK: no hay espacio suficiente para la particion" << endl;
            return;
        }

        int slot = -1;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_s == 0 || mbr.mbr_partitions[i].part_start == -1) {
                slot = i;
                break;
            }
        }
        if (slot == -1) {
            cout << "[ERROR] FDISK: no hay slot disponible en el MBR" << endl;
            return;
        }

        Partition& part = mbr.mbr_partitions[slot];
        part = Partition();
        part.part_status = '0';
        part.part_type = type[0];
        part.part_fit = fit[0];
        part.part_start = start;
        part.part_s = part_size;
        part.part_correlative = -1;
        write_fixed(part.part_name, 16, name);
        memset(part.part_id, 0, 4);

        if (type == "E") {
            EBR ebr{};
            ebr.part_mount = '0';
            ebr.part_fit = fit[0];
            ebr.part_start = start;
            ebr.part_s = 0;
            ebr.part_next = -1;
            memset(ebr.part_name, 0, 16);

            FILE* f = fopen(path.c_str(), "r+b");
            if (f == NULL) {
                cout << "[ERROR] FDISK: no se pudo abrir el disco" << endl;
                return;
            }
            fseek(f, start, SEEK_SET);
            fwrite(&ebr, sizeof(EBR), 1, f);
            fclose(f);
        }

        write_mbr(path, mbr);
        cout << "[OK] FDISK: particion " << name
             << " creada (" << type << ", " << part_size << " bytes)" << endl;
        return;
    }

    // particion logica
    int ext_idx = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_type == 'E' && mbr.mbr_partitions[i].part_s > 0) {
            ext_idx = i;
            break;
        }
    }
    if (ext_idx == -1) {
        cout << "[ERROR] FDISK: no hay particion extendida para crear logica" << endl;
        return;
    }

    Partition ext = mbr.mbr_partitions[ext_idx];
    int ext_end = ext.part_start + ext.part_s;
    vector<FreeSpace> spaces = get_free_spaces_ext(path, ext);
    int needed = (int)sizeof(EBR) + part_size;
    int start = apply_fit(spaces, needed, ext.part_fit);
    if (start == -1 || start + needed > ext_end) {
        cout << "[ERROR] FDISK: no hay espacio en la extendida para la logica" << endl;
        return;
    }

    EBR new_ebr{};
    new_ebr.part_mount = '0';
    new_ebr.part_fit = fit[0];
    new_ebr.part_start = start;
    new_ebr.part_s = part_size;
    new_ebr.part_next = -1;
    write_fixed(new_ebr.part_name, 16, name);

    FILE* f = fopen(path.c_str(), "r+b");
    if (f == NULL) {
        cout << "[ERROR] FDISK: no se pudo abrir el disco" << endl;
        return;
    }

    EBR first_ebr{};
    fseek(f, ext.part_start, SEEK_SET);
    if (fread(&first_ebr, sizeof(EBR), 1, f) != 1) {
        cout << "[ERROR] FDISK: no se pudo leer el EBR inicial" << endl;
        fclose(f);
        return;
    }

    if (first_ebr.part_s == 0) {
        new_ebr.part_start = ext.part_start;
        new_ebr.part_next = first_ebr.part_next;
        fseek(f, ext.part_start, SEEK_SET);
        fwrite(&new_ebr, sizeof(EBR), 1, f);
        fclose(f);
        cout << "[OK] FDISK: particion logica " << name
             << " creada (" << part_size << " bytes)" << endl;
        return;
    }

    int last_pos = ext.part_start;
    EBR current = first_ebr;
    int iters = 0;
    while (current.part_next != -1 && iters++ < 500) {
        if (current.part_next < ext.part_start || current.part_next >= ext_end) break;
        last_pos = current.part_next;
        fseek(f, current.part_next, SEEK_SET);
        if (fread(&current, sizeof(EBR), 1, f) != 1) break;
    }

    current.part_next = start;
    fseek(f, last_pos, SEEK_SET);
    fwrite(&current, sizeof(EBR), 1, f);
    fseek(f, start, SEEK_SET);
    fwrite(&new_ebr, sizeof(EBR), 1, f);
    fclose(f);

    cout << "[OK] FDISK: particion logica " << name
         << " creada (" << part_size << " bytes)" << endl;
}

void cmd_fdisk(map<string, string>& params) {
    if (params.find("path") == params.end()) {
        cout << "[ERROR] FDISK: falta parametro -path" << endl;
        return;
    }
    if (params.find("name") == params.end()) {
        cout << "[ERROR] FDISK: falta parametro -name" << endl;
        return;
    }

    string path = params["path"];
    string name = params["name"];

    if (path.empty()) {
        cout << "[ERROR] FDISK: path no puede estar vacio" << endl;
        return;
    }
    if (!ends_with_mia(path)) {
        cout << "[ERROR] FDISK: el archivo debe tener extension .mia" << endl;
        return;
    }
    if (name.empty()) {
        cout << "[ERROR] FDISK: name no puede estar vacio" << endl;
        return;
    }
    if ((int)name.size() > 16) {
        cout << "[ERROR] FDISK: nombre demasiado largo (max 16 caracteres)" << endl;
        return;
    }
    if (!file_exists(path)) {
        cout << "[ERROR] FDISK: el disco no existe en " << path << endl;
        return;
    }

    MBR mbr = read_mbr(path);

    // El enunciado indica que, si se mezclan size/add/delete, se utiliza la
    // primera operacion que aparezca en el comando. scanner.cpp conserva ese
    // dato en __mode antes de convertir parametros a map.
    string mode;
    auto mode_it = params.find("__mode");
    if (mode_it != params.end()) mode = mode_it->second;
    else if (params.find("delete") != params.end()) mode = "delete";
    else if (params.find("add") != params.end()) mode = "add";
    else if (params.find("size") != params.end()) mode = "size";

    if (mode == "delete") {
        fdisk_delete(params, path, name, mbr);
    } else if (mode == "add") {
        fdisk_add(params, path, name, mbr);
    } else if (mode == "size") {
        fdisk_create(params, path, name, mbr);
    } else {
        cout << "[ERROR] FDISK: debe indicar -size, -add o -delete" << endl;
    }
}
