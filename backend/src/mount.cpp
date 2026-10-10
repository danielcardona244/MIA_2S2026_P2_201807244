#include "mount.h"
#include "disk.h"
#include "shared.h"

#include <iostream>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <set>

using namespace std;

// tabla global de montajes en memoria
vector<MountEntry> mounted_list;

// registrar que discos ya se han visto y que letra les corresponde
struct DiskLetter {
    string path;
    char letter;
};
static vector<DiskLetter> disk_letters;
static char next_letter = 'A';

// obtener la letra asignada a un disco, o asignar una nueva
static char get_disk_letter(const string& path) {
    for (auto& dl : disk_letters) {
        if (dl.path == path) {
            return dl.letter;
        }
    }

    DiskLetter nuevo;
    nuevo.path = path;
    nuevo.letter = next_letter;
    disk_letters.push_back(nuevo);
    next_letter++;
    return nuevo.letter;
}

// Obtener el primer correlativo libre del disco. Esto evita IDs duplicados si
// se desmonta una particion intermedia y luego se vuelve a montar otra.
static int next_correlative_for_disk(const string& path) {
    set<int> used;
    for (const auto& m : mounted_list) {
        if (m.path != path || m.id.size() != 4) continue;
        char digit = m.id[2];
        if (digit >= '1' && digit <= '9') used.insert(digit - '0');
    }

    for (int i = 1; i <= 9; ++i) {
        if (!used.count(i)) return i;
    }
    return -1;
}

static int find_primary_index(const MBR& mbr, const string& name) {
    for (int i = 0; i < 4; i++) {
        const Partition& p = mbr.mbr_partitions[i];
        if (p.part_s <= 0 || p.part_start == -1 || p.part_type != 'P') continue;
        if (compare_ci(read_fixed(p.part_name, 16), name)) return i;
    }
    return -1;
}

static void update_superblock_mount_time(const string& path,
                                         const Partition& part,
                                         bool mounting) {
    FILE* fs_file = fopen(path.c_str(), "r+b");
    if (fs_file == NULL) return;

    Superblock sb{};
    fseek(fs_file, part.part_start, SEEK_SET);
    if (fread(&sb, sizeof(Superblock), 1, fs_file) == 1 &&
        sb.s_magic == 0xEF53 &&
        (sb.s_filesystem_type == 2 || sb.s_filesystem_type == 3)) {
        if (mounting) {
            sb.s_mtime = time(nullptr);
            sb.s_mnt_count++;
        } else {
            sb.s_umtime = time(nullptr);
        }
        fseek(fs_file, part.part_start, SEEK_SET);
        fwrite(&sb, sizeof(Superblock), 1, fs_file);
    }
    fclose(fs_file);
}

void cmd_mount(map<string, string>& params) {
    if (params.find("path") == params.end()) {
        cout << "[ERROR] MOUNT: falta parametro -path" << endl;
        return;
    }
    if (params.find("name") == params.end()) {
        cout << "[ERROR] MOUNT: falta parametro -name" << endl;
        return;
    }

    string path = params["path"];
    string name = params["name"];

    // Un disco existente puede haber sido creado en una ejecucion anterior del
    // programa. Al montarlo se incorpora al registro usado por el visualizador.
    register_disk_path(path);

    FILE* check = fopen(path.c_str(), "rb");
    if (check == NULL) {
        cout << "[ERROR] MOUNT: el disco no existe en " << path << endl;
        return;
    }
    fclose(check);

    MBR mbr = read_mbr(path);
    int part_idx = find_primary_index(mbr, name);

    if (part_idx == -1) {
        cout << "[ERROR] MOUNT: no se encontro particion primaria con nombre " << name << endl;
        return;
    }

    for (auto& m : mounted_list) {
        if (m.path == path && compare_ci(m.name, name)) {
            cout << "[ERROR] MOUNT: la particion " << name
                 << " ya esta montada (id=" << m.id << ")" << endl;
            return;
        }
    }

    char letter = get_disk_letter(path);
    int part_num = next_correlative_for_disk(path);
    if (part_num == -1) {
        cout << "[ERROR] MOUNT: no hay correlativos disponibles para este disco" << endl;
        return;
    }

    // carnet 201807244 -> ultimos dos digitos 44
    string id = "44" + to_string(part_num) + string(1, letter);
    if ((int)id.size() != 4) {
        cout << "[ERROR] MOUNT: error generando id de montaje" << endl;
        return;
    }

    Partition& part = mbr.mbr_partitions[part_idx];
    part.part_status = '1';
    part.part_correlative = part_num;
    memcpy(part.part_id, id.c_str(), 4);
    write_mbr(path, mbr);

    update_superblock_mount_time(path, part, true);

    MountEntry entry;
    entry.id = id;
    entry.path = path;
    entry.name = name;
    mounted_list.push_back(entry);

    cout << "[OK] MOUNT: particion " << name << " montada con id=" << id << endl;
}

void cmd_unmount(map<string, string>& params) {
    if (params.find("id") == params.end()) {
        cout << "[ERROR] UNMOUNT: falta parametro -id" << endl;
        return;
    }

    string id = params["id"];
    if (id.empty()) {
        cout << "[ERROR] UNMOUNT: id no puede estar vacio" << endl;
        return;
    }

    auto it = find_if(mounted_list.begin(), mounted_list.end(), [&](const MountEntry& m) {
        return compare_ci(m.id, id);
    });

    if (it == mounted_list.end()) {
        cout << "[ERROR] UNMOUNT: no existe una particion montada con id=" << id << endl;
        return;
    }

    MountEntry entry = *it;
    MBR mbr = read_mbr(entry.path);
    int part_idx = find_primary_index(mbr, entry.name);

    if (part_idx == -1) {
        cout << "[ERROR] UNMOUNT: la particion asociada al id ya no existe en el disco" << endl;
        return;
    }

    Partition& part = mbr.mbr_partitions[part_idx];
    update_superblock_mount_time(entry.path, part, false);

    part.part_status = '0';
    part.part_correlative = 0; // estado inicial exigido por Proyecto 2
    memset(part.part_id, 0, sizeof(part.part_id));
    write_mbr(entry.path, mbr);

    mounted_list.erase(it);

    cout << "[OK] UNMOUNT: particion " << entry.name
         << " desmontada (id=" << entry.id << ")" << endl;
}

void cmd_mounted() {
    if (mounted_list.empty()) {
        cout << "[INFO] MOUNTED: no hay particiones montadas" << endl;
        return;
    }

    cout << "--- Particiones montadas ---" << endl;
    for (auto& m : mounted_list) {
        cout << "  id=" << m.id << "  disco=" << m.path
             << "  particion=" << m.name << endl;
    }
    cout << "----------------------------" << endl;
}

bool get_mount_info(const string& id, string& out_path, string& out_name) {
    for (auto& m : mounted_list) {
        if (compare_ci(m.id, id)) {
            out_path = m.path;
            out_name = m.name;
            return true;
        }
    }
    return false;
}
