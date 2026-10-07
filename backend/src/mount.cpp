#include "mount.h"
#include "disk.h"
#include "shared.h"

#include <iostream>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <ctime>

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
    // disco nuevo, asignar siguiente letra
    DiskLetter nuevo;
    nuevo.path = path;
    nuevo.letter = next_letter;
    disk_letters.push_back(nuevo);
    next_letter++;
    return nuevo.letter;
}

// contar cuantas particiones de un disco estan montadas
static int count_mounted_for_disk(const string& path) {
    int count = 0;
    for (auto& m : mounted_list) {
        if (m.path == path) {
            count++;
        }
    }
    return count;
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

    // verificar que el disco exista
    FILE* check = fopen(path.c_str(), "rb");
    if (check == NULL) {
        cout << "[ERROR] MOUNT: el disco no existe en " << path << endl;
        return;
    }
    fclose(check);

    // leer MBR
    MBR mbr = read_mbr(path);

    // buscar la particion por nombre (solo primarias)
    int part_idx = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_s > 0) {
            string existing = read_fixed(mbr.mbr_partitions[i].part_name, 16);
            if (mbr.mbr_partitions[i].part_type == 'P' && compare_ci(existing, name)) {
                part_idx = i;
                break;
            }
        }
    }

    if (part_idx == -1) {
        cout << "[ERROR] MOUNT: no se encontro particion primaria con nombre " << name << endl;
        return;
    }

    // verificar que no este ya montada
    for (auto& m : mounted_list) {
        if (m.path == path && compare_ci(m.name, name)) {
            cout << "[ERROR] MOUNT: la particion " << name
                 << " ya esta montada (id=" << m.id << ")" << endl;
            return;
        }
    }

    // generar ID: ultimos 2 digitos carnet(44) + num_particion + letra_disco
    char letter = get_disk_letter(path);
    int part_num = count_mounted_for_disk(path) + 1;

    // carnet: 201807244, ultimos 2 digitos: 44
    string id = "44" + to_string(part_num) + string(1, letter);

    // validar que el id tenga exactamente 4 caracteres (cabe en part_id[4])
    if ((int)id.size() != 4) {
        cout << "[ERROR] MOUNT: error generando id de montaje" << endl;
        return;
    }

    // actualizar la particion en el disco
    mbr.mbr_partitions[part_idx].part_status = '1';
    mbr.mbr_partitions[part_idx].part_correlative = part_num;
    // copia segura de exactamente 4 bytes
    memcpy(mbr.mbr_partitions[part_idx].part_id, id.c_str(), 4);

    write_mbr(path, mbr);

    // Si la particion ya estaba formateada como EXT2, actualizar la informacion
    // de montaje del superbloque. En un primer montaje previo a MKFS estos bytes
    // aun no representan un superbloque valido y simplemente se omiten.
    {
        FILE* fs_file = fopen(path.c_str(), "r+b");
        if (fs_file != NULL) {
            Superblock sb;
            fseek(fs_file, mbr.mbr_partitions[part_idx].part_start, SEEK_SET);
            if (fread(&sb, sizeof(Superblock), 1, fs_file) == 1 &&
                sb.s_magic == 0xEF53 && sb.s_filesystem_type == 2) {
                sb.s_mtime = time(nullptr);
                sb.s_mnt_count++;
                fseek(fs_file, mbr.mbr_partitions[part_idx].part_start, SEEK_SET);
                fwrite(&sb, sizeof(Superblock), 1, fs_file);
            }
            fclose(fs_file);
        }
    }

    // agregar a la tabla de montajes en RAM
    MountEntry entry;
    entry.id = id;
    entry.path = path;
    entry.name = name;
    mounted_list.push_back(entry);

    cout << "[OK] MOUNT: particion " << name << " montada con id=" << id << endl;
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
