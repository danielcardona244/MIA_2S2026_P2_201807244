// utilidad de inspeccion binaria para pruebas estructurales
// usa las mismas structs.h del proyecto para leer archivos .mia

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include "structs.h"

using namespace std;

// leer nombre de campo char fijo sin depender de null
static string safe_name(const char* buf, int maxlen) {
    int len = 0;
    while (len < maxlen && buf[len] != '\0') len++;
    return string(buf, len);
}

// crear disco con particiones preubicadas para producir huecos controlados:
// Hueco 1: [sizeof(MBR)=168, 3168] -> tamano 3000
// P0: [3168, 3668] -> tamano 500
// Hueco 2: [3668, 5168] -> tamano 1500
// P1: [5168, 5668] -> tamano 500
// Hueco 3: [5668, 10668] -> tamano 5000
void cmd_create_gapped_disk(const string& path, char fit) {
    int total_bytes = 10668;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        cerr << "Error creando archivo: " << path << endl;
        return;
    }
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    int written = 0;
    while (written < total_bytes) {
        int to_write = min(1024, total_bytes - written);
        fwrite(buffer, 1, to_write, f);
        written += to_write;
    }

    MBR mbr;
    memset(&mbr, 0, sizeof(MBR));
    mbr.mbr_tamano = total_bytes;
    mbr.mbr_fecha_creacion = time(NULL);
    mbr.mbr_dsk_signature = 12345;
    mbr.dsk_fit = fit;

    for (int i = 0; i < 4; i++) {
        mbr.mbr_partitions[i] = Partition();
    }

    // P0 en slot 0
    mbr.mbr_partitions[0].part_status = '1';
    mbr.mbr_partitions[0].part_type = 'P';
    mbr.mbr_partitions[0].part_fit = 'W';
    mbr.mbr_partitions[0].part_start = 3168;
    mbr.mbr_partitions[0].part_s = 500;
    mbr.mbr_partitions[0].part_correlative = -1;
    strncpy(mbr.mbr_partitions[0].part_name, "GapP0", 15);

    // P1 en slot 1
    mbr.mbr_partitions[1].part_status = '1';
    mbr.mbr_partitions[1].part_type = 'P';
    mbr.mbr_partitions[1].part_fit = 'W';
    mbr.mbr_partitions[1].part_start = 5168;
    mbr.mbr_partitions[1].part_s = 500;
    mbr.mbr_partitions[1].part_correlative = -1;
    strncpy(mbr.mbr_partitions[1].part_name, "GapP1", 15);

    fseek(f, 0, SEEK_SET);
    fwrite(&mbr, sizeof(MBR), 1, f);
    fclose(f);
    cout << "OK=disco_gapped_creado fit=" << fit << endl;
}

// ---------- comandos ----------

void cmd_sizes() {
    cout << "MBR=" << sizeof(MBR) << endl;
    cout << "Partition=" << sizeof(Partition) << endl;
    cout << "EBR=" << sizeof(EBR) << endl;
    cout << "Superblock=" << sizeof(Superblock) << endl;
    cout << "Inode=" << sizeof(Inode) << endl;
    cout << "FolderBlock=" << sizeof(FolderBlock) << endl;
    cout << "FileBlock=" << sizeof(FileBlock) << endl;
    cout << "PointerBlock=" << sizeof(PointerBlock) << endl;
}

void cmd_mbr(const string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { cout << "ERROR=no_se_pudo_abrir" << endl; return; }
    MBR mbr;
    memset(&mbr, 0, sizeof(MBR));
    fread(&mbr, sizeof(MBR), 1, f);
    fclose(f);

    cout << "mbr_tamano=" << mbr.mbr_tamano << endl;
    cout << "mbr_dsk_signature=" << mbr.mbr_dsk_signature << endl;
    cout << "dsk_fit=" << mbr.dsk_fit << endl;

    for (int i = 0; i < 4; i++) {
        string pfx = "p" + to_string(i) + "_";
        Partition& p = mbr.mbr_partitions[i];
        cout << pfx << "status=" << p.part_status << endl;
        cout << pfx << "type=" << p.part_type << endl;
        cout << pfx << "fit=" << p.part_fit << endl;
        cout << pfx << "start=" << p.part_start << endl;
        cout << pfx << "s=" << p.part_s << endl;
        cout << pfx << "name=" << safe_name(p.part_name, 16) << endl;
        cout << pfx << "correlative=" << p.part_correlative << endl;
        // part_id: 4 bytes exactos, omitiendo nulos si no esta montada
        cout << pfx << "id=";
        for (int j = 0; j < 4; j++) {
            if (p.part_id[j] != '\0') cout << p.part_id[j];
        }
        cout << endl;
    }
}

void cmd_ebr_chain(const string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { cout << "ERROR=no_se_pudo_abrir" << endl; return; }

    // leer MBR para encontrar la extendida
    MBR mbr;
    fread(&mbr, sizeof(MBR), 1, f);

    int ext_start = -1, ext_size = 0;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_type == 'E' && mbr.mbr_partitions[i].part_s > 0) {
            ext_start = mbr.mbr_partitions[i].part_start;
            ext_size = mbr.mbr_partitions[i].part_s;
            break;
        }
    }

    if (ext_start == -1) {
        cout << "ERROR=no_hay_extendida" << endl;
        fclose(f);
        return;
    }

    cout << "ext_start=" << ext_start << endl;
    cout << "ext_size=" << ext_size << endl;
    int ext_end = ext_start + ext_size;

    int pos = ext_start;
    int idx = 0;
    while (pos != -1 && idx < 100) {
        if (pos < ext_start || pos >= ext_end) {
            cout << "ERROR=pos_fuera_de_rango pos=" << pos << endl;
            break;
        }
        EBR ebr;
        fseek(f, pos, SEEK_SET);
        if (fread(&ebr, sizeof(EBR), 1, f) != 1) break;

        string pfx = "ebr" + to_string(idx) + "_";
        cout << pfx << "pos=" << pos << endl;
        cout << pfx << "mount=" << ebr.part_mount << endl;
        cout << pfx << "fit=" << ebr.part_fit << endl;
        cout << pfx << "start=" << ebr.part_start << endl;
        cout << pfx << "s=" << ebr.part_s << endl;
        cout << pfx << "next=" << ebr.part_next << endl;
        cout << pfx << "name=" << safe_name(ebr.part_name, 16) << endl;

        pos = ebr.part_next;
        idx++;
    }
    cout << "ebr_count=" << idx << endl;

    fclose(f);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "uso: test_structs <sizes|mbr|ebr_chain|create_gapped> [params...]" << endl;
        return 1;
    }
    string cmd = argv[1];

    if (cmd == "sizes") {
        cmd_sizes();
    } else if (cmd == "mbr" && argc >= 3) {
        cmd_mbr(argv[2]);
    } else if (cmd == "ebr_chain" && argc >= 3) {
        cmd_ebr_chain(argv[2]);
    } else if (cmd == "create_gapped" && argc >= 4) {
        cmd_create_gapped_disk(argv[2], argv[3][0]);
    } else {
        cerr << "comando desconocido: " << cmd << endl;
        return 1;
    }
    return 0;
}
