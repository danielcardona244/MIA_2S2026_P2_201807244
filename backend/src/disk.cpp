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

using namespace std;
namespace fs = std::filesystem;

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

// encontrar espacios libres en el disco (entre particiones P/E)
static vector<FreeSpace> get_free_spaces(const MBR& mbr) {
    vector<FreeSpace> spaces;

    // recopilar particiones activas ordenadas por inicio
    vector<pair<int, int>> used; // start, size
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_s > 0 && mbr.mbr_partitions[i].part_start != -1) {
            used.push_back({mbr.mbr_partitions[i].part_start, mbr.mbr_partitions[i].part_s});
        }
    }
    sort(used.begin(), used.end());

    int cursor = (int)sizeof(MBR);
    for (auto& p : used) {
        if (p.first > cursor) {
            spaces.push_back({cursor, p.first - cursor});
        }
        cursor = p.first + p.second;
    }

    if (cursor < mbr.mbr_tamano) {
        spaces.push_back({cursor, mbr.mbr_tamano - cursor});
    }

    return spaces;
}

// aplicar ajuste segun tipo (FF, BF, WF)
static int apply_fit(vector<FreeSpace>& spaces, int needed, char fit) {
    int best_idx = -1;

    if (fit == 'F') {
        // first fit
        for (int i = 0; i < (int)spaces.size(); i++) {
            if (spaces[i].size >= needed) {
                best_idx = i;
                break;
            }
        }
    } else if (fit == 'B') {
        // best fit
        int min_size = -1;
        for (int i = 0; i < (int)spaces.size(); i++) {
            if (spaces[i].size >= needed) {
                if (min_size == -1 || spaces[i].size < min_size) {
                    min_size = spaces[i].size;
                    best_idx = i;
                }
            }
        }
    } else { // W
        // worst fit
        int max_size = -1;
        for (int i = 0; i < (int)spaces.size(); i++) {
            if (spaces[i].size >= needed) {
                if (spaces[i].size > max_size) {
                    max_size = spaces[i].size;
                    best_idx = i;
                }
            }
        }
    }

    if (best_idx == -1) return -1;
    return spaces[best_idx].start;
}

// encontrar espacios libres dentro de la particion extendida
static vector<FreeSpace> get_free_spaces_ext(const string& path, const Partition& ext) {
    vector<FreeSpace> spaces;
    vector<pair<int, int>> used; // start, size (ebr + datos)

    FILE* file = fopen(path.c_str(), "rb");
    if (file == NULL) return spaces;

    int ext_end = ext.part_start + ext.part_s;
    int pos = ext.part_start;
    int iterations = 0;

    while (pos != -1) {
        // proteccion contra loops infinitos o punteros fuera de rango
        if (iterations++ > 500) break;
        if (pos < ext.part_start || pos >= ext_end) break;

        EBR ebr;
        fseek(file, pos, SEEK_SET);
        if (fread(&ebr, sizeof(EBR), 1, file) != 1) break;

        if (ebr.part_s > 0) {
            // EBR con particion logica activa: ocupa sizeof(EBR) + datos
            used.push_back({pos, (int)sizeof(EBR) + ebr.part_s});
        }
        // si part_s == 0 es un EBR vacio (placeholder), no lo contamos como usado

        pos = ebr.part_next;
    }
    fclose(file);

    sort(used.begin(), used.end());

    int cursor = ext.part_start;
    for (auto& u : used) {
        if (u.first > cursor) {
            spaces.push_back({cursor, u.first - cursor});
        }
        cursor = u.first + u.second;
    }

    if (cursor < ext_end) {
        spaces.push_back({cursor, ext_end - cursor});
    }

    return spaces;
}

void cmd_fdisk(map<string, string>& params) {
    // validar obligatorios
    if (params.find("size") == params.end()) {
        cout << "[ERROR] FDISK: falta parametro -size" << endl;
        return;
    }
    if (params.find("path") == params.end()) {
        cout << "[ERROR] FDISK: falta parametro -path" << endl;
        return;
    }
    if (params.find("name") == params.end()) {
        cout << "[ERROR] FDISK: falta parametro -name" << endl;
        return;
    }

    string path = params["path"];
    if (path.empty()) {
        cout << "[ERROR] FDISK: path no puede estar vacio" << endl;
        return;
    }
    if (!ends_with_mia(path)) {
        cout << "[ERROR] FDISK: el archivo debe tener extension .mia" << endl;
        return;
    }

    string name = params["name"];
    if (name.empty()) {
        cout << "[ERROR] FDISK: name no puede estar vacio" << endl;
        return;
    }
    // part_name tiene tamano 16, rechazar nombres mas largos
    if ((int)name.size() > 16) {
        cout << "[ERROR] FDISK: nombre demasiado largo (max 16 caracteres)" << endl;
        return;
    }

    // conversion numerica estricta
    int size = 0;
    if (!strict_stoi(params["size"], size)) {
        cout << "[ERROR] FDISK: size no es un numero valido" << endl;
        return;
    }
    if (size <= 0) {
        cout << "[ERROR] FDISK: size debe ser mayor a 0" << endl;
        return;
    }

    // opcionales con defaults
    string unit = "K";
    if (params.find("unit") != params.end()) {
        unit = to_upper(params["unit"]);
    }
    if (unit != "B" && unit != "K" && unit != "M") {
        cout << "[ERROR] FDISK: unidad no valida, usar B, K o M" << endl;
        return;
    }

    string type = "P";
    if (params.find("type") != params.end()) {
        type = to_upper(params["type"]);
    }
    if (type != "P" && type != "E" && type != "L") {
        cout << "[ERROR] FDISK: tipo no valido, usar P, E o L" << endl;
        return;
    }

    string fit = "WF";
    if (params.find("fit") != params.end()) {
        fit = to_upper(params["fit"]);
    }
    if (fit != "BF" && fit != "FF" && fit != "WF") {
        cout << "[ERROR] FDISK: ajuste no valido, usar BF, FF o WF" << endl;
        return;
    }

    // verificar que el disco exista
    FILE* check = fopen(path.c_str(), "rb");
    if (check == NULL) {
        cout << "[ERROR] FDISK: el disco no existe en " << path << endl;
        return;
    }
    fclose(check);

    // calcular tamano en bytes con verificacion de overflow
    long long part_ll = (long long)size;
    if (unit == "K") {
        part_ll *= 1024LL;
    } else if (unit == "M") {
        part_ll *= 1024LL * 1024LL;
    }
    if (part_ll > INT_MAX) {
        cout << "[ERROR] FDISK: tamano de particion excede el limite" << endl;
        return;
    }
    int part_size = (int)part_ll;

    MBR mbr = read_mbr(path);

    // verificar nombre duplicado entre primarias/extendidas
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_s > 0) {
            string existing = read_fixed(mbr.mbr_partitions[i].part_name, 16);
            if (compare_ci(existing, name)) {
                cout << "[ERROR] FDISK: ya existe una particion con el nombre " << name << endl;
                return;
            }
        }
    }

    // verificar nombre duplicado en logicas (si hay extendida)
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_type == 'E' && mbr.mbr_partitions[i].part_s > 0) {
            FILE* f = fopen(path.c_str(), "rb");
            if (f != NULL) {
                int ext_end = mbr.mbr_partitions[i].part_start + mbr.mbr_partitions[i].part_s;
                int pos = mbr.mbr_partitions[i].part_start;
                int iters = 0;
                while (pos != -1 && iters++ < 500) {
                    if (pos < mbr.mbr_partitions[i].part_start || pos >= ext_end) break;
                    EBR ebr;
                    fseek(f, pos, SEEK_SET);
                    if (fread(&ebr, sizeof(EBR), 1, f) != 1) break;
                    if (ebr.part_s > 0) {
                        string ename = read_fixed(ebr.part_name, 16);
                        if (compare_ci(ename, name)) {
                            fclose(f);
                            cout << "[ERROR] FDISK: ya existe una particion logica con nombre " << name << endl;
                            return;
                        }
                    }
                    pos = ebr.part_next;
                }
                fclose(f);
            }
            break;
        }
    }

    if (type == "P" || type == "E") {
        // contar particiones primarias y extendidas existentes
        int count = 0;
        bool has_extended = false;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_s > 0) {
                count++;
                if (mbr.mbr_partitions[i].part_type == 'E') {
                    has_extended = true;
                }
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

        // si es extendida, verificar que quepa al menos un EBR
        if (type == "E" && part_size < (int)sizeof(EBR)) {
            cout << "[ERROR] FDISK: la extendida es muy pequena, no cabe ni un EBR" << endl;
            return;
        }

        // buscar espacio con el ajuste del disco
        vector<FreeSpace> spaces = get_free_spaces(mbr);
        int start = apply_fit(spaces, part_size, mbr.dsk_fit);

        if (start == -1) {
            cout << "[ERROR] FDISK: no hay espacio suficiente para la particion" << endl;
            return;
        }

        // buscar slot libre en el MBR
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

        // llenar datos de la particion
        mbr.mbr_partitions[slot].part_status = '0';
        mbr.mbr_partitions[slot].part_type = type[0];
        mbr.mbr_partitions[slot].part_fit = fit[0];
        mbr.mbr_partitions[slot].part_start = start;
        mbr.mbr_partitions[slot].part_s = part_size;
        mbr.mbr_partitions[slot].part_correlative = -1;
        write_fixed(mbr.mbr_partitions[slot].part_name, 16, name);
        memset(mbr.mbr_partitions[slot].part_id, 0, 4);

        // si es extendida, crear primer EBR vacio como placeholder
        if (type == "E") {
            EBR ebr;
            memset(&ebr, 0, sizeof(EBR));
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

    } else if (type == "L") {
        // buscar particion extendida
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

        // buscar espacio dentro de la extendida
        // el EBR vacio inicial no se cuenta como espacio usado
        vector<FreeSpace> spaces = get_free_spaces_ext(path, ext);
        int needed = (int)sizeof(EBR) + part_size;

        int start = apply_fit(spaces, needed, ext.part_fit);
        if (start == -1) {
            cout << "[ERROR] FDISK: no hay espacio en la extendida para la logica" << endl;
            return;
        }

        // verificar que no se escriba fuera de la extendida
        if (start + needed > ext_end) {
            cout << "[ERROR] FDISK: la logica excederia el limite de la extendida" << endl;
            return;
        }

        // preparar EBR para la nueva logica
        EBR new_ebr;
        memset(&new_ebr, 0, sizeof(EBR));
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

        // leer el primer EBR
        EBR first_ebr;
        fseek(f, ext.part_start, SEEK_SET);
        if (fread(&first_ebr, sizeof(EBR), 1, f) != 1) {
            cout << "[ERROR] FDISK: no se pudo leer el EBR inicial" << endl;
            fclose(f);
            return;
        }

        // si el primer EBR es el placeholder vacio, reutilizarlo
        if (first_ebr.part_s == 0) {
            new_ebr.part_start = ext.part_start;
            new_ebr.part_next = first_ebr.part_next; // preservar cadena si existiera
            fseek(f, ext.part_start, SEEK_SET);
            fwrite(&new_ebr, sizeof(EBR), 1, f);
            fclose(f);
            cout << "[OK] FDISK: particion logica " << name
                 << " creada (" << part_size << " bytes)" << endl;
            return;
        }

        // buscar el ultimo EBR de la lista enlazada
        int last_pos = ext.part_start;
        EBR current = first_ebr;
        int iters = 0;
        while (current.part_next != -1 && iters++ < 500) {
            if (current.part_next < ext.part_start || current.part_next >= ext_end) break;
            last_pos = current.part_next;
            fseek(f, current.part_next, SEEK_SET);
            if (fread(&current, sizeof(EBR), 1, f) != 1) break;
        }

        // enlazar el nuevo EBR al final de la cadena
        current.part_next = start;
        fseek(f, last_pos, SEEK_SET);
        fwrite(&current, sizeof(EBR), 1, f);

        // escribir el nuevo EBR en su posicion
        fseek(f, start, SEEK_SET);
        fwrite(&new_ebr, sizeof(EBR), 1, f);

        fclose(f);
        cout << "[OK] FDISK: particion logica " << name
             << " creada (" << part_size << " bytes)" << endl;
    }
}
